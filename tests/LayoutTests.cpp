#include <layouts/SpeakerLayout.h>
#include <map>
#include <set>

using namespace locus;

class LayoutTests final : public juce::UnitTest
{
public:
    LayoutTests() : juce::UnitTest ("Speaker layouts", "Locus") {}

    void runTest() override
    {
        beginTest ("Every preset builds and validates");
        for (const auto& p : getSpeakerPresets())
        {
            const auto layout = makePresetLayout (p.name, 2.0);
            expect (layout.has_value(), p.name);
            if (! layout) continue;

            const auto problems = layout->validate();
            expect (problems.isEmpty(), p.name + ": " + problems.joinIntoString ("; "));
        }

        beginTest ("Every preset has unique output channels");
        for (const auto& p : getSpeakerPresets())
        {
            const auto layout = *makePresetLayout (p.name);
            std::set<int> outputs;
            for (const auto& s : layout.speakers)
                outputs.insert (s.output);

            expectEquals ((int) outputs.size(), layout.size(), p.name);
        }

        beginTest ("Preset speaker counts and 2D/3D");
        const std::map<juce::String, std::pair<int, bool>> expected {
            { "4.0", { 4, false } },   { "5.0", { 5, false } },   { "7.0", { 7, false } },
            { "5.0.2", { 7, true } },  { "5.0.4", { 9, true } },  { "7.0.2", { 9, true } },
            { "7.0.4", { 11, true } }, { "9.0.4", { 13, true } }, { "9.0.6", { 15, true } },
        };

        for (const auto& [presetName, countAnd3D] : expected)
        {
            const auto layout = *makePresetLayout (presetName);
            expectEquals (layout.size(), countAnd3D.first, presetName);
            expect (layout.is3D() == countAnd3D.second, presetName);
        }

        for (int n = 4; n <= 32; ++n)
        {
            const auto layout = *makePresetLayout (juce::String (n) + "ch");
            expectEquals (layout.size(), n);
            expect (! layout.is3D());
        }

        expectEquals ((int) getSpeakerPresets().size(), 3 + 6 + 29);
        expect (! makePresetLayout ("Custom").has_value());
        expect (! makePresetLayout ("3ch").has_value());
        expect (! makePresetLayout ("33ch").has_value());

        beginTest ("5.0 skips the LFE channel");
        {
            const auto layout = *makePresetLayout ("5.0");
            expectEquals (layout.speakers[2].label, juce::String ("C"));
            expectEquals (layout.speakers[2].output, 3);
            expectEquals (layout.speakers[3].output, 5);
        }

        beginTest ("Circular azimuths");
        {
            const auto layout = *makePresetLayout ("8ch");
            expectWithinAbsoluteError (layout.speakers[0].azimuth, 0.0, 1.0e-9);
            expectWithinAbsoluteError (layout.speakers[1].azimuth, 45.0, 1.0e-9);
            expectWithinAbsoluteError (layout.speakers[4].azimuth, 180.0, 1.0e-9);
            expectWithinAbsoluteError (layout.speakers[7].azimuth, -45.0, 1.0e-9);
        }

        beginTest ("Speaker radius applies to presets");
        {
            const auto layout = *makePresetLayout ("7.0.4", 3.5);
            expectEquals (layout.size(), 11);
            for (const auto& s : layout.speakers)
                expectWithinAbsoluteError (s.distance, 3.5, 1.0e-12);
        }

        beginTest ("Align: equal distances need no correction");
        for (const auto& c : makePresetLayout ("5.0")->computeAlign())
        {
            expectWithinAbsoluteError (c.delaySeconds, 0.0, 1.0e-12);
            expectWithinAbsoluteError (c.gainDb, 0.0, 1.0e-12);
        }

        beginTest ("Align: hand-computed example");
        {
            SpeakerLayout layout;
            layout.speakers = { { "A", 0.0, 0.0, 2.0, 1 }, { "B", 90.0, 0.0, 4.0, 2 } };
            const auto align = layout.computeAlign();

            // A is 2 m closer: (4 - 2) / 343 = 5.8309 ms later, 20 log10(2 / 4) = -6.0206 dB quieter.
            expectWithinAbsoluteError (align[0].delaySeconds * 1000.0, 5.8309, 1.0e-4);
            expectWithinAbsoluteError (align[0].gainDb, -6.0206, 1.0e-4);
            expectWithinAbsoluteError (align[1].delaySeconds, 0.0, 1.0e-12);
            expectWithinAbsoluteError (align[1].gainDb, 0.0, 1.0e-12);
        }

        beginTest ("Validation catches bad custom layouts");
        {
            SpeakerLayout layout;
            layout.speakers = { { "Front", 0.0, 0.0, 2.0, 1 }, { "TooLongLabel", 90.0, 0.0, 2.0, 1 },
                                { "Far", 180.0, 0.0, 60.0, 3 }, { "Bad", -90.0, 0.0, 2.0, 33 } };
            const auto problems = layout.validate();
            expectEquals (problems.size(), 4, problems.joinIntoString ("; "));

            expect (! SpeakerLayout {}.validate().isEmpty());
        }
    }
};

static LayoutTests layoutTests;
