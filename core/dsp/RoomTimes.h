#pragma once

#include <array>
#include <cmath>

namespace locus
{
    /** Time structure of the room effect (spec §7.4), shared by the engine and the Room plot
        so the plot always shows what is rendered. Times in ms, gains linear. */
    struct RoomTimes
    {
        static constexpr int maxEarlyTaps = 16;
        static constexpr int numClusterTaps = 4;

        double scale = 1.0;  // cbrt(roomSize / 2500)
        int numEarlyTaps = 8;
        std::array<double, maxEarlyTaps> earlyMs {};
        std::array<double, maxEarlyTaps> earlyGain {};   // normalised to the early energy
        std::array<double, numClusterTaps> clusterMs {};
        double clusterStartMs = 40.0, clusterEndMs = 100.0;
        double lateOnsetMs = 100.0;
    };

    namespace RoomCalibration
    {
        constexpr double earlyStartMs = 20.0, earlyEndMs = 40.0;
        constexpr double clusterStartMs = 40.0, clusterEndMs = 100.0;
        constexpr double lateOnsetMs = 100.0;
        constexpr double earlyEnergy = 0.5;
        constexpr double clusterEnergy = 0.35;
        constexpr double earlyJitter[RoomTimes::maxEarlyTaps] { 0.13, -0.21, 0.27, -0.08, 0.19, -0.26, 0.04, -0.15,
                                                                0.22, -0.11, 0.29, -0.24, 0.09, -0.18, 0.16, -0.02 };
        constexpr double clusterPosition[RoomTimes::numClusterTaps] { 0.14, 0.36, 0.61, 0.87 };
    }

    /** earlyShape 0..1. */
    inline RoomTimes computeRoomTimes (double roomSize, double offsetMs, double earlyShape, bool hd) noexcept
    {
        using namespace RoomCalibration;

        RoomTimes t;
        t.scale = std::cbrt (roomSize / 2500.0);
        t.numEarlyTaps = hd ? 16 : 8;

        const auto earlyStart = earlyStartMs * t.scale + offsetMs;
        const auto earlySpan = (earlyEndMs - earlyStartMs) * t.scale;
        double sumSquares = 0.0;

        for (int k = 0; k < t.numEarlyTaps; ++k)
        {
            const auto position = (k + 0.5 + earlyJitter[k]) / t.numEarlyTaps;
            t.earlyMs[(size_t) k] = earlyStart + position * earlySpan;

            const auto x = (double) k / (t.numEarlyTaps - 1);
            const auto g = 1.0 + (2.0 * earlyShape - 1.0) * (2.0 * x - 1.0);
            t.earlyGain[(size_t) k] = g;
            sumSquares += g * g;
        }

        const auto norm = sumSquares > 0.0 ? std::sqrt (earlyEnergy / sumSquares) : 0.0;
        for (int k = 0; k < t.numEarlyTaps; ++k)
            t.earlyGain[(size_t) k] *= norm;

        t.clusterStartMs = clusterStartMs * t.scale + offsetMs;
        t.clusterEndMs = clusterEndMs * t.scale + offsetMs;
        for (int k = 0; k < RoomTimes::numClusterTaps; ++k)
            t.clusterMs[(size_t) k] = t.clusterStartMs + clusterPosition[k] * (t.clusterEndMs - t.clusterStartMs);

        t.lateOnsetMs = lateOnsetMs * t.scale + offsetMs;
        return t;
    }
}
