// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// How many semitones to move the snare so that it sits on a note of the song key (pure C++, no JUCE).
//
// Main suggestion: the nearest note of the key chord (root, 3rd or 5th) - it is the most musical choice
// and it is never more than ~2.5 semitones away.
// Alternative: the nearest note of the scale, when it needs a smaller change.

#include <cmath>
#include <string>

#include "DrumAnalyzer.h"
#include "KeyDetector.h"

namespace scriba
{
struct SnareTarget
{
    bool valid = false;
    int midi = 0;          // target note
    double shift = 0.0;    // exact shift in semitones (target - measured)
    int semitones = 0;     // rounded to whole semitones
    int cents = 0;         // what is left after the whole semitones (-50..+50)
    std::string role;      // "root", "3rd", "5th", "in the scale"
};

struct SnareTuning
{
    bool valid = false;          // snare pitch AND song key known
    bool alreadyInTune = false;  // within 25 cents of a key-chord note
    double snareMidi = 0.0;      // measured pitch as a fractional MIDI note
    SnareTarget main;            // nearest root / 3rd / 5th
    SnareTarget smaller;         // nearest scale note, only when it is a smaller change than main
};

inline SnareTuning computeSnareTuning (const SnarePitch& p, int keyIdx)
{
    SnareTuning t;
    if (! p.valid || p.hz <= 0.0 || keyIdx < 0 || keyIdx > 23)
        return t;

    static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const bool minor = keyIdx >= 12;
    const int tonic = keyIdx % 12;
    const int* scale = minor ? minorScale : majorScale;
    const double midiF = 69.0 + 12.0 * std::log2 (p.hz / 440.0);
    t.snareMidi = midiF;

    auto nearest = [&] (const int* degrees, int count, const char* const* roles)
    {
        SnareTarget best;
        double bestAbs = 1.0e9;
        for (int i = 0; i < count; ++i)
        {
            const int pc = (tonic + scale[degrees[i]]) % 12;
            for (int oct = 0; oct < 11; ++oct)
            {
                const int m = oct * 12 + pc;
                const double sh = (double) m - midiF;
                // on a tie prefer going down: a slacker head is usually easier than a tighter one
                if (std::abs (sh) < bestAbs - 1.0e-9 || (std::abs (std::abs (sh) - bestAbs) < 1.0e-9 && sh < best.shift))
                {
                    bestAbs = std::abs (sh);
                    best.valid = true;
                    best.midi = m;
                    best.shift = sh;
                    best.role = roles[i];
                }
            }
        }
        best.semitones = (int) std::lround (best.shift);
        best.cents = (int) std::lround ((best.shift - (double) best.semitones) * 100.0);
        return best;
    };

    static const int chordDegrees[3] = { 0, 2, 4 };
    static const char* const chordRoles[3] = { "root", "3rd", "5th" };
    static const int scaleDegrees[7] = { 0, 1, 2, 3, 4, 5, 6 };
    static const char* const scaleRoles[7] = { "root", "in the scale", "3rd", "in the scale", "5th", "in the scale", "in the scale" };

    t.main = nearest (chordDegrees, 3, chordRoles);
    const SnareTarget scaleNote = nearest (scaleDegrees, 7, scaleRoles);
    if (scaleNote.midi != t.main.midi && std::abs (scaleNote.shift) + 0.5 <= std::abs (t.main.shift))
        t.smaller = scaleNote;

    t.alreadyInTune = std::abs (t.main.shift) <= 0.25;
    t.valid = true;
    return t;
}

// "+2" / "-1" / "0"
inline std::string signedInt (int v)
{
    return v > 0 ? "+" + std::to_string (v) : std::to_string (v);
}
} // namespace scriba
