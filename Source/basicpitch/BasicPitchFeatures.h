// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Input features of Basic Pitch, computed in plain C++ (no ONNX Runtime):
//   CQT (nnAudio CQT2010v2 as in basic_pitch/layers/nnaudio.py: 22050 Hz, hop 256, fmin 27.5 Hz, 309 bins, 36 per octave)
//   -> magnitude -> NormalizedLog (dB, scaled 0..1 over the window) -> BatchNorm (2 constants)
//   -> harmonic stacking (harmonics 0.5, 1..7) -> 264 bins x 8 channels per frame.
// One call = one 2 s window of 43844 samples, like the training examples of Basic Pitch.

#include <complex>
#include <vector>

namespace basicpitch
{
class Features
{
public:
    static constexpr int kSampleRate = 22050;
    static constexpr int kHop = 256;
    static constexpr int kWindowSamples = 22050 * 2 - 256;   // AUDIO_N_SAMPLES
    static constexpr int kBins = 309;                        // 103 semitones x 3
    static constexpr int kBinsPerOctave = 36;
    static constexpr int kOutBins = 264;                     // 88 keys x 3
    static constexpr int kHarmonics = 8;

    Features();

    // features of one window (n <= kWindowSamples, zero padded). out: frames x (kOutBins * kHarmonics),
    // laid out [frame][bin][harmonic]. Returns the number of frames (172 for a full window).
    int compute (const float* x, int n, std::vector<float>& out) const;

    // the log-normalized CQT before batch norm and stacking (frames x kBins), for tests
    int normalizedCqt (const float* x, int n, std::vector<float>& out) const;

    // the batch normalisation that follows the CQT in the trained model: y = scale * x + offset
    float bnScale = 1.0f, bnOffset = 0.0f;

private:
    std::vector<float> octaveCqt (const std::vector<float>& sig, int hop, int frames) const;   // frames x 36 magnitudes^2 (re, im kept)
    static std::vector<float> downsample2 (const std::vector<float>& x, const std::vector<double>& h);

    int fftLen = 256;
    std::vector<std::vector<std::complex<double>>> kernels;   // 36 kernels, fftLen taps
    std::vector<int> kStart, kEnd;                            // non-zero span of each kernel
    std::vector<double> lowpass;                              // 256 taps, half band
    std::vector<double> lengthsSqrt;                          // sqrt of the librosa filter lengths, 309 bins
    int shifts[kHarmonics] {};
};
} // namespace basicpitch
