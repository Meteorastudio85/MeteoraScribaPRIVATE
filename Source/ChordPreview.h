// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// A small soft-piano voice to listen to a chord when its button is clicked (pure C++, no allocation on the
// audio thread). The interface hands over a short list of note events; the audio thread plays them and mixes
// them into the output, after the recording, so they never end up in the analysis.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "NoteTranscriber.h"

namespace scriba
{
class ChordPreview
{
public:
    static constexpr int kMaxEvents = 192;
    static constexpr int kVoices = 24;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& v : voices)
            v.active = false;
        count = 0;
    }

    // message thread: play these notes now (times in seconds from now); replaces what was playing
    void play (const std::vector<Note>& notes)
    {
        std::lock_guard<std::mutex> lk (handoff);
        pendingCount = 0;
        for (const auto& n : notes)
        {
            if (pendingCount >= kMaxEvents)
                break;
            pending[(size_t) pendingCount++] = n;
        }
        pendingSeq.fetch_add (1);
    }

    void stop()
    {
        std::lock_guard<std::mutex> lk (handoff);
        pendingCount = 0;
        pendingSeq.fetch_add (1);
    }

    // audio thread: adds the preview to the buffer (all channels the same)
    void render (float* const* channels, int numChannels, int numSamples)
    {
        const int seq = pendingSeq.load();
        if (seq != takenSeq)
        {
            std::unique_lock<std::mutex> lk (handoff, std::try_to_lock);
            if (lk.owns_lock())
            {
                takenSeq = seq;
                count = pendingCount;
                std::copy (pending.begin(), pending.begin() + count, events.begin());
                std::fill (started.begin(), started.end(), false);
                clock = 0;
                for (auto& v : voices)
                    if (v.active)
                        v.releaseAt = std::min (v.releaseAt, v.age);   // fade out what was playing
            }
        }
        bool any = count > 0;
        for (const auto& v : voices)
            any = any || v.active;
        if (! any || numChannels <= 0)
            return;

        for (int i = 0; i < numSamples; ++i, ++clock)
        {
            const double t = (double) clock / sr;
            for (int e = 0; e < count; ++e)
                if (! started[(size_t) e] && t >= events[(size_t) e].start)
                {
                    started[(size_t) e] = true;
                    startVoice (events[(size_t) e]);
                }
            float s = 0.0f;
            for (auto& v : voices)
                if (v.active)
                    s += v.next (sr);
            s = std::tanh (s * 0.9f) * 0.55f;   // soft limit: never louder than about -5 dBFS
            for (int c = 0; c < numChannels; ++c)
                channels[c][i] += s;
        }
        // all events played and all voices done: idle again
        bool pendingLeft = false;
        for (int e = 0; e < count; ++e)
            pendingLeft = pendingLeft || ! started[(size_t) e];
        if (! pendingLeft)
            count = 0;
    }

private:
    struct Voice
    {
        bool active = false;
        double phase = 0.0, inc = 0.0;
        long age = 0, releaseAt = 0;
        float amp = 0.0f, rel = 1.0f;

        float next (double sr)
        {
            const double t = (double) age / sr;
            // soft piano: fast attack, two-stage decay, a little 2nd and 3rd harmonic that fade sooner
            float env = (float) std::min (1.0, t / 0.004) * (0.65f * (float) std::exp (-t * 2.2) + 0.35f * (float) std::exp (-t * 0.6));
            if (age >= releaseAt)
            {
                rel *= 0.9993f;   // ~ 70 ms fade at 44.1 kHz
                if (rel < 0.001f)
                {
                    active = false;
                    return 0.0f;
                }
            }
            const double p = phase * 2.0 * 3.14159265358979323846;
            const float bright = (float) std::exp (-t * 3.0);
            const float s = (float) (std::sin (p) + 0.22 * bright * std::sin (2.0 * p) + 0.07 * bright * std::sin (3.0 * p));
            phase += inc;
            if (phase >= 1.0)
                phase -= 1.0;
            ++age;
            return s * env * amp * rel;
        }
    };

    void startVoice (const Note& n)
    {
        Voice* v = nullptr;
        for (auto& x : voices)
            if (! x.active)
            {
                v = &x;
                break;
            }
        if (v == nullptr)   // steal the oldest
        {
            v = &voices[0];
            for (auto& x : voices)
                if (x.age > v->age)
                    v = &x;
        }
        const double f = 440.0 * std::pow (2.0, ((double) n.midi - 69.0) / 12.0);
        v->active = true;
        v->phase = 0.0;
        v->inc = f / sr;
        v->age = 0;
        v->releaseAt = (long) (std::max (0.05, n.end - n.start) * sr);
        v->rel = 1.0f;
        // quieter up high, a bit louder in the bass, scaled by velocity
        v->amp = 0.16f * ((float) n.velocity / 110.0f) * (float) std::pow (2.0, -((double) n.midi - 60.0) / 48.0);
    }

    double sr = 44100.0;
    std::array<Voice, kVoices> voices {};
    std::array<Note, kMaxEvents> events {};
    std::array<bool, kMaxEvents> started {};
    int count = 0;
    long clock = 0;
    int takenSeq = 0;

    std::mutex handoff;
    std::array<Note, kMaxEvents> pending {};
    int pendingCount = 0;
    std::atomic<int> pendingSeq { 0 };
};
} // namespace scriba
