#include "SpeakerLayout.h"

#include <set>

namespace locus
{
    bool SpeakerLayout::is3D() const noexcept
    {
        for (const auto& s : speakers)
            if (std::abs (s.elevation) > 1.0e-6)
                return true;

        return false;
    }

    juce::StringArray SpeakerLayout::validate() const
    {
        juce::StringArray problems;

        if (speakers.empty() || size() > maxSpeakers)
            problems.add ("Speaker count must be 1 to " + juce::String (maxSpeakers));

        std::set<int> usedOutputs;

        for (int i = 0; i < size(); ++i)
        {
            const auto& s = speakers[(size_t) i];
            const auto prefix = "Speaker " + juce::String (i + 1) + ": ";

            if (s.label.length() > maxSpeakerLabelChars)
                problems.add (prefix + "label longer than " + juce::String (maxSpeakerLabelChars) + " characters");

            if (s.distance < minSpeakerDistance || s.distance > maxSpeakerDistance)
                problems.add (prefix + "distance must be 0.5 to 50 m");

            if (s.output < 1 || s.output > maxSpeakers)
                problems.add (prefix + "output must be 1 to " + juce::String (maxSpeakers));
            else if (! usedOutputs.insert (s.output).second)
                problems.add (prefix + "output " + juce::String (s.output) + " is already used");
        }

        return problems;
    }

    std::vector<AlignCorrection> SpeakerLayout::computeAlign() const
    {
        double maxDistance = 0.0;
        for (const auto& s : speakers)
            maxDistance = std::max (maxDistance, s.distance);

        std::vector<AlignCorrection> result;
        result.reserve (speakers.size());

        for (const auto& s : speakers)
        {
            AlignCorrection c;
            if (maxDistance > 0.0 && s.distance > 0.0)
            {
                c.delaySeconds = (maxDistance - s.distance) / speedOfSound;
                c.gainDb       = 20.0 * std::log10 (s.distance / maxDistance);
            }
            result.push_back (c);
        }

        return result;
    }

    //==============================================================================
    namespace
    {
        struct Def
        {
            const char* label;
            double azimuth, elevation;
            int output;
        };

        using Defs = std::vector<Def>;

        Defs operator+ (Defs a, const Defs& b)
        {
            a.insert (a.end(), b.begin(), b.end());
            return a;
        }

        const Defs quad   { { "L", -45, 0, 1 }, { "R", 45, 0, 2 }, { "Ls", -135, 0, 3 }, { "Rs", 135, 0, 4 } };
        const Defs five   { { "L", -30, 0, 1 }, { "R", 30, 0, 2 }, { "C", 0, 0, 3 }, { "Ls", -110, 0, 5 }, { "Rs", 110, 0, 6 } };
        const Defs seven  { { "L", -30, 0, 1 }, { "R", 30, 0, 2 }, { "C", 0, 0, 3 },
                            { "Lss", -90, 0, 5 }, { "Rss", 90, 0, 6 }, { "Lrs", -150, 0, 7 }, { "Rrs", 150, 0, 8 } };

        Defs topMiddle (int firstOutput)
        {
            return { { "Ltm", -90, 45, firstOutput }, { "Rtm", 90, 45, firstOutput + 1 } };
        }

        Defs topFrontRear (int firstOutput)
        {
            return { { "Ltf", -45, 45, firstOutput },     { "Rtf", 45, 45, firstOutput + 1 },
                     { "Ltr", -135, 45, firstOutput + 2 }, { "Rtr", 135, 45, firstOutput + 3 } };
        }

        const Defs wides { { "Lw", -60, 0, 9 }, { "Rw", 60, 0, 10 } };

        std::optional<Defs> namedPresetDefs (const juce::String& name)
        {
            if (name == "4.0")   return quad;
            if (name == "5.0")   return five;
            if (name == "7.0")   return seven;
            if (name == "5.0.2") return five + topMiddle (7);
            if (name == "5.0.4") return five + topFrontRear (7);
            if (name == "7.0.2") return seven + topMiddle (9);
            if (name == "7.0.4") return seven + topFrontRear (9);
            if (name == "9.0.4") return seven + wides + topFrontRear (11);
            if (name == "9.0.6") return seven + wides + topFrontRear (11) + topMiddle (15);
            return std::nullopt;
        }

        constexpr int minCircular = 4;

        int circularCount (const juce::String& name)
        {
            if (! name.endsWith ("ch"))
                return 0;

            const auto digits = name.dropLastCharacters (2);
            if (digits.isEmpty() || ! digits.containsOnly ("0123456789"))
                return 0;

            const auto n = digits.getIntValue();
            return (n >= minCircular && n <= maxSpeakers) ? n : 0;
        }
    }

    const std::vector<PresetInfo>& getSpeakerPresets()
    {
        static const std::vector<PresetInfo> presets = []
        {
            std::vector<PresetInfo> p;

            for (auto* n : { "4.0", "5.0", "7.0" })
                p.push_back ({ "2D", n });

            for (auto* n : { "5.0.2", "5.0.4", "7.0.2", "7.0.4", "9.0.4", "9.0.6" })
                p.push_back ({ "3D", n });

            for (int n = minCircular; n <= maxSpeakers; ++n)
                p.push_back ({ "Circular", juce::String (n) + "ch" });

            return p;
        }();

        return presets;
    }

    std::optional<SpeakerLayout> makePresetLayout (const juce::String& name, double radius)
    {
        SpeakerLayout layout;
        layout.name = name;

        if (auto defs = namedPresetDefs (name))
        {
            for (const auto& d : *defs)
                layout.speakers.push_back ({ d.label, d.azimuth, d.elevation, radius, d.output });

            return layout;
        }

        if (const auto n = circularCount (name); n > 0)
        {
            for (int i = 0; i < n; ++i)
                layout.speakers.push_back ({ juce::String (i + 1), wrapAzimuth (i * 360.0 / n), 0.0, radius, i + 1 });

            return layout;
        }

        return std::nullopt;
    }
}
