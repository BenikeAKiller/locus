#pragma once

#include <juce_core/juce_core.h>
#include <optional>

#include "layouts/SpeakerLayout.h"
#include "params/Parameters.h"

namespace locus
{
    constexpr const char* speakerFileExtension = ".locusspk";
    constexpr const char* sessionFileExtension = ".locussession";
    constexpr const char* customSetupName = "Custom";

    //==============================================================================
    /** Speaker file (spec §5.3): JSON with each speaker's label, x / y / z in metres, and output.
        { "format": "locus-speakers", "version": 1, "speakers": [ { "label", "x", "y", "z", "output" } ] } */
    juce::var speakersToJson (const SpeakerLayout&);

    /** Reads a speaker file's JSON. The result is not validated (call SpeakerLayout::validate()). */
    std::optional<SpeakerLayout> speakersFromJson (const juce::var&, juce::String& error);

    //==============================================================================
    /** Everything a session file holds (spec §10). Parameters are stored by id, so files keep
        working if parameters are added later; missing values fall back to defaults. */
    struct SessionData
    {
        juce::String setupName { "5.0" };       // a preset name or "Custom"
        SpeakerLayout customLayout;              // used when setupName is "Custom"
        double speakerRadius = defaultSpeakerRadius;
        bool align = true;

        std::array<float, RoomParam::count> room {};
        std::array<float, OutputParam::count> output {};

        struct Source
        {
            bool known = false;
            juce::String name;
            std::array<float, SourceParam::count> params {};
        };

        std::array<Source, maxSources> sources;

        /** Every parameter at its default, setup 5.0, no known sources. */
        static SessionData defaults();
    };

    juce::var sessionToJson (const SessionData&);
    std::optional<SessionData> sessionFromJson (const juce::var&, juce::String& error);

    /** File helpers: return false and set error on failure. */
    bool writeJsonFile (const juce::File&, const juce::var&, juce::String& error);
    std::optional<juce::var> readJsonFile (const juce::File&, juce::String& error);
}
