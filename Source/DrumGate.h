// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Drum gate and snare pitch shifter (pure C++, no JUCE).
//
// DrumGate: the same code runs LIVE on the track (what you hear) and OFFLINE on the recording
// (where the MIDI hits come from), so the MIDI has exactly the hits the gate lets through.
//   - Detector: the signal filtered for the drum (kick: 30-120 Hz, snare: 180 Hz-8 kHz, so the kick
//     does not open the snare gate), then a peak follower in dB.
//   - Opens when the detector goes over the threshold, stays open at least for the hold time and
//     closes when it falls 4 dB under the threshold (hysteresis: no chattering).
//   - Every opening is one hit; its velocity comes from how far over the threshold the hit goes.
//
// SnareShifter: pitch shifter for one-shot drums. Two read heads on a delay line, crossfaded
// (classic "rotating tape head"), but at every hit the active head is reset close to the dry signal,
// so the attack of each hit comes out clean and the grains only touch the decaying tail.

#include <algorithm>
#include <cmath>
#include <vector>

namespace scriba
{
class DrumGate
{
public:
    void prepare (double sampleRate, bool isKick)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        kick = isKick;
        const double hp = kick ? 30.0 : 180.0;
        const double lp = kick ? 120.0 : std::min (8000.0, sr * 0.4);
        hp1 = makeBiquad (true, hp);
        hp2 = hp1;
        lp1 = makeBiquad (false, lp);
        lp2 = lp1;
        envRelease = (float) std::exp (-1.0 / (0.025 * sr));
        gainAttack = (float) std::exp (-1.0 / (0.0003 * sr));
        gainRelease = (float) std::exp (-1.0 / ((kick ? 0.045 : 0.060) * sr));
        holdSamples = (int) ((kick ? 0.060 : 0.080) * sr);
        minGapSamples = (int) (0.040 * sr);
        velWindow = (int) (0.030 * sr);
        reset();
    }

    void reset()
    {
        for (auto* b : { &hp1, &hp2, &lp1, &lp2 })
            b->z1 = b->z2 = 0.0;
        env = 0.0f;
        gain = 0.0f;
        open = false;
        holdLeft = 0;
        sinceOpen = 1 << 30;
        velLeft = 0;
        peakDb = -120.0f;
    }

    void setThresholdDb (float db) { thresholdDb = db; }
    bool isKick() const { return kick; }

    // one mono input sample. Returns true when the gate opens (= a hit starts)
    bool process (float x)
    {
        double y = hp1.run (x);
        y = hp2.run (y);
        y = lp1.run (y);
        y = lp2.run (y);
        const float a = (float) std::abs (y);
        env = a > env ? a : env * envRelease;
        levelDb = 20.0f * std::log10 (env + 1.0e-9f);

        bool opened = false;
        ++sinceOpen;
        if (! open)
        {
            if (levelDb > thresholdDb && sinceOpen > minGapSamples)
            {
                open = true;
                opened = true;
                holdLeft = holdSamples;
                sinceOpen = 0;
                velLeft = velWindow;
                peakDb = levelDb;
            }
        }
        else
        {
            if (holdLeft > 0)
                --holdLeft;
            else if (levelDb < thresholdDb - 4.0f)
                open = false;
        }
        if (velLeft > 0)
        {
            --velLeft;
            peakDb = std::max (peakDb, levelDb);
        }

        const float target = open ? 1.0f : 0.0f;
        const float c = target > gain ? gainAttack : gainRelease;
        gain = target + (gain - target) * c;
        return opened;
    }

    float getGain() const { return gain; }
    float getLevelDb() const { return levelDb; }
    bool isOpen() const { return open; }
    bool velocityReady() const { return velLeft == 0; }
    float getHitPeakDb() const { return peakDb; }

    // velocity of a hit from how far it goes over the threshold (30 dB over = full velocity)
    static int velocityFor (float peakDb, float thresholdDb)
    {
        const float x = std::max (0.0f, std::min (1.0f, (peakDb - thresholdDb) / 30.0f));
        return std::max (30, std::min (127, (int) std::lround (30.0f + 97.0f * std::pow (x, 0.7f))));
    }

    // offline: every hit of a recording, plus the detector level for drawing (bins values in dB)
    struct OfflineHit { double time; int velocity; float peakDb; };
    static std::vector<OfflineHit> run (const float* x, size_t n, double sr, bool kick, float thresholdDb,
                                        std::vector<float>* overviewDb = nullptr, size_t bins = 600)
    {
        DrumGate g;
        g.prepare (sr, kick);
        g.setThresholdDb (thresholdDb);
        std::vector<OfflineHit> hits;
        if (overviewDb != nullptr)
            overviewDb->assign (bins, -120.0f);
        bool pending = false;
        for (size_t i = 0; i < n; ++i)
        {
            if (g.process (x[i]))
            {
                if (pending && ! hits.empty())
                    hits.back().velocity = velocityFor (g.getHitPeakDb(), thresholdDb);
                hits.push_back ({ (double) i / sr, 64, g.getHitPeakDb() });
                pending = true;
            }
            if (pending && g.velocityReady())
            {
                hits.back().peakDb = g.getHitPeakDb();
                hits.back().velocity = velocityFor (g.getHitPeakDb(), thresholdDb);
                pending = false;
            }
            if (overviewDb != nullptr && n > 0)
            {
                const size_t b = std::min (bins - 1, (size_t) ((double) i / (double) n * (double) bins));
                (*overviewDb)[b] = std::max ((*overviewDb)[b], g.getLevelDb());
            }
        }
        return hits;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    Biquad makeBiquad (bool highpass, double f) const
    {
        const double w0 = 2.0 * 3.14159265358979323846 * std::min (f, sr * 0.45) / sr;
        const double alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double c = std::cos (w0);
        const double a0 = 1.0 + alpha;
        Biquad b;
        if (highpass) { b.b0 = (1.0 + c) * 0.5 / a0; b.b1 = -(1.0 + c) / a0; }
        else          { b.b0 = (1.0 - c) * 0.5 / a0; b.b1 = (1.0 - c) / a0; }
        b.b2 = b.b0;
        b.a1 = -2.0 * c / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    double sr = 44100.0;
    bool kick = false;
    Biquad hp1, hp2, lp1, lp2;
    float env = 0.0f, levelDb = -120.0f, gain = 0.0f;
    float envRelease = 0.999f, gainAttack = 0.9f, gainRelease = 0.999f;
    float thresholdDb = -30.0f, peakDb = -120.0f;
    bool open = false;
    int holdSamples = 0, holdLeft = 0, minGapSamples = 0, sinceOpen = 1 << 30, velWindow = 0, velLeft = 0;
};

//==============================================================================
// Multi-channel delay line with a dry tap (fixed latency) and a pitch-shifted tap.
class SnareShifter
{
public:
    void prepare (double sampleRate, int channels, int latencySamples)
    {
        sr = sampleRate;
        latency = latencySamples;
        window = std::max (64.0, 0.030 * sr);
        fadeLen = std::max (16.0, 0.006 * sr);
        size_t need = (size_t) (latency + window * 2 + fadeLen + 64);
        size = 1;
        while (size < need)
            size <<= 1;
        buf.assign ((size_t) std::max (1, channels), std::vector<float> (size, 0.0f));
        w = 0;
        resetHeads();
    }

    int getLatency() const { return latency; }

    // writes one input frame (one sample per channel)
    void push (const float* frame, int channels)
    {
        for (int c = 0; c < channels && c < (int) buf.size(); ++c)
            buf[(size_t) c][w] = frame[c];
        w = (w + 1) & (size - 1);
    }

    // a hit starts now (at the input): the next head starts again right next to the dry signal
    void onset()
    {
        if (std::abs (ratio - 1.0) < 1.0e-4)
            return;
        // the old head must not reach the new attack while it fades out (it would play it early)
        const double room = head[active] - 2.0;
        startFade ((double) latency, std::max (1.0, std::min (0.001 * sr, room * 0.5)));
    }

    void setSemitones (double st)
    {
        ratio = std::pow (2.0, st / 12.0);
    }

    bool isShifting() const { return std::abs (ratio - 1.0) >= 1.0e-4; }

    // one output frame (call after push). dry = fixed latency, else pitch shifted
    void read (float* out, int channels)
    {
        if (! isShifting())
        {
            for (int c = 0; c < channels && c < (int) buf.size(); ++c)
                out[c] = buf[(size_t) c][(w - 1 - (size_t) latency) & (size - 1)];
            resetHeads();
            return;
        }

        for (auto& d : head)
            d += 1.0 - ratio;

        const double minD = 2.0, maxD = (double) latency + window;
        if (! fading)
        {
            const double d = head[active];
            if (ratio > 1.0 && d < minD)
                startFade (d + window, fadeLen);
            else if (ratio < 1.0 && d > maxD)
                startFade (std::max (minD, d - window), fadeLen);
        }

        float ga = 1.0f, gb = 0.0f;
        if (fading)
        {
            xf += xfStep;
            if (xf >= 1.0)
            {
                active = 1 - active;
                fading = false;
                xf = 0.0;
            }
            else
            {
                ga = (float) std::cos (xf * 1.5707963267948966);
                gb = (float) std::sin (xf * 1.5707963267948966);
            }
        }
        const int other = 1 - active;
        for (int c = 0; c < channels && c < (int) buf.size(); ++c)
        {
            float v = ga * tap ((size_t) c, std::max (minD, head[active]));
            if (fading)
                v += gb * tap ((size_t) c, std::max (minD, head[other]));
            out[c] = v;
        }
    }

private:
    void resetHeads()
    {
        head[0] = head[1] = (double) latency;
        active = 0;
        fading = false;
        xf = 0.0;
    }

    void startFade (double newDelay, double len)
    {
        if (fading)
        {
            // finish the running fade at once
            active = 1 - active;
            fading = false;
        }
        head[1 - active] = std::max (2.0, std::min ((double) size - 4.0, newDelay));
        fading = true;
        xf = 0.0;
        xfStep = 1.0 / std::max (1.0, len);
    }

    float tap (size_t c, double delay) const
    {
        // the sample written "delay" samples before the last one, linear interpolation
        const double pos = (double) ((w - 1) & (size - 1)) - delay;
        const double p = pos < 0.0 ? pos + (double) size : pos;
        const size_t i0 = (size_t) p & (size - 1);
        const size_t i1 = (i0 + 1) & (size - 1);
        const float f = (float) (p - std::floor (p));
        return buf[c][i0] + (buf[c][i1] - buf[c][i0]) * f;
    }

    double sr = 44100.0, ratio = 1.0, window = 1323.0, fadeLen = 264.0;
    int latency = 0;
    std::vector<std::vector<float>> buf;
    size_t size = 1, w = 0;
    double head[2] { 0.0, 0.0 };
    int active = 0;
    bool fading = false;
    double xf = 0.0, xfStep = 0.01;
};
} // namespace scriba
