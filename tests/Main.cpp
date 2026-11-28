#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <cstdio>
#include <iostream>

namespace
{
    /** Prints the runner's log (section starts and failure messages) to stdout. */
    class ConsoleRunner final : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::printf ("%s\n", message.toRawUTF8());
        }
    };
}

void writeUiSnapshots (const juce::File& dir); // UiTests.cpp

// Runs every juce::UnitTest registered in this binary. Exit code is non-zero on any failure.
// With "--snapshots <dir>" it instead renders the app's main window to PNG files.
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui; // the UI tests build real components (never shown)

    if (argc >= 3 && juce::String (argv[1]) == "--snapshots")
    {
        writeUiSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])));
        return 0;
    }

    // "--devices [type]": lists every output device of each device type (or only of the given
    // type, e.g. ASIO) with the outputs Locus would get, opening each one briefly. Diagnostic.
    if (argc >= 2 && juce::String (argv[1]) == "--devices")
    {
        juce::AudioDeviceManager manager;
        for (auto* type : manager.getAvailableDeviceTypes())
        {
            if (argc >= 3 && type->getTypeName() != juce::String (argv[2]))
                continue;

            type->scanForDevices();
            for (const auto& name : type->getDeviceNames (false))
            {
                juce::String line = type->getTypeName() + " | " + name + " | ";
                if (std::unique_ptr<juce::AudioIODevice> device { type->createDevice (name, {}) })
                {
                    juce::BigInteger all;
                    all.setRange (0, 64, true);
                    const auto error = device->open ({}, all, 0.0, 0);
                    line += error.isNotEmpty() ? "cannot open: " + error
                                               : juce::String (device->getActiveOutputChannels().countNumberOfSetBits()) + " outputs of "
                                                   + juce::String (device->getOutputChannelNames().size()) + ", "
                                                   + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz";
                    device->close();
                }
                else
                {
                    line += "cannot create";
                }
                std::cout << line << std::endl;
            }
        }
        return 0;
    }

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int passes = 0, failures = 0;
    std::printf ("\n");
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* r = runner.getResult (i))
        {
            std::printf ("%-8s %s / %s: %d passed, %d failed\n", r->failures == 0 ? "ok" : "FAILED",
                         r->unitTestName.toRawUTF8(), r->subcategoryName.toRawUTF8(), r->passes, r->failures);
            passes += r->passes;
            failures += r->failures;
        }

    std::printf ("\n%s: %d checks passed, %d failed\n", failures == 0 ? "PASSED" : "FAILED", passes, failures);
    return failures == 0 ? 0 : 1;
}
