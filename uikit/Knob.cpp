#include "Knob.h"

namespace locus::ui
{
    Knob::Knob (const ParamSpec& specToUse, juce::Colour accentToUse, const juce::String& labelText, Style style)
        : spec (specToUse), accent (accentToUse)
    {
        label.setText (labelText.isNotEmpty() ? labelText : spec.name, juce::dontSendNotification);
        label.setFont (Fonts::regular (Fonts::normal));
        label.setJustificationType (juce::Justification::centred);
        label.setBorderSize ({});
        label.setMinimumHorizontalScale (1.0f);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        slider.setSliderStyle (style == Style::horizontal ? juce::Slider::LinearHorizontal
                             : style == Style::vertical   ? juce::Slider::LinearVertical
                                                          : juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (200);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::thumbColourId, accent);
        slider.setNormalisableRange (makeRange (spec));
        slider.setDoubleClickReturnValue (true, spec.defaultValue, juce::ModifierKeys::altModifier); // Alt-click resets
        slider.onDoubleClick = [this] { value.showEditor(); };                                       // double-click types
        slider.addListener (this);
        addAndMakeVisible (slider);

        value.setFont (Fonts::regular (Fonts::small));
        value.setColour (juce::Label::textColourId, Colours::textDim);
        value.setJustificationType (juce::Justification::centred);
        value.setBorderSize ({});
        value.setMinimumHorizontalScale (1.0f);
        value.setEditable (false, true, false);
        value.onTextChange = [this]
        {
            if (const auto v = parseValue (spec, value.getText()))
                slider.setValue (*v, juce::sendNotificationSync);
            updateValueText();
        };
        addAndMakeVisible (value);

        applySpecFormatting();
    }

    Knob::~Knob()
    {
        slider.removeListener (this);
    }

    ToggleDot& Knob::addHeaderToggle (const juce::String& text)
    {
        headerToggle = std::make_unique<ToggleDot> (text, accent);
        addAndMakeVisible (*headerToggle);
        label.setVisible (false);
        resized();
        return *headerToggle;
    }

    void Knob::applySpecFormatting()
    {
        slider.textFromValueFunction = [this] (double v) { return formatValue (spec, v); };
        slider.valueFromTextFunction = [this] (const juce::String& t) { return parseValue (spec, t).value_or (slider.getValue()); };
        updateValueText();
    }

    void Knob::sliderValueChanged (juce::Slider*)
    {
        updateValueText();
    }

    void Knob::updateValueText()
    {
        if (! value.isBeingEdited())
            value.setText (formatValue (spec, slider.getValue()), juce::dontSendNotification);
    }

    void Knob::resized()
    {
        auto area = getLocalBounds();
        const auto top = area.removeFromTop (Metrics::cellLabelHeight);
        label.setBounds (top);
        if (headerToggle != nullptr)
            headerToggle->setBounds (top.withSizeKeepingCentre (juce::jmin (top.getWidth(), headerToggle->getIdealWidth()), top.getHeight()));
        value.setBounds (area.removeFromBottom (Metrics::cellValueHeight));
        slider.setBounds (area);
    }

    void Knob::enablementChanged()
    {
        setAlpha (isEnabled() ? 1.0f : Colours::disabledAlpha);
    }
}
