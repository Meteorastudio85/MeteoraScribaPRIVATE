// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// FFT radix-2 con tabelle precalcolate (C++ puro, nessuna dipendenza).

#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace scriba
{
class FftPlan
{
public:
    // n deve essere una potenza di due (>= 2)
    explicit FftPlan (size_t n) : size_ (n), rev_ (n), tw_ (n / 2)
    {
        size_t lg = 0;
        while ((size_t (1) << lg) < n)
            ++lg;

        for (size_t i = 0; i < n; ++i)
            rev_[i] = (i == 0) ? 0 : ((rev_[i >> 1] >> 1) | ((i & 1) << (lg - 1)));

        const double pi = 3.14159265358979323846;
        for (size_t k = 0; k < n / 2; ++k)
            tw_[k] = std::polar (1.0, -2.0 * pi * (double) k / (double) n);
    }

    size_t size() const noexcept { return size_; }

    void forward (std::complex<double>* a) const { run (a, false); }

    // inversa, con scala 1/N
    void inverse (std::complex<double>* a) const { run (a, true); }

private:
    void run (std::complex<double>* a, bool inv) const
    {
        const size_t n = size_;
        for (size_t i = 0; i < n; ++i)
            if (i < rev_[i])
                std::swap (a[i], a[rev_[i]]);

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const size_t half = len >> 1;
            const size_t step = n / len;
            for (size_t i = 0; i < n; i += len)
            {
                for (size_t k = 0; k < half; ++k)
                {
                    std::complex<double> w = tw_[k * step];
                    if (inv)
                        w = std::conj (w);
                    const std::complex<double> u = a[i + k];
                    const std::complex<double> v = a[i + k + half] * w;
                    a[i + k] = u + v;
                    a[i + k + half] = u - v;
                }
            }
        }

        if (inv)
        {
            const double s = 1.0 / (double) n;
            for (size_t i = 0; i < n; ++i)
                a[i] *= s;
        }
    }

    size_t size_;
    std::vector<size_t> rev_;
    std::vector<std::complex<double>> tw_;
};
} // namespace scriba
