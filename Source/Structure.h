// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// Song structure: Intro / Verse / Pre-Chorus / Chorus / Bridge / Outro (pure C++, no JUCE).
//
//  1. The recording is cut into bars (bar 1 = the start of the recording, as in the text report).
//  2. Every bar gets a harmony fingerprint (12-note chroma) and a loudness value.
//  3. A bar-by-bar similarity matrix shows which parts of the song look alike.
//  4. Section borders are placed where the music changes most (checkerboard novelty), on phrase boundaries
//     (multiples of 4 and 2 bars are preferred).
//  5. Sections that look alike are grouped. The loudest repeated group is the Chorus, other repeated
//     groups are Verses, a one-off part in the last third is the Bridge, the first / last one-off parts
//     are Intro / Outro.
//
// This is a musical guess, not a certainty: it is meant to give you markers to start from.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

#include "FftUtil.h"

namespace scriba
{
struct Section
{
    int startBar = 1;        // first bar (1-based)
    int endBar = 1;          // last bar (inclusive)
    double start = 0.0;      // seconds
    double end = 0.0;
    std::string label;       // "Verse 1", "Chorus 2", ...
};

class StructureDetector
{
public:
    // barSeconds: length of one bar.
    // phaseSec: how far into its bar the recording started (0 = the recording starts on a downbeat).
    // Bar k (0-based) covers [k * barSeconds - phaseSec, (k + 1) * barSeconds - phaseSec].
    // Returns an empty list when the recording is too short.
    static std::vector<Section> analyse (const float* x, size_t n, double sr, double barSeconds, double phaseSec = 0.0)
    {
        std::vector<Section> out;
        if (x == nullptr || sr <= 0.0 || barSeconds <= 0.2)
            return out;
        phaseSec = std::max (0.0, std::min (phaseSec, barSeconds * 0.999));

        const double total = (double) n / sr;
        const int bars = (int) std::floor ((total + phaseSec) / barSeconds + 0.5);   // last partial bar counts if >= half
        if (bars < 8)
            return out;

        // 1-2: features per bar
        std::vector<std::array<double, 12>> chroma;
        std::vector<double> energy;
        barFeatures (x, n, sr, barSeconds, phaseSec, bars, chroma, energy);

        // 3: similarity (chroma cosine, reduced when the loudness differs)
        std::vector<double> S ((size_t) bars * (size_t) bars, 0.0);
        for (int i = 0; i < bars; ++i)
            for (int j = 0; j < bars; ++j)
            {
                const double c = cosine (chroma[(size_t) i], chroma[(size_t) j]);
                const double de = std::min (1.0, std::abs (energy[(size_t) i] - energy[(size_t) j]) / 8.0);
                S[(size_t) i * (size_t) bars + (size_t) j] = c - 0.6 * de;
            }

        // 4: novelty and borders
        const int L = bars >= 32 ? 4 : 2;
        const int minLen = bars >= 24 ? 4 : 2;
        std::vector<double> nov ((size_t) bars + 1, 0.0);
        auto block = [&] (int a0, int a1, int b0, int b1)
        {
            double s = 0.0;
            int cnt = 0;
            for (int i = a0; i < a1; ++i)
                for (int j = b0; j < b1; ++j)
                    if (i >= 0 && j >= 0 && i < bars && j < bars)
                    {
                        s += S[(size_t) i * (size_t) bars + (size_t) j];
                        ++cnt;
                    }
            return cnt > 0 ? s / (double) cnt : 0.0;
        };
        for (int b = L; b + L <= bars; ++b)
            nov[(size_t) b] = block (b - L, b, b - L, b) + block (b, b + L, b, b + L)
                              - block (b - L, b, b, b + L) - block (b, b + L, b - L, b);

        double mean = 0.0, mx = 0.0;
        int cntN = 0;
        for (int b = L; b + L <= bars; ++b)
        {
            mean += nov[(size_t) b];
            mx = std::max (mx, nov[(size_t) b]);
            ++cntN;
        }
        mean /= (double) std::max (1, cntN);
        double var = 0.0;
        for (int b = L; b + L <= bars; ++b)
            var += (nov[(size_t) b] - mean) * (nov[(size_t) b] - mean);
        const double sd = std::sqrt (var / (double) std::max (1, cntN));
        const double thr = std::max (mean + 0.6 * sd, 0.25 * mx);

        struct Peak { int bar; double score; };
        std::vector<Peak> peaks;
        for (int b = L; b + L <= bars; ++b)
        {
            const double v = nov[(size_t) b];
            if (v < thr || v < nov[(size_t) b - 1] || v < nov[(size_t) b + 1])
                continue;
            double bonus = 0.0;
            if (b % 4 == 0)
                bonus = 0.25 * mx;
            else if (b % 2 == 0)
                bonus = 0.10 * mx;
            peaks.push_back ({ b, v + bonus });
        }
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.score > b.score; });

        std::vector<int> borders { 0, bars };
        for (const Peak& p : peaks)
        {
            bool ok = true;
            for (int b : borders)
                if (std::abs (b - p.bar) < minLen)
                    ok = false;
            if (ok)
                borders.push_back (p.bar);
        }
        std::sort (borders.begin(), borders.end());

        struct Seg { int a, z; std::array<double, 12> c; double e; int group; };
        std::vector<Seg> segs;
        for (size_t i = 0; i + 1 < borders.size(); ++i)
        {
            Seg s;
            s.a = borders[i];
            s.z = borders[i + 1];
            s.c.fill (0.0);
            s.e = 0.0;
            for (int b = s.a; b < s.z; ++b)
            {
                for (size_t k = 0; k < 12; ++k)
                    s.c[k] += chroma[(size_t) b][k];
                s.e += energy[(size_t) b];
            }
            s.e /= (double) (s.z - s.a);
            s.group = -1;
            segs.push_back (s);
        }

        // 5a: group sections that look alike
        int groups = 0;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            if (segs[i].group >= 0)
                continue;
            segs[i].group = groups;
            for (size_t j = i + 1; j < segs.size(); ++j)
            {
                if (segs[j].group >= 0)
                    continue;
                const double li = (double) (segs[i].z - segs[i].a), lj = (double) (segs[j].z - segs[j].a);
                const double ratio = li > lj ? li / lj : lj / li;
                if (cosine (segs[i].c, segs[j].c) >= 0.93 && std::abs (segs[i].e - segs[j].e) <= 3.0 && ratio <= 2.0)
                    segs[j].group = groups;
            }
            ++groups;
        }

        // two neighbours in the same group are one section
        for (size_t i = 1; i < segs.size();)
        {
            if (segs[i].group == segs[i - 1].group)
            {
                const double w0 = (double) (segs[i - 1].z - segs[i - 1].a), w1 = (double) (segs[i].z - segs[i].a);
                for (size_t k = 0; k < 12; ++k)
                    segs[i - 1].c[k] += segs[i].c[k];
                segs[i - 1].e = (segs[i - 1].e * w0 + segs[i].e * w1) / (w0 + w1);
                segs[i - 1].z = segs[i].z;
                segs.erase (segs.begin() + (long) i);
            }
            else
                ++i;
        }

        // 5b: names
        std::vector<int> groupCount ((size_t) groups, 0);
        std::vector<double> groupEnergy ((size_t) groups, 0.0);
        std::vector<int> groupBars ((size_t) groups, 0);
        for (const Seg& s : segs)
        {
            ++groupCount[(size_t) s.group];
            groupEnergy[(size_t) s.group] += s.e * (double) (s.z - s.a);
            groupBars[(size_t) s.group] += s.z - s.a;
        }
        for (int g = 0; g < groups; ++g)
            if (groupBars[(size_t) g] > 0)
                groupEnergy[(size_t) g] /= (double) groupBars[(size_t) g];

        double songMedian = 0.0;
        {
            std::vector<double> e (energy.begin(), energy.end());
            std::sort (e.begin(), e.end());
            songMedian = e[e.size() / 2];
        }

        int chorusGroup = -1;
        {
            double best = -1.0e9;
            for (int g = 0; g < groups; ++g)
            {
                if (groupCount[(size_t) g] < 2)
                    continue;
                const double score = groupEnergy[(size_t) g] + 0.15 * (double) groupBars[(size_t) g];
                if (score > best)
                {
                    best = score;
                    chorusGroup = g;
                }
            }
            // only one repeated group: it is a chorus only if it is clearly louder than the rest of the song
            if (chorusGroup >= 0)
            {
                int repeated = 0;
                for (int g = 0; g < groups; ++g)
                    repeated += groupCount[(size_t) g] >= 2 ? 1 : 0;
                if (repeated == 1 && groupEnergy[(size_t) chorusGroup] < songMedian + 0.5)
                    chorusGroup = -1;
            }
        }

        std::vector<std::string> base (segs.size());
        const int lastIdx = (int) segs.size() - 1;

        // an "edge group" is a short part that only appears at the very start and/or the very end: Intro / Outro
        std::vector<char> edgeGroup ((size_t) groups, 1);
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const bool edge = (i == 0 || (int) i == lastIdx) && segs[i].z - segs[i].a <= 12;
            if (! edge)
                edgeGroup[(size_t) segs[i].group] = 0;
        }

        int verseGroup = -1;
        for (int g = 0; g < groups; ++g)
            if (g != chorusGroup && groupCount[(size_t) g] >= 2 && ! edgeGroup[(size_t) g]
                && (verseGroup < 0 || groupBars[(size_t) g] > groupBars[(size_t) verseGroup]))
                verseGroup = g;

        for (size_t i = 0; i < segs.size(); ++i)
        {
            const Seg& s = segs[i];
            const int len = s.z - s.a;
            const bool repeated = groupCount[(size_t) s.group] >= 2;
            std::string name;

            if (s.group == chorusGroup)
                name = "Chorus";
            else if (repeated && s.group == verseGroup)
                name = "Verse";
            else if (edgeGroup[(size_t) s.group] && i == 0)
                name = "Intro";
            else if (edgeGroup[(size_t) s.group] && (int) i == lastIdx)
                name = "Outro";
            else if (repeated)
                name = len <= 4 ? "Pre-Chorus" : "Interlude";
            else if (i == 0 && (len <= 12 || s.e < songMedian))
                name = "Intro";
            else if ((int) i == lastIdx && (len <= 12 || s.e < songMedian))
                name = "Outro";
            else if ((double) s.a / (double) bars >= 0.5)
                name = "Bridge";
            else if (chorusGroup < 0 && verseGroup < 0)
                name = "Section";
            else
                name = (i + 1 < segs.size() && segs[i + 1].group == chorusGroup && len <= 4) ? "Pre-Chorus" : "Verse";
            base[i] = name;
        }

        // numbers when a name appears more than once
        std::vector<int> nameTotal (segs.size(), 0), seen (segs.size(), 0);
        for (size_t i = 0; i < segs.size(); ++i)
            for (size_t j = 0; j < segs.size(); ++j)
                if (base[j] == base[i])
                    ++nameTotal[i];
        std::vector<std::pair<std::string, int>> counters;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            int idx = -1;
            for (size_t k = 0; k < counters.size(); ++k)
                if (counters[k].first == base[i])
                    idx = (int) k;
            if (idx < 0)
            {
                counters.push_back ({ base[i], 0 });
                idx = (int) counters.size() - 1;
            }
            ++counters[(size_t) idx].second;
            Section sec;
            sec.label = nameTotal[i] > 1 ? base[i] + " " + std::to_string (counters[(size_t) idx].second) : base[i];
            sec.startBar = segs[i].a + 1;
            sec.endBar = segs[i].z;
            sec.start = std::max (0.0, (double) segs[i].a * barSeconds - phaseSec);
            sec.end = std::min (total, (double) segs[i].z * barSeconds - phaseSec);
            out.push_back (sec);
        }
        (void) seen;
        return out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static double cosine (const std::array<double, 12>& a, const std::array<double, 12>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < 12; ++i)
        {
            ab += a[i] * b[i];
            aa += a[i] * a[i];
            bb += b[i] * b[i];
        }
        return (aa > 0.0 && bb > 0.0) ? ab / std::sqrt (aa * bb) : 0.0;
    }

    static void barFeatures (const float* x, size_t n, double sr, double barSeconds, double phaseSec, int bars,
                             std::vector<std::array<double, 12>>& chroma, std::vector<double>& energy)
    {
        constexpr size_t N = 4096;
        constexpr size_t hop = 2048;
        static const FftPlan plan (N);

        std::vector<double> window (N);
        for (size_t i = 0; i < N; ++i)
            window[i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (N - 1));

        std::vector<int> binPc (N / 2, -1);
        for (size_t k = 1; k < N / 2; ++k)
        {
            const double f = (double) k * sr / (double) N;
            if (f < 65.0 || f > 2100.0)
                continue;
            const long midi = std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
            binPc[k] = (int) (((midi % 12) + 12) % 12);
        }

        chroma.assign ((size_t) bars, std::array<double, 12> {});
        std::vector<double> frames ((size_t) bars, 0.0);
        std::vector<double> sumSq ((size_t) bars, 0.0);
        std::vector<double> cntSq ((size_t) bars, 0.0);
        std::vector<std::complex<double>> buf (N);

        // loudness per bar (RMS in dB)
        for (size_t i = 0; i < n; ++i)
        {
            const long b = (long) (((double) i / sr + phaseSec) / barSeconds);
            if (b >= bars)
                break;
            sumSq[(size_t) b] += (double) x[i] * (double) x[i];
            cntSq[(size_t) b] += 1.0;
        }
        energy.assign ((size_t) bars, -100.0);
        for (int b = 0; b < bars; ++b)
            if (cntSq[(size_t) b] > 0.0)
                energy[(size_t) b] = 10.0 * std::log10 (sumSq[(size_t) b] / cntSq[(size_t) b] + 1.0e-10);

        for (size_t start = 0; start + N <= n; start += hop)
        {
            const double mid = ((double) start + (double) N * 0.5) / sr;
            const long b = (long) ((mid + phaseSec) / barSeconds);
            if (b < 0 || b >= bars)
                continue;
            double e = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                const double s = (double) x[start + i];
                e += s * s;
                buf[i] = std::complex<double> (s * window[i], 0.0);
            }
            if (std::sqrt (e / (double) N) < 3.0e-4)
                continue;
            plan.forward (buf.data());
            std::array<double, 12> f {};
            double sum = 0.0;
            for (size_t k = 1; k < N / 2; ++k)
            {
                if (binPc[k] < 0)
                    continue;
                const double v = std::sqrt (std::abs (buf[k]));
                f[(size_t) binPc[k]] += v;
                sum += v;
            }
            if (sum <= 0.0)
                continue;
            for (size_t k = 0; k < 12; ++k)
                chroma[(size_t) b][k] += f[k] / sum;
            frames[(size_t) b] += 1.0;
        }

        // remove the average colour of the whole song so that differences between bars stand out
        std::array<double, 12> avg {};
        int used = 0;
        for (int b = 0; b < bars; ++b)
        {
            if (frames[(size_t) b] <= 0.0)
                continue;
            for (size_t k = 0; k < 12; ++k)
            {
                chroma[(size_t) b][k] /= frames[(size_t) b];
                avg[k] += chroma[(size_t) b][k];
            }
            ++used;
        }
        if (used > 0)
            for (double& v : avg)
                v /= (double) used;
        for (int b = 0; b < bars; ++b)
            for (size_t k = 0; k < 12; ++k)
                chroma[(size_t) b][k] = frames[(size_t) b] > 0.0 ? chroma[(size_t) b][k] - avg[k] : 0.0;
    }
};
} // namespace scriba
