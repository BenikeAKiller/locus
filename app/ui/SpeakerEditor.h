#pragma once

#include "../model/AppController.h"
#include <uikit/ToggleDot.h>

/** Speaker Editor (spec §5.3), shown in place of the canvas while editing the Custom setup:
    Add / Remove / Start from preset / Load / Save, a table of every speaker, and Top and Front
    plots where speakers can be dragged. Valid edits apply live; an invalid setup is shown with
    the offending cells highlighted and is not applied until fixed. */
class SpeakerEditor final : public juce::Component
{
public:
    explicit SpeakerEditor (AppController&);
    ~SpeakerEditor() override;

    /** Loads the controller's custom setup into the editor. Call when the editor is shown. */
    void open();

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Replaces the setup being edited (Start from preset, Load, tests). */
    void setWorkingLayout (const locus::SpeakerLayout&);
    const locus::SpeakerLayout& getWorkingLayout() const noexcept { return working; }

    static constexpr int numColumns = 11;
    static const int columnWidths[numColumns];

private:
    class Plot;
    struct Row;

    void workingChanged (bool apply);
    void rebuildRows();
    void updateRows();
    void selectRow (int index);
    void editCell (int row, int column, const juce::String& text);
    void addSpeaker();
    void removeSpeaker();
    void loadFile();
    void saveFile();

    AppController& controller;
    locus::SpeakerLayout working;
    int selectedRow = 0;

    juce::TextButton add { "Add" }, remove { "Remove" };
    juce::TextButton load { juce::String (juce::CharPointer_UTF8 ("Load\xe2\x80\xa6")) };
    juce::TextButton save { juce::String (juce::CharPointer_UTF8 ("Save\xe2\x80\xa6")) };
    juce::ComboBox fromPreset;
    locus::ui::ToggleDot cartesian;
    juce::Label problem;
    juce::String problemText;

    std::array<juce::Label, numColumns> header;
    juce::Viewport viewport;
    juce::Component tableContent;
    std::vector<std::unique_ptr<Row>> rows;

    std::unique_ptr<Plot> topPlot, frontPlot;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerEditor)
};
