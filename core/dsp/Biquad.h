#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

#include "Vec3.h"

namespace locus
{
    /** Normalised biquad coefficients (a0 = 1) with RBJ "Audio EQ Cookbook" designs. */
    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        static double clampFrequency (double sampleRate, double hz) noexcept
        {
            return std::clamp (hz, 1.0, 0.49 * sampleRate);
        }

        static BiquadCoeffs lowPass (double sampleRate, double hz, double q = 0.70710678) noexcept
        {
            const auto w0 = 2.0 * pi * clampFrequency (sampleRate, hz) / sampleRate;
            const auto cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            return normalise ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
        }

        /** Shelf slope S = 1: steepest slope without overshoot. */
        static BiquadCoeffs lowShelf (double sampleRate, double hz, double gainDb) noexcept
        {
            const auto A = std::pow (10.0, gainDb / 40.0);
            const auto w0 = 2.0 * pi * clampFrequency (sampleRate, hz) / sampleRate;
            const auto cw = std::cos (w0), alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
            const auto k = 2.0 * std::sqrt (A) * alpha;

            return normalise (A * ((A + 1.0) - (A - 1.0) * cw + k),
                              2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                              A * ((A + 1.0) - (A - 1.0) * cw - k),
                              (A + 1.0) + (A - 1.0) * cw + k,
                              -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                              (A + 1.0) + (A - 1.0) * cw - k);
        }

        static BiquadCoeffs highShelf (double sampleRate, double hz, double gainDb) noexcept
        {
            const auto A = std::pow (10.0, gainDb / 40.0);
            const auto w0 = 2.0 * pi * clampFrequency (sampleRate, hz) / sampleRate;
            const auto cw = std::cos (w0), alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
            const auto k = 2.0 * std::sqrt (A) * alpha;

            return normalise (A * ((A + 1.0) + (A - 1.0) * cw + k),
                              -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                              A * ((A + 1.0) + (A - 1.0) * cw - k),
                              (A + 1.0) - (A - 1.0) * cw + k,
                              2.0 * ((A - 1.0) - (A + 1.0) * cw),
                              (A + 1.0) - (A - 1.0) * cw - k);
        }

        /** Magnitude response in dB at a frequency. */
        double magnitudeDb (double sampleRate, double hz) const noexcept
        {
            const auto w = 2.0 * pi * hz / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
            return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-12));
        }

    private:
        static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }
    };

    /** Transposed direct form II biquad. */
    class Biquad
    {
    public:
        void setCoefficients (const BiquadCoeffs& c) noexcept { coeffs = c; }
        void reset() noexcept { z1 = z2 = 0.0; }

        float process (float x) noexcept
        {
            const double in = x;
            const double out = coeffs.b0 * in + z1;
            z1 = coeffs.b1 * in - coeffs.a1 * out + z2;
            z2 = coeffs.b2 * in - coeffs.a2 * out;
            return (float) out;
        }

    private:
        BiquadCoeffs coeffs;
        double z1 = 0.0, z2 = 0.0;
    };

    /** Axis / Omni EQ settings (spec §6). Mid is applied as a broadband gain by the caller. */
    struct ShelfEqSettings
    {
        double low = 0.0, mid = 0.0, high = 0.0, freqLow = 177.0, freqHigh = 5660.0;

        bool operator== (const ShelfEqSettings& o) const noexcept
        {
            return low == o.low && mid == o.mid && high == o.high && freqLow == o.freqLow && freqHigh == o.freqHigh;
        }
    };

    /** Double-shelving EQ (spec §7.1): overall gain = Mid, plus a low shelf (Low - Mid)
        at Freq Low and a high shelf (High - Mid) at Freq High. */
    struct DoubleShelfDesign
    {
        BiquadCoeffs lowShelf, highShelf;
        double midGainDb = 0.0;

        static DoubleShelfDesign make (double sampleRate, const ShelfEqSettings& s) noexcept
        {
            return { BiquadCoeffs::lowShelf (sampleRate, s.freqLow, s.low - s.mid),
                     BiquadCoeffs::highShelf (sampleRate, s.freqHigh, s.high - s.mid),
                     s.mid };
        }

        double magnitudeDb (double sampleRate, double hz) const noexcept
        {
            return midGainDb + lowShelf.magnitudeDb (sampleRate, hz) + highShelf.magnitudeDb (sampleRate, hz);
        }
    };
}
