// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Kick and snare isolation (pure C++, no JUCE).
//
//  1. Zero-phase band filters split the signal into LOW (30-130 Hz), BODY (150-400 Hz) and NOISE (2-9 kHz).
//  2. A short-time envelope in dB is built for every band.
//  3. Onset candidates are found where a band rises quickly (this is the internal "gate": only hits that
//     are loud enough compared with the loudest hits, and that rise and decay like a drum, pass).
//  4. Kick  = fast rise in LOW, rise at least as big as in BODY, and it dies away (a sustained bass note does not).
//     Snare = fast rise in BODY and in NOISE, and it dies away (a hi-hat has no BODY, a guitar chord does not decay).
//  5. For the snare the pitch of the shell/head resonance is measured on the isolated hits.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "DrumGate.h"
#include "FftUtil.h"
#include "NoteTranscriber.h"

namespace scriba
{
struct DrumHit
{
    double time = 0.0;     // seconds from the start of the recording
    int velocity = 100;    // 1..127
    float level = 0.0f;    // internal: level in dB
    bool lowHeavy = false; // internal: a kick sounds at the same time
};

struct SnarePitch
{
    bool valid = false;
    double hz = 0.0;           // measured fundamental
    int midi = 0;              // nearest note
    double cents = 0.0;        // deviation from that note (-50..+50)
    double spreadCents = 0.0;  // how much the single hits differ
    float confidence = 0.0f;   // 0..1
    int hitsUsed = 0;
};

struct DrumResult
{
    std::vector<DrumHit> hits;
    SnarePitch pitch;          // only filled for the snare
    std::vector<float> detectorDb;   // gate detector level over the song (for drawing), dB
    float thresholdDb = -30.0f;      // gate threshold used
};

class DrumAnalyzer
{
public:
    using ProgressFn = std::function<void (float)>;
    using CancelFn = std::function<bool()>;

    static constexpr int kKickNote = 36;    // General MIDI: Bass Drum 1
    static constexpr int kSnareNote = 38;   // General MIDI: Acoustic Snare

    // sensitivity 0 = only the clear hits, 1 = also quiet / ghost hits
    static DrumResult analyse (const float* x, size_t n, double sr, Instrument inst, float sensitivity = 0.5f,
                               const ProgressFn& progress = {}, const CancelFn& cancelled = {})
    {
        DrumResult out;
        if (x == nullptr || n < 8192 || sr < 8000.0 || ! isDrum (inst))
            return out;
        sensitivity = std::max (0.0f, std::min (1.0f, sensitivity));

        const size_t nb = n / kHop;
        if (nb < 32)
            return out;

        // 1-2: band envelopes
        std::vector<float> work (n);
        std::vector<float> low = bandEnvelopeDb (x, n, sr, 30.0, 130.0, work, 512);
        if (progress) progress (0.25f);
        if (cancelled && cancelled()) return {};
        std::vector<float> body = bandEnvelopeDb (x, n, sr, 150.0, 400.0, work, 256);
        if (progress) progress (0.45f);
        if (cancelled && cancelled()) return {};
        std::vector<float> noise = bandEnvelopeDb (x, n, sr, 2000.0, std::min (9000.0, sr * 0.42), work);
        if (progress) progress (0.65f);
        if (cancelled && cancelled()) return {};

        const size_t blocks = low.size();
        const double blockSec = (double) kHop / sr;
        auto blocksFor = [&] (double sec) { return (size_t) std::max (1.0, std::round (sec / blockSec)); };

        // 3: candidates - rise of any band over ~12 ms
        const float riseThr = 10.0f - 5.5f * sensitivity;       // dB
        const size_t look = blocksFor (0.012);
        const size_t pre = blocksFor (0.030);
        const size_t minGap = blocksFor (0.045);

        const bool kick = inst == Instrument::Kick;
        const std::vector<float>& main = kick ? low : body;

        std::vector<float> flux (blocks, 0.0f);
        for (size_t b = pre + look; b + 1 < blocks; ++b)
        {
            const float m = main[b];
            float floorDb = m;
            for (size_t u = b - pre; u + look <= b; ++u)
                floorDb = std::min (floorDb, main[u]);
            flux[b] = m - floorDb;
        }

        struct Cand { size_t b; float peakDb; };
        std::vector<Cand> cands;
        for (size_t b = pre + look; b + 1 < blocks; ++b)
        {
            if (flux[b] < riseThr)
                continue;
            // the block with the biggest rise inside the neighbourhood
            size_t best = b;
            const size_t hiB = std::min (blocks - 2, b + minGap);
            for (size_t u = b; u <= hiB && flux[u] >= riseThr; ++u)
                if (flux[u] > flux[best])
                    best = u;
            cands.push_back ({ best, 0.0f });
            b = hiB;
            while (b + 1 < blocks && flux[b + 1] >= riseThr)
                ++b;
        }

        // 4: classification
        const size_t decayFrom = blocksFor (0.035), decayTo = blocksFor (0.150);
        struct Hit { size_t b; float level; bool lowHeavy; };
        std::vector<Hit> accepted;
        for (const Cand& c : cands)
        {
            const size_t b0 = c.b;
            if (b0 + decayTo + 2 >= blocks)
                continue;

            // peak, floor before the hit and the level the hit itself ADDS (background such as a sustained chord removed)
            struct Band { float peak = 0, floorDb = 0, rise = 0, added = 0; };
            auto measure = [&] (const std::vector<float>& e)
            {
                Band m;
                m.floorDb = e[b0];
                for (size_t u = b0 - pre; u + look <= b0; ++u)
                    m.floorDb = std::min (m.floorDb, e[u]);
                m.peak = e[b0];
                for (size_t u = b0; u <= b0 + blocksFor (0.025); ++u)
                    m.peak = std::max (m.peak, e[u]);
                m.rise = m.peak - m.floorDb;
                const double lin = std::pow (10.0, (double) m.peak / 10.0) - std::pow (10.0, (double) m.floorDb / 10.0);
                m.added = (float) (10.0 * std::log10 (std::max (lin, 1.0e-12)));
                return m;
            };
            auto decayOf = [&] (const std::vector<float>& e, float peak)
            {
                float trough = 1.0e9f;
                for (size_t u = b0 + decayFrom; u <= b0 + decayTo; ++u)
                    trough = std::min (trough, e[u]);
                return peak - trough;
            };

            const Band L = measure (low), B = measure (body), N = measure (noise);

            bool ok = false;
            float level = 0.0f;
            if (kick)
            {
                // a kick is mostly LOW; a snare or a bass note leaks into LOW but has much more BODY
                const float dec = decayOf (low, L.peak);
                const bool strong = L.rise >= riseThr && dec >= 6.0f;
                // on top of a sustained bass the rise is smaller: then the beater click (HIGH band) must confirm it
                const bool underBass = L.rise >= riseThr - 1.5f && dec >= 4.5f && N.rise >= riseThr;
                ok = (strong || underBass) && L.added - B.added >= -2.0f;
                level = L.added;
            }
            else
            {
                const float noiseThr = riseThr * 0.8f;
                ok = B.rise >= riseThr && N.rise >= noiseThr && decayOf (body, B.peak) >= 8.0f && decayOf (noise, N.peak) >= 6.0f
                     // a hi-hat has almost no BODY compared with its NOISE; a snare has a real body
                     && B.added - N.added >= -15.0f
                     // a kick alone (strong LOW, weak BODY) is not a snare
                     && B.added - L.added >= -8.0f;
                level = 0.5f * (B.added + N.added);
            }
            if (ok)
                accepted.push_back ({ b0, level, L.added - B.added > 0.0f && L.rise >= riseThr });
        }

        // the gate: hits much quieter than the loud ones are closed out
        if (! accepted.empty())
        {
            std::vector<float> lv;
            for (const Hit& h : accepted)
                lv.push_back (h.level);
            std::sort (lv.begin(), lv.end());
            const float ref = lv[(size_t) ((double) (lv.size() - 1) * 0.9)];
            const float range = 16.0f + 18.0f * sensitivity;   // dB below the loud hits
            std::vector<Hit> kept;
            for (const Hit& h : accepted)
                if (h.level >= ref - range)
                    kept.push_back (h);
            accepted.swap (kept);

            for (const Hit& h : accepted)
            {
                DrumHit d;
                d.time = refineTime (x, n, sr, h.b, kick) ;
                d.level = h.level;
                d.lowHeavy = h.lowHeavy;
                float x01 = std::max (0.0f, std::min (1.05f, (h.level - ref + 30.0f) / 30.0f));
                d.velocity = std::max (25, std::min (127, (int) std::lround (25.0f + 102.0f * std::pow (x01, 0.8f))));
                out.hits.push_back (d);
            }
            std::sort (out.hits.begin(), out.hits.end(), [] (const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
            // two hits closer than the minimum gap: keep the louder one
            std::vector<DrumHit> merged;
            for (const DrumHit& h : out.hits)
            {
                if (! merged.empty() && h.time - merged.back().time < 0.045)
                {
                    if (h.level > merged.back().level)
                        merged.back() = h;
                    continue;
                }
                merged.push_back (h);
            }
            out.hits.swap (merged);
        }

        if (progress) progress (0.85f);

        // 5: snare pitch
        if (! kick && out.hits.size() >= 2)
        {
            // hits that coincide with a kick are polluted in the low range: use the others if there are enough
            std::vector<double> times, clean;
            for (const DrumHit& h : out.hits)
            {
                times.push_back (h.time);
                if (! h.lowHeavy)
                    clean.push_back (h.time);
            }
            out.pitch = measureSnarePitch (x, n, sr, clean.size() >= 3 ? clean : times);
        }

        if (progress) progress (1.0f);
        return out;
    }

    // The hits are the openings of the gate (DrumGate.h): exactly what you hear with LISTEN on.
    static DrumResult gateHits (const float* x, size_t n, double sr, Instrument inst, float thresholdDb,
                                const ProgressFn& progress = {}, const CancelFn& cancelled = {})
    {
        DrumResult out;
        out.thresholdDb = thresholdDb;
        if (x == nullptr || n < 1024 || sr < 8000.0 || ! isDrum (inst))
            return out;
        const bool kick = inst == Instrument::Kick;
        const auto hits = DrumGate::run (x, n, sr, kick, thresholdDb, &out.detectorDb);
        if (progress) progress (0.4f);
        if (cancelled && cancelled()) return {};
        for (const auto& h : hits)
        {
            DrumHit d;
            d.time = std::max (0.0, h.time - 0.0005);
            d.velocity = h.velocity;
            d.level = h.peakDb;
            out.hits.push_back (d);
        }
        if (! kick && out.hits.size() >= 2)
        {
            std::vector<double> times;
            for (const DrumHit& h : out.hits)
                times.push_back (h.time);
            out.pitch = measureSnarePitch (x, n, sr, times);
        }
        if (progress) progress (1.0f);
        return out;
    }

    // MIDI notes for the hits (GM drum map), short gate-style notes
    static std::vector<Note> toNotes (const DrumResult& r, Instrument inst)
    {
        std::vector<Note> v;
        for (const DrumHit& h : r.hits)
        {
            Note n;
            n.start = h.time;
            n.end = h.time + 0.1;
            n.midi = inst == Instrument::Kick ? kKickNote : kSnareNote;
            n.velocity = h.velocity;
            v.push_back (n);
        }
        return v;
    }

private:
    static constexpr size_t kHop = 64;
    static constexpr size_t kWin = 128;

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };

    static Biquad makeBiquad (bool highpass, double f, double sr)
    {
        const double pi = 3.14159265358979323846;
        const double w0 = 2.0 * pi * std::min (f, sr * 0.45) / sr;
        const double q = 0.70710678;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        const double a0 = 1.0 + alpha;
        Biquad b;
        if (highpass)
        {
            b.b0 = (1.0 + c) * 0.5 / a0;
            b.b1 = -(1.0 + c) / a0;
            b.b2 = b.b0;
        }
        else
        {
            b.b0 = (1.0 - c) * 0.5 / a0;
            b.b1 = (1.0 - c) / a0;
            b.b2 = b.b0;
        }
        b.a1 = -2.0 * c / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    static void runBiquad (const Biquad& q, std::vector<float>& y, bool reverse)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        const size_t n = y.size();
        for (size_t k = 0; k < n; ++k)
        {
            const size_t i = reverse ? n - 1 - k : k;
            const double x0 = y[i];
            const double o = q.b0 * x0 + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
            x2 = x1; x1 = x0; y2 = y1; y1 = o;
            y[i] = (float) o;
        }
    }

    // band-pass (2 high-pass + 2 low-pass sections), applied forward and backward = zero phase
    static std::vector<float> bandEnvelopeDb (const float* x, size_t n, double sr, double fLo, double fHi, std::vector<float>& work, size_t win = kWin)
    {
        work.assign (x, x + n);
        const Biquad hp = makeBiquad (true, fLo, sr), lp = makeBiquad (false, fHi, sr);
        for (int pass = 0; pass < 2; ++pass)
        {
            runBiquad (hp, work, pass == 1);
            runBiquad (hp, work, pass == 1);
            runBiquad (lp, work, pass == 1);
            runBiquad (lp, work, pass == 1);
        }

        const size_t nb = n / kHop;
        std::vector<float> env (nb, -100.0f);
        for (size_t b = 0; b < nb; ++b)
        {
            // the window is centred on the same instant for every band (centre of the 128-sample block)
            const long c = (long) (b * kHop + kWin / 2);
            const size_t s0 = (size_t) std::max<long> (0, c - (long) (win / 2));
            const size_t s1 = std::min (n, (size_t) (c + (long) (win / 2)));
            if (s1 <= s0)
                continue;
            double e = 0.0;
            for (size_t i = s0; i < s1; ++i)
                e += (double) work[i] * (double) work[i];
            e /= (double) (s1 - s0);
            env[b] = (float) (10.0 * std::log10 (e + 1.0e-10));
        }
        return env;
    }

    // look at the raw signal around the candidate to find the real start of the hit
    static double refineTime (const float* x, size_t n, double sr, size_t block, bool kick)
    {
        (void) kick;
        const double centre = (double) (block * kHop + kWin / 2);   // block middle (sample)
        const long a = (long) std::max (0.0, centre - 0.030 * sr);
        const long z = (long) std::min ((double) n - 2.0, centre + 0.040 * sr);
        const long seg = std::max<long> (8, (long) std::round (0.0015 * sr));
        std::vector<double> e;
        for (long i = a; i + seg < z; i += seg / 2)
        {
            double s = 0.0;
            for (long k = 0; k < seg; ++k)
                s += (double) x[i + k] * (double) x[i + k];
            e.push_back (std::sqrt (s / (double) seg));
        }
        if (e.size() < 4)
            return centre / sr;
        double mx = 0.0, mn = 1.0e30;
        size_t mxIdx = 0;
        for (size_t i = 0; i < e.size(); ++i)
        {
            if (e[i] > mx) { mx = e[i]; mxIdx = i; }
        }
        for (size_t i = 0; i <= mxIdx; ++i)
            mn = std::min (mn, e[i]);
        const double thr = mn + 0.25 * (mx - mn);
        size_t idx = mxIdx;
        while (idx > 0 && e[idx - 1] >= thr)
            --idx;
        return ((double) a + (double) idx * (double) (seg / 2)) / sr;
    }

public:
    //==========================================================================
    static SnarePitch measureSnarePitch (const float* x, size_t n, double sr, const std::vector<double>& times)
    {
        SnarePitch res;
        constexpr size_t N = 8192;
        constexpr size_t len = 2048;
        static const FftPlan plan (N);

        std::vector<double> win (len);
        const double pi = 3.14159265358979323846;
        for (size_t i = 0; i < len; ++i)
            win[i] = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (len - 1));

        const double binHz = sr / (double) N;
        const size_t kLo = (size_t) std::ceil (90.0 / binHz), kHi = (size_t) std::floor (500.0 / binHz);
        std::vector<double> avg (N / 2, 0.0);
        std::vector<std::vector<double>> perHit;
        std::vector<std::complex<double>> buf (N);

        for (double tm : times)
        {
            const long s0 = (long) std::round ((tm + 0.004) * sr);
            const long p0 = (long) std::round ((tm - 0.003) * sr) - (long) len;   // the same length just BEFORE the hit
            if (s0 < 0 || p0 < 0 || (size_t) s0 + len >= n)
                continue;

            // spectrum after the hit minus spectrum before it: sustained notes (chords, bass) cancel out,
            // only what the hit itself adds remains
            std::vector<double> post (N / 2), pre (N / 2);
            for (int pass = 0; pass < 2; ++pass)
            {
                const long base = pass == 0 ? s0 : p0;
                for (size_t i = 0; i < N; ++i)
                    buf[i] = i < len ? std::complex<double> ((double) x[(size_t) base + i] * win[i], 0.0) : std::complex<double> (0.0, 0.0);
                plan.forward (buf.data());
                for (size_t k = 0; k < N / 2; ++k)
                    (pass == 0 ? post : pre)[k] = std::abs (buf[k]);
            }
            std::vector<double> mag (N / 2, 0.0);
            double tot = 0.0;
            for (size_t k = 0; k < N / 2; ++k)
                mag[k] = std::max (0.0, post[k] - pre[k]);
            for (size_t k = kLo; k <= kHi; ++k)
                tot += mag[k];
            if (tot <= 1.0e-9)
                continue;
            for (size_t k = 0; k < N / 2; ++k)
            {
                mag[k] /= tot;
                avg[k] += mag[k];
            }
            perHit.push_back (std::move (mag));
        }
        if (perHit.size() < 2)
            return res;
        for (double& v : avg)
            v /= (double) perHit.size();

        // remove the smooth background (median over ~90 Hz), keep the peaks
        const size_t half = (size_t) std::max (4.0, std::round (45.0 / binHz));
        std::vector<double> sal (N / 2, 0.0);
        std::vector<double> tmp;
        for (size_t k = kLo; k <= kHi; ++k)
        {
            const size_t a = k > half ? k - half : 0;
            const size_t z = std::min (N / 2 - 1, k + half);
            tmp.assign (avg.begin() + (long) a, avg.begin() + (long) z + 1);
            std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
            sal[k] = std::max (0.0, avg[k] - tmp[tmp.size() / 2]);
        }

        size_t pk = kLo;
        for (size_t k = kLo; k <= kHi; ++k)
            if (sal[k] > sal[pk])
                pk = k;
        if (sal[pk] <= 0.0 || pk <= kLo || pk >= kHi)
            return res;

        // parabolic interpolation on the log magnitude
        const double ym = std::log (avg[pk - 1] + 1e-12), y0 = std::log (avg[pk] + 1e-12), yp = std::log (avg[pk + 1] + 1e-12);
        const double den = ym - 2.0 * y0 + yp;
        const double d = std::abs (den) > 1e-12 ? 0.5 * (ym - yp) / den : 0.0;
        const double hz = ((double) pk + std::max (-0.5, std::min (0.5, d))) * binHz;

        // peak height compared with the background
        const double ratio = avg[pk] / std::max (1e-12, avg[pk] - sal[pk]);
        res.confidence = (float) std::max (0.0, std::min (1.0, (ratio - 1.0) / 2.0));

        // single hits: where is the peak near the global one?
        std::vector<double> cents;
        for (const auto& mag : perHit)
        {
            const size_t a = (size_t) std::max (1.0, std::floor (hz * 0.88 / binHz));
            const size_t z = (size_t) std::min ((double) N / 2 - 2.0, std::ceil (hz * 1.12 / binHz));
            size_t p = a;
            for (size_t k = a; k <= z; ++k)
                if (mag[k] > mag[p])
                    p = k;
            if (p <= a || p >= z)
                continue;
            const double m1 = std::log (mag[p - 1] + 1e-12), m0 = std::log (mag[p] + 1e-12), m2 = std::log (mag[p + 1] + 1e-12);
            const double dd = m1 - 2.0 * m0 + m2;
            const double off = std::abs (dd) > 1e-12 ? 0.5 * (m1 - m2) / dd : 0.0;
            const double f = ((double) p + std::max (-0.5, std::min (0.5, off))) * binHz;
            cents.push_back (1200.0 * std::log2 (f / hz));
        }
        if (cents.size() >= 3)
        {
            std::sort (cents.begin(), cents.end());
            const double q1 = cents[cents.size() / 4], q3 = cents[(cents.size() * 3) / 4];
            res.spreadCents = (q3 - q1) * 0.5;
        }

        const double midiF = 69.0 + 12.0 * std::log2 (hz / 440.0);
        res.hz = hz;
        res.midi = (int) std::lround (midiF);
        res.cents = (midiF - (double) res.midi) * 100.0;
        res.hitsUsed = (int) perHit.size();
        res.valid = true;
        return res;
    }
};
} // namespace scriba
