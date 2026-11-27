#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <params/Parameters.h>

/** How a control reads and writes one parameter. set(value, gestureActive): gestureActive is
    true while the user is still dragging, so the Send plugin can wrap it in a host gesture. */
struct ParamAccess
{
    const locus::ParamSpec* spec = nullptr;
    std::function<float()> get;
    std::function<void (float, bool)> set;
};

/** Keeps a slider and a parameter in step: user edits go out through set(), refresh() pulls
    the current value in (values also change from plugins and automation). */
class SliderBinding final : private juce::Slider::Listener
{
public:
    SliderBinding (juce::Slider&, ParamAccess);
    ~SliderBinding() override;

    void refresh();

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;

    juce::Slider& slider;
    ParamAccess access;
    bool dragging = false, updating = false;
};

class ButtonBinding final
{
public:
    ButtonBinding (juce::Button&, ParamAccess);
    void refresh();

private:
    juce::Button& button;
    ParamAccess access;
};

class ChoiceBinding final
{
public:
    ChoiceBinding (juce::ComboBox&, ParamAccess);
    void refresh();

private:
    juce::ComboBox& box;
    ParamAccess access;
};
