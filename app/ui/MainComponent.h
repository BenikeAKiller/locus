#pragma once

#include "../model/AppController.h"
#include "CanvasView.h"
#include "Chrome.h"
#include "SpeakerEditor.h"
#include "Tabs.h"
#include <uikit/LocusLookAndFeel.h>

/** The main window (spec Â§9.2): top bar, canvas on the left, sources and tabs on the right,
    output strip at the bottom. Everything refreshes at 30 Hz. */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    /** onSettingsChanged is called after the user changes anything in the Settings dialog. */
    MainComponent (AppController&, std::function<void()> onSettingsChanged = {});
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 1280, defaultHeight = 800;
    static constexpr int minWidth = 1024, minHeight = 640;

    static const juce::StringArray tabNames;
    void showTab (int index);

    /** Runs one refresh pass now (the timer does this 30 times a second). */
    void refreshAll();

private:
    void timerCallback() override { refreshAll(); }
    void openSettings();
    void loadSession();
    void saveSession();

public:
    SpeakerEditor& getSpeakerEditor() noexcept { return speakerEditor; }

private:

    locus::ui::LocusLookAndFeel lookAndFeel;
    AppController& controller;
    std::function<void()> onSettingsChanged;

    TopBar topBar;
    ViewRow viewRow;
    CanvasView canvas;
    SpeakerEditor speakerEditor;
    bool showingEditor = false;
    SourceList sources;
    TabBar tabs { tabNames };
    std::vector<std::unique_ptr<TabPage>> pages;
    OutputStrip output;

    juce::Rectangle<int> panelArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
