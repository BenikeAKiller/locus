#include <LocusCore.h>

class CoreSmokeTest final : public juce::UnitTest
{
public:
    CoreSmokeTest() : juce::UnitTest ("Core smoke", "Locus") {}

    void runTest() override
    {
        beginTest ("Version string is set");
        expect (locus::getVersionString().isNotEmpty());

        beginTest ("Spec limits");
        expectEquals (locus::maxSources, 16);
        expectEquals (locus::maxSpeakers, 32);
    }
};

static CoreSmokeTest coreSmokeTest;
