// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#include "BasicPitchTranscriber.h"

#include <algorithm>
#include <cmath>

#include "BasicPitchCNN.h"
#include "BasicPitchFeatures.h"
#include "Notes.h"

namespace basicpitch
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// windowed-sinc resampler (Blackman window, 24 zero crossings each side)
std::vector<float> resample (const float* x, size_t n, double from, double to)
{
    if (std::abs (from - to) < 1.0e-6)
        return std::vector<float> (x, x + n);
    const double ratio = to / from;
    const double cutoff = std::min (1.0, ratio) * 0.94;   // relative to the input Nyquist
    const int zc = 24;
    const double halfLen = (double) zc / cutoff;
    const size_t m = (size_t) std::floor ((double) n * ratio);
    std::vector<float> y (m);
    for (size_t i = 0; i < m; ++i)
    {
        const double center = (double) i / ratio;
        const long k0 = (long) std::ceil (center - halfLen), k1 = (long) std::floor (center + halfLen);
        double s = 0.0;
        for (long k = std::max (0L, k0); k <= std::min ((long) n - 1, k1); ++k)
        {
            const double d = (double) k - center;
            const double a = d * cutoff;
            const double sinc = std::abs (a) < 1.0e-9 ? 1.0 : std::sin (kPi * a) / (kPi * a);
            const double u = d / halfLen;   // -1..1
            const double w = 0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2.0 * kPi * u);
            s += (double) x[k] * sinc * w * cutoff;
        }
        y[i] = (float) s;
    }
    return y;
}
// energy of one frequency (and its octave) in a Hann window, Goertzel
double toneEnergy (const std::vector<float>& a, double sr, double f0, long start, int len)
{
    if (start < 0)
        start = 0;
    if (start + len > (long) a.size())
        len = (int) ((long) a.size() - start);
    if (len < 64)
        return 0.0;
    double total = 0.0;
    for (int h = 1; h <= 2; ++h)
    {
        const double f = f0 * h;
        if (f >= sr * 0.45)
            break;
        const double w = 2.0 * kPi * f / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (len - 1));
            const double s0 = (double) a[(size_t) (start + i)] * win + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        total += (s1 * s1 + s2 * s2 - c * s1 * s2) * (h == 1 ? 1.0 : 0.5);
    }
    return total;
}

// A string that keeps ringing can be cut in two when another string is plucked (the new attack makes a
// false onset at its pitch too). Same pitch, back to back: it is a new note only if that pitch really gets
// louder again; otherwise the two pieces are one note.
void mergeFalseRestarts (std::vector<NoteOut>& notes, const std::vector<float>& a, double sr)
{
    std::sort (notes.begin(), notes.end(), [] (const NoteOut& x, const NoteOut& y) { return x.midi < y.midi || (x.midi == y.midi && x.start < y.start); });
    std::vector<NoteOut> out;
    const int len = (int) (0.07 * sr);
    for (const auto& n : notes)
    {
        if (! out.empty() && out.back().midi == n.midi && n.start - out.back().end < 0.035)
        {
            const double f0 = 440.0 * std::pow (2.0, ((double) n.midi - 69.0) / 12.0);
            const long at = (long) (n.start * sr);
            const double before = toneEnergy (a, sr, f0, at - (long) (0.012 * sr) - len, len);
            const double after = toneEnergy (a, sr, f0, at + (long) (0.008 * sr), len);
            if (after < 1.6 * before)   // no new attack at this pitch (+2 dB)
            {
                auto& prev = out.back();
                const double d0 = prev.end - prev.start, d1 = n.end - n.start;
                prev.amplitude = (float) ((prev.amplitude * d0 + n.amplitude * d1) / std::max (1.0e-6, d0 + d1));
                prev.end = std::max (prev.end, n.end);
                continue;
            }
        }
        out.push_back (n);
    }
    notes.swap (out);
}
} // namespace

std::vector<NoteOut> transcribe (const float* x, size_t n, double sr, const Options& opt,
                                 const ProgressFn& progress, const CancelFn& cancelled)
{
    std::vector<NoteOut> result;
    if (x == nullptr || n == 0 || sr <= 0.0)
        return result;

    const std::vector<float> audio = resample (x, n, sr, Features::kSampleRate);
    if (progress)
        progress (0.05f);

    // windows as in basic_pitch/inference.py
    constexpr int overlapFrames = 30;
    constexpr int overlapLen = overlapFrames * Features::kHop;
    constexpr int hopSize = Features::kWindowSamples - overlapLen;
    std::vector<float> padded ((size_t) (overlapLen / 2), 0.0f);
    padded.insert (padded.end(), audio.begin(), audio.end());

    Features features;
    BasicPitchCNN cnn;
    const int lookahead = BasicPitchCNN::getNumFramesLookahead();

    std::vector<std::vector<float>> notesPG, onsetsPG, contoursPG;
    std::vector<float> feat;
    std::vector<float> zeros ((size_t) Features::kOutBins * Features::kHarmonics, 0.0f);
    std::vector<float> c ((size_t) NUM_FREQ_IN), nt ((size_t) NUM_FREQ_OUT), on ((size_t) NUM_FREQ_OUT);

    const size_t windows = (padded.size() + (size_t) hopSize - 1) / (size_t) hopSize;
    const int keepFrom = overlapFrames / 2;
    for (size_t w = 0; w < windows; ++w)
    {
        if (cancelled && cancelled())
            return {};
        const size_t start = w * (size_t) hopSize;
        const int len = (int) std::min ((size_t) Features::kWindowSamples, padded.size() - start);
        const int frames = features.compute (padded.data() + start, len, feat);

        // the streaming CNN of NeuralNote, run over this window only (zero frames around it, like 'same' padding)
        std::vector<std::vector<float>> wc ((size_t) frames, std::vector<float> ((size_t) NUM_FREQ_IN)),
            wn ((size_t) frames, std::vector<float> ((size_t) NUM_FREQ_OUT)), wo ((size_t) frames, std::vector<float> ((size_t) NUM_FREQ_OUT));
        cnn.reset();
        for (int i = 0; i < lookahead; ++i)
            cnn.frameInference (zeros.data(), c, nt, on);
        for (int f = 0; f < frames + lookahead; ++f)
        {
            const float* in = f < frames ? feat.data() + (size_t) f * zeros.size() : zeros.data();
            if (f < lookahead)
                cnn.frameInference (in, c, nt, on);
            else
                cnn.frameInference (in, wc[(size_t) (f - lookahead)], wn[(size_t) (f - lookahead)], wo[(size_t) (f - lookahead)]);
        }
        for (int f = keepFrom; f < frames - keepFrom; ++f)
        {
            contoursPG.push_back (wc[(size_t) f]);
            notesPG.push_back (wn[(size_t) f]);
            onsetsPG.push_back (wo[(size_t) f]);
        }
        if (progress)
            progress (0.05f + 0.85f * (float) (w + 1) / (float) windows);
    }

    // trim to the length of the audio
    const size_t expected = (size_t) ((double) audio.size() / (double) hopSize * (double) (2 * 86 - overlapFrames));
    if (notesPG.size() > expected)
    {
        notesPG.resize (expected);
        onsetsPG.resize (expected);
        contoursPG.resize (expected);
    }
    if (notesPG.empty())
        return result;

    Notes::ConvertParams p;
    p.frameThreshold = opt.frameThreshold;
    p.onsetThreshold = opt.onsetThreshold;
    p.minNoteLengthFrames = std::max (1, (int) std::lround (opt.minNoteMs / 1000.0 / ((double) FFT_HOP / BASIC_PITCH_SAMPLE_RATE)));
    p.inferOnsets = true;
    p.melodiaTrick = opt.melodiaTrick;
    p.minFrequency = opt.minHz;
    p.maxFrequency = opt.maxHz;
    p.pitchBend = NoPitchBend;

    Notes creator;
    const auto events = creator.convert (notesPG, onsetsPG, contoursPG, p, true);
    for (const auto& e : events)
        result.push_back ({ std::max (0.0, e.startTime), std::max (e.startTime, e.endTime), e.pitch, (float) e.amplitude });
    mergeFalseRestarts (result, audio, Features::kSampleRate);
    std::sort (result.begin(), result.end(), [] (const NoteOut& a, const NoteOut& b) { return a.start < b.start || (a.start == b.start && a.midi < b.midi); });
    if (progress)
        progress (1.0f);
    return result;
}
} // namespace basicpitch
