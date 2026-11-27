#include "LocusLookAndFeel.h"

namespace locus::ui
{
    LocusLookAndFeel::LocusLookAndFeel()
    {
        using namespace Colours;

        setDefaultSansSerifTypeface (Fonts::regularTypeface());

        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::DocumentWindow::backgroundColourId, bg);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Label::textWhenEditingColourId, text);
        setColour (juce::Label::backgroundWhenEditingColourId, panel);
        setColour (juce::Label::outlineWhenEditingColourId, stroke);

        setColour (juce::TextButton::buttonColourId, panel);
        setColour (juce::TextButton::buttonOnColourId, stroke);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);

        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::outlineColourId, stroke);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);

        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, stroke);
        setColour (juce::PopupMenu::highlightedTextColourId, text);

        setColour (juce::Slider::trackColourId, stroke);
        setColour (juce::Slider::rotarySliderOutlineColourId, stroke);
        setColour (juce::Slider::rotarySliderFillColourId, accentSource);
        setColour (juce::Slider::thumbColourId, accentSource);
        setColour (juce::Slider::backgroundColourId, stroke);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        setColour (juce::TextEditor::backgroundColourId, panel);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, stroke);
        setColour (juce::TextEditor::highlightedTextColourId, text);
        setColour (juce::TextEditor::outlineColourId, stroke);
        setColour (juce::TextEditor::focusedOutlineColourId, textDim);
        setColour (juce::CaretComponent::caretColourId, text);

        setColour (juce::ScrollBar::thumbColourId, stroke);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::textColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, text);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::GroupComponent::outlineColourId, stroke);
        setColour (juce::GroupComponent::textColourId, textDim);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, stroke);
        setColour (juce::TooltipWindow::backgroundColourId, panel);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, stroke);
    }

    juce::Typeface::Ptr LocusLookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        return f.isBold() ? Fonts::semiboldTypeface() : Fonts::regularTypeface();
    }

    void LocusLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
    {
        // 270-degree arc track, value arc and pointer line.
        const auto size = (float) juce::jmin (width, height, Metrics::knobSize);
        const auto bounds = juce::Rectangle<float> (size, size)
                                .withCentre ({ (float) x + (float) width * 0.5f, (float) y + (float) height * 0.5f })
                                .reduced (2.0f);
        const auto radius = bounds.getWidth() * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);

        const juce::PathStrokeType arc (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
        g.setColour (Colours::stroke);
        g.strokePath (track, arc);
        g.setColour (accent);
        g.strokePath (value, arc);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.3f, angle),
                      centre.getPointOnCircumference (radius - 4.0f, angle) }, 2.0f);
    }

    void LocusLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        const auto accent = slider.findColour (juce::Slider::thumbColourId);
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        constexpr float track = 3.0f, thumb = 10.0f;

        if (style == juce::Slider::LinearVertical)
        {
            const auto cx = area.getCentreX();
            g.setColour (Colours::stroke);
            g.fillRect (juce::Rectangle<float> (track, area.getHeight()).withCentre ({ cx, area.getCentreY() }));
            g.setColour (accent);
            g.fillRect (juce::Rectangle<float> (cx - track * 0.5f, pos, track, area.getBottom() - pos));
            g.fillRect (juce::Rectangle<float> (thumb, 3.0f).withCentre ({ cx, pos }));
        }
        else
        {
            const auto cy = area.getCentreY();
            g.setColour (Colours::stroke);
            g.fillRect (juce::Rectangle<float> (area.getWidth(), track).withCentre ({ area.getCentreX(), cy }));
            g.setColour (accent);
            g.fillRect (juce::Rectangle<float> (area.getX(), cy - track * 0.5f, pos - area.getX(), track));
            g.fillRect (juce::Rectangle<float> (3.0f, thumb).withCentre ({ pos, cy }));
        }
    }

    void LocusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool highlighted, bool down)
    {
        const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = b.getToggleState() ? Colours::stroke : Colours::panel;
        if (down)             fill = Colours::stroke;
        else if (highlighted) fill = fill.brighter (0.06f);

        g.setColour (fill);
        g.fillRoundedRectangle (area, (float) Metrics::controlCorner);
        g.setColour (Colours::stroke);
        g.drawRoundedRectangle (area, (float) Metrics::controlCorner, 1.0f);
    }

    juce::Font LocusLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return Fonts::regular (Fonts::normal);
    }

    void LocusLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (buttonTextPadding, 0), juce::Justification::centred, false);
    }

    void LocusLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&)
    {
        const auto area = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area, (float) Metrics::controlCorner);
        g.setColour (Colours::stroke);
        g.drawRoundedRectangle (area, (float) Metrics::controlCorner, 1.0f);

        // Flat down-arrow
        const auto arrowArea = juce::Rectangle<float> ((float) (width - comboArrowWidth), 0.0f, (float) comboArrowWidth, (float) height);
        const auto c = arrowArea.getCentre();
        juce::Path arrow;
        arrow.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
        g.setColour (Colours::textDim);
        g.fillPath (arrow);
    }

    juce::Font LocusLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return Fonts::regular (Fonts::normal);
    }

    void LocusLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (0, 0, box.getWidth() - comboArrowWidth, box.getHeight());
        label.setBorderSize ({ 0, buttonTextPadding, 0, 0 });
        label.setFont (getComboBoxFont (box));
        label.setMinimumHorizontalScale (1.0f);
    }

    juce::Font LocusLookAndFeel::getPopupMenuFont()
    {
        return Fonts::regular (Fonts::normal);
    }

    void LocusLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (Colours::panel);
        g.setColour (Colours::stroke);
        g.drawRect (0, 0, width, height, 1);
    }

    juce::Font LocusLookAndFeel::getLabelFont (juce::Label& label)
    {
        return label.getFont();
    }

    void LocusLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                          bool vertical, int thumbStart, int thumbSize, bool, bool)
    {
        const auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
                                    : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
        g.setColour (Colours::stroke);
        g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
    }

    void LocusLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
    {
        g.setColour (Colours::panel);
        g.fillRect (0, 0, width, height);
    }

    void LocusLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
    {
        g.setColour (e.hasKeyboardFocus (true) ? Colours::textDim : Colours::stroke);
        g.drawRect (0, 0, width, height, 1);
    }
}
