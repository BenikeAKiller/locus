#include <session/SessionFile.h>

using namespace locus;

class SessionTests final : public juce::UnitTest
{
public:
    SessionTests() : juce::UnitTest ("Session files", "Locus") {}

    void runTest() override
    {
        beginTest ("Speaker position: cartesian <-> polar");
        {
            Speaker s;
            s.setPosition ({ 1.0, 1.0, 0.0 });
            expectWithinAbsoluteError (s.azimuth, 45.0, 1.0e-9);
            expectWithinAbsoluteError (s.elevation, 0.0, 1.0e-9);
            expectWithinAbsoluteError (s.distance, std::sqrt (2.0), 1.0e-9);

            s.setPosition ({ 0.0, -2.0, 2.0 });
            expectWithinAbsoluteError (std::abs (s.azimuth), 180.0, 1.0e-9);
            expectWithinAbsoluteError (s.elevation, 45.0, 1.0e-9);

            const auto p = s.getPosition();
            expectWithinAbsoluteError (p.y, -2.0, 1.0e-9);
            expectWithinAbsoluteError (p.z, 2.0, 1.0e-9);
        }

        beginTest ("Speaker file round trip");
        {
            const auto original = *makePresetLayout ("7.0.4", 2.5);
            juce::String error;
            const auto text = juce::JSON::toString (speakersToJson (original));
            const auto loaded = speakersFromJson (juce::JSON::parse (text), error);
            expect (loaded.has_value(), error);

            expectEquals (loaded->size(), original.size());
            for (int i = 0; i < original.size(); ++i)
            {
                const auto& a = original.speakers[(size_t) i];
                const auto& b = loaded->speakers[(size_t) i];
                expectEquals (b.label, a.label);
                expectEquals (b.output, a.output);
                expectWithinAbsoluteError (length (b.getPosition() - a.getPosition()), 0.0, 0.002); // millimetres
            }
            expect (loaded->validate().isEmpty());
        }

        beginTest ("Speaker file: wrong format is rejected");
        {
            juce::String error;
            expect (! speakersFromJson (juce::JSON::parse ("{\"format\":\"something-else\"}"), error).has_value());
            expect (error.isNotEmpty());
        }

        beginTest ("Session round trip keeps every value");
        {
            auto d = SessionData::defaults();
            d.setupName = customSetupName;
            d.customLayout = *makePresetLayout ("5.0", 3.0);
            d.customLayout.speakers[0].setPosition ({ -1.5, 2.5, 0.4 });
            d.customLayout.speakers[0].label = "Front L";
            d.speakerRadius = 4.0;
            d.align = false;
            d.room[RoomParam::decay] = 4.2f;
            d.room[RoomParam::hd] = 1.0f;
            d.output[OutputParam::volume] = -6.5f;
            d.sources[3].known = true;
            d.sources[3].name = "Choir";
            d.sources[3].params[SourceParam::azimuth] = -75.0f;
            d.sources[3].params[SourceParam::panning] = 2.0f;

            juce::String error;
            const auto loaded = sessionFromJson (juce::JSON::parse (juce::JSON::toString (sessionToJson (d))), error);
            expect (loaded.has_value(), error);

            expectEquals (loaded->setupName, juce::String (customSetupName));
            expectEquals (loaded->customLayout.size(), 5);
            expectEquals (loaded->customLayout.speakers[0].label, juce::String ("Front L"));
            expectWithinAbsoluteError (loaded->customLayout.speakers[0].getPosition().z, 0.4, 0.002);
            expectWithinAbsoluteError (loaded->speakerRadius, 4.0, 1.0e-9);
            expect (! loaded->align);
            expectEquals (loaded->room[RoomParam::decay], 4.2f);
            expectEquals (loaded->room[RoomParam::hd], 1.0f);
            expectEquals (loaded->output[OutputParam::volume], -6.5f);
            expect (loaded->sources[3].known);
            expectEquals (loaded->sources[3].name, juce::String ("Choir"));
            expectEquals (loaded->sources[3].params[SourceParam::azimuth], -75.0f);
            expectEquals (loaded->sources[3].params[SourceParam::panning], 2.0f);
            expect (! loaded->sources[2].known);
        }

        beginTest ("Session: missing values fall back to defaults; bad files are rejected");
        {
            juce::String error;
            const auto sparse = sessionFromJson (juce::JSON::parse (R"({"format":"locus-session","room":{"decay":3.0}})"), error);
            expect (sparse.has_value(), error);
            expectEquals (sparse->setupName, juce::String ("5.0"));
            expectEquals (sparse->room[RoomParam::decay], 3.0f);
            expectEquals (sparse->room[RoomParam::roomSize], getRoomParamSpecs()[RoomParam::roomSize].defaultValue);
            expectEquals (sparse->output[OutputParam::limiter], 1.0f);

            const auto clamped = sessionFromJson (juce::JSON::parse (R"({"format":"locus-session","output":{"volume":99}})"), error);
            expectEquals (clamped->output[OutputParam::volume], 12.0f);

            expect (! sessionFromJson (juce::JSON::parse ("[1,2,3]"), error).has_value());

            // A custom setup with duplicate outputs is refused rather than half-applied.
            auto bad = SessionData::defaults();
            bad.setupName = customSetupName;
            bad.customLayout = *makePresetLayout ("4.0");
            bad.customLayout.speakers[1].output = 1;
            expect (! sessionFromJson (sessionToJson (bad), error).has_value());
        }

        beginTest ("Session file write and read");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("LocusTest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + sessionFileExtension);
            auto d = SessionData::defaults();
            d.setupName = "9.0.6";

            juce::String error;
            expect (writeJsonFile (file, sessionToJson (d), error), error);
            const auto json = readJsonFile (file, error);
            expect (json.has_value(), error);
            expectEquals (sessionFromJson (*json, error)->setupName, juce::String ("9.0.6"));
            file.deleteFile();

            expect (! readJsonFile (file, error).has_value());
        }
    }
};

static SessionTests sessionTests;
