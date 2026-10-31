// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// From a list of notes to a simple chart: one chord (or one bass note) per beat, then per bar.
// Pure C++, no JUCE.
//
//  1. Every beat collects how long each of the 12 notes sounds in it (louder and lower notes count more).
//  2. Every beat is compared with chord shapes (major, minor, 7, m7, maj7, sus2, sus4, dim, power chord).
//  3. A smoothing pass (Viterbi) picks the chord sequence: changing chord costs something, more in the
//     middle of a bar than on beat 1. So a passing note does not become a new chord.
//  4. Bass (or a single-note melody): one note per beat, the one that sounds the most.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "KeyDetector.h"
#include "NoteTranscriber.h"

namespace scriba
{
struct ChartGrid
{
    double beatSec = 0.5;    // length of one beat (seconds)
    double phaseSec = 0.0;   // the recording started this far into its first bar
    int beatsPerBar = 4;
    int bars = 0;            // number of bars covered (relative, bar 0 = first bar of the recording)
};

struct BarCell
{
    std::vector<std::string> items;   // chords or notes in order (consecutive repeats removed)
    bool empty() const { return items.empty(); }
};

// Key of a bass / guitar / piano track from its transcribed notes, blended with the spectral key.
// The notes have no overtones, so they are a cleaner picture of the harmony than the raw spectrum.
// Between a key and its relative (C major / A minor: same notes) the low notes at the start and at the
// end of the song decide: songs usually start and end on their tonic.
inline KeyResult keyFromNotes (const KeyResult& spectral, const std::vector<Note>& notes)
{
    if (notes.size() < 8)
        return spectral;

    std::array<float, 12> fromNotes {};
    double songEnd = 0.0;
    for (const Note& n : notes)
    {
        fromNotes[(size_t) (n.midi % 12)] += (float) std::min (2.0, n.end - n.start) * (0.4f + 0.6f * (float) n.velocity / 127.0f);
        songEnd = std::max (songEnd, n.end);
    }
    float total = 0.0f;
    for (float v : fromNotes)
        total += v;
    if (total <= 0.0f)
        return spectral;

    std::array<float, 12> mix {};
    for (size_t i = 0; i < 12; ++i)
        mix[i] = (spectral.valid ? 0.65f : 1.0f) * fromNotes[i] / total + (spectral.valid ? 0.35f * spectral.chroma[i] : 0.0f);
    KeyResult r = KeyDetector::fromChroma (mix);
    if (! r.valid)
        return spectral;

    const int rel = r.relativeOfBest;
    if (rel >= 0 && r.scores[(size_t) rel] >= r.scores[(size_t) r.best] - 0.15f)
    {
        double songStart = songEnd;
        for (const Note& n : notes)
            songStart = std::min (songStart, n.start);
        const double win = std::max (4.0, 0.08 * (songEnd - songStart));
        double ev[12] {};
        for (const Note& n : notes)
        {
            const double inStart = std::max (0.0, std::min (n.end, songStart + win) - n.start);
            const double inEnd = std::max (0.0, n.end - std::max (n.start, songEnd - win));
            const double w = (inStart + 1.5 * inEnd) * (n.midi <= 55 ? 2.0 : 1.0);   // the very end counts most
            ev[n.midi % 12] += w;
        }
        const int tBest = r.best % 12, tRel = rel % 12;
        if (ev[tRel] > 1.25 * ev[tBest])
        {
            const int oldBest = r.best;
            r.best = rel;
            r.second = oldBest;
            r.relativeOfBest = KeyDetector::relativeOf (r.best);
            r.bestShare = 0.62f;   // decided by the start / end of the song: likely, not certain
            r.secondShare = 1.0f - r.bestShare;
        }
    }
    return r;
}

class ChordChart
{
public:
    // true when the part is mostly single notes (melody / riff) rather than chords
    static bool isMostlySingleNotes (const std::vector<Note>& notes)
    {
        if (notes.size() < 4)
            return true;
        int alone = 0, total = 0;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            int together = 0;
            for (size_t j = 0; j < notes.size(); ++j)
                if (i != j && notes[j].start < notes[i].start + 0.05 && notes[j].end > notes[i].start + 0.05
                    && notes[j].midi % 12 != notes[i].midi % 12)
                    ++together;
            alone += together == 0 ? 1 : 0;
            ++total;
        }
        return (double) alone / (double) total > 0.75;
    }

    // chord names, one cell per bar
    static std::vector<BarCell> chords (const std::vector<Note>& notes, const ChartGrid& g, int keyIdx)
    {
        return cellsFromBeats (beatChords (notes, g, keyIdx), g, true);
    }

    // one chord name per beat ("" = no chord)
    static std::vector<std::string> beatChords (const std::vector<Note>& notes, const ChartGrid& g, int keyIdx)
    {
        const int beats = g.bars * g.beatsPerBar;
        std::vector<std::string> names ((size_t) std::max (0, beats));
        if (beats <= 0)
            return names;

        std::vector<std::array<double, 12>> chroma;
        std::vector<int> bassPc;
        std::vector<double> energy;
        beatChroma (notes, g, beats, chroma, bassPc, energy);

        // energy needed for a beat to have a chord: a fraction of a typical beat
        std::vector<double> sorted (energy);
        std::sort (sorted.begin(), sorted.end());
        const double typical = sorted.empty() ? 0.0 : sorted[(size_t) ((double) (sorted.size() - 1) * 0.75)];
        const double minEnergy = std::max (1.0e-6, typical * 0.12);

        // candidate states: 12 roots x shapes, plus "no chord"
        const int shapes = (int) shapeList().size();
        const int states = 12 * shapes + 1;
        const int nc = states - 1;

        std::vector<double> emit ((size_t) beats * (size_t) states, 0.0);
        for (int b = 0; b < beats; ++b)
        {
            double* e = &emit[(size_t) b * (size_t) states];
            const bool silent = energy[(size_t) b] < minEnergy;
            for (int root = 0; root < 12; ++root)
                for (int sh = 0; sh < shapes; ++sh)
                    e[root * shapes + sh] = silent ? -1.0 : score (chroma[(size_t) b], bassPc[(size_t) b], root, sh, keyIdx);
            e[nc] = silent ? 0.5 : -0.4;
        }

        // Viterbi
        std::vector<double> prev ((size_t) states, 0.0), cur ((size_t) states, 0.0);
        std::vector<int> back ((size_t) beats * (size_t) states, 0);
        for (int s = 0; s < states; ++s)
            prev[(size_t) s] = emit[(size_t) s];
        for (int b = 1; b < beats; ++b)
        {
            const int inBar = b % g.beatsPerBar;
            const double change = inBar == 0 ? 0.18 : (g.beatsPerBar % 2 == 0 && inBar == g.beatsPerBar / 2) ? 0.32 : 0.55;
            int bestPrev = 0;
            for (int s = 1; s < states; ++s)
                if (prev[(size_t) s] > prev[(size_t) bestPrev])
                    bestPrev = s;
            for (int s = 0; s < states; ++s)
            {
                const double stay = prev[(size_t) s];
                const double move = prev[(size_t) bestPrev] - change;
                double v;
                int from;
                if (stay >= move) { v = stay; from = s; }
                else              { v = move; from = bestPrev; }
                cur[(size_t) s] = v + emit[(size_t) b * (size_t) states + (size_t) s];
                back[(size_t) b * (size_t) states + (size_t) s] = from;
            }
            prev.swap (cur);
        }
        std::vector<int> path ((size_t) beats, nc);
        int s = 0;
        for (int k = 1; k < states; ++k)
            if (prev[(size_t) k] > prev[(size_t) s])
                s = k;
        for (int b = beats - 1; b >= 0; --b)
        {
            path[(size_t) b] = s;
            s = back[(size_t) b * (size_t) states + (size_t) s];
        }

        for (int b = 0; b < beats; ++b)
        {
            const int st = path[(size_t) b];
            names[(size_t) b] = st == nc ? std::string() : chordName (st / shapes, st % shapes);
        }
        return names;
    }

    // per-beat names -> one cell per bar (consecutive repeats removed)
    static std::vector<BarCell> cellsFromBeats (const std::vector<std::string>& names, const ChartGrid& g, bool breakOnSilence)
    {
        std::vector<BarCell> cells ((size_t) std::max (0, g.bars));
        for (int bar = 0; bar < g.bars; ++bar)
        {
            BarCell& c = cells[(size_t) bar];
            std::string last;
            for (int k = 0; k < g.beatsPerBar; ++k)
            {
                const size_t b = (size_t) (bar * g.beatsPerBar + k);
                const std::string name = b < names.size() ? names[b] : std::string();
                if (name.empty())
                {
                    if (breakOnSilence)
                        last.clear();
                    continue;
                }
                if (name != last && (c.items.empty() || c.items.back() != name))
                    c.items.push_back (name);
                last = name;
            }
        }
        return cells;
    }

    // one note name per beat (the one that sounds the most), consecutive repeats removed
    static std::vector<BarCell> singleNotes (const std::vector<Note>& notes, const ChartGrid& g)
    {
        return cellsFromBeats (beatNotes (notes, g), g, false);
    }

    // one note name per beat ("" = nothing)
    static std::vector<std::string> beatNotes (const std::vector<Note>& notes, const ChartGrid& g)
    {
        const int beats = g.bars * g.beatsPerBar;
        std::vector<std::string> names ((size_t) std::max (0, beats));
        if (beats <= 0)
            return names;

        std::vector<std::array<double, 12>> chroma;
        std::vector<int> bassPc;
        std::vector<double> energy;
        beatChroma (notes, g, beats, chroma, bassPc, energy, false);

        for (int b = 0; b < beats; ++b)
        {
            const auto& ch = chroma[(size_t) b];
            int best = -1;
            double bestV = 0.0;
            for (int pc = 0; pc < 12; ++pc)
                if (ch[(size_t) pc] > bestV)
                {
                    bestV = ch[(size_t) pc];
                    best = pc;
                }
            if (best >= 0)
                names[(size_t) b] = pitchClassName (best);
        }
        return names;
    }

private:
    struct Shape
    {
        const char* suffix;
        std::vector<int> tones;   // intervals from the root
        double bias;              // simpler shapes are preferred when the evidence is the same
    };

    static const std::vector<Shape>& shapeList()
    {
        static const std::vector<Shape> list {
            { "",     { 0, 4, 7 },     0.06 },
            { "m",    { 0, 3, 7 },     0.06 },
            { "7",    { 0, 4, 7, 10 }, 0.00 },
            { "m7",   { 0, 3, 7, 10 }, 0.00 },
            { "maj7", { 0, 4, 7, 11 }, 0.00 },
            { "sus4", { 0, 5, 7 },     0.00 },
            { "sus2", { 0, 2, 7 },     0.00 },
            { "dim",  { 0, 3, 6 },     -0.02 },
            { "5",    { 0, 7 },        0.02 },
        };
        return list;
    }

    static std::string chordName (int root, int shape)
    {
        return std::string (pitchClassName (root)) + shapeList()[(size_t) shape].suffix;
    }

    static bool inKey (int pc, int keyIdx)
    {
        if (keyIdx < 0)
            return false;
        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int tonic = keyIdx % 12;
        const int* sc = keyIdx >= 12 ? minorScale : majorScale;
        for (int i = 0; i < 7; ++i)
            if ((tonic + sc[i]) % 12 == pc)
                return true;
        return false;
    }

    static double score (const std::array<double, 12>& c, int bass, int root, int shapeIdx, int keyIdx)
    {
        const Shape& sh = shapeList()[(size_t) shapeIdx];
        bool member[12] {};
        for (int t : sh.tones)
            member[(root + t) % 12] = true;

        double in = 0.0, out = 0.0, missing = 0.0;
        for (int pc = 0; pc < 12; ++pc)
            (member[pc] ? in : out) += c[(size_t) pc];
        for (int t : sh.tones)
            if (c[(size_t) ((root + t) % 12)] < 0.04)
                missing += t == 0 ? 0.25 : 0.12;

        double s = in - 0.7 * out - missing + sh.bias;
        if (bass == root)
            s += 0.15;
        else if (bass >= 0 && ! member[bass])
            s -= 0.05;

        // a small nudge towards the chords of the key (only breaks near ties)
        bool allInKey = keyIdx >= 0;
        for (int t : sh.tones)
            allInKey = allInKey && inKey ((root + t) % 12, keyIdx);
        if (allInKey)
            s += 0.04;
        return s;
    }

    // duration-weighted chroma for every beat, plus the lowest note sounding in that beat
    static void beatChroma (const std::vector<Note>& notes, const ChartGrid& g, int beats,
                            std::vector<std::array<double, 12>>& chroma, std::vector<int>& bassPc,
                            std::vector<double>& energy, bool normalise = true)
    {
        chroma.assign ((size_t) beats, std::array<double, 12> {});
        bassPc.assign ((size_t) beats, -1);
        energy.assign ((size_t) beats, 0.0);
        std::vector<int> lowest ((size_t) beats, 1000);

        for (const Note& n : notes)
        {
            // the beat index of a time t (seconds from the start of the recording)
            const double b0 = (n.start + g.phaseSec) / g.beatSec;
            const double b1 = (std::max (n.end, n.start + 0.05) + g.phaseSec) / g.beatSec;
            const int first = std::max (0, (int) std::floor (b0));
            const int last = std::min (beats - 1, (int) std::floor (b1 - 1.0e-9));
            const double w = 0.4 + 0.6 * (double) n.velocity / 127.0;
            // lower notes weigh more (they define the chord), very high ones less
            const double reg = n.midi < 52 ? 1.3 : (n.midi > 76 ? 0.7 : 1.0);
            for (int b = first; b <= last; ++b)
            {
                const double ov = std::min (b1, (double) b + 1.0) - std::max (b0, (double) b);
                if (ov <= 0.0)
                    continue;
                // an attack inside the beat counts double (the note is played there, not only held)
                const double attack = (b0 >= (double) b && b0 < (double) b + 1.0) ? 2.0 : 1.0;
                const double v = ov * w * reg * attack;
                chroma[(size_t) b][(size_t) (n.midi % 12)] += v;
                energy[(size_t) b] += v;
                if (n.midi < lowest[(size_t) b] && ov > 0.25)
                {
                    lowest[(size_t) b] = n.midi;
                    bassPc[(size_t) b] = n.midi % 12;
                }
            }
        }

        if (normalise)
            for (int b = 0; b < beats; ++b)
            {
                const double e = energy[(size_t) b];
                if (e > 0.0)
                    for (double& v : chroma[(size_t) b])
                        v /= e;
            }
    }
};
} // namespace scriba
