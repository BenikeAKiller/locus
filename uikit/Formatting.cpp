#include "Formatting.h"

namespace locus::ui
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    }

    juce::String formatValue (const ParamSpec& spec, double v)
    {
        if (spec.type == ParamType::toggle)
            return v >= 0.5 ? "On" : "Off";

        if (spec.type == ParamType::choice)
            return spec.choices[juce::jlimit (0, spec.choices.size() - 1, (int) std::lround (v))];

        const auto& u = spec.unit;

        if (u == "Hz")
            return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        if (u == utf8 ("\xc2\xb0"))      return juce::String (v, 1) + u;                 // degrees
        if (u == "m")                    return juce::String (v, 2) + " m";
        if (u == "%")                    return juce::String (v, 1) + " %";
        if (u == "dB")                   return juce::String (v, 1) + " dB";
        if (u == "s")                    return juce::String (v, 2) + " s";
        if (u == "ms")                   return juce::String (v, 1) + " ms";
        if (u == utf8 ("m\xc2\xb3"))     return juce::String (juce::roundToInt (v)) + " " + u; // cubic metres
        if (u == utf8 ("\xc3\x97"))      return juce::String (v, 2) + u;                 // multiplier

        return juce::String (v, 2);
    }

    std::optional<double> parseValue (const ParamSpec& spec, const juce::String& text)
    {
        const auto t = text.trim();
        if (t.isEmpty())
            return std::nullopt;

        if (spec.type == ParamType::choice)
        {
            const auto index = spec.choices.indexOf (t, true);
            if (index >= 0)
                return (double) index;
        }

        if (spec.type == ParamType::toggle)
        {
            if (t.equalsIgnoreCase ("on"))  return 1.0;
            if (t.equalsIgnoreCase ("off")) return 0.0;
        }

        const auto numeric = t.initialSectionContainingOnly ("+-.0123456789");
        if (numeric.isEmpty() || numeric == "-" || numeric == "+" || numeric == ".")
            return std::nullopt;

        auto v = numeric.getDoubleValue();
        if (spec.unit == "Hz" && t.substring (numeric.length()).trim().startsWithIgnoreCase ("k"))
            v *= 1000.0;

        return (double) spec.clamp ((float) v);
    }

    juce::NormalisableRange<double> makeRange (const ParamSpec& spec)
    {
        juce::NormalisableRange<double> range (spec.minValue, spec.maxValue);

        if (spec.type != ParamType::continuous)
            range.interval = 1.0;
        else if (spec.midpoint > 0.0f)
            range.setSkewForCentre (spec.midpoint);

        return range;
    }
}
