#include "Theme.h"
#include "LocusFonts.h"

namespace locus::ui
{
    juce::Colour Colours::source (int slot)
    {
        static const juce::Colour palette[16] {
            juce::Colour (0xfff2a93b), juce::Colour (0xff4fc3d9), juce::Colour (0xffd96c8a), juce::Colour (0xff7bc96f),
            juce::Colour (0xffb48ef0), juce::Colour (0xfff28cb1), juce::Colour (0xffe8d44d), juce::Colour (0xff5b9bf0),
            juce::Colour (0xfff07f4f), juce::Colour (0xff4dd0a1), juce::Colour (0xffc9a26b), juce::Colour (0xff8fa4f5),
            juce::Colour (0xffe06fd8), juce::Colour (0xff9ccc65), juce::Colour (0xfff5b971), juce::Colour (0xff6ec6ff)
        };

        return palette[juce::jlimit (0, 15, slot)];
    }

    juce::Typeface::Ptr Fonts::regularTypeface()
    {
        static const auto tf = juce::Typeface::createSystemTypefaceFor (LocusFonts::InterRegular_ttf,
                                                                         (size_t) LocusFonts::InterRegular_ttfSize);
        return tf;
    }

    juce::Typeface::Ptr Fonts::semiboldTypeface()
    {
        static const auto tf = juce::Typeface::createSystemTypefaceFor (LocusFonts::InterSemiBold_ttf,
                                                                         (size_t) LocusFonts::InterSemiBold_ttfSize);
        return tf;
    }

    juce::Font Fonts::regular (float size)
    {
        return juce::Font (juce::FontOptions (regularTypeface()).withPointHeight (size));
    }

    juce::Font Fonts::semibold (float size)
    {
        return juce::Font (juce::FontOptions (semiboldTypeface()).withPointHeight (size));
    }

    float Fonts::width (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    juce::String Fonts::elide (const juce::Font& font, const juce::String& text, float maxWidth)
    {
        if (width (font, text) <= maxWidth)
            return text;

        const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        auto t = text;
        while (t.isNotEmpty() && width (font, t.trimEnd() + ellipsis) > maxWidth)
            t = t.dropLastCharacters (1);

        return t.isEmpty() ? juce::String() : t.trimEnd() + ellipsis;
    }
}
