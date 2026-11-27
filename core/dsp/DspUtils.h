#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Vec3.h"

namespace locus
{
    inline double dbToGain (double db) noexcept { return db <= -120.0 ? 0.0 : std::pow (10.0, db / 20.0); }
    inline double gainToDb (double g) noexcept  { return g <= 1.0e-6 ? -120.0 : 20.0 * std::log10 (g); }

    /** Distance attenuation (spec §7.1). Never above 0 dB: clipped inside the near radius. */
    inline double distanceGainDb (double distance, double nearRadius, double drop, bool logarithmic) noexcept
    {
        const auto r = std::max (nearRadius, 1.0e-3);
        const auto d = std::max (distance, r);
        const auto db = logarithmic ? -drop * std::log2 (d / r)
                                    : -drop * (d - r) / r;
        return std::max (db, -120.0);
    }

    /** High-shelf gain (dB, at 3 kHz) for a source whose facing direction is
        `orientationDeg` away from the listener (0 = facing the listener). */
    inline double directivityShelfDb (double orientationDeg, double apertureDeg) noexcept
    {
        const auto theta = std::abs (wrapAzimuth (orientationDeg));
        const auto halfAperture = std::clamp (apertureDeg * 0.5, 0.0, 180.0);

        if (theta <= halfAperture || halfAperture >= 180.0)
            return 0.0;

        return -12.0 * (theta - halfAperture) / (180.0 - halfAperture);
    }

    /** Air absorption low-pass cutoff (Hz) for a distance (spec §7.1). */
    inline double airCutoffHz (double airFreq, double nearRadius, double distance, double sampleRate) noexcept
    {
        const auto r = std::max (nearRadius, 1.0e-3);
        const auto d = std::max (distance, r);
        const auto upper = std::min (20000.0, 0.45 * sampleRate);
        return std::clamp (airFreq * r / d, 500.0, upper);
    }

    /** Linear ramp toward a target, advanced in blocks. Used for every continuous parameter. */
    class Smoother
    {
    public:
        void setRampLength (double sampleRate, double seconds) noexcept
        {
            rampSamples = std::max (1, (int) std::lround (sampleRate * seconds));
        }

        void reset (double value) noexcept
        {
            current = target = value;
            remaining = 0;
        }

        void setTarget (double newTarget) noexcept
        {
            if (newTarget == target)
                return;

            target = newTarget;
            remaining = rampSamples;
            step = (target - current) / remaining;
        }

        /** Advances by numSamples and returns the value at the end of that span. */
        double advance (int numSamples) noexcept
        {
            if (remaining <= 0)
                return current = target;

            if (numSamples >= remaining)
            {
                current = target;
                remaining = 0;
            }
            else
            {
                current += step * numSamples;
                remaining -= numSamples;
            }

            return current;
        }

        void snapToTarget() noexcept { reset (target); }

        double getCurrent() const noexcept { return current; }
        double getTarget() const noexcept  { return target; }
        bool isRamping() const noexcept    { return remaining > 0; }

    private:
        double current = 0.0, target = 0.0, step = 0.0;
        int remaining = 0, rampSamples = 1;
    };

    /** Smoother for angles in degrees: always ramps the short way round. */
    class AngleSmoother
    {
    public:
        void setRampLength (double sampleRate, double seconds) noexcept { s.setRampLength (sampleRate, seconds); }
        void reset (double deg) noexcept                                { s.reset (wrapAzimuth (deg)); }

        void setTarget (double deg) noexcept
        {
            if (! s.isRamping())
                s.reset (wrapAzimuth (s.getCurrent()));

            s.setTarget (s.getCurrent() + wrapAzimuth (deg - s.getCurrent()));
        }

        double advance (int numSamples) noexcept { return wrapAzimuth (s.advance (numSamples)); }

        void snapToTarget() noexcept { s.reset (wrapAzimuth (s.getTarget())); }

    private:
        Smoother s;
    };

    /** Next power of two at or above n. */
    inline int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    /** Circular delay line. After push(x), read(0) returns x. */
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            buffer.assign ((size_t) nextPowerOfTwo (maxDelaySamples + 4), 0.0f);
            mask = (int) buffer.size() - 1;
            writePos = 0;
        }

        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        void push (float x) noexcept
        {
            buffer[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
        }

        float at (int delay) const noexcept { return buffer[(size_t) ((writePos - 1 - delay) & mask)]; }

        int getMaxDelay() const noexcept { return mask - 3; }

        float readLinear (double delay) const noexcept
        {
            delay = std::clamp (delay, 0.0, (double) getMaxDelay());
            const auto i = (int) delay;
            const auto f = (float) (delay - i);
            const auto a = at (i);
            return a + f * (at (i + 1) - a);
        }

        /** 4-point (3rd-order) Lagrange interpolation; needs delay >= 1. */
        float readCubic (double delay) const noexcept
        {
            delay = std::clamp (delay, 1.0, (double) getMaxDelay() - 1.0);
            const auto i = (int) delay;
            const auto f = (float) (delay - i);

            const auto ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
            const auto cm1 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
            const auto c0  = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
            const auto c1  = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
            const auto c2  = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
            return cm1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
        }

    private:
        std::vector<float> buffer;
        int mask = 0, writePos = 0;
    };
}
