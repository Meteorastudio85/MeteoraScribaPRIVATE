// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Basic Pitch (Spotify, Apache 2.0) inside Scriba: polyphonic audio -> notes, for guitar and piano.
// Same inference as basic_pitch/inference.py: 22050 Hz, 2 s windows overlapping by 30 frames, each window
// normalised on its own (as in training), CNN, then note creation (onsets + frames, melodia trick).

#include <functional>
#include <vector>

namespace basicpitch
{
struct NoteOut
{
    double start = 0.0, end = 0.0;   // seconds
    int midi = 60;
    float amplitude = 0.0f;          // 0..1, mean activation
};

struct Options
{
    float frameThreshold = 0.3f;     // lower = more notes
    float onsetThreshold = 0.5f;     // lower = notes split more
    float minNoteMs = 127.7f;
    float minHz = -1.0f, maxHz = -1.0f;
    bool melodiaTrick = true;
};

using ProgressFn = std::function<void (float)>;
using CancelFn = std::function<bool()>;

std::vector<NoteOut> transcribe (const float* x, size_t n, double sampleRate, const Options& options,
                                 const ProgressFn& progress = {}, const CancelFn& cancelled = {});
} // namespace basicpitch
