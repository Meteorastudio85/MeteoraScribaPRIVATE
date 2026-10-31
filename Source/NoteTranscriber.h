// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Trascrizione delle note da audio mono (C++ puro, nessuna dipendenza da JUCE).
//
//  - Basso:           algoritmo YIN (una nota alla volta), molto preciso.
//  - Chitarra/Piano:  analisi a più altezze: spettro -> somma delle armoniche ->
//                     scelta della nota più forte -> cancellazione delle sue armoniche -> si ripete.
//  In entrambi i casi gli attacchi vengono agganciati all'inviluppo del segnale
//  (la finestra di analisi è lunga e tende ad anticipare le note).

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "FftUtil.h"
#include "basicpitch/BasicPitchTranscriber.h"

namespace scriba
{
struct Note
{
    double start = 0.0;   // secondi dall'inizio della registrazione
    double end = 0.0;
    int midi = 60;        // 60 = Do centrale
    int velocity = 80;    // 1..127
    float strength = 0.0f; // forza media dell'attivazione (uso interno)
};

enum class Instrument { Bass = 0, Guitar = 1, Piano = 2, Kick = 3, Snare = 4, Vocals = 5 };

inline bool isDrum (Instrument i) { return i == Instrument::Kick || i == Instrument::Snare; }

inline const char* instrumentName (Instrument i)
{
    switch (i)
    {
        case Instrument::Bass:   return "Bass";
        case Instrument::Guitar: return "Guitar";
        case Instrument::Piano:  return "Piano";
        case Instrument::Kick:   return "Kick";
        case Instrument::Snare:  return "Snare";
        case Instrument::Vocals: return "Vocals";
    }
    return "";
}

class NoteTranscriber
{
public:
    using ProgressFn = std::function<void (float)>;   // 0..1
    using CancelFn = std::function<bool()>;

    // sensitivity: 0 = poche note ma sicure, 1 = piu' note (anche dubbie). 0.5 e' il valore di partenza.
    static std::vector<Note> transcribe (const float* x, size_t n, double sr, Instrument inst, float sensitivity = 0.5f,
                                         const ProgressFn& progress = {}, const CancelFn& cancelled = {})
    {
        sensitivity = std::max (0.0f, std::min (1.0f, sensitivity));
        if (x == nullptr || n < 16384 || sr < 8000.0 || isDrum (inst))
            return {};   // drums are handled by DrumAnalyzer.h

        const Envelope env = makeEnvelope (x, n, sr);

        // guitar and piano: Basic Pitch (neural network, Spotify, Apache 2.0)
        if (inst == Instrument::Guitar || inst == Instrument::Piano)
        {
            auto notes = neuralPoly (x, n, sr, inst, sensitivity, progress, cancelled);
            if (cancelled && cancelled())
                return {};
            finalizeNeural (notes, env, sr);
            if (progress)
                progress (1.0f);
            return notes;
        }

        // bass and voice are one note at a time: YIN, each in its own range
        double bassTuning = 0.0;
        if (inst == Instrument::Bass)
        {
            double conf = 0.0;
            const double c = estimateTuningCents (x, n, sr, &conf);
            if (std::abs (c) >= 10.0 && conf >= 0.25)
                bassTuning = c / 100.0;
        }
        std::vector<Note> notes = (inst == Instrument::Bass)     ? trackMono (x, n, sr, env, progress, cancelled, 30.0, 500.0, 23.5, 76.5, bassTuning)
                                : (inst == Instrument::Vocals)   ? trackMono (x, n, sr, env, progress, cancelled, 70.0, 1100.0, 35.5, 88.5)
                                                                 : trackPoly (x, n, sr, inst, sensitivity, env, progress, cancelled);

        if (cancelled && cancelled())
            return {};

        finalize (notes, env, sr);
        if (progress)
            progress (1.0f);
        return notes;
    }

    // How far the instrument is from A = 440 Hz, in cents (-50..+50), from the strong spectral peaks.
    // confidence 0..1 (how consistent the peaks are). Guitars a bit flat or sharp otherwise get notes
    // rounded to the wrong semitone.
    static double estimateTuningCents (const float* x, size_t n, double sr, double* confidence = nullptr)
    {
        constexpr size_t N = 4096;
        if (confidence) *confidence = 0.0;
        if (x == nullptr || n < N * 2 || sr < 8000.0)
            return 0.0;
        static const FftPlan plan (N);
        std::vector<double> win (N);
        for (size_t i = 0; i < N; ++i)
            win[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) (N - 1));
        std::vector<std::complex<double>> buf (N);
        std::vector<double> mag (N / 2);
        const size_t kLo = (size_t) (75.0 * N / sr), kHi = std::min (N / 2 - 2, (size_t) (1100.0 * N / sr));
        double sx = 0.0, sy = 0.0, wsum = 0.0;
        const size_t hop = std::max<size_t> (N, n / 400);   // at most ~400 frames
        for (size_t start = 0; start + N <= n; start += hop)
        {
            double e = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                buf[i] = std::complex<double> ((double) x[start + i] * win[i], 0.0);
                e += (double) x[start + i] * x[start + i];
            }
            if (e / (double) N < 1.0e-7)
                continue;
            plan.forward (buf.data());
            double mx = 0.0;
            for (size_t k = 0; k < N / 2; ++k)
            {
                mag[k] = std::abs (buf[k]);
                mx = std::max (mx, mag[k]);
            }
            for (size_t k = kLo + 1; k < kHi; ++k)
            {
                if (mag[k] < 0.1 * mx || mag[k] < mag[k - 1] || mag[k] < mag[k + 1])
                    continue;
                const double a = std::log (mag[k - 1] + 1e-12), b = std::log (mag[k] + 1e-12), c = std::log (mag[k + 1] + 1e-12);
                const double den = a - 2.0 * b + c;
                const double d = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
                const double f = ((double) k + std::max (-0.5, std::min (0.5, d))) * sr / (double) N;
                const double midiF = 69.0 + 12.0 * std::log2 (f / 440.0);
                const double dev = midiF - std::round (midiF);                // -0.5..0.5 semitone
                const double ang = 2.0 * 3.14159265358979323846 * dev;
                const double w = mag[k] / mx;
                sx += w * std::cos (ang);
                sy += w * std::sin (ang);
                wsum += w;
            }
        }
        if (wsum <= 0.0)
            return 0.0;
        if (confidence)
            *confidence = std::sqrt (sx * sx + sy * sy) / wsum;
        return std::atan2 (sy, sx) / (2.0 * 3.14159265358979323846) * 100.0;
    }

    // the previous hand-made polyphonic tracker (kept for comparison tests)
    static std::vector<Note> transcribeClassic (const float* x, size_t n, double sr, Instrument inst, float sensitivity = 0.5f)
    {
        if (x == nullptr || n < 16384 || sr < 8000.0)
            return {};
        const Envelope env = makeEnvelope (x, n, sr);
        auto notes = trackPoly (x, n, sr, inst, std::max (0.0f, std::min (1.0f, sensitivity)), env, {}, {});
        finalize (notes, env, sr);
        return notes;
    }

private:
    static std::vector<Note> neuralPoly (const float* x, size_t n, double sr, Instrument inst, float sens,
                                         const ProgressFn& progress, const CancelFn& cancelled)
    {
        basicpitch::Options o;
        // sensitivity 0.55 = Basic Pitch defaults (frame 0.3, onset 0.5)
        o.frameThreshold = 0.48f - 0.27f * sens;
        o.onsetThreshold = 0.64f - 0.22f * sens;
        o.minNoteMs = 127.7f - 50.0f * std::max (0.0f, sens - 0.55f);
        if (inst == Instrument::Guitar)
        {
            o.minHz = 58.0f;     // B1: 7-string guitar, drop tunings
            o.maxHz = 1500.0f;   // above the 24th fret of the high E string
        }
        // an instrument off from A = 440: read the audio at a slightly different rate so that the notes fall
        // in the middle of their semitone (times are scaled back afterwards)
        double conf = 0.0;
        const double cents = estimateTuningCents (x, n, sr, &conf);
        const double fix = (std::abs (cents) >= 10.0 && conf >= 0.25) ? std::pow (2.0, -cents / 1200.0) : 1.0;
        const auto out = basicpitch::transcribe (x, n, sr * fix, o, progress, cancelled);
        std::vector<Note> notes;
        notes.reserve (out.size());
        for (const auto& e : out)
        {
            Note nt;
            nt.start = e.start * fix;
            nt.end = std::max (e.end * fix, nt.start + 0.05);
            nt.midi = e.midi;
            nt.strength = e.amplitude;
            nt.velocity = std::max (1, std::min (127, (int) std::lround (127.0f * e.amplitude)));
            notes.push_back (nt);
        }
        return notes;
    }

    //==========================================================================
    // Inviluppo e attacchi
    static constexpr size_t kBlock = 128;
    static constexpr size_t kHalfWin = 256;

    struct Envelope
    {
        std::vector<float> rms;        // un valore ogni kBlock campioni
        std::vector<float> rise;       // salita in dB rispetto al minimo dei blocchi precedenti
        std::vector<double> onsets;    // tempi degli attacchi (s)
        std::vector<float> onsetRise;  // forza di ogni attacco (dB)
        float ref = 0.0f;              // riferimento di volume (percentile alto)
        float gate = 0.0f;             // sotto questo rms si considera silenzio
    };

    static Envelope makeEnvelope (const float* x, size_t n, double sr)
    {
        Envelope e;
        // RMS su finestra scorrevole di 512 campioni (>= un periodo anche per le note piu' gravi)
        const size_t nb = n / kBlock;
        std::vector<double> prefix (n + 1, 0.0);
        for (size_t i = 0; i < n; ++i)
            prefix[i + 1] = prefix[i] + (double) x[i] * (double) x[i];

        e.rms.assign (nb, 0.0f);
        for (size_t b = 0; b < nb; ++b)
        {
            const size_t centre = b * kBlock;
            const size_t a = centre >= kHalfWin ? centre - kHalfWin : 0;
            const size_t z = std::min (n, centre + kHalfWin);
            e.rms[b] = (float) std::sqrt ((prefix[z] - prefix[a]) / (double) std::max<size_t> (1, z - a));
        }

        std::vector<float> sorted = e.rms;
        std::sort (sorted.begin(), sorted.end());
        e.ref = sorted.empty() ? 0.0f : sorted[(size_t) ((double) (sorted.size() - 1) * 0.95)];
        e.gate = std::max (4.0e-4f, e.ref * 0.04f);

        std::vector<float> db (nb, -100.0f);
        for (size_t b = 0; b < nb; ++b)
            db[b] = 20.0f * std::log10 (e.rms[b] + 1.0e-5f);

        e.rise.assign (nb, 0.0f);
        for (size_t b = 4; b < nb; ++b)
        {
            float m = db[b - 1];
            for (size_t k = 2; k <= 4; ++k)
                m = std::min (m, db[b - k]);
            e.rise[b] = db[b] - m;
        }

        // picchi della salita
        const double minGap = 0.05;
        double lastT = -1.0;
        for (size_t b = 4; b + 3 < nb; ++b)
        {
            if (e.rise[b] < 4.0f || e.rms[b] < e.gate * 0.7f)
                continue;
            bool isMax = true;
            for (size_t k = b >= 3 ? b - 3 : 0; k <= b + 3; ++k)
                if (e.rise[k] > e.rise[b])
                {
                    isMax = false;
                    break;
                }
            if (! isMax)
                continue;
            const double t = (double) (b * kBlock + 110) / sr;
            if (t - lastT < minGap && ! e.onsets.empty())
            {
                if (e.rise[b] > e.onsetRise.back())
                {
                    e.onsets.back() = t;
                    e.onsetRise.back() = e.rise[b];
                    lastT = t;
                }
                continue;
            }
            e.onsets.push_back (t);
            e.onsetRise.push_back (e.rise[b]);
            lastT = t;
        }
        return e;
    }

    // aggancia un tempo all'attacco più forte vicino (finestra asimmetrica: la finestra di analisi anticipa)
    static double snapToOnset (double t, const Envelope& env, double before, double after)
    {
        double best = t;
        float bestRise = 0.0f;
        for (size_t i = 0; i < env.onsets.size(); ++i)
        {
            const double o = env.onsets[i];
            if (o < t - before)
                continue;
            if (o > t + after)
                break;
            if (env.onsetRise[i] > bestRise)
            {
                bestRise = env.onsetRise[i];
                best = o;
            }
        }
        return best;
    }

    static float peakRms (const Envelope& env, double t0, double t1, double sr)
    {
        if (env.rms.empty())
            return 0.0f;
        size_t b0 = (size_t) std::max (0.0, t0 * sr / (double) kBlock);
        size_t b1 = (size_t) std::max (0.0, t1 * sr / (double) kBlock) + 1;
        b0 = std::min (b0, env.rms.size() - 1);
        b1 = std::min (b1, env.rms.size());
        float m = 0.0f;
        for (size_t b = b0; b < b1; ++b)
            m = std::max (m, env.rms[b]);
        return m;
    }

    // Basic Pitch already places the notes on their attacks: only same-pitch overlaps are cleaned and
    // the velocity comes from the real loudness of the attack
    static void finalizeNeural (std::vector<Note>& notes, const Envelope& env, double sr)
    {
        // Basic Pitch tends to place attacks late (low strings most: long analysis windows).
        // The start goes back to the real attack seen in the waveform, if there is one just before.
        for (Note& nt : notes)
        {
            const double t = snapToOnset (nt.start, env, 0.10, 0.03);
            if (t < nt.end - 0.03)
                nt.start = std::max (0.0, t);
        }
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                   { return a.start < b.start || (a.start == b.start && a.midi < b.midi); });
        for (size_t i = 0; i < notes.size(); ++i)
            for (size_t j = i + 1; j < notes.size(); ++j)
            {
                if (notes[j].start >= notes[i].end)
                    break;
                if (notes[j].midi == notes[i].midi)
                {
                    notes[i].end = std::max (notes[i].start + 0.03, notes[j].start);
                    break;
                }
            }
        const float ref = std::max (env.ref, 1.0e-6f);
        for (Note& nt : notes)
        {
            const float pk = peakRms (env, nt.start, std::min (nt.end, nt.start + 0.25), sr);
            const float rel = std::min (1.3f, pk / ref);
            const int fromLevel = (int) std::lround (35.0f + 85.0f * std::pow (rel, 0.7f));
            nt.velocity = std::max (25, std::min (127, (fromLevel + nt.velocity) / 2));
        }
    }

    //==========================================================================
    // Basso: YIN
    static std::vector<Note> trackMono (const float* x, size_t n, double sr, const Envelope& env,
                                        const ProgressFn& progress, const CancelFn& cancelled,
                                        double fMin, double fMax, double midiLo, double midiHi, double tuningSemis = 0.0)
    {
        const size_t Wi = 1024;
        const size_t tauMax = (size_t) (sr / fMin);
        const size_t tauMin = std::max<size_t> (2, (size_t) (sr / fMax));
        const size_t Wf = Wi + tauMax;
        const size_t NF = 4096;
        const size_t hop = 512;

        if (n < Wf + hop)
            return {};

        const FftPlan plan (NF);
        const size_t frames = (n - Wf) / hop + 1;
        std::vector<float> midiFrame (frames, -1.0f);
        std::vector<float> rmsFrame (frames, 0.0f);

        std::vector<std::complex<double>> fa (NF), fb (NF);
        std::vector<double> cmnd (tauMax + 1), prefix (Wf + 1);

        for (size_t t = 0; t < frames; ++t)
        {
            if ((t & 63) == 0)
            {
                if (cancelled && cancelled())
                    return {};
                if (progress)
                    progress (0.9f * (float) t / (float) frames);
            }

            const size_t start = t * hop;
            prefix[0] = 0.0;
            for (size_t i = 0; i < Wf; ++i)
                prefix[i + 1] = prefix[i] + (double) x[start + i] * (double) x[start + i];

            const double A = prefix[Wi];
            rmsFrame[t] = (float) std::sqrt (A / (double) Wi);
            if (rmsFrame[t] < env.gate)
                continue;

            for (size_t j = 0; j < NF; ++j)
            {
                fa[j] = j < Wi ? std::complex<double> ((double) x[start + j], 0.0) : std::complex<double> (0.0, 0.0);
                fb[j] = j < Wf ? std::complex<double> ((double) x[start + j], 0.0) : std::complex<double> (0.0, 0.0);
            }
            plan.forward (fa.data());
            plan.forward (fb.data());
            for (size_t k = 0; k < NF; ++k)
                fa[k] = std::conj (fa[k]) * fb[k];
            plan.inverse (fa.data());

            cmnd[0] = 1.0;
            double run = 0.0;
            for (size_t tau = 1; tau <= tauMax; ++tau)
            {
                const double d = A + (prefix[tau + Wi] - prefix[tau]) - 2.0 * fa[tau].real();
                run += d;
                cmnd[tau] = run > 0.0 ? d * (double) tau / run : 1.0;
            }

            long tauEst = -1;
            for (size_t tau = tauMin; tau < tauMax; ++tau)
            {
                if (cmnd[tau] < 0.15)
                {
                    while (tau + 1 <= tauMax && cmnd[tau + 1] < cmnd[tau])
                        ++tau;
                    tauEst = (long) tau;
                    break;
                }
            }
            if (tauEst < 0)
            {
                size_t arg = tauMin;
                for (size_t tau = tauMin; tau <= tauMax; ++tau)
                    if (cmnd[tau] < cmnd[arg])
                        arg = tau;
                if (cmnd[arg] < 0.3)
                    tauEst = (long) arg;
            }
            if (tauEst < 0)
                continue;

            double tauF = (double) tauEst;
            if (tauEst > 1 && (size_t) tauEst < tauMax)
            {
                const double s0 = cmnd[(size_t) tauEst - 1], s1 = cmnd[(size_t) tauEst], s2 = cmnd[(size_t) tauEst + 1];
                const double den = s0 + s2 - 2.0 * s1;
                if (std::abs (den) > 1.0e-12)
                    tauF += 0.5 * (s0 - s2) / den;
            }

            const double f0 = sr / tauF;
            const double midiF = 69.0 + 12.0 * std::log2 (f0 / 440.0) - tuningSemis;
            if (midiF >= midiLo && midiF <= midiHi)
                midiFrame[t] = (float) midiF;
        }

        const double centreOffset = ((double) Wi * 0.5 + (double) tauMax * 0.25) / sr;
        auto frameTime = [&] (size_t t) { return (double) (t * hop) / sr + centreOffset; };

        // segue l'altezza con isteresi: una nota lunga che si spegne puo' scivolare di qualche
        // decina di centesimi senza diventare una nota nuova; per cambiare nota servono 4 frame coerenti
        std::vector<Note> notes;
        bool active = false;
        int curPitch = 0;
        size_t curFirst = 0, curLast = 0, gap = 0;
        int pendPitch = -1;
        size_t pendStart = 0, pendCount = 0;

        auto closeNote = [&] (size_t lastFrame)
        {
            if (lastFrame + 1 - curFirst >= 4)   // almeno ~45 ms
            {
                Note nt;
                nt.midi = curPitch;
                nt.start = frameTime (curFirst);
                nt.end = frameTime (lastFrame) + (double) hop / sr;
                notes.push_back (nt);
            }
            active = false;
        };

        // la coda che si spegne di una nota lunga non deve generare note fantasma
        auto onsetNear = [&] (double tm)
        {
            auto it = std::lower_bound (env.onsets.begin(), env.onsets.end(), tm - 0.08);
            return it != env.onsets.end() && *it <= tm + 0.08;
        };
        float curPeak = 0.0f;

        for (size_t t = 0; t < frames; ++t)
        {
            float v = midiFrame[t];

            if (active && v >= 0.0f)
            {
                curPeak = std::max (curPeak, rmsFrame[t]);
                if (rmsFrame[t] < 0.08f * curPeak)
                {
                    closeNote (curLast);
                    v = -1.0f;
                }
            }
            else if (! active && v >= 0.0f)
            {
                // il basso e' monofonico: una nota nuova parte con un attacco (o con una salita netta di volume)
                float recentMin = rmsFrame[t];
                for (size_t k = (t > 8 ? t - 8 : 0); k < t; ++k)
                    recentMin = std::min (recentMin, rmsFrame[k]);
                if (! onsetNear (frameTime (t)) && rmsFrame[t] < 2.0f * recentMin)
                    v = -1.0f;
            }

            if (v < 0.0f)
            {
                if (active && ++gap > 2)
                    closeNote (curLast);
                pendCount = 0;
                continue;
            }

            const int r = (int) std::lround (v);
            if (! active)
            {
                if (pendCount > 0 && pendPitch == r)
                    ++pendCount;
                else
                {
                    pendPitch = r;
                    pendStart = t;
                    pendCount = 1;
                }
                if (pendCount >= 3)
                {
                    active = true;
                    curPitch = pendPitch;
                    curFirst = pendStart;
                    curLast = t;
                    curPeak = rmsFrame[t];
                    gap = 0;
                    pendCount = 0;
                }
                continue;
            }

            gap = 0;
            if (std::abs (v - (float) curPitch) <= 0.85f)
            {
                curLast = t;
                pendCount = 0;
            }
            else
            {
                if (pendCount > 0 && pendPitch == r)
                    ++pendCount;
                else
                {
                    pendPitch = r;
                    pendStart = t;
                    pendCount = 1;
                }
                if (pendCount >= 4)
                {
                    closeNote (pendStart > 0 ? pendStart - 1 : 0);
                    active = true;
                    curPitch = pendPitch;
                    curFirst = pendStart;
                    curLast = t;
                    curPeak = rmsFrame[t];
                    pendCount = 0;
                }
            }
        }
        if (active)
            closeNote (curLast);

        // spezza le note ripetute sugli attacchi dell'inviluppo
        std::vector<Note> out;
        for (const Note& nt : notes)
        {
            Note cur = nt;
            for (size_t i = 0; i < env.onsets.size(); ++i)
            {
                const double o = env.onsets[i];
                if (o > cur.start + 0.06 && o < cur.end - 0.05)
                {
                    Note first = cur;
                    first.end = o;
                    out.push_back (first);
                    cur.start = o;
                }
            }
            out.push_back (cur);
        }
        return out;
    }

    //==========================================================================
    // Chitarra / Piano: più altezze per frame
    static constexpr int kMaxPartials = 28;

    struct Cand
    {
        int midi = 0;
        int nh = 0;          // armoniche totali (fino a ~5 kHz): servono per cancellare l'intera serie
        int nScore = 0;      // le prime armoniche, usate per il punteggio
        int lo[kMaxPartials] {};
        int hi[kMaxPartials] {};
        float bin[kMaxPartials] {};
        float w[kMaxPartials] {};
    };

    static float lerp (float a, float b, float t) { return a + (b - a) * t; }

    static std::vector<Note> trackPoly (const float* x, size_t n, double sr, Instrument inst, float sens, const Envelope& env,
                                        const ProgressFn& progress, const CancelFn& cancelled)
    {
        const float relThr = lerp (0.40f, 0.12f, sens);      // nota secondaria / nota principale
        const float absThr = lerp (0.90f, 0.35f, sens);      // rispetto al picco massimo del frame
        const float fundThr = lerp (0.25f, 0.08f, sens);     // presenza della fondamentale
        const size_t minFrames = (size_t) std::lround (lerp (10.0f, 5.0f, sens));

        const size_t W = 4096, NF = 8192, hop = 512;
        const int lowMidi = inst == Instrument::Guitar ? 38 : 28;
        const int highMidi = inst == Instrument::Guitar ? 88 : 100;
        const int maxPoly = inst == Instrument::Guitar ? 6 : 8;
        const double fMax = std::min (5000.0, sr * 0.45);
        const size_t kMax = std::min<size_t> (NF / 2 - 2, (size_t) (fMax * (double) NF / sr) + 2);
        const size_t kLow = std::max<size_t> (2, (size_t) (40.0 * (double) NF / sr));

        if (n < W + hop)
            return {};

        // candidati
        std::vector<Cand> cands;
        for (int m = lowMidi; m <= highMidi; ++m)
        {
            Cand c;
            c.midi = m;
            const double f0 = 440.0 * std::pow (2.0, (double) (m - 69) / 12.0);
            for (int h = 1; h <= kMaxPartials; ++h)
            {
                const double f = f0 * (double) h;
                if (f > fMax)
                    break;
                const double bin = f * (double) NF / sr;
                const double hw = std::max (0.9, f * 0.02 * (double) NF / sr);
                c.bin[c.nh] = (float) bin;
                c.lo[c.nh] = std::max (1, (int) std::floor (bin - hw));
                c.hi[c.nh] = std::min ((int) kMax, (int) std::ceil (bin + hw));
                c.w[c.nh] = (float) (1.0 / std::sqrt ((double) h));
                ++c.nh;
            }
            c.nScore = std::min (c.nh, 8);
            if (c.nScore >= 2)
                cands.push_back (c);
        }
        const size_t nc = cands.size();

        const FftPlan plan (NF);
        std::vector<double> hann (W);
        for (size_t i = 0; i < W; ++i)
            hann[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) (W - 1));

        const size_t frames = (n - W) / hop + 1;
        std::vector<std::vector<float>> act (nc, std::vector<float> (frames, 0.0f));
        std::vector<std::vector<float>> rawF (nc, std::vector<float> (frames, 0.0f));   // fondamentale prima delle cancellazioni

        std::vector<std::complex<double>> buf (NF);
        std::vector<float> M (kMax + 2), R (kMax + 2), tmp;
        tmp.reserve (kMax);

        for (size_t t = 0; t < frames; ++t)
        {
            if ((t & 31) == 0)
            {
                if (cancelled && cancelled())
                    return {};
                if (progress)
                    progress (0.9f * (float) t / (float) frames);
            }

            const size_t start = t * hop;
            double e = 0.0;
            for (size_t i = 0; i < W; ++i)
            {
                const double s = (double) x[start + i];
                e += s * s;
                buf[i] = std::complex<double> (s * hann[i], 0.0);
            }
            for (size_t i = W; i < NF; ++i)
                buf[i] = std::complex<double> (0.0, 0.0);

            if (std::sqrt (e / (double) W) < (double) env.gate)
                continue;

            plan.forward (buf.data());

            tmp.clear();
            for (size_t k = 0; k <= kMax; ++k)
            {
                M[k] = (float) std::pow (std::abs (buf[k]), 0.6);
                if (k >= kLow)
                    tmp.push_back (M[k]);
            }
            std::nth_element (tmp.begin(), tmp.begin() + (long) (tmp.size() / 2), tmp.end());
            const float floorV = tmp[tmp.size() / 2];

            float rmax = 0.0f;
            for (size_t k = 0; k <= kMax; ++k)
            {
                R[k] = std::max (0.0f, M[k] - 2.0f * floorV);
                rmax = std::max (rmax, R[k]);
            }
            if (rmax <= 0.0f)
                continue;

            for (size_t ci = 0; ci < nc; ++ci)
            {
                float pkF = 0.0f;
                for (int k = cands[ci].lo[0]; k <= cands[ci].hi[0]; ++k)
                    pkF = std::max (pkF, R[(size_t) k]);
                rawF[ci][t] = pkF;
            }

            float S0 = 0.0f;
            for (int pick = 0; pick < maxPoly; ++pick)
            {
                size_t best = nc;
                float bestS = 0.0f;
                for (size_t ci = 0; ci < nc; ++ci)
                {
                    const Cand& c = cands[ci];
                    float s = 0.0f;
                    float fund = 0.0f;
                    for (int h = 0; h < c.nScore; ++h)
                    {
                        float pk = 0.0f;
                        for (int k = c.lo[h]; k <= c.hi[h]; ++k)
                            pk = std::max (pk, R[(size_t) k]);
                        if (h == 0)
                            fund = pk;
                        s += c.w[h] * pk;
                    }
                    if (fund < fundThr * rmax)   // deve esserci davvero la fondamentale
                        continue;
                    if (s > bestS)
                    {
                        bestS = s;
                        best = ci;
                    }
                }

                if (best == nc)
                    break;
                if (pick == 0)
                    S0 = bestS;
                else if (bestS < relThr * S0)
                    break;
                if (bestS < absThr * rmax)
                    break;

                act[best][t] = bestS;

                // Cancella l'intera serie armonica della nota scelta.
                // Le armoniche "musicali" (ottava, ottava+quinta, due ottave...) possono coincidere con note vere
                // suonate insieme: li' si toglie solo l'inviluppo "liscio" (principio della levigatezza spettrale).
                // Tutte le altre armoniche (5, 7, 9, 10, 11...) non sono note plausibili: si tolgono per intero.
                const Cand& c = cands[best];
                float pk[kMaxPartials] {};
                for (int h = 0; h < c.nh; ++h)
                    for (int k = c.lo[h]; k <= c.hi[h]; ++k)
                        pk[h] = std::max (pk[h], R[(size_t) k]);

                for (int h = 0; h < c.nh; ++h)
                {
                    const int order = h + 1;
                    const bool musical = order == 2 || order == 3 || order == 4 || order == 6 || order == 8 || order == 12 || order == 16;
                    float q = pk[h];
                    float factor = 1.0f;
                    if (musical)
                    {
                        factor = 0.85f;
                        if (h + 1 < c.nh)
                            q = std::min (q, 0.5f * (pk[h - 1] + pk[h + 1]));
                        else
                            q = std::min (q, pk[h - 1]);
                    }

                    const int ctr = (int) std::lround (c.bin[h]);
                    const int a = std::max (1, std::min (c.lo[h], ctr - 4));
                    const int b = std::min ((int) kMax, std::max (c.hi[h], ctr + 4));
                    for (int k = a; k <= b; ++k)
                        R[(size_t) k] = std::max (0.0f, R[(size_t) k] - q * factor);
                }
            }
        }

        // da attivazioni a note
        const double centre = (double) W * 0.5 / sr;
        auto frameTime = [&] (size_t t) { return (double) (t * hop) / sr + centre; };
        auto onsetNear = [&] (double tm)
        {
            auto it = std::lower_bound (env.onsets.begin(), env.onsets.end(), tm - 0.07);
            return it != env.onsets.end() && *it <= tm + 0.07;
        };

        struct Run { size_t s, e; };
        std::vector<Note> notes;

        for (size_t ci = 0; ci < nc; ++ci)
        {
            const std::vector<float>& a = act[ci];
            const std::vector<float>& rf = rawF[ci];

            std::vector<Run> runs;
            for (size_t t = 0; t < frames;)
            {
                if (a[t] <= 0.0f)
                {
                    ++t;
                    continue;
                }
                size_t t1 = t;
                while (t1 + 1 < frames && a[t1 + 1] > 0.0f)
                    ++t1;
                runs.push_back ({ t, t1 });
                t = t1 + 1;
            }

            // richiude le interruzioni di una nota tenuta (la nota e' ancora li', solo mascherata)
            std::vector<Run> merged;
            for (const Run& r : runs)
            {
                if (! merged.empty())
                {
                    Run& m = merged.back();
                    const size_t gap = r.s - m.e - 1;
                    bool join = gap <= 2;
                    if (! join && gap <= 12)
                    {
                        float runMax = 0.0f, gapMin = 1.0e30f, after = 0.0f;
                        for (size_t u = m.s; u <= m.e; ++u)
                            runMax = std::max (runMax, rf[u]);
                        for (size_t u = m.e + 1; u < r.s; ++u)
                            gapMin = std::min (gapMin, rf[u]);
                        for (size_t u = r.s; u <= std::min (r.s + 3, r.e); ++u)
                            after = std::max (after, rf[u]);
                        const bool sustained = gapMin >= 0.35f * runMax;
                        const bool restrike = after > 1.8f * gapMin && onsetNear (frameTime (r.s));
                        join = sustained && ! restrike;
                    }
                    if (join)
                    {
                        m.e = r.e;
                        continue;
                    }
                }
                merged.push_back (r);
            }

            for (const Run& m : merged)
            {
                // nuova nota solo se la nota risale davvero e c'e' un attacco in quel punto
                std::vector<size_t> cuts { m.s };
                float peak = a[m.s], valleyA = a[m.s], valleyRf = rf[m.s];
                bool declined = false;
                size_t lastCut = m.s;
                for (size_t u = m.s + 1; u <= m.e; ++u)
                {
                    if (! declined)
                    {
                        if (a[u] > 0.0f)
                            peak = std::max (peak, a[u]);
                        if (a[u] > 0.0f && a[u] < 0.9f * peak)
                        {
                            declined = true;
                            valleyA = a[u];
                            valleyRf = rf[u];
                        }
                    }
                    else
                    {
                        if (a[u] > 0.0f)
                            valleyA = std::min (valleyA, a[u]);
                        valleyRf = std::min (valleyRf, rf[u]);
                        if (u - lastCut >= 4 && a[u] > 1.35f * valleyA && rf[u] > 1.35f * valleyRf)
                        {
                            cuts.push_back (u);
                            lastCut = u;
                            declined = false;
                            peak = a[u];
                            valleyA = a[u];
                            valleyRf = rf[u];
                        }
                    }
                }
                cuts.push_back (m.e + 1);

                for (size_t k = 0; k + 1 < cuts.size(); ++k)
                {
                    const size_t f0 = cuts[k], f1 = cuts[k + 1] - 1;
                    if (f1 + 1 - f0 < minFrames)
                        continue;
                    Note nt;
                    nt.midi = cands[ci].midi;
                    nt.start = frameTime (f0);
                    nt.end = frameTime (f1) + (double) hop / sr * 0.5;
                    float sum = 0.0f;
                    for (size_t u = f0; u <= f1; ++u)
                        sum += a[u];
                    nt.strength = sum / (float) (f1 + 1 - f0);
                    notes.push_back (nt);
                }
            }
        }
        removeHarmonicGhosts (notes);
        return notes;
    }

    // Scarta le note deboli che stanno a un intervallo armonico sopra una nota piu' forte
    // che parte quasi insieme (sono le armoniche della corda, non note suonate).
    static void removeHarmonicGhosts (std::vector<Note>& notes)
    {
        std::vector<char> drop (notes.size(), 0);
        for (size_t i = 0; i < notes.size(); ++i)
            for (size_t j = 0; j < notes.size(); ++j)
            {
                if (i == j || notes[i].midi <= notes[j].midi)
                    continue;
                // la nota piu' bassa deve suonare quando parte quella alta (anche se e' partita prima)
                if (notes[j].start > notes[i].start + 0.08 || notes[j].end < notes[i].start)
                    continue;
                const int iv = notes[i].midi - notes[j].midi;
                float ratio = 0.0f;
                if (iv == 19 || iv == 28 || iv == 31 || iv == 34)
                    ratio = 0.6f;
                else if (iv == 12 || iv == 24)
                    ratio = 0.22f;   // l'ottava puo' essere una nota vera: scarta solo se molto piu' debole
                else
                    continue;
                if (notes[i].strength < ratio * notes[j].strength)
                    drop[i] = 1;
            }
        std::vector<Note> kept;
        for (size_t i = 0; i < notes.size(); ++i)
            if (! drop[i])
                kept.push_back (notes[i]);
        notes.swap (kept);
    }

    //==========================================================================
    static void finalize (std::vector<Note>& notes, const Envelope& env, double sr)
    {
        // aggancia gli inizi agli attacchi
        for (Note& nt : notes)
        {
            nt.start = std::max (0.0, snapToOnset (nt.start, env, 0.04, 0.11));
            if (nt.end < nt.start + 0.04)
                nt.end = nt.start + 0.04;
        }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                   { return a.start < b.start || (a.start == b.start && a.midi < b.midi); });

        // niente sovrapposizioni sulla stessa altezza
        for (size_t i = 0; i < notes.size(); ++i)
            for (size_t j = i + 1; j < notes.size(); ++j)
            {
                if (notes[j].start >= notes[i].end)
                    break;
                if (notes[j].midi == notes[i].midi)
                {
                    notes[i].end = std::max (notes[i].start + 0.04, notes[j].start);
                    break;
                }
            }

        // velocity dall'inviluppo
        const float ref = std::max (env.ref, 1.0e-6f);
        for (Note& nt : notes)
        {
            const float pk = peakRms (env, nt.start, std::min (nt.end, nt.start + 0.25), sr);
            const float rel = std::min (1.3f, pk / ref);
            nt.velocity = std::max (25, std::min (127, (int) std::lround (35.0f + 85.0f * std::pow (rel, 0.7f))));
        }
    }
};
} // namespace scriba
