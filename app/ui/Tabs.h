#pragma once

#include "../model/AppController.h"
#include <uikit/Knob.h>
#include <uikit/ToggleDot.h>
#include <dsp/Biquad.h>

/** A page in the right panel's tab area. refresh() runs at 30 Hz. */
class TabPage : public juce::Component
{
public:
    virtual void refresh() = 0;
    virtual int getPreferredHeight() const = 0;
};

/** Title on the left, Reset on the right (spec §9.2: every tab has a Reset button). */
class TabHeader final : public juce::Component
{
public:
    TabHeader();
    void setTitle (const juce::String& text, juce::Colour colour);
    void resized() override;

    juce::Label title;
    juce::TextButton reset { "Reset" };
    /** Optional controls placed after the title (Room tab: HD, Infinite, Air). */
    std::vector<locus::ui::ToggleDot*> extras;

private:
    juce::String fullTitle;
};

//==============================================================================
class SourceTab final : public TabPage
{
public:
    explicit SourceTab (AppController&);
    void refresh() override;
    int getPreferredHeight() const override;
    void resized() override;

private:
    AppController& controller;
    TabHeader header;
    std::vector<std::unique_ptr<locus::ui::Knob>> knobs; // orientation .. drop, see constructor
    juce::Label dropModeLabel { {}, "Drop Mode" }, panningLabel { {}, "Panning" };
    juce::ComboBox dropMode, panning;
    locus::ui::ToggleDot doppler, air;

    std::vector<std::unique_ptr<SliderBinding>> sliderBindings;
    std::vector<std::unique_ptr<ButtonBinding>> buttonBindings;
    std::vector<std::unique_ptr<ChoiceBinding>> choiceBindings;

    locus::ui::Knob* knob (int param) const;
};

//==============================================================================
class EqPlot final : public juce::Component
{
public:
    void setResponse (const locus::ShelfEqSettings& settings, bool bypassed, bool muted, juce::Colour accent);
    void paint (juce::Graphics&) override;

private:
    locus::ShelfEqSettings settings;
    bool bypassed = false, muted = false;
    juce::Colour accent;
};

/** Axis (direct sound) or Omni (room send) EQ for the selected source. */
class EqTab final : public TabPage
{
public:
    EqTab (AppController&, int firstParam, juce::Colour accent);
    void refresh() override;
    int getPreferredHeight() const override;
    void resized() override;

private:
    AppController& controller;
    const int first;
    const juce::Colour accent;
    TabHeader header;
    EqPlot plot;
    std::vector<std::unique_ptr<locus::ui::Knob>> knobs; // Low, Mid, High, Freq Low, Freq High, Gain
    locus::ui::ToggleDot bypass, mute;

    std::vector<std::unique_ptr<SliderBinding>> sliderBindings;
    std::vector<std::unique_ptr<ButtonBinding>> buttonBindings;
};

//==============================================================================
class RoomPlot final : public juce::Component
{
public:
    explicit RoomPlot (AppController& c) : controller (c) {}
    void paint (juce::Graphics&) override;

private:
    AppController& controller;
};

class RoomTab final : public TabPage
{
public:
    explicit RoomTab (AppController&);
    void refresh() override;
    int getPreferredHeight() const override;
    void resized() override;

private:
    AppController& controller;
    TabHeader header;
    locus::ui::ToggleDot hd, infinite, air;
    RoomPlot plot;
    std::vector<std::unique_ptr<locus::ui::Knob>> knobs;   // size, offset, shape, decay, low, high, fLow, fHigh, airFreq
    std::vector<std::unique_ptr<locus::ui::Knob>> mixes;   // early, cluster, reverb

    std::vector<std::unique_ptr<SliderBinding>> sliderBindings;
    std::vector<std::unique_ptr<ButtonBinding>> buttonBindings;
};

//==============================================================================
class SpeakersTab final : public TabPage
{
public:
    explicit SpeakersTab (AppController&);
    void refresh() override;
    int getPreferredHeight() const override;
    void resized() override;

private:
    AppController& controller;
    TabHeader header;
    juce::Label setupLabel { {}, "Setup" };
    juce::ComboBox setup;
    locus::ui::Knob radius;
    locus::ui::ToggleDot align;
    juce::TextButton editCustom { juce::String (juce::CharPointer_UTF8 ("Edit custom\xe2\x80\xa6")) };

    std::unique_ptr<SliderBinding> radiusBinding;
    std::unique_ptr<ButtonBinding> alignBinding;
};
