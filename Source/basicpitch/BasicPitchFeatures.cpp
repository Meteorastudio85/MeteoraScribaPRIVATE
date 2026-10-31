// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#include "BasicPitchFeatures.h"

#include <algorithm>
#include <cmath>

namespace basicpitch
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// scipy.signal.firwin2 (default nfreqs, hamming window, type II for an even number of taps)
std::vector<double> firwin2 (int numtaps, const std::vector<double>& freq, const std::vector<double>& gain)
{
    const int nfreqs = 1 + (1 << (int) std::ceil (std::log2 ((double) numtaps)));
    std::vector<std::complex<double>> fx2 ((size_t) nfreqs);
    for (int i = 0; i < nfreqs; ++i)
    {
        const double x = (double) i / (double) (nfreqs - 1);
        // np.interp
        double g = gain.back();
        for (size_t k = 0; k + 1 < freq.size(); ++k)
            if (x >= freq[k] && x <= freq[k + 1])
            {
                const double span = freq[k + 1] - freq[k];
                g = span > 0.0 ? gain[k] + (gain[k + 1] - gain[k]) * (x - freq[k]) / span : gain[k + 1];
                break;
            }
        const double ph = -(double) (numtaps - 1) / 2.0 * kPi * x;
        fx2[(size_t) i] = g * std::complex<double> (std::cos (ph), std::sin (ph));
    }
    // irfft, length n = 2 (nfreqs - 1)
    const int n = 2 * (nfreqs - 1);
    std::vector<double> out ((size_t) numtaps);
    for (int t = 0; t < numtaps; ++t)
    {
        double s = fx2[0].real() + ((t % 2) ? -1.0 : 1.0) * fx2[(size_t) nfreqs - 1].real();
        for (int k = 1; k < nfreqs - 1; ++k)
        {
            const double a = 2.0 * kPi * (double) k * (double) t / (double) n;
            s += 2.0 * (fx2[(size_t) k].real() * std::cos (a) - fx2[(size_t) k].imag() * std::sin (a));
        }
        const double w = 0.54 - 0.46 * std::cos (2.0 * kPi * (double) t / (double) (numtaps - 1));   // symmetric hamming
        out[(size_t) t] = s / (double) n * w;
    }
    return out;
}
} // namespace

Features::Features()
{
    const double sr = kSampleRate;
    const double Q = 1.0 / (std::pow (2.0, 1.0 / kBinsPerOctave) - 1.0);

    // half band low-pass used between octaves
    lowpass = firwin2 (256, { 0.0, 0.5 / 1.001, 0.5 * 1.001, 1.0 }, { 1.0, 1.0, 0.0, 0.0 });

    // top octave kernels (no early downsampling for these settings)
    const int nOctaves = (int) std::ceil ((double) kBins / kBinsPerOctave);
    const double fminT0 = 27.5 * std::pow (2.0, nOctaves - 1);
    const int remainder = kBins % kBinsPerOctave;
    const double fmaxT = remainder == 0 ? fminT0 * std::pow (2.0, (kBinsPerOctave - 1.0) / kBinsPerOctave)
                                        : fminT0 * std::pow (2.0, (remainder - 1.0) / kBinsPerOctave);
    const double fminT = fmaxT / std::pow (2.0, 1.0 - 1.0 / kBinsPerOctave);

    fftLen = 1 << (int) std::ceil (std::log2 (std::ceil (Q * sr / fminT)));
    kernels.assign (kBinsPerOctave, std::vector<std::complex<double>> ((size_t) fftLen));
    kStart.assign (kBinsPerOctave, 0);
    kEnd.assign (kBinsPerOctave, 0);
    for (int k = 0; k < kBinsPerOctave; ++k)
    {
        const double f = fminT * std::pow (2.0, (double) k / kBinsPerOctave);
        const double l = std::ceil (Q * sr / f);
        const int L = (int) l;
        const int start = (int) std::ceil ((double) fftLen / 2.0 - l / 2.0) - (L % 2);
        std::vector<std::complex<double>> sig ((size_t) L);
        double norm1 = 0.0;
        const int n0 = (int) std::floor (-l / 2.0);   // np.r_[-l//2 : l//2]
        for (int i = 0; i < L; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / l);   // periodic hann
            const double a = (double) (n0 + i) * 2.0 * kPi * f / sr;
            sig[(size_t) i] = w * std::complex<double> (std::cos (a), std::sin (a)) / l;
            norm1 += std::abs (sig[(size_t) i]);
        }
        for (int i = 0; i < L; ++i)
            kernels[(size_t) k][(size_t) (start + i)] = sig[(size_t) i] / norm1;
        kStart[(size_t) k] = start;
        kEnd[(size_t) k] = start + L;
    }

    lengthsSqrt.resize (kBins);
    for (int b = 0; b < kBins; ++b)
        lengthsSqrt[(size_t) b] = std::sqrt (std::ceil (Q * sr / (27.5 * std::pow (2.0, (double) b / kBinsPerOctave))));

    const double harmonics[kHarmonics] = { 0.5, 1, 2, 3, 4, 5, 6, 7 };
    for (int h = 0; h < kHarmonics; ++h)
        shifts[h] = (int) std::lround (12.0 * 3.0 * std::log2 (harmonics[h]));

    // BatchNormalization after the CQT in the trained Basic Pitch model (moving mean / variance, gamma, beta),
    // measured on the original model: y = bnScale * x + bnOffset
    // (fitted against features_model.onnx of the original model: max error 4e-4 on the stacked output)
    bnScale = 2.48103f;
    bnOffset = -0.87720f;
}

std::vector<float> Features::downsample2 (const std::vector<float>& x, const std::vector<double>& h)
{
    // zero padding of (taps - 1) / 2 on both sides, correlation with stride 2 ("match_torch_exactly")
    const int taps = (int) h.size();
    const int pad = (taps - 1) / 2;
    const int n = (int) x.size();
    const int padded = n + 2 * pad;
    const int outN = padded < taps ? 0 : (padded - taps) / 2 + 1;
    std::vector<float> y ((size_t) outN);
    for (int i = 0; i < outN; ++i)
    {
        double s = 0.0;
        const int base = 2 * i - pad;
        const int k0 = std::max (0, -base), k1 = std::min (taps, n - base);
        for (int k = k0; k < k1; ++k)
            s += (double) x[(size_t) (base + k)] * h[(size_t) k];
        y[(size_t) i] = (float) s;
    }
    return y;
}

std::vector<float> Features::octaveCqt (const std::vector<float>& sig, int hop, int frames) const
{
    // reflect padding of fftLen / 2, conv1d (correlation) with stride hop; returns |CQT| for the 36 bins
    const int pad = fftLen / 2;
    const int n = (int) sig.size();
    auto at = [&] (int i)   // reflect without repeating the edge
    {
        if (n == 1)
            return sig[0];
        while (i < 0 || i >= n)
            i = i < 0 ? -i : 2 * (n - 1) - i;
        return sig[(size_t) i];
    };
    std::vector<float> mag ((size_t) frames * kBinsPerOctave, 0.0f);
    std::vector<float> win ((size_t) fftLen);
    for (int t = 0; t < frames; ++t)
    {
        const int base = t * hop - pad;
        for (int k = 0; k < fftLen; ++k)
            win[(size_t) k] = at (base + k);
        for (int b = 0; b < kBinsPerOctave; ++b)
        {
            double re = 0.0, im = 0.0;
            const auto& ker = kernels[(size_t) b];
            for (int k = kStart[(size_t) b]; k < kEnd[(size_t) b]; ++k)
            {
                re += (double) win[(size_t) k] * ker[(size_t) k].real();
                im += (double) win[(size_t) k] * ker[(size_t) k].imag();
            }
            mag[(size_t) t * kBinsPerOctave + (size_t) b] = (float) std::sqrt (re * re + im * im);
        }
    }
    return mag;
}

int Features::normalizedCqt (const float* x, int n, std::vector<float>& out) const
{
    std::vector<float> sig ((size_t) kWindowSamples, 0.0f);
    std::copy (x, x + std::min (n, kWindowSamples), sig.begin());
    const int frames = kWindowSamples / kHop + 1;
    const int nOctaves = (int) std::ceil ((double) kBins / kBinsPerOctave);

    // octaves from the top down; bins are stored from the lowest
    std::vector<float> all ((size_t) frames * (size_t) (nOctaves * kBinsPerOctave), 0.0f);
    std::vector<float> cur = sig;
    int hop = kHop;
    for (int o = 0; o < nOctaves; ++o)
    {
        if (o > 0)
        {
            cur = downsample2 (cur, lowpass);
            hop /= 2;
        }
        const auto m = octaveCqt (cur, hop, frames);
        const int binOffset = (nOctaves - 1 - o) * kBinsPerOctave;
        for (int t = 0; t < frames; ++t)
            for (int b = 0; b < kBinsPerOctave; ++b)
                all[(size_t) t * (size_t) (nOctaves * kBinsPerOctave) + (size_t) (binOffset + b)] = m[(size_t) t * kBinsPerOctave + (size_t) b];
    }

    // keep the top kBins, librosa scaling, power in dB, normalised 0..1 over the window
    const int skip = nOctaves * kBinsPerOctave - kBins;
    out.assign ((size_t) frames * kBins, 0.0f);
    float mn = 1.0e30f, mx = -1.0e30f;
    for (int t = 0; t < frames; ++t)
        for (int b = 0; b < kBins; ++b)
        {
            const double v = (double) all[(size_t) t * (size_t) (nOctaves * kBinsPerOctave) + (size_t) (skip + b)] * lengthsSqrt[(size_t) b];
            const float db = (float) (10.0 * std::log10 (v * v + 1.0e-10));
            out[(size_t) t * kBins + (size_t) b] = db;
            mn = std::min (mn, db);
        }
    for (auto& v : out)
    {
        v -= mn;
        mx = std::max (mx, v);
    }
    if (mx > 0.0f)
        for (auto& v : out)
            v /= mx;
    return frames;
}

int Features::compute (const float* x, int n, std::vector<float>& out) const
{
    std::vector<float> cqt;
    const int frames = normalizedCqt (x, n, cqt);
    out.assign ((size_t) frames * kOutBins * kHarmonics, 0.0f);
    for (int t = 0; t < frames; ++t)
        for (int h = 0; h < kHarmonics; ++h)
            for (int b = 0; b < kOutBins; ++b)
            {
                const int src = b + shifts[h];
                const float v = (src >= 0 && src < kBins) ? cqt[(size_t) t * kBins + (size_t) src] * bnScale + bnOffset : 0.0f;
                out[((size_t) t * kOutBins + (size_t) b) * kHarmonics + (size_t) h] = v;
            }
    return frames;
}
} // namespace basicpitch
