#pragma once

#include "Theme.h"

namespace locus::ui
{
    /** Flat look for every standard JUCE control: solid fills, 1 px strokes, 2 px corners,
        Inter everywhere, no gradients or shadows. */
    class LocusLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LocusLookAndFeel();

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

        juce::Font getLabelFont (juce::Label&) override;

        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override;
        int getDefaultScrollbarWidth() override { return 8; }

        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

        /** Horizontal padding a TextButton keeps on each side of its text. */
        static constexpr int buttonTextPadding = 8;
        /** Width ComboBox keeps for its arrow. */
        static constexpr int comboArrowWidth = 20;
    };
}
