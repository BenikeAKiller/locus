#pragma once

#include <algorithm>
#include <cmath>

namespace locus
{
    constexpr double pi = 3.14159265358979323846;

    inline double degToRad (double d) noexcept { return d * (pi / 180.0); }
    inline double radToDeg (double r) noexcept { return r * (180.0 / pi); }

    /** Wraps an azimuth in degrees to (-180, 180]. */
    inline double wrapAzimuth (double a) noexcept
    {
        a = std::fmod (a, 360.0);
        if (a <= -180.0) a += 360.0;
        if (a >   180.0) a -= 360.0;
        return a;
    }

    /** x right, y front, z up. */
    struct Vec3
    {
        double x = 0.0, y = 0.0, z = 0.0;

        Vec3 operator+ (Vec3 o) const noexcept   { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator- (Vec3 o) const noexcept   { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator- () const noexcept         { return { -x, -y, -z }; }
        Vec3 operator* (double s) const noexcept { return { x * s, y * s, z * s }; }
        Vec3 operator/ (double s) const noexcept { return { x / s, y / s, z / s }; }
    };

    inline double dot (Vec3 a, Vec3 b) noexcept   { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 cross (Vec3 a, Vec3 b) noexcept   { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline double length (Vec3 v) noexcept        { return std::sqrt (dot (v, v)); }

    inline Vec3 normalised (Vec3 v) noexcept
    {
        const auto len = length (v);
        return len > 0.0 ? v / len : Vec3 {};
    }

    /** Unit vector for an azimuth (0 = front, positive = right) and elevation (+90 = up), in degrees. */
    inline Vec3 directionFromAzEl (double azimuthDeg, double elevationDeg) noexcept
    {
        const auto az = degToRad (azimuthDeg);
        const auto el = degToRad (elevationDeg);
        return { std::cos (el) * std::sin (az), std::cos (el) * std::cos (az), std::sin (el) };
    }

    inline double azimuthOf (Vec3 v) noexcept   { return radToDeg (std::atan2 (v.x, v.y)); }
    inline double elevationOf (Vec3 v) noexcept { return radToDeg (std::atan2 (v.z, std::hypot (v.x, v.y))); }

    /** A direction `localAzimuth` degrees to the side of a source at (azimuth, elevation),
        rotated with it: pitch by elevation, then yaw by azimuth. Used for stereo width and the
        early-reflection sides, so the UI draws exactly where the engine pans. */
    inline Vec3 rotateWithSource (double localAzimuth, double azimuthDeg, double elevationDeg) noexcept
    {
        const auto v = directionFromAzEl (localAzimuth, 0.0);
        const auto e = degToRad (elevationDeg), a = degToRad (azimuthDeg);
        const Vec3 pitched { v.x, v.y * std::cos (e), v.y * std::sin (e) };
        return { pitched.x * std::cos (a) + pitched.y * std::sin (a),
                 -pitched.x * std::sin (a) + pitched.y * std::cos (a),
                 pitched.z };
    }

    /** Angle between two unit vectors, in degrees. */
    inline double angleBetween (Vec3 a, Vec3 b) noexcept
    {
        return radToDeg (std::acos (std::clamp (dot (a, b), -1.0, 1.0)));
    }
}
