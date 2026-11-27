#pragma once

#include "Theme.h"
#include "Formatting.h"
#include "ToggleDot.h"

namespace locus::ui
{
    /** A control in a fixed-height cell (spec Â§9.1): label above (13 px), the control, and the
        value with its unit below (11 px). Rotary cells are 80 x 84 with a 44 px knob; the linear
        styles are the Gain slider and the room mix levels (horizontal).
        Drag to change, double-click to type a value, Alt-click to reset to the default. */
    class Knob : public juce::Component,
                 private juce::Slider::Listener
    {
    public:
        enum class Style { rotary, horizontal, vertical };

        Knob (const ParamSpec& spec, juce::Colour accent, const juce::String& labelText = {}, Style style = Style::rotary);
        ~Knob() override;

        /** Replaces the label above the control with an on/off dot (the room mix cells). */
        ToggleDot& addHeaderToggle (const juce::String& text);

        juce::Slider& getSlider() noexcept { return slider; }
        const ParamSpec& getSpec() const noexcept { return spec; }

        /** Re-applies the formatting after something (e.g. a host attachment) replaced the
            slider's text functions. */
        void applySpecFormatting();

        void resized() override;
        void enablementChanged() override;

        static constexpr int preferredWidth = Metrics::cellWidth;
        static constexpr int preferredHeight = Metrics::cellHeight;

    private:
        struct KnobSlider final : juce::Slider
        {
            std::function<void()> onDoubleClick;
            void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
        };

        void sliderValueChanged (juce::Slider*) override;
        void updateValueText();

        ParamSpec spec;
        juce::Label label, value;
        KnobSlider slider;
        std::unique_ptr<ToggleDot> headerToggle;
        juce::Colour accent;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
    };
}
