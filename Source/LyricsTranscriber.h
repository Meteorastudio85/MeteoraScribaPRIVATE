// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Lyrics transcription from a vocal track, with whisper.cpp (runs locally, no internet needed once the model is there).
//
//  1. The recording is resampled to 16 kHz (what Whisper wants) and brought to a steady level.
//  2. Parts where nobody sings are found with an energy gate and silenced: Whisper tends to "invent" sentences
//     in silence or over instruments, so it is better that it only hears the voice.
//  3. Language: Italian or English (automatic choice between the two, or forced).
//  4. Whisper (beam search) writes the text with the time of every word.
//  5. Clean-up: sentences in parts without voice, typical Whisper inventions ("Thanks for watching",
//     "Sottotitoli a cura di...") and endless repetitions are removed; the text is split into lyric lines
//     on the pauses of the singer.

#include <functional>
#include <string>
#include <vector>

namespace scriba
{
struct LyricWord
{
    double t0 = 0.0, t1 = 0.0;   // seconds from the start of the recording
    std::string text;            // UTF-8
};

struct LyricLine
{
    double t0 = 0.0, t1 = 0.0;
    std::vector<LyricWord> words;
    std::string text() const
    {
        std::string s;
        for (const auto& w : words)
            s += (s.empty() ? "" : " ") + w.text;
        return s;
    }
};

enum class LyricsLanguage { Auto = 0, Italian = 1, English = 2 };

struct LyricsResult
{
    bool ran = false;            // the transcription was attempted
    bool ok = false;
    std::string error;           // why it did not work (no model, model damaged, ...)
    std::string language;        // "Italian" / "English"
    std::vector<LyricLine> lines;
};

class LyricsTranscriber
{
public:
    using ProgressFn = std::function<void (float)>;
    using CancelFn = std::function<bool()>;

    static LyricsResult transcribe (const float* x, size_t n, double sampleRate, const std::string& modelPath,
                                    LyricsLanguage language, const ProgressFn& progress = {}, const CancelFn& cancelled = {});

    // quick check: the file exists and starts like a whisper.cpp model
    static bool looksLikeModel (const std::string& path);
};
} // namespace scriba
