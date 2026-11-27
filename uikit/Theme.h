#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Visual tokens shared by the app and the Send plugin (spec §9.1). Flat colours only:
    no gradients, shadows or blur anywhere. */
namespace locus::ui
{
    namespace Colours
    {
        inline const juce::Colour bg           { 0xff16181b };
        inline const juce::Colour panel        { 0xff1e2125 };
        inline const juce::Colour stroke       { 0xff2c3036 };
        inline const juce::Colour text         { 0xffe6e8eb };
        inline const juce::Colour textDim      { 0xff9aa1a9 };
        inline const juce::Colour accentSource { 0xfff2a93b };
        inline const juce::Colour accentRoom   { 0xff4fc3d9 };
        inline const juce::Colour warn         { 0xffe5534b };

        constexpr float disabledAlpha = 0.4f;
        constexpr float plotFillAlpha = 0.35f;

        /** Fixed flat colour per source slot (0..15). */
        juce::Colour source (int slot);
    }

    namespace Metrics
    {
        constexpr int grid = 8;
        constexpr int panelCorner = 4, controlCorner = 2;
        constexpr int knobSize = 44;
        constexpr int cellWidth = 80, cellHeight = 84;
        constexpr int cellLabelHeight = 16, cellValueHeight = 20;
        constexpr int rowHeight = 24;
        constexpr int dotSize = 16, dotGap = 6;
        constexpr int topBarHeight = 40;
        constexpr int outputStripHeight = 64;
        constexpr int panelWidth = 400;
        constexpr int tabBarHeight = 28;
        constexpr int viewRowHeight = 28;
    }

    namespace Fonts
    {
        constexpr float small = 11.0f;  // values, small labels
        constexpr float normal = 13.0f; // control labels, tabs
        constexpr float title = 15.0f;  // section titles

        juce::Typeface::Ptr regularTypeface();
        juce::Typeface::Ptr semiboldTypeface();

        juce::Font regular (float size);
        juce::Font semibold (float size);

        /** Width of text in a font, for layout and the layout checker. */
        float width (const juce::Font& font, const juce::String& text);

        /** The text, shortened with an ellipsis if needed so it fits maxWidth. */
        juce::String elide (const juce::Font& font, const juce::String& text, float maxWidth);
    }
}
