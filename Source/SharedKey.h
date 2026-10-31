// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Song key shared between all the Scriba instances loaded in the same DAW.
//
// Every Scriba that analyses a bass, guitar or piano track publishes its 12-note chroma here.
// A Scriba in Snare mode reads the combined chroma of all the others and gets the key of the song.
// Combining the instruments (bass + guitar + piano...) gives a more reliable key than any one of them.
//
// The registry is a plain static object, so it works for all the instances that the DAW loads in the
// same process (Reaper, Ableton Live, Logic, Cubase, Studio One, FL Studio and Bitwig in its default mode).

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>
#include <juce_core/juce_core.h>
#include "KeyDetector.h"

namespace scriba
{
class SharedKeyRegistry
{
public:
    struct Entry
    {
        int instanceId = 0;
        juce::String source;               // track name, or the instrument name when the DAW gives no track name
        std::array<float, 12> chroma {};
        float share = 0.0f;                // confidence of that instance's own key
        int best = -1;                     // the key that instance decided (relative major / minor included)
        juce::int64 timeMs = 0;
        // song map on the DAW timeline (quarter notes from the project start), for the lyrics
        std::vector<std::pair<double, std::string>> chords;    // chord (or bass note) changes
        std::vector<std::pair<double, std::string>> sections;  // Intro / Verse / Chorus ...
        bool realChords = false;           // guitar / piano chords (true) or bass notes (false)
    };

    struct SongMap
    {
        bool valid = false;
        juce::String source;
        bool fromBass = false;
        std::vector<std::pair<double, std::string>> chords, sections;
    };

    struct Combined
    {
        bool valid = false;
        KeyResult key;
        juce::StringArray sources;
    };

    static SharedKeyRegistry& get()
    {
        static SharedKeyRegistry instance;
        return instance;
    }

    static int newInstanceId()
    {
        static std::atomic<int> next { 1 };
        return next++;
    }

    void publish (int instanceId, const juce::String& source, const KeyResult& key)
    {
        if (! key.valid)
        {
            const juce::ScopedLock sl (lock);
            if (Entry* e = find (instanceId))
            {
                e->best = -1;
                ++changeCount;
            }
            return;
        }
        const juce::ScopedLock sl (lock);
        Entry* e = find (instanceId);
        if (e == nullptr)
        {
            entries.push_back ({});
            e = &entries.back();
            e->instanceId = instanceId;
        }
        e->source = source;
        e->chroma = key.chroma;
        e->share = key.bestShare;
        e->best = key.best;
        e->timeMs = juce::Time::currentTimeMillis();
        ++changeCount;
    }

    void publishMap (int instanceId, const juce::String& source, std::vector<std::pair<double, std::string>> chords,
                     std::vector<std::pair<double, std::string>> sections, bool realChords)
    {
        const juce::ScopedLock sl (lock);
        Entry* e = find (instanceId);
        if (e == nullptr)
        {
            entries.push_back ({});
            e = &entries.back();
            e->instanceId = instanceId;
            e->best = -1;
        }
        e->source = source;
        e->chords = std::move (chords);
        e->sections = std::move (sections);
        e->realChords = realChords;
        ++changeCount;
    }

    // chords and sections for the lyrics: the guitar / piano with the most chords, else the bass
    SongMap songMap (int excludingId) const
    {
        SongMap m;
        const juce::ScopedLock sl (lock);
        const Entry* best = nullptr;
        for (const Entry& e : entries)
        {
            if (e.instanceId == excludingId || e.chords.empty())
                continue;
            if (best == nullptr || (e.realChords && ! best->realChords)
                || (e.realChords == best->realChords && e.chords.size() > best->chords.size()))
                best = &e;
        }
        if (best == nullptr)
            return m;
        m.valid = true;
        m.source = best->source;
        m.fromBass = ! best->realChords;
        m.chords = best->chords;
        m.sections = best->sections;
        return m;
    }

    void rename (int instanceId, const juce::String& source)
    {
        const juce::ScopedLock sl (lock);
        if (Entry* e = find (instanceId))
        {
            e->source = source;
            ++changeCount;
        }
    }

    void remove (int instanceId)
    {
        const juce::ScopedLock sl (lock);
        const auto before = entries.size();
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.instanceId == instanceId; }),
                       entries.end());
        if (entries.size() != before)
            ++changeCount;
    }

    // the key of the song from all the instances except the one asking
    Combined combined (int excludingId) const
    {
        Combined c;
        std::array<float, 12> sum {};
        std::vector<std::pair<int, float>> votes;
        const juce::ScopedLock sl (lock);
        for (const Entry& e : entries)
        {
            if (e.instanceId == excludingId || e.best < 0)
                continue;
            // the more sure an instance is, the more it counts (between 0.5x and 1x)
            const float w = 0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, (e.share - 0.5f) * 2.0f);
            for (size_t i = 0; i < 12; ++i)
                sum[i] += w * e.chroma[i];
            votes.push_back ({ e.best, w });
            c.sources.add (e.source);
        }
        if (c.sources.isEmpty())
            return c;
        c.key = KeyDetector::fromChroma (sum);
        c.valid = c.key.valid;

        // major or its relative minor (same notes): the instances already decided it from the start and
        // the end of the song, so their vote wins over the bare chroma
        if (c.valid)
        {
            const int rel = c.key.relativeOfBest;
            float forBest = 0.0f, forRel = 0.0f;
            for (const auto& v : votes)
            {
                forBest += v.first == c.key.best ? v.second : 0.0f;
                forRel += v.first == rel ? v.second : 0.0f;
            }
            if (forRel > forBest)
            {
                const int old = c.key.best;
                c.key.best = rel;
                c.key.second = old;
                c.key.relativeOfBest = KeyDetector::relativeOf (rel);
                c.key.bestShare = 0.62f;
                c.key.secondShare = 0.38f;
            }
        }
        return c;
    }

    int getChangeCount() const
    {
        const juce::ScopedLock sl (lock);
        return changeCount;
    }

private:
    Entry* find (int id)
    {
        for (Entry& e : entries)
            if (e.instanceId == id)
                return &e;
        return nullptr;
    }

    mutable juce::CriticalSection lock;
    std::vector<Entry> entries;
    int changeCount = 0;
};
} // namespace scriba
