// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// The chords of a key and the ways to play them (pure C++, no JUCE).
//
//  - diatonicChords(): the 7 chords of the key (I ii iii IV V vi vii in major, i ii III iv v VI VII in minor,
//    plus the major V that minor songs use so often), as triads, sevenths, sus2, sus4 or add9.
//  - voice(): the notes of a chord placed on the keyboard (close, open, with the bass note).
//  - render(): the chord played in a style (block, strum, pulse, arpeggios, Alberti, ballad) for a length in beats.
//  - parseChord(): "Am", "G7", "Fmaj7"... back to notes, for the progression of the whole song.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "KeyDetector.h"
#include "NoteTranscriber.h"

namespace scriba
{
struct ChordDef
{
    int root = 0;                  // pitch class 0..11
    std::vector<int> intervals;    // from the root, in semitones
    std::string name;              // "Am", "G7", "Bm7b5" ...
    std::string roman;             // "vi", "V7" ...
};

enum class ChordType { Triad = 0, Seventh, Sus2, Sus4, Add9 };
enum class ChordStyle { Block = 0, Strum, Quarters, Eighths, ArpUp, ArpUpDown, Alberti, Ballad,
                        // two-hand piano parts (left hand bass line + right hand rhythm, humanised)
                        PopPiano, BalladArp, Gospel, DrivingPop, Cinematic, NeoSoul };

inline bool isPianoStyle (ChordStyle s) { return (int) s >= (int) ChordStyle::PopPiano; }
enum class ChordVoicing { Close = 0, Inv1, Inv2, Open, WithBass };

namespace chord_detail
{
struct Quality { const char* suffix; std::vector<int> iv; };

inline const Quality& quality (const std::string& q)
{
    static const std::vector<std::pair<std::string, Quality>> table {
        { "maj",  { "",     { 0, 4, 7 } } },       { "min",  { "m",    { 0, 3, 7 } } },
        { "dim",  { "dim",  { 0, 3, 6 } } },       { "maj7", { "maj7", { 0, 4, 7, 11 } } },
        { "m7",   { "m7",   { 0, 3, 7, 10 } } },   { "7",    { "7",    { 0, 4, 7, 10 } } },
        { "m7b5", { "m7b5", { 0, 3, 6, 10 } } },   { "sus2", { "sus2", { 0, 2, 7 } } },
        { "sus4", { "sus4", { 0, 5, 7 } } },       { "add9", { "add9", { 0, 4, 7, 14 } } },
        { "madd9",{ "madd9",{ 0, 3, 7, 14 } } },   { "5",    { "5",    { 0, 7, 12 } } },
        { "6",    { "6",    { 0, 4, 7, 9 } } },    { "m6",   { "m6",   { 0, 3, 7, 9 } } },
    };
    for (const auto& e : table)
        if (e.first == q)
            return e.second;
    return table[0].second;
}

inline std::string toRoman (int degree, bool upper)
{
    static const char* r[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    std::string s = r[degree % 7];
    if (! upper)
        for (auto& c : s)
            c = (char) (c - 'A' + 'a');
    return s;
}
} // namespace chord_detail

inline ChordDef makeChord (int root, const std::string& q, const std::string& roman)
{
    const auto& Q = chord_detail::quality (q);
    return { ((root % 12) + 12) % 12, Q.iv, std::string (pitchClassName (root)) + Q.suffix, roman };
}

// the chords of a key: 7 diatonic chords, plus the major V in minor keys
inline std::vector<ChordDef> diatonicChords (int keyIdx, ChordType type)
{
    std::vector<ChordDef> out;
    if (keyIdx < 0 || keyIdx > 23)
        return out;
    const bool minor = keyIdx >= 12;
    const int tonic = keyIdx % 12;
    static const int majScale[7] = { 0, 2, 4, 5, 7, 9, 11 }, minScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static const char* majTri[7] = { "maj", "min", "min", "maj", "maj", "min", "dim" };
    static const char* minTri[7] = { "min", "dim", "maj", "min", "min", "maj", "maj" };
    static const char* maj7[7] = { "maj7", "m7", "m7", "maj7", "7", "m7", "m7b5" };
    static const char* min7[7] = { "m7", "m7b5", "maj7", "m7", "m7", "maj7", "7" };

    auto build = [&] (int degree, const std::string& tri, const std::string& sev)
    {
        const int root = tonic + (minor ? minScale[degree] : majScale[degree]);
        const bool upper = tri == "maj";
        std::string roman = chord_detail::toRoman (degree, upper);
        if (tri == "dim")
            roman += "\xc2\xb0";   // degree sign
        std::string q = tri;
        switch (type)
        {
            case ChordType::Triad:   break;
            case ChordType::Seventh: q = sev; roman += "7"; break;
            case ChordType::Sus2:    q = "sus2"; roman += "sus2"; break;
            case ChordType::Sus4:    q = "sus4"; roman += "sus4"; break;
            case ChordType::Add9:    q = tri == "min" ? "madd9" : (tri == "dim" ? "m7b5" : "add9"); roman += tri == "dim" ? "7" : "add9"; break;
        }
        out.push_back (makeChord (root, q, roman));
    };
    for (int d = 0; d < 7; ++d)
        build (d, minor ? minTri[d] : majTri[d], minor ? min7[d] : maj7[d]);
    if (minor)   // harmonic-minor dominant: E / E7 in A minor
        build (4, "maj", "7");
    return out;
}

// "Am", "C#maj7", "G7", "F#m7b5", "Dsus4", "E5"... -> chord (empty intervals if not a chord name)
inline ChordDef parseChord (const std::string& name)
{
    ChordDef c;
    if (name.empty())
        return c;
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int root = -1;
    size_t used = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        const std::string n = names[pc];
        if (name.compare (0, n.size(), n) == 0 && n.size() > used)
        {
            root = pc;
            used = n.size();
        }
    }
    if (root < 0)
        return c;
    const std::string suffix = name.substr (used);
    static const std::vector<std::pair<std::string, std::string>> map {
        { "", "maj" }, { "m", "min" }, { "dim", "dim" }, { "maj7", "maj7" }, { "m7", "m7" }, { "7", "7" },
        { "m7b5", "m7b5" }, { "sus2", "sus2" }, { "sus4", "sus4" }, { "add9", "add9" }, { "madd9", "madd9" },
        { "5", "5" }, { "6", "6" }, { "m6", "m6" } };
    for (const auto& m : map)
        if (m.first == suffix)
            return makeChord (root, m.second, "");
    return makeChord (root, "maj", "");
}

// a bass note alone (from a bass track) -> the chord of the key built on it (major if outside the key)
inline ChordDef chordOnBass (const std::string& noteName, int keyIdx)
{
    const ChordDef n = parseChord (noteName);
    if (n.intervals.empty())
        return n;
    if (keyIdx >= 0)
        for (const auto& c : diatonicChords (keyIdx, ChordType::Triad))
            if (c.root == n.root)
                return c;
    return makeChord (n.root, "maj", "");
}

// the notes of a chord on the keyboard (MIDI numbers, low to high)
inline std::vector<int> voice (const ChordDef& c, ChordVoicing v)
{
    std::vector<int> notes;
    if (c.intervals.empty())
        return notes;
    // root placed so that the chord sits around middle C
    int root = 48 + c.root;
    if (root < 53)
        root += 12;
    if (v == ChordVoicing::Open)
    {
        // root low, fifth, then the third (and the rest) one octave up: a wide piano / pad sound
        const int r = root - 12;
        notes.push_back (r);
        for (size_t i = 1; i < c.intervals.size(); ++i)
        {
            const int iv = c.intervals[i];
            notes.push_back (r + (iv == 7 || iv == 6 || iv == 5 ? iv : iv + 12));
        }
    }
    else
    {
        for (int iv : c.intervals)
            notes.push_back (root + iv);
        // inversions: the lowest note (or the two lowest) goes up one octave
        const int lift = v == ChordVoicing::Inv1 ? 1 : v == ChordVoicing::Inv2 ? 2 : 0;
        for (int i = 0; i < lift && i < (int) notes.size() - 1; ++i)
            notes[(size_t) i] += 12;
        if (lift > 0)
        {
            std::sort (notes.begin(), notes.end());
            if (notes.front() > 64)   // keep it around middle C
                for (auto& m : notes)
                    m -= 12;
        }
        if (v == ChordVoicing::WithBass)
            notes.push_back (root - 12);
    }
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    return notes;
}

// close voicing that moves as little as possible from the previous chord (smooth voice leading for a
// progression); with no previous chord it is the chord in root position
inline std::vector<int> voiceSmooth (const ChordDef& c, const std::vector<int>& prev, bool withBass)
{
    std::vector<int> base = voice (c, ChordVoicing::Close);
    if (base.empty() || prev.empty())
    {
        if (withBass && ! base.empty())
            base.insert (base.begin(), 36 + c.root + (36 + c.root < 40 ? 12 : 0));
        return base;
    }
    std::vector<int> upper;   // previous chord without its bass note
    for (size_t i = (withBass ? 1 : 0); i < prev.size(); ++i)
        upper.push_back (prev[i]);
    if (upper.empty())
        upper = prev;
    std::vector<int> best;
    double bestCost = 1.0e9;
    const int n = (int) base.size();
    for (int inv = 0; inv < n; ++inv)
        for (int oct = -1; oct <= 1; ++oct)
        {
            std::vector<int> cand (base);
            for (int i = 0; i < inv; ++i)
                cand[(size_t) i] += 12;
            for (auto& m : cand)
                m += 12 * oct;
            std::sort (cand.begin(), cand.end());
            if (cand.front() < 50 || cand.back() > 81)
                continue;
            double cost = 0.0;   // every note to the nearest note of the previous chord
            for (int m : cand)
            {
                int d = 99;
                for (int p : upper)
                    d = std::min (d, std::abs (m - p));
                cost += d;
            }
            cost += 0.15 * std::abs ((cand.front() + cand.back()) * 0.5 - 64.0);   // stay near the middle
            if (cost < bestCost)
            {
                bestCost = cost;
                best = cand;
            }
        }
    if (best.empty())
        best = base;
    if (withBass)
        best.insert (best.begin(), 36 + c.root + (36 + c.root < 40 ? 12 : 0));
    return best;
}

// the same chord as `c` (same root and third) in the chosen type when it belongs to the key, else `c`
inline ChordDef withType (const ChordDef& c, ChordType type, int keyIdx)
{
    if (type == ChordType::Triad || keyIdx < 0 || c.intervals.size() < 2)
        return c;
    const auto tri = diatonicChords (keyIdx, ChordType::Triad);
    const auto typed = diatonicChords (keyIdx, type);
    for (size_t i = 0; i < tri.size() && i < typed.size(); ++i)
        if (tri[i].root == c.root && tri[i].intervals.size() >= 2 && tri[i].intervals[1] == c.intervals[1])
            return typed[i];
    return c;
}

// the chord played in a style, from time t0 (seconds) for `beats` quarter-note beats
inline void render (std::vector<Note>& out, const std::vector<int>& v, ChordStyle style, double t0, double beats,
                    double bpm, double beatsPerBar)
{
    if (v.empty() || beats <= 0.0)
        return;
    const double q = 60.0 / bpm;   // one beat in seconds
    auto add = [&] (int midi, double startBeat, double lenBeats, int vel)
    {
        if (startBeat >= beats - 1.0e-6)
            return;
        lenBeats = std::min (lenBeats, beats - startBeat);
        Note n;
        n.start = t0 + startBeat * q;
        n.end = n.start + std::max (0.05, lenBeats * q * 0.97);
        n.midi = std::max (0, std::min (127, midi));
        n.velocity = std::max (1, std::min (127, vel));
        out.push_back (n);
    };
    auto accent = [&] (double b) { return std::fmod (b, beatsPerBar) < 1.0e-6 ? 100 : (std::fmod (b, 1.0) < 1.0e-6 ? 86 : 74); };

    switch (style)
    {
        case ChordStyle::Block:
            for (int m : v) add (m, 0.0, beats, 92);
            break;
        case ChordStyle::Strum:
            for (size_t i = 0; i < v.size(); ++i)
                add (v[i], (double) i * 0.022 / q, beats, 96 - (int) i * 3);
            break;
        case ChordStyle::Quarters:
            for (double b = 0.0; b < beats; b += 1.0)
                for (int m : v) add (m, b, 0.9, accent (b));
            break;
        case ChordStyle::Eighths:
            for (double b = 0.0; b < beats; b += 0.5)
                for (int m : v) add (m, b, 0.45, accent (b));
            break;
        case ChordStyle::ArpUp:
        {
            int i = 0;
            for (double b = 0.0; b < beats; b += 0.5, ++i)
                add (v[(size_t) i % v.size()], b, 0.5, accent (b));
            break;
        }
        case ChordStyle::ArpUpDown:
        {
            std::vector<int> seq (v);
            for (int i = (int) v.size() - 2; i >= 1; --i)
                seq.push_back (v[(size_t) i]);
            int i = 0;
            for (double b = 0.0; b < beats; b += 0.5, ++i)
                add (seq[(size_t) i % seq.size()], b, 0.5, accent (b));
            break;
        }
        case ChordStyle::Alberti:
        {
            // low - high - middle - high
            const int lo = v.front(), hi = v.back(), mid = v[v.size() / 2];
            const int pat[4] = { lo, hi, mid, hi };
            int i = 0;
            for (double b = 0.0; b < beats; b += 0.5, ++i)
                add (pat[i % 4], b, 0.5, accent (b));
            break;
        }
        case ChordStyle::Ballad:
        {
            // left hand: root on 1, fifth on 3; right hand: the chord on 1 and 3
            const int bass = v.front() >= 48 ? v.front() - 12 : v.front();
            for (double b = 0.0; b < beats; b += beatsPerBar)
            {
                add (bass, b, std::min (beatsPerBar, 2.0), 96);
                if (beatsPerBar >= 3.0)
                    add (bass + 7, b + 2.0, beatsPerBar - 2.0, 80);
                for (size_t i = 0; i < v.size(); ++i)
                    if (v[i] != bass)
                    {
                        add (v[i], b, 2.0, 88);
                        if (beatsPerBar >= 3.0)
                            add (v[i], b + 2.0, beatsPerBar - 2.0, 76);
                    }
            }
            break;
        }
        default:
            break;
    }
}

//==============================================================================
// Two-hand piano parts. A pattern is a list of hits on the bar (in beats); the left hand plays bass, octaves,
// fifths, tenths and flowing arpeggios, with a walk into the next chord; the right hand plays the chord with
// syncopations, inversions, grace notes, rolls and runs. Timing and velocity are humanised a little
// (always the same for the same chord, so the MIDI does not change every time you drag it).
namespace piano_detail
{
enum Kind { RH, RHUP, RHTOP, RHARP, LBASS, LOCT, LFIFTH, LARP, LAPPR, RHGRACE, RHROLL };
struct Hit { double pos, len; Kind kind; int vel; int arg; };
constexpr double kPedal = -1.0;   // length: held to the next half bar (like the sustain pedal)

inline std::vector<Hit> pattern (ChordStyle s)
{
    switch (s)
    {
        case ChordStyle::PopPiano:
            return { { 0.0, 1.5, LOCT, 96, 0 },  { 1.5, 0.5, LBASS, 74, 0 }, { 2.0, 1.0, LFIFTH, 84, 0 },
                     { 3.0, 0.5, LOCT, 84, 0 },  { 3.5, 0.5, LAPPR, 78, 0 },
                     { 0.0, 1.5, RH, 92, 0 },    { 1.5, 0.5, RH, 72, 0 },    { 2.5, 0.5, RH, 80, 0 },
                     { 3.0, 0.5, RHUP, 84, 0 },  { 3.5, 0.5, RHUP, 72, 0 } };
        case ChordStyle::BalladArp:
            return { { 0.0, kPedal, LARP, 82, 0 }, { 0.5, kPedal, LARP, 62, 1 }, { 1.0, kPedal, LARP, 66, 2 }, { 1.5, kPedal, LARP, 62, 3 },
                     { 2.0, kPedal, LARP, 76, 4 }, { 2.5, kPedal, LARP, 60, 5 }, { 3.0, kPedal, LARP, 64, 6 }, { 3.5, kPedal, LARP, 58, 7 },
                     { 0.0, 2.0, RHROLL, 80, 0 }, { 2.0, 1.0, RHUP, 70, 0 }, { 3.0, 1.0, RHTOP, 74, 0 } };
        case ChordStyle::Gospel:
            return { { 0.0, 1.5, LOCT, 100, 0 }, { 1.5, 1.0, LOCT, 88, 0 }, { 3.0, 0.5, LBASS, 84, 0 }, { 3.5, 0.5, LAPPR, 82, 0 },
                     { 0.0, 1.0, RHGRACE, 96, 0 }, { 1.5, 0.5, RH, 82, 0 }, { 2.5, 0.5, RHGRACE, 90, 0 },
                     { 3.0, 0.25, RHARP, 78, 1 }, { 3.25, 0.25, RHARP, 82, 2 }, { 3.5, 0.25, RHARP, 86, 3 }, { 3.75, 0.25, RHARP, 92, 4 } };
        case ChordStyle::DrivingPop:
            return { { 0.0, 1.5, LOCT, 102, 0 }, { 1.5, 1.5, LOCT, 92, 0 }, { 3.0, 0.5, LOCT, 90, 0 }, { 3.5, 0.5, LAPPR, 84, 0 },
                     { 0.0, 0.45, RH, 100, 0 }, { 0.5, 0.45, RH, 66, 0 }, { 1.0, 0.45, RH, 70, 0 }, { 1.5, 0.45, RH, 96, 0 },
                     { 2.0, 0.45, RH, 68, 0 }, { 2.5, 0.45, RH, 70, 0 }, { 3.0, 0.45, RHUP, 96, 0 }, { 3.5, 0.45, RHUP, 74, 0 } };
        case ChordStyle::Cinematic:
        {
            std::vector<Hit> h { { 0.0, 4.0, LOCT, 84, 0 } };
            for (int i = 0; i < 16; ++i)
            {
                const int idx = i < 8 ? i : 16 - i;   // up two octaves and back down
                h.push_back ({ i * 0.25, 0.75, RHARP, 58 + (i % 4 == 0 ? 16 : 0) + idx * 2, idx });
            }
            return h;
        }
        case ChordStyle::NeoSoul:
            return { { 0.0, 1.5, LBASS, 92, 0 }, { 1.5, 0.25, LOCT, 66, 0 }, { 2.5, 1.0, LBASS, 84, 0 }, { 3.5, 0.5, LAPPR, 76, 0 },
                     { 0.03, 1.5, RHROLL, 84, 0 }, { 1.78, 0.5, RH, 68, 0 }, { 2.53, 1.25, RH, 78, 0 } };
        default:
            return {};
    }
}

struct Rng
{
    unsigned state;
    double next() { state = state * 1664525u + 1013904223u; return (double) (state >> 8) / 16777216.0; }   // 0..1
};
} // namespace piano_detail

// right-hand voicing for the piano parts: for neo soul a rich rootless voicing (3rd, 5th, 7th, 9th)
inline std::vector<int> pianoRightHand (const ChordDef& c, const std::vector<int>& close, ChordStyle style)
{
    std::vector<int> rh;
    if (style == ChordStyle::NeoSoul && c.intervals.size() >= 3)
    {
        const int third = c.intervals[1], fifth = c.intervals[2];
        int seventh = third == 3 ? 10 : 11;
        for (int iv : c.intervals)
            if (iv == 10 || iv == 11 || iv == 9)
                seventh = iv;
        if (fifth == 6)
            seventh = 10;   // half-diminished
        int base = 48 + c.root;   // root position reference, the third lands from G3 up
        while (base + third < 55)
            base += 12;
        for (int iv : { third, fifth, seventh, 14 })
            rh.push_back (base + iv);
    }
    else
    {
        for (int m : close)
            rh.push_back (m);
    }
    std::sort (rh.begin(), rh.end());
    while (! rh.empty() && rh.front() < 55)
        for (auto& m : rh)
            m += 12;
    while (! rh.empty() && rh.back() > 82)
        for (auto& m : rh)
            m -= 12;
    return rh;
}

// a two-hand piano part. t0 = start (s), beats = length, phase = position of t0 inside its bar (beats),
// nextRoot = root of the next chord (-1 = none): the left hand walks into it at the end
inline void renderPiano (std::vector<Note>& out, const ChordDef& c, const std::vector<int>& rhIn, ChordStyle style,
                         double t0, double beats, double bpm, double beatsPerBar, double phase = 0.0, int nextRoot = -1)
{
    using namespace piano_detail;
    if (c.intervals.empty() || beats <= 0.0)
        return;
    const auto rh = pianoRightHand (c, rhIn, style);
    if (rh.empty())
        return;
    const double q = 60.0 / bpm;
    const int bass = 36 + c.root > 44 ? 24 + c.root : 36 + c.root;   // A1..G#2: low, not muddy
    const int third = c.intervals.size() > 1 ? c.intervals[1] : 4;
    const int fifth = c.intervals.size() > 2 ? c.intervals[2] : 7;
    const int lhArp[8] = { bass, bass + fifth, bass + 12, bass + 12 + third, bass + 12 + fifth, bass + 12 + third, bass + 12, bass + fifth };
    std::vector<int> rhArp (rh);   // the right hand two octaves up
    for (int m : rh) rhArp.push_back (m + 12);
    rhArp.push_back (rh.front() + 24);

    Rng rng { 0x9E3779B9u ^ (unsigned) (c.root * 131 + (int) style * 977 + (int) (t0 * 1000.0)) };
    const auto hits = pattern (style);

    auto add = [&] (int midi, double atBeat, double lenBeats, int vel, bool rightHand)
    {
        // humanise: right hand a hair late and freer, left hand steadier
        const double jitter = (rng.next() - 0.5) * (rightHand ? 0.024 : 0.012);
        double start = std::max (0.0, atBeat + (atBeat > 0.0 ? jitter : std::abs (jitter) * 0.5));
        if (start >= beats - 1.0e-6)
            return;
        lenBeats = std::min (lenBeats, beats - start);
        Note n;
        n.start = t0 + start * q;
        n.end = n.start + std::max (0.04, lenBeats * q * 0.96);
        n.midi = std::max (0, std::min (127, midi));
        n.velocity = std::max (1, std::min (127, vel + (int) std::lround ((rng.next() - 0.5) * 10.0)));
        out.push_back (n);
    };
    auto chordAt = [&] (const std::vector<int>& v, double at, double len, int vel)
    {
        for (size_t i = 0; i < v.size(); ++i)
            add (v[i], at, len, vel - 6 + (i + 1 == v.size() ? 8 : 0), true);   // the top note sings
    };
    std::vector<int> rhUp (rh);   // next inversion up
    rhUp.front() += 12;
    std::sort (rhUp.begin(), rhUp.end());
    if (rhUp.back() > 84)
        for (auto& m : rhUp) m -= 12;

    // bars covered by this segment, in the bar's own positions
    const double barLenFull = beatsPerBar;
    bool hitAtStart = false;
    int barNumber = (int) std::floor (t0 / q / barLenFull + 1.0e-3);   // bar of the song (or of the clip)
    for (double barStart = -phase; barStart < beats - 1.0e-6; barStart += barLenFull, ++barNumber)
    {
        const bool lastBar = barStart + barLenFull >= beats - 1.0e-6;
        // every other bar the right hand moves to the next inversion: the part breathes instead of repeating
        const bool flip = (barNumber & 1) != 0 && style != ChordStyle::Cinematic && style != ChordStyle::NeoSoul;
        const auto& rhA = flip ? rhUp : rh;
        const auto& rhB = flip ? rh : rhUp;
        for (const auto& h : hits)
        {
            if (h.pos >= barLenFull - 1.0e-6)
                continue;   // 3/4 and other meters: the hits that fit in the bar
            const double at = barStart + h.pos;
            if (at < -1.0e-6 || at >= beats - 1.0e-6)
                continue;
            if (at < 1.0e-6)
                hitAtStart = true;
            double len = h.len;
            if (len < 0.0)   // pedal: to the next half bar
                len = std::max (0.5, (std::floor ((h.pos + 1.0e-6) / 2.0) + 1.0) * 2.0 - h.pos);
            len = std::min (len, barLenFull - h.pos);
            switch (h.kind)
            {
                case RH:      chordAt (rhA, at, len, h.vel); break;
                case RHUP:    chordAt (rhB, at, len, h.vel); break;
                case RHTOP:   add (rhB.back(), at, len, h.vel + 4, true); break;
                case RHARP:   add (rhArp[(size_t) std::min (h.arg + (style == ChordStyle::Gospel ? (int) rh.size() - 1 : 0), (int) rhArp.size() - 1)], at, len, h.vel, true); break;
                case RHROLL:
                    for (size_t i = 0; i < rhA.size(); ++i)
                        add (rhA[i], at + (double) i * 0.045, len - (double) i * 0.045, h.vel - 4 + (i + 1 == rhA.size() ? 8 : 0), true);
                    break;
                case RHGRACE:
                {
                    // a "crushed" grace note a semitone under the third, then the chord
                    for (size_t i = 0; i < rhA.size(); ++i)
                        if (((rhA[i] - c.root) % 12 + 12) % 12 == third)
                            add (rhA[i] - 1, at, 0.11, h.vel - 14, true);
                    chordAt (rhA, at + 0.07, len - 0.07, h.vel);
                    break;
                }
                case LBASS:   add (bass, at, len, h.vel, false); break;
                case LOCT:    add (bass, at, len, h.vel, false); add (bass + 12, at, len, h.vel - 8, false); break;
                case LFIFTH:  add (bass + fifth, at, len, h.vel, false); break;
                case LARP:    add (lhArp[h.arg % 8], at, len, h.vel, false); break;
                case LAPPR:
                {
                    // walk into the next chord at the end of the segment, else the octave
                    if (lastBar && nextRoot >= 0 && ((nextRoot - c.root) % 12 + 12) % 12 != 0)
                    {
                        int target = 36 + nextRoot > 44 ? 24 + nextRoot : 36 + nextRoot;
                        add (target > bass ? target - 1 : target + 1, at, len, h.vel, false);
                    }
                    else
                        add (bass + 12, at, len, h.vel - 6, false);
                    break;
                }
            }
        }
    }
    // the chord changes where the pattern has no hit: play it there anyway
    if (! hitAtStart)
    {
        const double len = std::min (beats, 1.0);
        add (bass, 0.0, len, 88, false);
        chordAt (rh, 0.0, len, 84);
    }
}

// one entry point for all the styles
inline void renderAny (std::vector<Note>& out, const ChordDef& c, const std::vector<int>& v, ChordStyle style,
                       double t0, double beats, double bpm, double beatsPerBar, double phase = 0.0, int nextRoot = -1)
{
    if (isPianoStyle (style))
        renderPiano (out, c, v, style, t0, beats, bpm, beatsPerBar, phase, nextRoot);
    else
        render (out, v, style, t0, beats, bpm, beatsPerBar);
}
} // namespace scriba
