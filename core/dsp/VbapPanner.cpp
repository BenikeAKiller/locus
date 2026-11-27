#include "Panners.h"

#include <numeric>

namespace locus
{
    namespace
    {
        bool invert3x3 (const double m[3][3], double out[3][3]) noexcept
        {
            const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
            const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
            const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
            const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;

            if (std::abs (det) < 1.0e-12)
                return false;

            const double s = 1.0 / det;
            out[0][0] = c00 * s;
            out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * s;
            out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * s;
            out[1][0] = c01 * s;
            out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * s;
            out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * s;
            out[2][0] = c02 * s;
            out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * s;
            out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * s;
            return true;
        }

        /** Pairs wider than this use the sine/cosine law instead of the VBAP inverse,
            which becomes ill-conditioned (and goes negative) near 180 degrees. */
        constexpr double maxInversePairWidth = 170.0;

        /** Elevation beyond which a real speaker counts as "below" or "above" the listener. */
        constexpr double poleCoverageElevation = 10.0;
    }

    VbapPanner::VbapPanner (const SpeakerLayout& layout)
        : Panner (layout)
    {
        if (numSpeakers < 2)
            return;

        if (! (threeD && buildTriangulation()))
        {
            triangles.clear();
            virtualNeighbours.clear();
            buildPairs();
        }
    }

    //==============================================================================
    void VbapPanner::buildPairs()
    {
        vertexDirs = speakerDirs;

        std::vector<double> az ((size_t) numSpeakers);
        for (int i = 0; i < numSpeakers; ++i)
            az[(size_t) i] = azimuthOf (speakerDirs[(size_t) i]);

        std::vector<int> order ((size_t) numSpeakers);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return az[(size_t) a] < az[(size_t) b]; });

        for (int k = 0; k < numSpeakers; ++k)
        {
            Pair p;
            p.a = order[(size_t) k];
            p.b = order[(size_t) ((k + 1) % numSpeakers)];
            p.startAzimuth = az[(size_t) p.a];
            p.width = std::fmod (az[(size_t) p.b] - az[(size_t) p.a] + 360.0, 360.0);

            if (p.width < 1.0e-9)
                continue; // two speakers at the same azimuth

            if (p.width < maxInversePairWidth)
            {
                const auto la = directionFromAzEl (az[(size_t) p.a], 0.0);
                const auto lb = directionFromAzEl (az[(size_t) p.b], 0.0);
                const auto det = la.x * lb.y - lb.x * la.y;

                if (std::abs (det) > 1.0e-12)
                {
                    p.useInverse = true;
                    p.inv[0][0] =  lb.y / det;
                    p.inv[0][1] = -lb.x / det;
                    p.inv[1][0] = -la.y / det;
                    p.inv[1][1] =  la.x / det;
                }
            }

            pairs.push_back (p);
        }
    }

    //==============================================================================
    bool VbapPanner::buildTriangulation()
    {
        vertexDirs = speakerDirs;

        bool hasLow = false, hasHigh = false;
        for (const auto& d : speakerDirs)
        {
            const auto el = elevationOf (d);
            hasLow  = hasLow  || el < -poleCoverageElevation;
            hasHigh = hasHigh || el >  poleCoverageElevation;
        }

        if (! hasLow)  vertexDirs.push_back ({ 0.0, 0.0, -1.0 });
        if (! hasHigh) vertexDirs.push_back ({ 0.0, 0.0,  1.0 });

        const int n = (int) vertexDirs.size();
        if (n < 4)
            return false;

        // Convex hull by brute force (n <= 34): every triple whose plane has all other points
        // on one side is a hull face. Coplanar triples are merged into one polygon per plane.
        struct Face
        {
            Vec3 normal;
            double offset;
            std::vector<int> verts;
        };

        std::vector<Face> faces;
        constexpr double eps = 1.0e-7;

        for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        for (int k = j + 1; k < n; ++k)
        {
            const auto& origin = vertexDirs[(size_t) i];
            auto normal = cross (vertexDirs[(size_t) j] - origin, vertexDirs[(size_t) k] - origin);
            const auto len = length (normal);

            if (len < 1.0e-9)
                continue; // collinear or duplicate points

            normal = normal / len;
            auto offset = dot (normal, origin);
            bool above = false, below = false;

            for (int m = 0; m < n && ! (above && below); ++m)
            {
                if (m == i || m == j || m == k)
                    continue;

                const auto s = dot (normal, vertexDirs[(size_t) m]) - offset;
                if (s > eps)       above = true;
                else if (s < -eps) below = true;
            }

            if (above && below)
                continue;

            if (! above && ! below)
                return false; // every point lies in one plane: no volume to triangulate

            if (above)
            {
                normal = -normal;
                offset = -offset;
            }

            auto existing = std::find_if (faces.begin(), faces.end(), [&] (const Face& f)
            {
                return dot (f.normal, normal) > 1.0 - 1.0e-9 && std::abs (f.offset - offset) < eps;
            });

            if (existing == faces.end())
            {
                faces.push_back ({ normal, offset, { i, j, k } });
            }
            else
            {
                for (int v : { i, j, k })
                    if (std::find (existing->verts.begin(), existing->verts.end(), v) == existing->verts.end())
                        existing->verts.push_back (v);
            }
        }

        triangles.clear();

        for (auto& f : faces)
        {
            if (f.verts.size() > 3)
            {
                // Points on a sphere that share a plane lie on a circle, so all are polygon
                // vertices: sort them around the centroid and fan-triangulate.
                Vec3 centre;
                for (int v : f.verts)
                    centre = centre + vertexDirs[(size_t) v];
                centre = centre / (double) f.verts.size();

                const auto u = normalised (vertexDirs[(size_t) f.verts.front()] - centre);
                const auto w = cross (f.normal, u);

                std::sort (f.verts.begin(), f.verts.end(), [&] (int a, int b)
                {
                    const auto da = vertexDirs[(size_t) a] - centre;
                    const auto db = vertexDirs[(size_t) b] - centre;
                    return std::atan2 (dot (da, w), dot (da, u)) < std::atan2 (dot (db, w), dot (db, u));
                });
            }

            for (size_t t = 1; t + 1 < f.verts.size(); ++t)
            {
                Triangle tri;
                tri.v[0] = f.verts[0];
                tri.v[1] = f.verts[t];
                tri.v[2] = f.verts[t + 1];
                triangles.push_back (tri);
            }
        }

        // A closed triangulated convex hull with V vertices has exactly 2V - 4 triangles.
        std::vector<bool> used ((size_t) n, false);
        for (const auto& t : triangles)
            for (int v : t.v)
                used[(size_t) v] = true;

        const auto numUsed = (int) std::count (used.begin(), used.end(), true);
        if ((int) triangles.size() != 2 * numUsed - 4)
            return false;

        for (auto& t : triangles)
        {
            double m[3][3];
            for (int c = 0; c < 3; ++c)
            {
                const auto& l = vertexDirs[(size_t) t.v[c]];
                m[0][c] = l.x;
                m[1][c] = l.y;
                m[2][c] = l.z;
            }

            if (! invert3x3 (m, t.inv))
                return false;
        }

        virtualNeighbours.assign ((size_t) (n - numSpeakers), {});
        for (const auto& t : triangles)
            for (int v : t.v)
                if (v >= numSpeakers)
                    for (int other : t.v)
                        if (other < numSpeakers)
                        {
                            auto& list = virtualNeighbours[(size_t) (v - numSpeakers)];
                            if (std::find (list.begin(), list.end(), other) == list.end())
                                list.push_back (other);
                        }

        return true;
    }

    //==============================================================================
    void VbapPanner::computePairGains (Vec3 direction, double* g) const noexcept
    {
        const auto az = azimuthOf (direction);

        for (const auto& p : pairs)
        {
            const auto t = std::fmod (az - p.startAzimuth + 720.0, 360.0);
            if (t > p.width + 1.0e-9)
                continue;

            double ga, gb;
            if (p.useInverse)
            {
                const auto d = directionFromAzEl (az, 0.0);
                ga = p.inv[0][0] * d.x + p.inv[0][1] * d.y;
                gb = p.inv[1][0] * d.x + p.inv[1][1] * d.y;
            }
            else
            {
                const auto theta = (t / p.width) * (pi * 0.5);
                ga = std::cos (theta);
                gb = std::sin (theta);
            }

            g[p.a] = std::max (0.0, ga);
            g[p.b] = std::max (0.0, gb);
            return;
        }

        nearestSpeaker (direction, g);
    }

    void VbapPanner::computeTriangleGains (Vec3 direction, double* g) const noexcept
    {
        const Triangle* chosen = nullptr;
        double chosenGains[3] {};
        double bestMin = -1.0e300;

        for (const auto& t : triangles)
        {
            double tg[3];
            for (int r = 0; r < 3; ++r)
                tg[r] = t.inv[r][0] * direction.x + t.inv[r][1] * direction.y + t.inv[r][2] * direction.z;

            const auto minGain = std::min ({ tg[0], tg[1], tg[2] });
            if (minGain > bestMin)
            {
                bestMin = minGain;
                chosen = &t;
                std::copy (tg, tg + 3, chosenGains);

                if (minGain >= -1.0e-9)
                    break; // the direction lies inside this triangle
            }
        }

        if (chosen != nullptr)
            for (int r = 0; r < 3; ++r)
                g[chosen->v[r]] = std::max (0.0, chosenGains[r]);
    }

    void VbapPanner::computePointGains (Vec3 direction, double* gains) const noexcept
    {
        if (numSpeakers == 1)
        {
            gains[0] = 1.0;
            return;
        }

        std::array<double, maxPanVertices> g {};

        if (triangles.empty())
        {
            computePairGains (direction, g.data());
        }
        else
        {
            computeTriangleGains (direction, g.data());

            // Hand a virtual speaker's energy to its real neighbours, so no direction is silent.
            for (size_t vi = 0; vi < virtualNeighbours.size(); ++vi)
            {
                const auto gv = g[(size_t) numSpeakers + vi];
                const auto& neighbours = virtualNeighbours[vi];

                if (gv <= 0.0 || neighbours.empty())
                    continue;

                const auto share = gv * gv / (double) neighbours.size();
                for (int r : neighbours)
                    g[(size_t) r] = std::sqrt (g[(size_t) r] * g[(size_t) r] + share);
            }
        }

        std::copy (g.begin(), g.begin() + numSpeakers, gains);

        if (! normalisePower (gains, numSpeakers))
            nearestSpeaker (direction, gains);
    }

    void VbapPanner::computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept
    {
        const auto target = targetDirection (azimuthDeg, elevationDeg);

        std::array<double, maxSpeakers> sum {}, point {};
        computePointGains (target, sum.data());

        spread = std::clamp (spread, 0.0, 1.0);

        if (spread > 1.0e-6 && numSpeakers > 1)
        {
            // MDAP: 8 extra directions on a ring of spread * 90 degrees around the target.
            const auto alpha = spread * (pi * 0.5);
            const auto helper = std::abs (target.z) < 0.9 ? Vec3 { 0.0, 0.0, 1.0 } : Vec3 { 1.0, 0.0, 0.0 };
            const auto u = normalised (cross (target, helper));
            const auto v = cross (target, u);

            for (int k = 0; k < 8; ++k)
            {
                const auto phi = k * (pi / 4.0);
                auto dir = target * std::cos (alpha) + (u * std::cos (phi) + v * std::sin (phi)) * std::sin (alpha);

                if (! threeD)
                {
                    dir.z = 0.0;
                    if (length (dir) < 1.0e-6)
                        continue;
                }

                computePointGains (normalised (dir), point.data());
                for (int i = 0; i < numSpeakers; ++i)
                    sum[(size_t) i] += point[(size_t) i];
            }

            normalisePower (sum.data(), numSpeakers);
        }

        for (int i = 0; i < numSpeakers; ++i)
            gains[i] = (float) sum[(size_t) i];
    }
}
