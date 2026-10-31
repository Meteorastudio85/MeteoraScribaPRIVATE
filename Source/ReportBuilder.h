// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Text reports (pure C++, UTF-8, English note names: C C# D D# E F F# G G# A A# B).
//
// Bass / Guitar / Piano: a simple chart, one cell per bar, 4 bars per line, grouped by song section.
//   Guitar and piano show chord names, bass (or a single-note part) shows the main note of each beat.
//   Every single note with its exact timing is in the MIDI file, not here.
// Snare: one answer - how many semitones up or down to tune the snare to the song key.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ChordChart.h"
#include "DrumAnalyzer.h"
#include "KeyDetector.h"
#include "LyricsTranscriber.h"
#include "NoteTranscriber.h"
#include "SnareTuning.h"
#include "Structure.h"

namespace scriba
{
// the song key used by the snare (normally taken from another Scriba instance)
struct SongKeyInfo
{
    bool valid = false;
    int key = -1;            // 0..23
    float share = 0.0f;      // confidence of the source (0..1), 0 = chosen by hand
    bool manual = false;     // chosen in the SONG KEY menu
    std::string source;      // e.g. "Scriba on 'Guitar'"
};

// chords and sections of another Scriba, already moved onto the time of the vocal recording (seconds)
struct LyricsSheetInfo
{
    bool haveMap = false;
    std::string source;
    bool fromBass = false;
    std::vector<std::pair<double, std::string>> chords;     // (time, chord) changes, "" = no chord
    std::vector<std::pair<double, std::string>> sections;   // (time, label)
};

struct ReportInfo
{
    KeyResult key;
    Instrument instrument = Instrument::Guitar;
    double bpm = 0.0;           // 0 = not provided by the host
    int tsNum = 0;              // 0 = not provided by the host
    int tsDen = 0;
    bool tempoVaries = false;   // tempo changed while recording
    double seconds = 0.0;
    float sensitivity = 0.5f;
    int firstBar = 1;           // DAW bar number of the bar where the recording started
    double phaseSec = 0.0;      // the recording started this far into that bar
    bool barsFromHost = false;  // firstBar / phaseSec come from the DAW timeline
    std::vector<Section> sections;   // Intro / Verse / Chorus ... (may be empty)
    SongKeyInfo songKey;        // snare only
    double pitchSemis = 0.0;    // snare only: pitch set on Scriba itself
    float gateDb = -30.0f;      // kick / snare: gate threshold used for the hits
};

// C4 = MIDI note 60 (middle C)
inline std::string noteLabel (int midi)
{
    return std::string (pitchClassName (midi % 12)) + std::to_string (midi / 12 - 1);
}

namespace report_detail
{
inline bool bpmKnown (const ReportInfo& info) { return info.bpm >= 20.0 && info.bpm <= 400.0; }

inline std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}

inline std::string clock (double secs)
{
    const int s = std::max (0, (int) secs);
    char b[32];
    std::snprintf (b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

inline double barSecondsOf (const ReportInfo& info)
{
    const double bpm = bpmKnown (info) ? info.bpm : 120.0;
    const int num = info.tsNum > 0 ? info.tsNum : 4;
    const int den = info.tsDen > 0 ? info.tsDen : 4;
    return 60.0 / bpm * (4.0 / (double) den) * (double) num;
}

inline std::string pad (std::string s, size_t w)
{
    // pads by visible characters (UTF-8 aware for the few symbols used here)
    size_t visible = 0;
    for (char ch : s)
        if ((static_cast<unsigned char> (ch) & 0xC0) != 0x80)
            ++visible;
    while (visible < w)
    {
        s += ' ';
        ++visible;
    }
    return s;
}

inline std::string title (const std::string& text)
{
    return text + "\n" + std::string (text.size(), '=') + "\n\n";
}

inline std::string heading (const std::string& text)
{
    return text + "\n" + std::string (text.size(), '-') + "\n";
}

inline std::string confidenceWord (float share)
{
    return share >= 0.75f ? "sure" : (share >= 0.6f ? "likely" : "not sure");
}

inline std::string keyLine (const KeyResult& k)
{
    std::string t = keyName (k.best) + "   (" + confidenceWord (k.bestShare) + ")";
    if (k.bestShare < 0.6f)
        t += "  - could also be " + keyName (k.second);
    return t;
}

inline std::string tempoLine (const ReportInfo& info)
{
    const int num = info.tsNum > 0 ? info.tsNum : 4;
    const int den = info.tsDen > 0 ? info.tsDen : 4;
    std::string t;
    if (bpmKnown (info))
        t = fmt ("%.0f", info.bpm) + " BPM, " + std::to_string (num) + "/" + std::to_string (den);
    else
        t = "unknown (120 BPM assumed)";
    if (info.tempoVaries)
        t += "  - the tempo changes in the song, the first one is used";
    return t;
}
} // namespace report_detail

inline std::string buildReport (const std::vector<Note>& notesIn, const ReportInfo& info)
{
    using namespace report_detail;

    std::vector<Note> notes = notesIn;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    const int num = info.tsNum > 0 ? info.tsNum : 4;
    const int den = info.tsDen > 0 ? info.tsDen : 4;
    const double bpm = bpmKnown (info) ? info.bpm : 120.0;
    const double barSec = barSecondsOf (info);

    const bool bass = info.instrument == Instrument::Bass;
    const bool single = bass || ChordChart::isMostlySingleNotes (notes);

    std::string t;
    std::string inst = instrumentName (info.instrument);
    std::transform (inst.begin(), inst.end(), inst.begin(), [] (unsigned char c) { return (char) std::toupper (c); });
    t += title ("SCRIBA  -  " + inst);

    t += "KEY      " + (info.key.valid ? keyLine (info.key) : std::string ("not found")) + "\n";
    t += "TEMPO    " + tempoLine (info) + "\n";
    t += "LENGTH   " + clock (info.seconds) + "\n\n";

    if (notes.empty())
    {
        t += "No notes found.\nTry a higher SENSITIVITY, or check that the right INSTRUMENT is selected.\n";
        return t;
    }

    // --- song map ---
    const auto barOf = [&] (int relativeBar1) { return info.firstBar + relativeBar1 - 1; };
    if (! info.sections.empty())
    {
        t += heading ("SONG MAP");
        for (const Section& s : info.sections)
        {
            std::string name = s.label + " ";
            while (name.size() < 16)
                name += '.';
            t += "  " + name + (bpmKnown (info) ? " bar " + std::to_string (barOf (s.startBar)) : " " + clock (s.start)) + "\n";
        }
        t += "\n";
    }

    // --- chart ---
    ChartGrid g;
    g.beatSec = 60.0 / bpm * (4.0 / (double) den);
    g.beatsPerBar = num;
    g.phaseSec = info.phaseSec;
    double lastEnd = info.seconds;
    for (const Note& n : notes)
        lastEnd = std::max (lastEnd, n.start);
    g.bars = std::max (1, (int) std::ceil ((lastEnd + info.phaseSec) / barSec - 1.0e-6));

    std::vector<BarCell> cells = single ? ChordChart::singleNotes (notes, g)
                                        : ChordChart::chords (notes, g, info.key.valid ? info.key.best : -1);
    // silence at the end of the recording is not shown
    while (g.bars > 1 && cells[(size_t) g.bars - 1].empty())
        --g.bars;
    cells.resize ((size_t) g.bars);

    t += heading (single ? "NOTES, BAR BY BAR" : "CHORDS, BAR BY BAR");
    if (single)
        t += "Each box is one bar: the main notes in the order they are played.\n";
    else
        t += "Each box is one bar. Two chords in a box = the chord changes inside that bar.\n";
    t += "\"-\" = nothing played. The numbers on the left are " + std::string (info.barsFromHost ? "the bars of your DAW." : "bars (bar 1 = where you pressed play).") + "\n\n";

    // width of a box: the longest content, at least 6 characters
    size_t w = 6;
    for (const BarCell& c : cells)
    {
        std::string s;
        for (size_t k = 0; k < c.items.size(); ++k)
            s += (k ? " " : "") + c.items[k];
        w = std::max (w, s.size());
    }
    w = std::min<size_t> (w, 22);

    auto cellText = [&] (int bar)
    {
        if (bar < 0 || bar >= (int) cells.size() || cells[(size_t) bar].empty())
            return std::string ("-");
        std::string s;
        for (size_t k = 0; k < cells[(size_t) bar].items.size(); ++k)
            s += (k ? " " : "") + cells[(size_t) bar].items[k];
        if (s.size() > w)
            s = s.substr (0, w - 1) + "~";
        return s;
    };

    // section borders (relative 0-based bars); without sections the whole song is one block
    struct Block { int a, z; std::string label; };
    std::vector<Block> blocks;
    if (info.sections.empty())
        blocks.push_back ({ 0, g.bars, "" });
    else
        for (const Section& s : info.sections)
            blocks.push_back ({ s.startBar - 1, std::min (g.bars, s.endBar), s.label });
    if (! blocks.empty() && blocks.back().z < g.bars)
        blocks.back().z = g.bars;

    const int numW = (int) std::to_string (barOf (g.bars)).size();
    for (const Block& blk : blocks)
    {
        if (blk.z <= blk.a)
            continue;
        if (! blk.label.empty())
            t += "[" + blk.label + "]\n";

        // lines of 4 bars; a line equal to the one before is not repeated, it gets "x2", "x3" ...
        std::string prevContent;
        int prevStart = 0, repeats = 0;
        std::vector<std::string> out;

        auto flush = [&] (int lastBarOfRun)
        {
            if (prevContent.empty())
                return;
            std::string label = std::to_string (barOf (prevStart + 1));
            if (repeats > 1)
                label += "-" + std::to_string (barOf (lastBarOfRun + 1));
            std::string line = "  " + pad (label, (size_t) numW * 2 + 1) + " |" + prevContent;
            if (repeats > 1)
                line += "  x" + std::to_string (repeats);
            out.push_back (line);
        };

        for (int a = blk.a; a < blk.z; a += 4)
        {
            std::string content;
            for (int k = 0; k < 4 && a + k < blk.z; ++k)
                content += " " + pad (cellText (a + k), w) + " |";
            if (content == prevContent && a + 4 <= blk.z)
            {
                ++repeats;
                continue;
            }
            flush (a - 1);
            prevContent = content;
            prevStart = a;
            repeats = 1;
        }
        flush (blk.z - 1);
        for (const std::string& l : out)
            t += l + "\n";
        t += "\n";
    }

    // --- notes used ---
    int perClass[12] {};
    int lo = 127, hi = 0;
    for (const Note& n : notes)
    {
        ++perClass[n.midi % 12];
        lo = std::min (lo, n.midi);
        hi = std::max (hi, n.midi);
    }
    std::vector<int> order;
    for (int pc = 0; pc < 12; ++pc)
        if (perClass[pc] > 0)
            order.push_back (pc);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return perClass[a] > perClass[b]; });

    t += heading ("NOTES USED (most played first)");
    t += " ";
    for (size_t k = 0; k < order.size() && k < 7; ++k)
        t += std::string (" ") + pitchClassName (order[k]);
    t += "\n  lowest " + noteLabel (lo) + ", highest " + noteLabel (hi) + "   (C4 = middle C)\n\n";

    t += heading ("GOOD TO KNOW");
    t += "- Every single note, with its exact timing, is in the MIDI file: drag it into your DAW.\n";
    if (! info.sections.empty())
        t += "- Song sections are a guess from harmony and loudness: move them by ear if needed.\n";
    if (! bpmKnown (info))
        t += "- The DAW gave no tempo: set the project tempo and analyze again for correct bars.\n";
    t += "- This is an automatic transcription: check it by ear.\n";
    return t;
}

// Text report for the snare: how many semitones to move it to sit in the song key.
inline std::string buildSnareReport (const DrumResult& drum, const ReportInfo& info)
{
    using namespace report_detail;
    std::string t = title ("SCRIBA  -  SNARE TUNING");

    const SongKeyInfo& sk = info.songKey;
    const SnarePitch& p = drum.pitch;

    // --- song key ---
    if (sk.valid)
    {
        t += "SONG KEY     " + keyName (sk.key);
        if (sk.manual)
            t += "   (chosen by hand)";
        else if (! sk.source.empty())
            t += "   (from " + sk.source + ")";
        t += "\n";
    }
    else
        t += "SONG KEY     not found\n";

    // --- snare note ---
    t += "HITS         " + std::to_string (drum.hits.size()) + "   (gate " + fmt ("%.1f", (double) info.gateDb) + " dB)\n";
    if (drum.hits.empty())
    {
        t += "\nNo snare hits found.\nLower the GATE (move it to the left) until you hear every hit, then save again.\n";
        return t;
    }
    if (p.valid)
    {
        const int c = (int) std::lround (p.cents);
        t += "SNARE NOTE   " + noteLabel (p.midi);
        if (std::abs (c) >= 5)
            t += " " + std::string (c > 0 ? "+" : "") + std::to_string (c) + " cents";
        t += "   (" + fmt ("%.0f", p.hz) + " Hz, from " + std::to_string (p.hitsUsed) + " hits)\n\n";
    }
    else
    {
        t += "SNARE NOTE   not measurable (no clear tone in the hits)\n\n";
        t += "The MIDI file with the " + std::to_string (drum.hits.size()) + " snare hits is still fine.\n";
        return t;
    }

    if (! sk.valid)
    {
        t += "To get the tuning, the song key is needed:\n";
        t += "  1. Put another Scriba on the bass, guitar or piano track.\n";
        t += "  2. Press ANALYZE there and play the song.\n";
        t += "  3. Come back here and save this file again.\n";
        t += "Or choose the key by hand in the SONG KEY menu of this Scriba.\n";
        return t;
    }

    const SnareTuning tune = computeSnareTuning (p, sk.key);
    if (! tune.valid)
        return t;

    const SnareTarget& m = tune.main;
    std::string answer;
    if (tune.alreadyInTune || m.semitones == 0)
        answer = "THE SNARE IS ALREADY IN TUNE  -  leave it at 0";
    else
        answer = std::string ("TUNE THE SNARE ") + (m.semitones > 0 ? "UP " : "DOWN ") + std::to_string (std::abs (m.semitones))
                 + (std::abs (m.semitones) == 1 ? " SEMITONE" : " SEMITONES") + "   (" + signedInt (m.semitones) + ")";

    const std::string bar (answer.size() + 4, '*');
    t += bar + "\n  " + answer + "\n" + bar + "\n";
    t += "  It becomes " + noteLabel (m.midi) + ", the " + m.role + " of " + keyName (sk.key) + ".\n\n";

    if (std::abs (m.cents) >= 10)
        t += "For a perfect match also set the FINE TUNE to " + signedInt (m.cents) + " cents  (exact: " + fmt ("%+.2f", m.shift) + " semitones).\n";
    if (tune.smaller.valid)
        t += "Smaller change: " + signedInt (tune.smaller.semitones) + " semitone" + (std::abs (tune.smaller.semitones) == 1 ? "" : "s")
             + " (to " + pitchClassName (tune.smaller.midi % 12) + ", also in the key, a bit less strong).\n";
    t += "\nHow to use it, two ways:\n";
    t += "  - in Scriba: SNARE PITCH, press SET (or use - / +). You hear the snare tuned right away.\n";
    t += "  - in a sampler: put the number in the PITCH / TRANSPOSE of the snare sample.\n";
    t += "+ = higher, - = lower.\n";
    if (std::abs (info.pitchSemis) >= 0.005)
    {
        const double nowMidi = tune.snareMidi + info.pitchSemis;
        const int nowNote = (int) std::lround (nowMidi);
        const int nowCents = (int) std::lround ((nowMidi - nowNote) * 100.0);
        t += "\nPitch set in Scriba now: " + fmt ("%+.2f", info.pitchSemis) + " semitones -> the snare sounds "
             + noteLabel (nowNote) + (std::abs (nowCents) >= 5 ? " " + signedInt (nowCents) + " cents" : std::string()) + ".\n";
    }
    if (p.confidence < 0.33f || p.spreadCents > 40.0)
        t += "\nThe snare tone was weak or uneven in this recording: check by ear.\n";
    if (! sk.manual && sk.share > 0.0f && sk.share < 0.6f)
        t += "\nThe song key is not certain: check it, or choose it by hand in SONG KEY.\n";
    return t;
}

namespace report_detail
{
inline size_t visibleLen (const std::string& s)
{
    size_t n = 0;
    for (char ch : s)
        if ((static_cast<unsigned char> (ch) & 0xC0) != 0x80)
            ++n;
    return n;
}
} // namespace report_detail

// Lyric sheet: the words with the chords written above them, by section, with the bar numbers of the DAW.
inline std::string buildLyricsReport (const LyricsResult& L, const ReportInfo& info, const LyricsSheetInfo& sheet, size_t melodyNotes)
{
    using namespace report_detail;
    std::string t = title ("SCRIBA  -  LYRICS");

    t += "LANGUAGE   " + (L.language.empty() ? std::string ("-") : L.language) + "\n";
    if (sheet.haveMap)
        t += "CHORDS     from " + sheet.source + (sheet.fromBass ? "  (bass notes: put Scriba on a guitar or piano for real chords)" : "") + "\n";
    else
        t += "CHORDS     none - analyze the guitar or piano with another Scriba to see them above the words\n";
    t += "TEMPO      " + tempoLine (info) + "\n";
    t += "LENGTH     " + clock (info.seconds) + "\n\n";

    if (! L.ran)
    {
        t += "The lyrics were not transcribed.\n";
        return t;
    }
    if (! L.ok)
    {
        if (L.error == "voice model missing")
        {
            t += "The voice model is not installed yet.\n";
            t += "In Scriba (VOCALS) press DOWNLOAD VOICE MODEL (about 550 MB, only once), then ANALYZE AGAIN.\n";
        }
        else
            t += "The lyrics could not be transcribed: " + L.error + "\n";
        if (melodyNotes > 0)
            t += "\nThe MIDI file with the melody of the voice (" + std::to_string (melodyNotes) + " notes) is ready anyway.\n";
        return t;
    }
    if (L.lines.empty())
    {
        t += "No sung words found.\nCheck that Scriba is on the vocal track, before reverb and delay.\n";
        return t;
    }

    const bool timed = bpmKnown (info);
    const double barSec = barSecondsOf (info);
    auto barOf = [&] (double sec) { return info.firstBar + (int) std::floor ((std::max (0.0, sec) + info.phaseSec) / barSec + 1.0e-6); };
    auto label = [&] (double sec) { return timed ? std::to_string (barOf (sec)) : clock (sec); };

    // the chord sounding at a moment, and the changes inside a time span
    auto chordAt = [&] (double sec)
    {
        std::string c;
        for (const auto& ev : sheet.chords)
        {
            if (ev.first > sec)
                break;
            c = ev.second;
        }
        return c;
    };

    const size_t pre = 8;   // width of the bar column
    size_t nextSection = 0;
    double prevEnd = -1.0;

    t += heading ("LYRICS");
    t += timed ? (info.barsFromHost ? "The numbers on the left are the bars of your DAW.\n" : "The numbers on the left are bars (bar 1 = where you pressed play).\n")
               : "The numbers on the left are minutes:seconds (the DAW gave no tempo).\n";
    if (sheet.haveMap)
        t += "Each chord is written above the word where it starts.\n";
    t += "\n";

    for (size_t i = 0; i < L.lines.size(); ++i)
    {
        const LyricLine& line = L.lines[i];
        const double nextStart = i + 1 < L.lines.size() ? L.lines[i + 1].t0 : 1.0e12;

        // section titles reached by this line
        std::string header;
        while (nextSection < sheet.sections.size() && sheet.sections[nextSection].first <= line.t0 + 0.4)
            header = sheet.sections[nextSection++].second;
        // a long part without singing: show its chords
        if (sheet.haveMap && prevEnd >= 0.0 && line.t0 - prevEnd > 2.0 * barSec)
        {
            std::vector<std::string> inst;
            for (const auto& ev : sheet.chords)
                if (ev.first > prevEnd + 0.3 && ev.first < line.t0 - 0.3 && ! ev.second.empty() && (inst.empty() || inst.back() != ev.second))
                    inst.push_back (ev.second);
            if (! inst.empty())
            {
                std::string s2;
                for (size_t k = 0; k < inst.size() && k < 12; ++k)
                    s2 += (k ? "  " : "") + inst[k];
                t += pad ("", pre) + "(instrumental:  " + s2 + (inst.size() > 12 ? "  ..." : "") + ")\n\n";
            }
        }
        if (! header.empty())
            t += "[" + header + "]\n";

        // the text and where every word starts
        std::string text;
        std::vector<size_t> col;
        for (const auto& w : line.words)
        {
            if (! text.empty())
                text += " ";
            col.push_back (visibleLen (text));
            text += w.text;
        }

        if (sheet.haveMap)
        {
            struct Mark { size_t col; std::string name; };
            std::vector<Mark> marks;
            const std::string first = chordAt (line.t0 + 0.15);
            if (! first.empty())
                marks.push_back ({ 0, first });
            for (const auto& ev : sheet.chords)
            {
                if (ev.first <= line.t0 + 0.15 || ev.first > std::min (line.t1 + 0.3, nextStart - 0.15) || ev.second.empty())
                    continue;
                size_t wi = 0;
                for (size_t k = 0; k < line.words.size(); ++k)
                    if (line.words[k].t0 <= ev.first + 0.12)
                        wi = k;
                if (! marks.empty() && marks.back().name == ev.second)
                    continue;
                marks.push_back ({ col.empty() ? 0 : col[wi], ev.second });
            }
            std::string chordLine;
            for (const auto& m : marks)
            {
                size_t c = std::max (m.col, chordLine.empty() ? (size_t) 0 : visibleLen (chordLine) + 1);
                chordLine = pad (chordLine, c) + m.name;
            }
            t += "  " + pad (label (line.t0), pre - 2) + chordLine + "\n";
            t += pad ("", pre) + text + "\n";
        }
        else
            t += "  " + pad (label (line.t0), pre - 2) + text + "\n";

        // blank line between groups of lines (new section, or a clear pause)
        const bool newSectionNext = nextSection < sheet.sections.size() && sheet.sections[nextSection].first <= nextStart + 0.4;
        if (newSectionNext || nextStart - line.t1 > barSec * 0.9 || sheet.haveMap)
            t += "\n";
        prevEnd = line.t1;
    }

    t += "\n" + heading ("GOOD TO KNOW");
    t += "- This is an automatic transcription of singing: read it as a first draft and fix the words by ear.\n";
    t += "- Best results with the dry voice: put Scriba before reverb, delay and doubles.\n";
    if (melodyNotes > 0)
        t += "- The melody of the voice (" + std::to_string (melodyNotes) + " notes) is in the MIDI file.\n";
    if (sheet.haveMap)
        t += "- The chords come from the other Scriba: if they are wrong, check them there.\n";
    return t;
}
} // namespace scriba
