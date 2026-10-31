// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Rilevamento della tonalità: C++ puro, senza dipendenze da JUCE (testabile da solo).
// Metodo: STFT -> chromagram (12 classi di altezza) -> correlazione con i profili
// di Krumhansl-Schmuckler per le 24 tonalità (12 maggiori + 12 minori).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "FftUtil.h"

namespace scriba
{
inline const char* pitchClassName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

// idx 0..11 = maggiore con tonica idx, 12..23 = minore con tonica idx-12
inline std::string keyName (int idx)
{
    if (idx < 0 || idx > 23)
        return "-";
    return std::string (pitchClassName (idx % 12)) + (idx >= 12 ? " minor" : " major");
}

struct KeyResult
{
    bool valid = false;
    std::array<float, 12> chroma {};   // energia per classe di altezza (0 = Do), normalizzata a somma 1
    std::array<float, 24> scores {};   // correlazione di Pearson per ogni tonalità
    int best = -1;                     // indice della tonalità migliore
    int second = -1;                   // seconda migliore
    int relativeOfBest = -1;           // tonalità relativa (maggiore <-> minore) della migliore
    float bestShare = 0.0f;            // quota (0..1) della migliore nel confronto con la seconda
    float secondShare = 0.0f;          // quota della seconda (bestShare + secondShare = 1)
};

class KeyDetector
{
public:
    static KeyResult analyse (const float* mono, size_t numSamples, double sampleRate)
    {
        constexpr size_t N = 8192;
        constexpr size_t hop = 4096;

        KeyResult r;
        if (mono == nullptr || numSamples < N || sampleRate <= 0.0)
            return r;

        // finestra di Hann
        std::vector<double> window (N);
        for (size_t i = 0; i < N; ++i)
            window[i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (N - 1));

        // classe di altezza di ogni bin FFT (-1 = fuori banda)
        std::vector<int> binPc (N / 2, -1);
        for (size_t k = 1; k < N / 2; ++k)
        {
            const double f = (double) k * sampleRate / (double) N;
            if (f < 65.0 || f > 2100.0)
                continue;
            const long midi = std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
            binPc[k] = (int) (((midi % 12) + 12) % 12);
        }

        static const FftPlan plan (N);

        std::array<double, 12> acc {};
        size_t usedFrames = 0;
        std::vector<std::complex<double>> buf (N);

        for (size_t start = 0; start + N <= numSamples; start += hop)
        {
            double energy = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                const double s = (double) mono[start + i];
                energy += s * s;
                buf[i] = std::complex<double> (s * window[i], 0.0);
            }

            if (std::sqrt (energy / (double) N) < 3.0e-4)   // frame silenzioso (< ~ -70 dBFS)
                continue;

            plan.forward (buf.data());

            std::array<double, 12> frame {};
            double sum = 0.0;
            for (size_t k = 1; k < N / 2; ++k)
            {
                if (binPc[k] < 0)
                    continue;
                const double v = std::sqrt (std::abs (buf[k]));   // compressione: meno peso ai picchi forti
                frame[(size_t) binPc[k]] += v;
                sum += v;
            }

            if (sum <= 0.0)
                continue;
            for (size_t i = 0; i < 12; ++i)
                acc[i] += frame[i] / sum;
            ++usedFrames;
        }

        if (usedFrames == 0)
            return r;

        double total = 0.0;
        for (double v : acc)
            total += v;
        std::array<float, 12> chroma {};
        for (size_t i = 0; i < 12; ++i)
            chroma[i] = (float) (acc[i] / total);
        return fromChroma (chroma);
    }

    // Key from a 12-note chroma (also used to combine the chroma of several Scriba instances)
    static KeyResult fromChroma (const std::array<float, 12>& chromaIn)
    {
        KeyResult r;
        double total = 0.0;
        for (float v : chromaIn)
            total += (double) std::max (0.0f, v);
        if (total <= 0.0)
            return r;
        for (size_t i = 0; i < 12; ++i)
            r.chroma[i] = (float) ((double) std::max (0.0f, chromaIn[i]) / total);

        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        for (int tonic = 0; tonic < 12; ++tonic)
        {
            r.scores[(size_t) tonic]      = (float) pearson (r.chroma, major, tonic);
            r.scores[(size_t) tonic + 12] = (float) pearson (r.chroma, minor, tonic);
        }

        std::vector<int> order (24);
        for (int i = 0; i < 24; ++i)
            order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return r.scores[(size_t) a] > r.scores[(size_t) b]; });

        r.best = order[0];
        r.second = order[1];
        r.relativeOfBest = relativeOf (r.best);

        // Quota relativa tra le due candidate: softmax delle correlazioni con temperatura 0.05.
        // E' una stima di quanto l'analisi preferisca una o l'altra, non una probabilità statistica.
        constexpr double temperature = 0.05;
        const double e2 = std::exp ((double) (r.scores[(size_t) r.second] - r.scores[(size_t) r.best]) / temperature);
        r.bestShare = (float) (1.0 / (1.0 + e2));
        r.secondShare = 1.0f - r.bestShare;

        r.valid = true;
        return r;
    }

    static int relativeOf (int key)
    {
        if (key < 0 || key > 23)
            return -1;
        return key < 12 ? 12 + (key + 9) % 12      // maggiore -> relativa minore
                        : (key - 12 + 3) % 12;     // minore -> relativa maggiore
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static double pearson (const std::array<float, 12>& chroma, const double* profile, int tonic)
    {
        double mc = 0.0, mp = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            mc += chroma[(size_t) i];
            mp += profile[i];
        }
        mc /= 12.0;
        mp /= 12.0;

        double num = 0.0, dc = 0.0, dp = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const double c = chroma[(size_t) pc] - mc;
            const double p = profile[(pc - tonic + 12) % 12] - mp;
            num += c * p;
            dc += c * c;
            dp += p * p;
        }
        const double den = std::sqrt (dc * dp);
        return den > 0.0 ? num / den : 0.0;
    }
};
} // namespace scriba
