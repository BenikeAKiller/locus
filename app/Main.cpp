#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/MainComponent.h"

class LocusApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        juce::PropertiesFile::Options options;
        options.applicationName = "Locus";
        options.filenameSuffix = ".settings";
        options.folderName = "Locus";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (options);

        const auto deviceState = settings->getXmlValue ("audioDevice");
        AudioHostOptions hostOptions;
        hostOptions.savedDeviceState = deviceState.get();
        host = std::make_unique<AudioHost> (hostOptions);

        controller = std::make_unique<AppController> (*host);
        controller->setLinkBuffer (settings->getIntValue ("linkBuffer", 1024));
        const auto zoom = settings->getDoubleValue ("defaultZoom", 0.0);
        controller->defaultZoomSetting = zoom >= 0.5 && zoom <= 100.0 ? zoom : 0.0; // 0 = fit; guards a damaged file

        // Restore the last session (spec §10). A missing or unreadable autosave starts fresh.
        juce::String error;
        if (auto json = locus::readJsonFile (getAutosaveFile(), error))
            if (auto session = locus::sessionFromJson (*json, error))
                controller->applySession (*session);

        controller->resetZoom();

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *controller, [this] { saveSettings(); });
    }

    void shutdown() override
    {
        saveSettings();

        if (controller != nullptr)
        {
            juce::String error;
            locus::writeJsonFile (getAutosaveFile(), locus::sessionToJson (controller->captureSession()), error);
        }

        mainWindow = nullptr;
        controller = nullptr;
        host = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    /** The session saved on quit and restored on launch, in the user's app-data folder. */
    static juce::File getAutosaveFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Locus").getChildFile (juce::String ("Autosave") + locus::sessionFileExtension);
    }

    void saveSettings()
    {
        if (settings == nullptr || host == nullptr || controller == nullptr)
            return;

        if (auto xml = host->getDeviceManager().createStateXml())
            settings->setValue ("audioDevice", xml.get());

        settings->setValue ("linkBuffer", controller->getLinkBuffer());
        settings->setValue ("defaultZoom", controller->defaultZoomSetting);
        settings->saveIfNeeded();
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AppController& controller, std::function<void()> onSettingsChanged)
            : DocumentWindow (name, locus::ui::Colours::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (controller, std::move (onSettingsChanged)), true);
            setResizable (true, true);
            setResizeLimits (MainComponent::minWidth, MainComponent::minHeight, 10000, 10000);

            // Open at the default size, or smaller on small or high-scaling screens, so the whole
            // window (title bar included) is on screen. Leaves room for the native frame.
            int width = MainComponent::defaultWidth, height = MainComponent::defaultHeight;
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                width = juce::jlimit (MainComponent::minWidth, MainComponent::defaultWidth, display->userArea.getWidth() - 16);
                height = juce::jlimit (MainComponent::minHeight, MainComponent::defaultHeight, display->userArea.getHeight() - 48);
            }
            centreWithSize (width, height);
            if (getY() < 0)
                setTopLeftPosition (getX(), 0);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    locus::ui::LocusLookAndFeel lookAndFeel;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LocusApplication)
