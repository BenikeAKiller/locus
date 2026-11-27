#include "Bindings.h"

SliderBinding::SliderBinding (juce::Slider& s, ParamAccess a)
    : slider (s), access (std::move (a))
{
    slider.addListener (this);
    refresh();
}

SliderBinding::~SliderBinding()
{
    slider.removeListener (this);
}

void SliderBinding::refresh()
{
    if (dragging)
        return;

    const juce::ScopedValueSetter<bool> svs (updating, true);
    slider.setValue (access.get(), juce::sendNotificationSync);
}

void SliderBinding::sliderValueChanged (juce::Slider*)
{
    if (! updating)
        access.set ((float) slider.getValue(), dragging);
}

void SliderBinding::sliderDragStarted (juce::Slider*)
{
    dragging = true;
}

void SliderBinding::sliderDragEnded (juce::Slider*)
{
    dragging = false;
    access.set ((float) slider.getValue(), false); // ends the gesture
}

//==============================================================================
ButtonBinding::ButtonBinding (juce::Button& b, ParamAccess a)
    : button (b), access (std::move (a))
{
    button.setClickingTogglesState (true);
    button.onClick = [this] { access.set (button.getToggleState() ? 1.0f : 0.0f, false); };
    refresh();
}

void ButtonBinding::refresh()
{
    button.setToggleState (access.get() >= 0.5f, juce::dontSendNotification);
}

//==============================================================================
ChoiceBinding::ChoiceBinding (juce::ComboBox& b, ParamAccess a)
    : box (b), access (std::move (a))
{
    box.clear (juce::dontSendNotification);
    for (int i = 0; i < access.spec->choices.size(); ++i)
        box.addItem (access.spec->choices[i], i + 1);

    box.onChange = [this] { access.set ((float) (box.getSelectedId() - 1), false); };
    refresh();
}

void ChoiceBinding::refresh()
{
    if (! box.isPopupActive())
        box.setSelectedId ((int) std::lround (access.get()) + 1, juce::dontSendNotification);
}
