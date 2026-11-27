#include "ToggleDot.h"

namespace locus::ui
{
    ToggleDot::ToggleDot (const juce::String& text, juce::Colour accentToUse)
        : juce::Button (text), accent (accentToUse)
    {
        setClickingTogglesState (true);
    }

    int ToggleDot::getIdealWidth() const
    {
        return Metrics::dotSize + Metrics::dotGap + (int) std::ceil (Fonts::width (Fonts::regular (Fonts::normal), getButtonText()));
    }

    void ToggleDot::paintButton (juce::Graphics& g, bool highlighted, bool)
    {
        const auto dotArea = juce::Rectangle<float> (0.0f, ((float) getHeight() - Metrics::dotSize) * 0.5f,
                                                     (float) Metrics::dotSize, (float) Metrics::dotSize).reduced (2.0f);
        if (getToggleState())
        {
            g.setColour (accent);
            g.fillEllipse (dotArea);
        }
        else
        {
            g.setColour (highlighted ? Colours::text : Colours::textDim);
            g.drawEllipse (dotArea.reduced (0.5f), 1.0f);
        }

        g.setColour (Colours::text);
        g.setFont (Fonts::regular (Fonts::normal));
        g.drawText (getButtonText(), getLocalBounds().withTrimmedLeft (Metrics::dotSize + Metrics::dotGap),
                    juce::Justification::centredLeft, false);
    }

    void ToggleDot::enablementChanged()
    {
        setAlpha (isEnabled() ? 1.0f : Colours::disabledAlpha);
        juce::Button::enablementChanged();
    }
}
