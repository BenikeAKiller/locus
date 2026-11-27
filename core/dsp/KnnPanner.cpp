#include "Panners.h"

namespace locus
{
    KnnPanner::KnnPanner (const SpeakerLayout& layout)
        : Panner (layout)
    {
    }

    int KnnPanner::getNumNeighbours (double spread) const noexcept
    {
        const auto base = std::min (threeD ? 3 : 2, numSpeakers);
        const auto extra = (int) std::lround (std::clamp (spread, 0.0, 1.0) * (numSpeakers - base));
        return std::clamp (base + extra, 1, numSpeakers);
    }

    void KnnPanner::computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept
    {
        const auto target = targetDirection (azimuthDeg, elevationDeg);
        const auto k = getNumNeighbours (spread);

        std::array<double, maxSpeakers> angle {}, g {};
        std::array<int, maxSpeakers> index {};

        for (int i = 0; i < numSpeakers; ++i)
        {
            angle[(size_t) i] = angleBetween (target, speakerDirs[(size_t) i]);
            index[(size_t) i] = i;
        }

        std::partial_sort (index.begin(), index.begin() + k, index.begin() + numSpeakers,
                           [&] (int a, int b) { return angle[(size_t) a] < angle[(size_t) b]; });

        for (int n = 0; n < k; ++n)
        {
            const auto i = (size_t) index[(size_t) n];
            const auto w = 1.0 / (angle[i] + 1.0);
            g[i] = w * w;
        }

        normalisePower (g.data(), numSpeakers);

        for (int i = 0; i < numSpeakers; ++i)
            gains[i] = (float) g[(size_t) i];
    }
}
