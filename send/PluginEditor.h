#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <uikit/Knob.h>
#include <uikit/LocusLookAndFeel.h>

class LocusSendProcessor;

/** Locus Send editor (spec §9.5): slot, source name, status and the Azimuth, Distance and
    Elevation knobs. 320 x 160, not resizable, same theme as the app. */
class LocusSendEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit LocusSendEditor (LocusSendProcessor&);
    ~LocusSendEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 320, height = 160;

private:
    void timerCallback() override;

    locus::ui::LocusLookAndFeel lookAndFeel;
    LocusSendProcessor& processor;

    juce::ComboBox slotBox;
    juce::Label nameLabel, statusLabel;
    juce::String fullName;
    std::vector<std::unique_ptr<locus::ui::Knob>> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocusSendEditor)
};
