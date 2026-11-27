#include <ui/MainComponent.h>
#include <uikit/LayoutChecker.h>
#include <uikit/Formatting.h>
#include "../send/PluginProcessor.h"

using namespace locus;

class UiTests final : public juce::UnitTest
{
public:
    UiTests() : juce::UnitTest ("UI", "Locus") {}

    void runTest() override
    {
        beginTest ("Value formatting and parsing");
        {
            const auto& s = getSourceParamSpecs();
            expectEquals (ui::formatValue (s[SourceParam::distance], 2.1), juce::String ("2.10 m"));
            expectEquals (ui::formatValue (s[SourceParam::airFreq], 10000.0), juce::String ("10.00 kHz"));
            expectEquals (ui::formatValue (s[SourceParam::axisFreqLow], 177.0), juce::String ("177 Hz"));
            expectEquals (ui::formatValue (s[SourceParam::panning], 2.0), juce::String ("Ambisonics"));
            expectEquals (ui::formatValue (s[SourceParam::azimuth], -30.0), juce::String ("-30.0") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")));
            expectEquals (*ui::parseValue (s[SourceParam::airFreq], "5.6k"), 5600.0);
            expectEquals (*ui::parseValue (s[SourceParam::distance], "3.5 m"), 3.5);
            expectEquals (*ui::parseValue (s[SourceParam::distance], "99"), 50.0); // clamped
            expect (! ui::parseValue (s[SourceParam::distance], "abc").has_value());
        }

        beginTest ("Layout: nothing overlaps, nothing clips, at min and default size, every tab and view");
        {
            AudioHostOptions options;
            options.openDevice = false;
            options.sharedMemoryName = "LocusUiTest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
            AudioHost host (options);
            AppController controller (host);

            controller.addKnownSource (0, "Lead Vocal");
            controller.addKnownSource (1, "A very long track name that cannot possibly fit in the list");
            for (int s = 2; s < 12; ++s)
                controller.addKnownSource (s, "Track " + juce::String (s + 1));
            controller.setSelectedSlot (1); // long name in every tab header

            MainComponent main (controller);
            int checkedLayouts = 0, totalViolations = 0;

            for (const auto [w, h] : { std::pair { MainComponent::minWidth, MainComponent::minHeight },
                                       std::pair { MainComponent::defaultWidth, MainComponent::defaultHeight } })
                for (const auto* setup : { "5.0", "9.0.6", "32ch" })
                    for (const auto view : { AppController::CanvasView::top, AppController::CanvasView::topFront })
                        for (int tab = 0; tab < MainComponent::tabNames.size(); ++tab)
                        {
                            controller.setSetup (setup);
                            controller.setCanvasView (view);
                            main.setSize (w, h);
                            main.showTab (tab);
                            main.refreshAll();
                            main.resized();

                            const auto violations = ui::LayoutChecker::check (main);
                            ++checkedLayouts;
                            totalViolations += violations.size();

                            const auto where = juce::String (w) + "x" + juce::String (h) + " " + setup + " tab " + MainComponent::tabNames[tab];
                            for (int i = 0; i < juce::jmin (5, violations.size()); ++i)
                                logMessage ("  " + where + ": " + violations[i]);

                            expect (violations.isEmpty(), where + ": " + juce::String (violations.size()) + " violation(s)");
                        }

            logMessage ("  " + juce::String (checkedLayouts) + " layouts checked, " + juce::String (totalViolations) + " violations");
        }

        beginTest ("Custom setups and sessions in the controller");
        {
            AudioHostOptions options;
            options.openDevice = false;
            options.sharedMemoryName = "LocusUiTest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
            AudioHost host (options);
            AppController controller (host);

            controller.setSetup ("7.0.4");
            controller.beginSpeakerEdit();
            expect (controller.isEditingSpeakers());
            expect (controller.isCustomSetup());
            expectEquals (controller.getLayout().size(), 11); // starts from the current setup

            auto invalid = controller.getCustomLayout();
            invalid.speakers[1].output = invalid.speakers[0].output;
            expect (! controller.setCustomLayout (invalid));
            expectEquals (controller.getLayout().speakers[1].output, 2); // not applied

            auto moved = controller.getCustomLayout();
            moved.speakers[0].setPosition ({ -2.0, 3.0, 0.0 });
            expect (controller.setCustomLayout (moved));
            expectWithinAbsoluteError (controller.getLayout().speakers[0].getPosition().x, -2.0, 1.0e-9);

            controller.setSpeakerRadius (5.0, true); // ignored for Custom
            expectWithinAbsoluteError (controller.getLayout().speakers[0].getPosition().x, -2.0, 1.0e-9);

            controller.addKnownSource (2, "Piano");
            controller.getHost().getEngine().getSourceParams (2).set (SourceParam::distance, 7.5f);
            controller.getHost().getEngine().getRoomParams().set (RoomParam::decay, 3.3f);
            const auto session = controller.captureSession();

            AudioHostOptions options2 = options;
            options2.sharedMemoryName += "b";
            AudioHost host2 (options2);
            AppController restored (host2);
            restored.applySession (session);

            expect (restored.isCustomSetup());
            expectWithinAbsoluteError (restored.getLayout().speakers[0].getPosition().x, -2.0, 1.0e-9);
            expectEquals (restored.getSourceInfo (2).name, juce::String ("Piano"));
            expectEquals (restored.getSourceValue (2, SourceParam::distance), 7.5f);
            expectEquals (host2.getEngine().getRoomParams().get (RoomParam::decay), 3.3f);
            expect (std::find (restored.getListedSlots().begin(), restored.getListedSlots().end(), 2) != restored.getListedSlots().end());
        }

        beginTest ("Speaker Editor layout: 32 speakers and an error showing, min and default size");
        {
            AudioHostOptions options;
            options.openDevice = false;
            options.sharedMemoryName = "LocusUiTest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
            AudioHost host (options);
            AppController controller (host);
            MainComponent main (controller);

            controller.setSetup ("32ch");
            controller.beginSpeakerEdit();
            main.refreshAll();

            auto layout = controller.getCustomLayout();
            layout.speakers[5].output = 1;      // duplicate output
            layout.speakers[6].distance = 60.0; // out of range
            main.getSpeakerEditor().setWorkingLayout (layout);
            expectEquals (controller.getLayout().speakers[5].output, 6); // invalid edit not applied

            for (const auto [w, h] : { std::pair { MainComponent::minWidth, MainComponent::minHeight },
                                       std::pair { MainComponent::defaultWidth, MainComponent::defaultHeight } })
                for (int tab = 0; tab < MainComponent::tabNames.size(); ++tab)
                {
                    main.setSize (w, h);
                    main.showTab (tab);
                    main.refreshAll();
                    main.resized();

                    const auto violations = ui::LayoutChecker::check (main);
                    for (int i = 0; i < juce::jmin (5, violations.size()); ++i)
                        logMessage ("  " + juce::String (w) + "x" + juce::String (h) + ": " + violations[i]);
                    expect (violations.isEmpty(), juce::String (w) + "x" + juce::String (h) + " tab " + juce::String (tab));
                }
        }

        beginTest ("Send editor: 320 x 160, nothing overlaps or clips");
        {
            LocusSendProcessor processor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            expectEquals (editor->getWidth(), 320);
            expectEquals (editor->getHeight(), 160);

            const auto violations = ui::LayoutChecker::check (*editor);
            for (const auto& v : violations)
                logMessage ("  " + v);
            expect (violations.isEmpty());
        }

        beginTest ("Layout checker catches problems");
        {
            juce::Component parent;
            parent.setSize (100, 40);
            juce::Label tooLong ({}, "This label text is far too long");
            tooLong.setFont (ui::Fonts::regular (ui::Fonts::normal));
            tooLong.setBounds (0, 0, 60, 20);
            juce::Label overlapping ({}, "x");
            overlapping.setBounds (50, 10, 20, 20);
            juce::Label outside ({}, "y");
            outside.setBounds (90, 30, 20, 20);
            for (auto* c : { &tooLong, &overlapping, &outside })
                parent.addAndMakeVisible (c);

            const auto v = ui::LayoutChecker::check (parent);
            expect (v.size() >= 3, v.joinIntoString ("\n"));
        }
    }
};

static UiTests uiTests;

/** Makes the Locus look the default while in scope (dialogs use the default look in the app). */
struct LocusLookAndFeelScope
{
    LocusLookAndFeelScope()  { juce::LookAndFeel::setDefaultLookAndFeel (&lnf); }
    ~LocusLookAndFeelScope() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    ui::LocusLookAndFeel lnf;
};

/** Renders the main window offscreen to PNGs (every tab at min and default size, plus a 3D
    Top+Front view), for reviewing the layout without screenshots. */
void writeUiSnapshots (const juce::File& dir)
{
    dir.createDirectory();

    AudioHostOptions options;
    options.openDevice = false;
    options.sharedMemoryName = "LocusUiSnap_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    AudioHost host (options);
    AppController controller (host);

    controller.addKnownSource (0, "Lead Vocal");
    controller.addKnownSource (1, "Strings");
    controller.addKnownSource (2, "A very long track name that cannot possibly fit in the list");
    controller.getHost().getEngine().getSourceParams (1).set (SourceParam::azimuth, -60.0f);
    controller.getHost().getEngine().getSourceParams (1).set (SourceParam::distance, 2.5f);
    controller.getHost().getEngine().getSourceParams (2).set (SourceParam::azimuth, 135.0f);
    controller.setSelectedSlot (0);

    MainComponent main (controller);

    auto save = [&] (const juce::String& name)
    {
        main.refreshAll();
        main.resized();
        const auto image = main.createComponentSnapshot (main.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s.png\n", name.toRawUTF8());
    };

    for (const auto [w, h, tag] : { std::tuple { MainComponent::minWidth, MainComponent::minHeight, "min" },
                                    std::tuple { MainComponent::defaultWidth, MainComponent::defaultHeight, "default" } })
        for (int tab = 0; tab < MainComponent::tabNames.size(); ++tab)
        {
            main.setSize (w, h);
            main.showTab (tab);
            save (juce::String (tag) + "_" + MainComponent::tabNames[tab]);
        }

    {
        LocusSendProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out (dir.getChildFile ("send_editor@2x.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote send_editor@2x.png\n");
    }

    {
        LocusLookAndFeelScope scope;
        SettingsPanel settings (controller, {});
        const auto image = settings.createComponentSnapshot (settings.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile ("settings.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote settings.png\n");
    }

    controller.setSetup ("7.0.4");
    controller.setCanvasView (AppController::CanvasView::topFront);
    controller.getHost().getEngine().getSourceParams (0).set (SourceParam::elevation, 30.0f);
    main.setSize (MainComponent::defaultWidth, MainComponent::defaultHeight);
    main.showTab (0);
    save ("default_7.0.4_TopFront");

    // Speaker Editor, with one speaker dragged and one duplicate output showing the error.
    controller.beginSpeakerEdit();
    main.refreshAll();
    auto layout = controller.getCustomLayout();
    layout.speakers[0].setPosition ({ -1.2, 2.2, 0.0 });
    layout.speakers[4].output = 2;
    main.getSpeakerEditor().setWorkingLayout (layout);
    main.showTab (4);
    for (const auto [w, h, tag] : { std::tuple { MainComponent::minWidth, MainComponent::minHeight, "min" },
                                    std::tuple { MainComponent::defaultWidth, MainComponent::defaultHeight, "default" } })
    {
        main.setSize (w, h);
        save (juce::String (tag) + "_SpeakerEditor");
    }
}
