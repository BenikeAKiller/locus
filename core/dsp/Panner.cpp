#include "Panners.h"

namespace locus
{
    Panner::Panner (const SpeakerLayout& layout)
        : numSpeakers (layout.size()),
          threeD (layout.is3D())
    {
        jassert (numSpeakers >= 1 && numSpeakers <= maxSpeakers);

        speakerDirs.reserve ((size_t) numSpeakers);
        for (const auto& s : layout.speakers)
            speakerDirs.push_back (directionFromAzEl (s.azimuth, threeD ? s.elevation : 0.0));
    }

    Vec3 Panner::targetDirection (double azimuthDeg, double elevationDeg) const noexcept
    {
        return directionFromAzEl (azimuthDeg, threeD ? elevationDeg : 0.0);
    }

    bool Panner::normalisePower (double* gains, int num) noexcept
    {
        double sum = 0.0;
        for (int i = 0; i < num; ++i)
            sum += gains[i] * gains[i];

        if (sum < 1.0e-24)
            return false;

        const auto scale = 1.0 / std::sqrt (sum);
        for (int i = 0; i < num; ++i)
            gains[i] *= scale;

        return true;
    }

    void Panner::nearestSpeaker (Vec3 direction, double* gains) const noexcept
    {
        int best = 0;
        double bestDot = -2.0;

        for (int i = 0; i < numSpeakers; ++i)
        {
            gains[i] = 0.0;
            if (const auto d = dot (direction, speakerDirs[(size_t) i]); d > bestDot)
            {
                bestDot = d;
                best = i;
            }
        }

        gains[best] = 1.0;
    }

    std::unique_ptr<Panner> createPanner (PanningType type, const SpeakerLayout& layout)
    {
        switch (type)
        {
            case PanningType::nearest:    return std::make_unique<KnnPanner> (layout);
            case PanningType::ambisonics: return std::make_unique<HoaPanner> (layout);
            case PanningType::angular:    break;
        }

        return std::make_unique<VbapPanner> (layout);
    }
}
