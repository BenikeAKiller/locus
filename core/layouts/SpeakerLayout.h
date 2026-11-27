#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

#include "LocusCore.h"
#include "dsp/Vec3.h"

namespace locus
{
    constexpr double speedOfSound         = 343.0;
    constexpr double defaultSpeakerRadius = 2.0;
    constexpr double minSpeakerRadius     = 0.5;
    constexpr double maxSpeakerRadius     = 20.0;
    constexpr double minSpeakerDistance   = 0.5;
    constexpr double maxSpeakerDistance   = 50.0;
    constexpr int    maxSpeakerLabelChars = 8;

    struct Speaker
    {
        juce::String label;
        double azimuth   = 0.0;  // degrees, 0 = front, positive = right
        double elevation = 0.0;  // degrees, +90 = up
        double distance  = defaultSpeakerRadius; // metres
        int output = 1;          // 1-based device output channel

        Vec3 getDirection() const noexcept { return directionFromAzEl (azimuth, elevation); }
        Vec3 getPosition() const noexcept  { return getDirection() * distance; }

        /** Sets azimuth, elevation and distance from a position in metres (x right, y front, z up). */
        void setPosition (Vec3 p) noexcept
        {
            distance = length (p);
            if (distance > 1.0e-9)
            {
                azimuth = azimuthOf (p);
                elevation = elevationOf (p);
            }
        }
    };

    /** Per-speaker compensation used when Align is on (spec §5.2). */
    struct AlignCorrection
    {
        double delaySeconds = 0.0;
        double gainDb       = 0.0;
    };

    struct SpeakerLayout
    {
        juce::String name;
        std::vector<Speaker> speakers;

        int size() const noexcept { return (int) speakers.size(); }

        /** True if any speaker has a non-zero elevation. */
        bool is3D() const noexcept;

        /** Human-readable problems; empty when the layout can be applied. */
        juce::StringArray validate() const;

        std::vector<AlignCorrection> computeAlign() const;
    };

    struct PresetInfo
    {
        juce::String group; // "2D", "3D" or "Circular"
        juce::String name;
    };

    /** Every built-in preset in menu order (spec §5.1). "Custom" is not included. */
    const std::vector<PresetInfo>& getSpeakerPresets();

    /** Builds a preset with all speakers at the given radius, or nothing if the name is unknown. */
    std::optional<SpeakerLayout> makePresetLayout (const juce::String& name, double radius = defaultSpeakerRadius);
}
