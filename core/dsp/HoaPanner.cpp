#include "Panners.h"

namespace locus
{
    namespace
    {
        constexpr int numVirtual2D = 72;  // every 5 degrees
        constexpr int numVirtual3D = 240; // Fibonacci sphere

        /** Legendre polynomial P_n(x). */
        double legendre (int n, double x) noexcept
        {
            double p0 = 1.0, p1 = x;
            if (n == 0) return p0;

            for (int k = 1; k < n; ++k)
            {
                const auto p2 = ((2 * k + 1) * x * p1 - k * p0) / (k + 1);
                p0 = p1;
                p1 = p2;
            }

            return p1;
        }

        /** Associated Legendre function P_n^m(x), m >= 0 (Condon-Shortley phase included). */
        double associatedLegendre (int n, int m, double x) noexcept
        {
            double pmm = 1.0;
            const auto s = std::sqrt (std::max (0.0, (1.0 - x) * (1.0 + x)));
            for (int i = 1; i <= m; ++i)
                pmm *= -(2.0 * i - 1.0) * s;

            if (n == m)
                return pmm;

            double pm1 = x * (2.0 * m + 1.0) * pmm;
            if (n == m + 1)
                return pm1;

            double pn = 0.0;
            for (int k = m + 2; k <= n; ++k)
            {
                pn = ((2.0 * k - 1.0) * x * pm1 - (k + m - 1.0) * pmm) / (k - m);
                pmm = pm1;
                pm1 = pn;
            }

            return pn;
        }

        double factorial (int n) noexcept
        {
            double f = 1.0;
            for (int i = 2; i <= n; ++i)
                f *= i;
            return f;
        }

        /** Max-rE weights (3D): w_n = P_n(cos(137.9 deg / (N + 1.51))). */
        double maxReWeight3D (int n, int order) noexcept
        {
            return legendre (n, std::cos (degToRad (137.9) / (order + 1.51)));
        }

        /** Max-rE weights (2D): w_m = cos(m * pi / (2N + 2)). */
        double maxReWeight2D (int m, int order) noexcept
        {
            return std::cos (m * pi / (2.0 * order + 2.0));
        }
    }

    HoaPanner::HoaPanner (const SpeakerLayout& layout)
        : Panner (layout)
    {
        std::vector<Vec3> virtualDirs;

        if (threeD)
        {
            order = std::clamp ((int) std::floor (std::sqrt ((double) numSpeakers)) - 1, 0, maxOrder3D);
            numComponents = (order + 1) * (order + 1);

            for (int n = 0; n <= order; ++n)
                for (int m = -n; m <= n; ++m)
                    componentDegree.push_back (n);

            const auto golden = pi * (3.0 - std::sqrt (5.0));
            for (int j = 0; j < numVirtual3D; ++j)
            {
                const auto z = 1.0 - 2.0 * (j + 0.5) / numVirtual3D;
                const auto r = std::sqrt (1.0 - z * z);
                const auto phi = j * golden;
                virtualDirs.push_back ({ r * std::cos (phi), r * std::sin (phi), z });
            }
        }
        else
        {
            order = std::clamp ((numSpeakers - 1) / 2, 0, maxOrder2D);
            numComponents = 2 * order + 1;

            componentDegree.push_back (0);
            for (int m = 1; m <= order; ++m)
            {
                componentDegree.push_back (m); // cos
                componentDegree.push_back (m); // sin
            }

            for (int j = 0; j < numVirtual2D; ++j)
                virtualDirs.push_back (directionFromAzEl (j * 360.0 / numVirtual2D, 0.0));
        }

        for (int n = 0; n <= order; ++n)
            fullWeights[(size_t) n] = threeD ? maxReWeight3D (n, order) : maxReWeight2D (n, order);

        const auto lowOrder = std::min (order, 1);
        for (int n = 0; n <= lowOrder; ++n)
            order1Weights[(size_t) n] = threeD ? maxReWeight3D (n, lowOrder) : maxReWeight2D (n, lowOrder);

        // AllRAD: render each virtual point on the real speakers with VBAP, then project onto
        // the harmonic basis. By the addition theorem, panning a source with weights w_n is then
        //   gains = sum_k w_n(k) Y_k(source) D_k,  D_k = c / M * sum_j Y_k(v_j) G_j
        // with c = 4 pi for orthonormal spherical harmonics and c = 1 for the 2D basis below.
        const VbapPanner vbap (layout);
        const auto numVirtual = virtualDirs.size();
        const auto c = threeD ? 4.0 * pi : 1.0;

        decode.assign ((size_t) numComponents * (size_t) numSpeakers, 0.0);
        std::vector<double> basis ((size_t) numComponents), g ((size_t) numSpeakers);

        for (const auto& v : virtualDirs)
        {
            vbap.computePointGains (v, g.data());
            evaluateBasis (v, basis.data());

            for (int k = 0; k < numComponents; ++k)
                for (int i = 0; i < numSpeakers; ++i)
                    decode[(size_t) (k * numSpeakers + i)] += basis[(size_t) k] * g[(size_t) i];
        }

        for (auto& d : decode)
            d *= c / (double) numVirtual;
    }

    void HoaPanner::evaluateBasis (Vec3 dir, double* out) const noexcept
    {
        if (threeD)
        {
            // Real orthonormal spherical harmonics in ACN order.
            const auto cosTheta = std::clamp (dir.z, -1.0, 1.0);
            const auto phi = std::atan2 (dir.y, dir.x);

            for (int n = 0, k = 0; n <= order; ++n)
                for (int m = -n; m <= n; ++m, ++k)
                {
                    const auto am = std::abs (m);
                    const auto norm = std::sqrt ((2.0 * n + 1.0) / (4.0 * pi) * factorial (n - am) / factorial (n + am));
                    const auto p = norm * associatedLegendre (n, am, cosTheta);

                    if (m == 0)     out[k] = p;
                    else if (m > 0) out[k] = std::sqrt (2.0) * p * std::cos (m * phi);
                    else            out[k] = std::sqrt (2.0) * p * std::sin (am * phi);
                }
        }
        else
        {
            // Circular harmonics: w_0 + 2 sum w_m cos(m (a - b)) = sum_k w Y_k(a) Y_k(b)
            // with Y = { 1, sqrt2 cos m t, sqrt2 sin m t }.
            const auto theta = std::atan2 (dir.x, dir.y);
            out[0] = 1.0;
            for (int m = 1; m <= order; ++m)
            {
                out[2 * m - 1] = std::sqrt (2.0) * std::cos (m * theta);
                out[2 * m]     = std::sqrt (2.0) * std::sin (m * theta);
            }
        }
    }

    void HoaPanner::computeRawGains (double azimuthDeg, double elevationDeg, double spread, double* gains) const noexcept
    {
        spread = std::clamp (spread, 0.0, 1.0);

        std::array<double, (maxOrder3D + 1) * (maxOrder3D + 1) + 2 * maxOrder2D + 1> basis {};
        evaluateBasis (targetDirection (azimuthDeg, elevationDeg), basis.data());

        std::fill (gains, gains + numSpeakers, 0.0);

        for (int k = 0; k < numComponents; ++k)
        {
            const auto n = (size_t) componentDegree[(size_t) k];
            const auto w = (1.0 - spread) * fullWeights[n] + spread * order1Weights[n];
            const auto coeff = w * basis[(size_t) k];

            if (coeff == 0.0)
                continue;

            const auto* row = decode.data() + (size_t) (k * numSpeakers);
            for (int i = 0; i < numSpeakers; ++i)
                gains[i] += coeff * row[i];
        }
    }

    void HoaPanner::computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept
    {
        std::array<double, maxSpeakers> g {};
        computeRawGains (azimuthDeg, elevationDeg, spread, g.data());

        if (! normalisePower (g.data(), numSpeakers))
            nearestSpeaker (targetDirection (azimuthDeg, elevationDeg), g.data());

        for (int i = 0; i < numSpeakers; ++i)
            gains[i] = (float) g[(size_t) i];
    }
}
