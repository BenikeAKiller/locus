#pragma once

#include <array>
#include <memory>
#include <vector>

#include "Vec3.h"
#include "layouts/SpeakerLayout.h"

namespace locus
{
    enum class PanningType
    {
        angular = 0, // VBAP
        nearest,     // KNN
        ambisonics   // HOA, AllRAD decode
    };

    /** Real speakers plus up to two virtual ones (nadir, zenith) used by 3D VBAP. */
    constexpr int maxPanVertices = maxSpeakers + 2;

    /** Computes one gain per speaker for a point source direction (spec §7.2).

        Build panners on the message thread; computeGains() is real-time safe
        (no allocation, no locks). Gains are always normalised to constant power.
    */
    class Panner
    {
    public:
        explicit Panner (const SpeakerLayout& layout);
        virtual ~Panner() = default;

        int getNumSpeakers() const noexcept { return numSpeakers; }
        bool is3D() const noexcept          { return threeD; }

        /** @param azimuthDeg    0 = front, positive = right
            @param elevationDeg  ignored for 2D layouts
            @param spread        0..1
            @param gains         receives getNumSpeakers() values with sum of squares = 1
        */
        virtual void computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept = 0;

    protected:
        /** Source direction; projected onto the horizontal plane for 2D layouts. */
        Vec3 targetDirection (double azimuthDeg, double elevationDeg) const noexcept;

        /** Scales to sum of squares = 1. Returns false (and leaves gains alone) if all are zero. */
        static bool normalisePower (double* gains, int num) noexcept;

        /** Sets the speaker closest to the direction to 1 and the rest to 0. */
        void nearestSpeaker (Vec3 direction, double* gains) const noexcept;

        int numSpeakers = 0;
        bool threeD = false;
        std::vector<Vec3> speakerDirs; // unit vectors; horizontal for 2D layouts
    };

    std::unique_ptr<Panner> createPanner (PanningType type, const SpeakerLayout& layout);

    //==============================================================================
    /** Angular: pairwise VBAP for 2D layouts, triplet VBAP on the convex hull for 3D.
        Spread uses MDAP (8 extra directions on a ring of spread * 90 degrees). */
    class VbapPanner final : public Panner
    {
    public:
        explicit VbapPanner (const SpeakerLayout& layout);

        void computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept override;

        /** Gains for one unit direction without spread, power-normalised. */
        void computePointGains (Vec3 direction, double* gains) const noexcept;

        /** True when 3D triplet panning is in use (false for 2D layouts or if the hull could not be built). */
        bool usesTriangulation() const noexcept { return ! triangles.empty(); }
        int getNumTriangles() const noexcept    { return (int) triangles.size(); }

    private:
        struct Pair
        {
            int a = 0, b = 0;
            double startAzimuth = 0.0, width = 0.0;
            bool useInverse = false;
            double inv[2][2] {};
        };

        struct Triangle
        {
            int v[3] {};
            double inv[3][3] {};
        };

        bool buildTriangulation();
        void buildPairs();
        void computePairGains (Vec3 direction, double* vertexGains) const noexcept;
        void computeTriangleGains (Vec3 direction, double* vertexGains) const noexcept;

        std::vector<Vec3> vertexDirs; // real speakers, then virtual nadir/zenith
        std::vector<Pair> pairs;
        std::vector<Triangle> triangles;
        std::vector<std::vector<int>> virtualNeighbours; // index: vertex - numSpeakers
    };

    //==============================================================================
    /** Nearest: weights 1 / (angle + 1 deg)^2 over the k nearest speakers.
        k = 2 (2D) or 3 (3D), plus round(spread * (N - k)). */
    class KnnPanner final : public Panner
    {
    public:
        explicit KnnPanner (const SpeakerLayout& layout);

        void computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept override;

        int getNumNeighbours (double spread) const noexcept;
    };

    //==============================================================================
    /** Ambisonics: max-rE weighted point-source pattern of automatic order,
        decoded with AllRAD (dense virtual layout, then VBAP to the real speakers).
        Spread blends the order weights toward order 1. */
    class HoaPanner final : public Panner
    {
    public:
        explicit HoaPanner (const SpeakerLayout& layout);

        void computeGains (double azimuthDeg, double elevationDeg, double spread, float* gains) const noexcept override;

        /** Decoder output before power normalisation (used to verify decoder energy stability). */
        void computeRawGains (double azimuthDeg, double elevationDeg, double spread, double* gains) const noexcept;

        int getOrder() const noexcept { return order; }

        static constexpr int maxOrder2D = 7;
        static constexpr int maxOrder3D = 3;

        /** Number of harmonic components: (order + 1)^2 in 3D, 2 * order + 1 in 2D. */
        int getNumComponents() const noexcept { return numComponents; }

    private:
        void evaluateBasis (Vec3 direction, double* out) const noexcept;

        int order = 0;
        int numComponents = 1;
        std::array<double, maxOrder2D + 1> fullWeights {}, order1Weights {};
        std::vector<int> componentDegree;  // order n (3D) or m (2D) of each component
        std::vector<double> decode;        // numComponents rows x numSpeakers columns
    };
}
