#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <params/Parameters.h>

namespace locus::ui
{
    /** Display text for a parameter value with its unit, e.g. "2.10 m", "5.66 kHz", "-12.0 dB". */
    juce::String formatValue (const ParamSpec& spec, double value);

    /** Parses typed text ("3.5", "3.5 m", "5.6k", "5660 Hz"); returns the clamped value, or
        nothing if the text holds no number. */
    std::optional<double> parseValue (const ParamSpec& spec, const juce::String& text);

    /** Slider range for a spec, skewed when the spec has a midpoint. */
    juce::NormalisableRange<double> makeRange (const ParamSpec& spec);
}
