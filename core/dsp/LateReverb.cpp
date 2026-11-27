#include "LateReverb.h"

namespace locus
{
    namespace
    {
        // Line lengths in ms at Room Size 2500 m3; scaled by cbrt(roomSize / 2500) and
        // rounded to distinct primes, so the lengths stay mutually prime.
        constexpr double baseLengths8[8]   { 23.3, 28.7, 33.9, 39.7, 44.9, 51.1, 57.7, 64.1 };
        constexpr double baseLengths16[16] { 19.1, 21.7, 24.3, 26.9, 29.9, 32.3, 35.3, 38.1,
                                             41.3, 44.3, 47.3, 50.9, 54.1, 57.7, 61.1, 64.9 };

        constexpr double maxRoomScale      = 3.42;  // cbrt(100000 / 2500)
        constexpr double lateOnsetMs       = 100.0; // spec §7.4 time structure
        constexpr double maxPredelaySeconds = 0.5;
        constexpr double lengthRampSeconds = 0.020;
        constexpr float  outputGain        = 0.35f; // calibration, see DECISIONS.md

        bool isPrime (int n) noexcept
        {
            if (n < 2) return false;
            for (int d = 2; d * d <= n; ++d)
                if (n % d == 0)
                    return false;
            return true;
        }

        int nextPrime (int n) noexcept
        {
            while (! isPrime (n))
                ++n;
            return n;
        }

        /** In-place fast Walsh-Hadamard transform, normalised (orthogonal). */
        void hadamard (float* v, int n) noexcept
        {
            for (int h = 1; h < n; h <<= 1)
                for (int i = 0; i < n; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const auto a = v[j], b = v[j + h];
                        v[j] = a + b;
                        v[j + h] = a - b;
                    }

            const auto scale = 1.0f / std::sqrt ((float) n);
            for (int i = 0; i < n; ++i)
                v[i] *= scale;
        }
    }

    void LateReverb::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        const auto maxLength = (int) std::ceil (baseLengths16[15] * maxRoomScale * 0.001 * sampleRate) + 64;
        for (auto& l : lines)
            l.prepare (maxLength);

        for (auto& s : length)
            s.setRampLength (sampleRate, lengthRampSeconds);

        predelay.prepare ((int) std::ceil (maxPredelaySeconds * sampleRate) + renderChunk);
        setNumLines (numLines);
    }

    void LateReverb::setNumLines (int newNumLines) noexcept
    {
        numLines = newNumLines >= 16 ? 16 : 8;
        lastScale = -1.0;
        absorptionValid = false;
        clear();
    }

    void LateReverb::clear() noexcept
    {
        for (auto& l : lines)   l.clear();
        for (auto& f : lowShelf)  f.reset();
        for (auto& f : highShelf) f.reset();
        predelay.clear();
        airState = 0.0;
    }

    void LateReverb::updateLengths (double roomScale) noexcept
    {
        const bool first = lastScale < 0.0;
        lastScale = roomScale;

        const auto* base = numLines == 16 ? baseLengths16 : baseLengths8;
        int previous = 0;

        for (int l = 0; l < numLines; ++l)
        {
            auto n = (int) std::lround (base[l] * roomScale * 0.001 * sampleRate);
            n = nextPrime (std::max ({ n, 3, previous + 1 }));
            previous = n;

            if (first)
                length[(size_t) l].reset ((double) n);
            else
                length[(size_t) l].setTarget ((double) n);
        }
    }

    void LateReverb::updateAbsorption (const Settings& s) noexcept
    {
        const auto tMid  = effectiveDecay (s);
        const auto tLow  = s.infinite ? tMid : std::max (0.01, s.decay * s.decayLow);
        const auto tHigh = s.infinite ? tMid : std::max (0.01, s.decay * s.decayHigh);

        for (int l = 0; l < numLines; ++l)
        {
            const auto seconds = length[(size_t) l].getCurrent() / sampleRate;

            // Per pass through a line of this length, a band with decay time T loses 60 dB * len / T.
            midGain[(size_t) l] = (float) dbToGain (-60.0 * seconds / tMid);
            lowShelf[(size_t) l].setCoefficients (BiquadCoeffs::lowShelf (sampleRate, s.freqLow, -60.0 * seconds * (1.0 / tLow - 1.0 / tMid)));
            highShelf[(size_t) l].setCoefficients (BiquadCoeffs::highShelf (sampleRate, s.freqHigh, -60.0 * seconds * (1.0 / tHigh - 1.0 / tMid)));
        }
    }

    void LateReverb::processChunk (const float* input, int n, const Settings& s,
                                   const float* lineGains, float levelStart, float levelEnd, SpeakerBus& bus) noexcept
    {
        if (std::abs (s.roomScale - lastScale) > 1.0e-6)
            updateLengths (s.roomScale);

        bool ramping = false;
        double shortest = 1.0e9;
        for (int l = 0; l < numLines; ++l)
        {
            ramping = ramping || length[(size_t) l].isRamping();
            shortest = std::min (shortest, length[(size_t) l].advance (n));
        }

        const bool settingsChanged = ! absorptionValid
            || s.decay != lastAbsorption.decay || s.decayLow != lastAbsorption.decayLow
            || s.decayHigh != lastAbsorption.decayHigh || s.freqLow != lastAbsorption.freqLow
            || s.freqHigh != lastAbsorption.freqHigh || s.infinite != lastAbsorption.infinite;

        if (settingsChanged || ramping)
        {
            updateAbsorption (s);
            lastAbsorption = s;
            absorptionValid = true;
        }

        // The first late energy leaves the network after the predelay plus the shortest line.
        const auto onset = (lateOnsetMs * s.roomScale + s.offsetMs) * 0.001 * sampleRate;
        const auto predelaySamples = std::clamp (onset - shortest, 0.0, (double) predelay.getMaxDelay());
        const auto airCoeff = 1.0 - std::exp (-2.0 * pi * std::min (s.airFreq, 0.45 * sampleRate) / sampleRate);
        const auto inputScale = 1.0f / std::sqrt ((float) numLines);

        std::array<float, maxLateLines> v {};
        const auto invN = 1.0 / n;

        for (int i = 0; i < n; ++i)
        {
            const auto t = (i + 1) * invN;

            predelay.push (input[i]);
            double u = predelay.readLinear (previousPredelay + (predelaySamples - previousPredelay) * t);

            airState += airCoeff * (u - airState);
            u += (previousAirMix + (s.airMix - previousAirMix) * t) * (airState - u);

            for (int l = 0; l < numLines; ++l)
            {
                auto y = lines[(size_t) l].readLinear (length[(size_t) l].getCurrent() - 1.0);
                y = highShelf[(size_t) l].process (lowShelf[(size_t) l].process (y)) * midGain[(size_t) l];
                lineOut[(size_t) l][(size_t) i] = y;
                v[(size_t) l] = y;
            }

            hadamard (v.data(), numLines);

            for (int l = 0; l < numLines; ++l)
            {
                const auto sign = (l & 1) ? -1.0f : 1.0f;
                lines[(size_t) l].push (v[(size_t) l] + sign * inputScale * (float) u);
            }
        }

        previousPredelay = predelaySamples;
        previousAirMix = s.airMix;

        // Pan each line's output to its virtual direction.
        if (levelStart == 0.0f && levelEnd == 0.0f)
            return;

        const auto g0 = levelStart * outputGain, g1 = levelEnd * outputGain;
        const auto dg = (g1 - g0) / (float) n;

        for (int spk = 0; spk < bus.numSpeakers; ++spk)
        {
            auto* out = bus.channels[(size_t) spk];
            auto level = g0;

            for (int i = 0; i < n; ++i)
            {
                level += dg;
                float sum = 0.0f;
                for (int l = 0; l < numLines; ++l)
                    sum += lineGains[l * bus.numSpeakers + spk] * lineOut[(size_t) l][(size_t) i];
                out[i] += level * sum;
            }
        }
    }
}
