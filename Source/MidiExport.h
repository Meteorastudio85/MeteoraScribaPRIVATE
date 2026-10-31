// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Scrittura di un file MIDI standard (SMF formato 1) - C++ puro, senza JUCE.
// Track 1: tempo, time signature, key. Track 2: the notes.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "NoteTranscriber.h"

namespace scriba
{
struct MidiExportInfo
{
    double bpm = 120.0;
    int tsNum = 4;
    int tsDen = 4;
    int keyTonic = -1;          // 0 = Do ... 11 = Si, -1 = non scrivere la tonalita'
    bool keyMinor = false;
    std::string keyText;        // ASCII only, e.g. "A minor"
    std::string trackName;      // ASCII only, e.g. "Scriba - Guitar"
    std::vector<std::pair<double, std::string>> markers;   // (seconds, text): song sections, ASCII only
};

namespace midi_detail
{
constexpr int kPpq = 960;

struct Event
{
    uint32_t tick = 0;
    int order = 0;               // a pari tick: meta (-1), note off (0), note on (1)
    std::vector<uint8_t> data;
};

inline void putVlq (std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = (uint8_t) (v & 0x7f);
    while ((v >>= 7) > 0)
        buf[n++] = (uint8_t) ((v & 0x7f) | 0x80);
    while (n--)
        o.push_back (buf[n]);
}

inline void put16 (std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back ((uint8_t) ((v >> 8) & 0xff));
    o.push_back ((uint8_t) (v & 0xff));
}

inline void put32 (std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back ((uint8_t) ((v >> 24) & 0xff));
    o.push_back ((uint8_t) ((v >> 16) & 0xff));
    o.push_back ((uint8_t) ((v >> 8) & 0xff));
    o.push_back ((uint8_t) (v & 0xff));
}

inline Event metaText (uint8_t type, const std::string& text)
{
    Event e;
    e.order = -1;
    e.data = { 0xFF, type };
    putVlq (e.data, (uint32_t) text.size());
    e.data.insert (e.data.end(), text.begin(), text.end());
    return e;
}

inline void writeTrack (std::vector<uint8_t>& out, std::vector<Event>& events)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
                      { return a.tick < b.tick || (a.tick == b.tick && a.order < b.order); });

    std::vector<uint8_t> body;
    uint32_t last = 0;
    for (const Event& e : events)
    {
        putVlq (body, e.tick - last);
        last = e.tick;
        body.insert (body.end(), e.data.begin(), e.data.end());
    }
    putVlq (body, 0);
    body.push_back (0xFF);
    body.push_back (0x2F);
    body.push_back (0x00);

    out.insert (out.end(), { 'M', 'T', 'r', 'k' });
    put32 (out, (uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
}

// numero di diesis (+) o bemolli (-) per l'armatura di chiave
inline int keySignature (int tonic, bool minor)
{
    static const int major[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    const int t = minor ? (tonic + 3) % 12 : tonic;   // la minore usa l'armatura della sua relativa maggiore
    return major[t];
}
} // namespace midi_detail

inline std::vector<uint8_t> buildMidiFile (const std::vector<Note>& notes, const MidiExportInfo& info)
{
    using namespace midi_detail;

    const double bpm = (info.bpm >= 20.0 && info.bpm <= 400.0) ? info.bpm : 120.0;
    const int num = std::max (1, std::min (32, info.tsNum));
    int den = info.tsDen;
    if (den != 1 && den != 2 && den != 4 && den != 8 && den != 16 && den != 32)
        den = 4;

    auto toTick = [&] (double sec) { return (uint32_t) std::max<long long> (0, std::llround (sec * bpm / 60.0 * (double) kPpq)); };

    // traccia 0: informazioni generali
    std::vector<Event> conductor;
    conductor.push_back (metaText (0x03, "Scriba"));

    {
        Event e;
        e.order = -1;
        const uint32_t us = (uint32_t) std::llround (60000000.0 / bpm);
        e.data = { 0xFF, 0x51, 0x03, (uint8_t) ((us >> 16) & 0xff), (uint8_t) ((us >> 8) & 0xff), (uint8_t) (us & 0xff) };
        conductor.push_back (e);
    }
    {
        Event e;
        e.order = -1;
        uint8_t dd = 0;
        while ((1 << dd) < den)
            ++dd;
        e.data = { 0xFF, 0x58, 0x04, (uint8_t) num, dd, 24, 8 };
        conductor.push_back (e);
    }
    if (info.keyTonic >= 0 && info.keyTonic < 12)
    {
        Event e;
        e.order = -1;
        e.data = { 0xFF, 0x59, 0x02, (uint8_t) (int8_t) keySignature (info.keyTonic, info.keyMinor), (uint8_t) (info.keyMinor ? 1 : 0) };
        conductor.push_back (e);
        if (! info.keyText.empty())
            conductor.push_back (metaText (0x01, "Key: " + info.keyText));
    }

    for (const auto& m : info.markers)
    {
        Event e = metaText (0x06, m.second);   // marker meta event
        e.tick = toTick (m.first);
        conductor.push_back (e);
    }

    // traccia 1: note
    std::vector<Event> track;
    track.push_back (metaText (0x03, info.trackName.empty() ? std::string ("Scriba") : info.trackName));
    for (const Note& n : notes)
    {
        const uint32_t on = toTick (n.start);
        uint32_t off = toTick (n.end);
        if (off < on + 30)
            off = on + 30;
        const uint8_t pitch = (uint8_t) std::max (0, std::min (127, n.midi));
        const uint8_t vel = (uint8_t) std::max (1, std::min (127, n.velocity));

        Event a;
        a.tick = on;
        a.order = 1;
        a.data = { 0x90, pitch, vel };
        track.push_back (a);

        Event b;
        b.tick = off;
        b.order = 0;
        b.data = { 0x80, pitch, 0 };
        track.push_back (b);
    }

    std::vector<uint8_t> out;
    out.insert (out.end(), { 'M', 'T', 'h', 'd' });
    put32 (out, 6);
    put16 (out, 1);        // formato 1
    put16 (out, 2);        // due tracce
    put16 (out, (uint32_t) kPpq);
    writeTrack (out, conductor);
    writeTrack (out, track);
    return out;
}
} // namespace scriba
