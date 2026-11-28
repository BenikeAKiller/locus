#include "Chrome.h"
#include <uikit/Formatting.h>
#include <uikit/LayoutChecker.h>

using namespace locus;
using namespace locus::ui;

namespace
{
    constexpr int gap = Metrics::grid;

    void styleLabel (juce::Label& l, float size, juce::Colour colour, bool semibold = false)
    {
        l.setFont (semibold ? Fonts::semibold (size) : Fonts::regular (size));
        l.setColour (juce::Label::textColourId, colour);
        l.setBorderSize ({});
        l.setMinimumHorizontalScale (1.0f);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
    }

    int textWidth (const juce::Label& l)
    {
        return (int) std::ceil (Fonts::width (l.getFont(), l.getText())) + 1;
    }

    void setDisabledLook (juce::Component& c, bool enabled)
    {
        c.setEnabled (enabled);
        c.setAlpha (enabled ? 1.0f : Colours::disabledAlpha);
    }
}

//==============================================================================
TopBar::TopBar (AppController& c) : controller (c)
{
    styleLabel (title, Fonts::title, Colours::text, true);
    styleLabel (sessionLabel, Fonts::normal, Colours::textDim);
    styleLabel (device, Fonts::normal, Colours::textDim);
    styleLabel (cpu, Fonts::normal, Colours::textDim);
    cpu.setJustificationType (juce::Justification::centredRight);

    for (auto* comp : std::initializer_list<juce::Component*> { &title, &sessionLabel, &load, &save, &device, &cpu, &settings })
        addAndMakeVisible (comp);

}

/** "3", "3, 5" or "3–6": a sorted list of channel numbers with runs joined. */
static juce::String formatChannelList (const juce::Array<int>& channels)
{
    juce::StringArray parts;
    for (int i = 0; i < channels.size();)
    {
        int j = i;
        while (j + 1 < channels.size() && channels[j + 1] == channels[j] + 1)
            ++j;

        parts.add (j == i ? juce::String (channels[i])
                          : juce::String (channels[i]) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (channels[j]));
        i = j + 1;
    }
    return parts.joinIntoString (", ");
}

void TopBar::refresh()
{
    // Speakers routed to outputs the device can't play would be silent: say so first, in the
    // warning colour, so it isn't elided.
    const auto missing = controller.getMissingOutputs();
    auto summary = controller.getDeviceSummary();
    if (! missing.isEmpty())
        summary = juce::String (missing.size() == 1 ? "Output " : "Outputs ") + formatChannelList (missing)
                + " not available on this device (see Settings)" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + summary;

    if (summary != deviceText)
    {
        deviceText = summary;
        device.setColour (juce::Label::textColourId, missing.isEmpty() ? Colours::textDim : Colours::warn);
        resized();
    }

    cpu.setText ("CPU " + juce::String (juce::roundToInt (controller.getCpuPercent())) + "%", juce::dontSendNotification);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (gap, (Metrics::topBarHeight - Metrics::rowHeight) / 2);

    title.setBounds (area.removeFromLeft (textWidth (title)));
    area.removeFromLeft (24);
    sessionLabel.setBounds (area.removeFromLeft (textWidth (sessionLabel)));
    area.removeFromLeft (gap);
    load.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (4);
    save.setBounds (area.removeFromLeft (64));

    settings.setBounds (area.removeFromRight (88));
    area.removeFromRight (gap);
    cpu.setBounds (area.removeFromRight (72));
    area.removeFromRight (gap);

    area.removeFromLeft (24);
    device.setText (Fonts::elide (device.getFont(), deviceText, (float) area.getWidth()), juce::dontSendNotification);
    device.setBounds (area);
}

//==============================================================================
ViewRow::ViewRow (AppController& c)
    : controller (c), labels ("Labels", Colours::accentSource), vus ("VUs", Colours::accentSource)
{
    for (auto* b : { &top, &topFront })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    top.onClick = [this] { controller.setCanvasView (AppController::CanvasView::top); };
    topFront.onClick = [this] { controller.setCanvasView (AppController::CanvasView::topFront); };

    labels.onClick = [this] { controller.showSpeakerLabels = labels.getToggleState(); };
    vus.onClick = [this] { controller.showCanvasMeters = vus.getToggleState(); };
    addAndMakeVisible (labels);
    addAndMakeVisible (vus);
}

void ViewRow::refresh()
{
    const bool split = controller.getCanvasView() == AppController::CanvasView::topFront;
    top.setToggleState (! split, juce::dontSendNotification);
    topFront.setToggleState (split, juce::dontSendNotification);
    setDisabledLook (topFront, controller.getLayout().is3D()); // only for 3D setups

    labels.setToggleState (controller.showSpeakerLabels, juce::dontSendNotification);
    vus.setToggleState (controller.showCanvasMeters, juce::dontSendNotification);
}

void ViewRow::resized()
{
    auto area = getLocalBounds();
    top.setBounds (area.removeFromLeft (64));
    topFront.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (16);
    labels.setBounds (area.removeFromLeft (labels.getIdealWidth()));
    area.removeFromLeft (16);
    vus.setBounds (area.removeFromLeft (vus.getIdealWidth()));
}

//==============================================================================
TabBar::TabBar (const juce::StringArray& names)
{
    for (int i = 0; i < names.size(); ++i)
    {
        tabs.push_back (std::make_unique<Tab> (names[i]));
        tabs.back()->onClick = [this, i]
        {
            setSelected (i);
            if (onSelect) onSelect (i);
        };
        addAndMakeVisible (*tabs.back());
    }
    setSelected (0);
}

void TabBar::setSelected (int index)
{
    selected = index;
    for (int i = 0; i < (int) tabs.size(); ++i)
        tabs[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
}

void TabBar::resized()
{
    auto area = getLocalBounds();
    const auto w = area.getWidth() / juce::jmax (1, (int) tabs.size());
    for (auto& t : tabs)
        t->setBounds (area.removeFromLeft (w));
}

void TabBar::Tab::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const bool on = getToggleState();
    g.setColour (on || highlighted ? Colours::text : Colours::textDim);
    g.setFont (Fonts::regular (Fonts::normal));
    g.drawText (getButtonText(), getLocalBounds().withTrimmedBottom (2), juce::Justification::centred, false);

    g.setColour (on ? Colours::accentSource : Colours::stroke);
    g.fillRect (getLocalBounds().removeFromBottom (on ? 2 : 1));
}

//==============================================================================
SourceList::Row::Row()
{
    styleLabel (number, Fonts::normal, Colours::textDim);
    styleLabel (name, Fonts::normal, Colours::text);
    styleLabel (status, Fonts::small, Colours::textDim);
    styleLabel (underruns, Fonts::small, Colours::warn);
    underruns.setJustificationType (juce::Justification::centredRight);
    for (auto* l : { &number, &name, &status, &underruns })
        addAndMakeVisible (l);
}

void SourceList::Row::paint (juce::Graphics& g)
{
    if (selected)
    {
        g.setColour (Colours::stroke);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) Metrics::controlCorner);
    }

    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ 10.0f, getHeight() * 0.5f }));
}

void SourceList::Row::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (20); // colour swatch
    area.removeFromLeft (4);
    number.setBounds (area.removeFromLeft (20));
    area.removeFromLeft (gap);

    area.removeFromRight (4);
    if (underruns.getText().isNotEmpty())
    {
        underruns.setBounds (area.removeFromRight (textWidth (underruns)));
        area.removeFromRight (gap);
    }
    else
    {
        underruns.setBounds ({});
    }

    status.setBounds (area.removeFromRight (44));
    area.removeFromRight (gap);

    name.setText (Fonts::elide (name.getFont(), fullName, (float) area.getWidth()), juce::dontSendNotification);
    name.setBounds (area);
}

SourceList::SourceList (AppController& c) : controller (c)
{
    styleLabel (heading, Fonts::title, Colours::text, true);
    styleLabel (empty, Fonts::normal, Colours::textDim);
    addAndMakeVisible (heading);
    addAndMakeVisible (empty);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    LayoutChecker::markOverflow (content);
    addAndMakeVisible (viewport);
}

void SourceList::refresh()
{
    const auto slots = controller.getListedSlots();
    if (slots != shownSlots)
    {
        shownSlots = slots;
        rows.clear();
        for (const auto slot : slots)
        {
            auto row = std::make_unique<Row>();
            row->slot = slot;
            row->colour = Colours::source (slot);
            row->number.setText (juce::String (slot + 1), juce::dontSendNotification);
            row->onClick = [this, slot] { controller.setSelectedSlot (slot); };
            content.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        layoutRows();
    }

    empty.setVisible (rows.empty());
    viewport.setVisible (! rows.empty());

    for (auto& row : rows)
    {
        const auto info = controller.getSourceInfo (row->slot);
        const auto statusText = info.active ? "Active" : "Idle";
        const auto underrunText = info.underruns > 0 ? juce::String (info.underruns) + (info.underruns == 1 ? " underrun" : " underruns") : juce::String();
        const bool selected = row->slot == controller.getSelectedSlot();

        bool relayout = false;
        if (info.name != row->fullName)         { row->fullName = info.name; relayout = true; }
        if (underrunText != row->underruns.getText()) { row->underruns.setText (underrunText, juce::dontSendNotification); relayout = true; }
        row->status.setText (statusText, juce::dontSendNotification);
        row->status.setColour (juce::Label::textColourId, info.active ? Colours::text : Colours::textDim);

        if (selected != row->selected)
        {
            row->selected = selected;
            row->repaint();
        }

        if (relayout)
            row->resized();
    }
}

void SourceList::layoutRows()
{
    const auto rowsHeight = (int) rows.size() * Metrics::rowHeight;
    const auto needsScroll = rowsHeight > viewport.getHeight();
    const auto width = viewport.getWidth() - (needsScroll ? viewport.getScrollBarThickness() : 0);

    content.setSize (juce::jmax (0, width), rowsHeight);
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds (0, i * Metrics::rowHeight, width, Metrics::rowHeight);
}

void SourceList::resized()
{
    auto area = getLocalBounds();
    heading.setBounds (area.removeFromTop (Metrics::rowHeight).withWidth (textWidth (heading)));
    empty.setBounds (area.removeFromTop (Metrics::rowHeight).withWidth (juce::jmin (area.getWidth(), textWidth (empty))));
    viewport.setBounds (getLocalBounds().withTrimmedTop (Metrics::rowHeight));
    layoutRows();
}

//==============================================================================
void MeterBank::paint (juce::Graphics& g)
{
    struct Entry { int channel; juce::String label; };
    std::vector<Entry> entries;
    for (const auto& s : controller.getLayout().speakers)
        entries.push_back ({ s.output, s.label });
    std::sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b) { return a.channel < b.channel; });

    if (entries.empty())
        return;

    const auto font = Fonts::regular (Fonts::small);
    const auto n = (int) entries.size();
    float numberWidth = 0.0f, labelWidth = 0.0f;
    for (const auto& e : entries)
    {
        numberWidth = juce::jmax (numberWidth, Fonts::width (font, juce::String (e.channel)));
        labelWidth = juce::jmax (labelWidth, Fonts::width (font, e.label));
    }

    // Widest layout that fits: labels and numbers, then numbers only, then bars only.
    const auto available = (float) getWidth();
    bool showLabels = true, showNumbers = true;
    auto cell = juce::jmax (numberWidth, labelWidth) + 6.0f;
    if (cell * n > available)
    {
        showLabels = false;
        cell = numberWidth + 6.0f;
    }
    if (cell * n > available)
    {
        cell = available / n;
        showNumbers = cell >= numberWidth + 2.0f;
    }

    constexpr float textHeight = 14.0f;
    const auto barTop = textHeight, barBottom = (float) getHeight() - textHeight;
    g.setFont (font);

    for (int i = 0; i < n; ++i)
    {
        const auto& e = entries[(size_t) i];
        const auto x = (float) i * cell;
        const auto centreX = x + cell * 0.5f;
        const auto barWidth = juce::jmin (6.0f, cell - 2.0f);
        const auto bar = juce::Rectangle<float> (barWidth, barBottom - barTop).withCentre ({ centreX, (barTop + barBottom) * 0.5f });

        g.setColour (Colours::stroke);
        g.fillRect (bar);

        const auto db = controller.getMeterDb (e.channel - 1);
        const auto level = juce::jlimit (0.0f, 1.0f, 1.0f - db / AppController::meterFloorDb);
        g.setColour (db > -0.5f ? Colours::warn : Colours::accentRoom);
        g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * level));

        const auto holdDb = controller.getMeterHoldDb (e.channel - 1);
        if (holdDb > AppController::meterFloorDb)
        {
            const auto y = bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, 1.0f - holdDb / AppController::meterFloorDb);
            g.setColour (Colours::text);
            g.fillRect (bar.withTop (y).withHeight (1.0f));
        }

        g.setColour (Colours::textDim);
        if (showLabels)
            g.drawText (e.label, juce::Rectangle<float> (x, 0.0f, cell, textHeight), juce::Justification::centred, false);
        if (showNumbers)
            g.drawText (juce::String (e.channel), juce::Rectangle<float> (x, barBottom, cell, textHeight), juce::Justification::centred, false);
    }
}

OutputStrip::OutputStrip (AppController& c)
    : controller (c), limiter ("Limiter", Colours::accentSource), vus ("VUs", Colours::accentSource), meters (c)
{
    const auto& volumeSpec = getOutputParamSpecs()[OutputParam::volume];

    styleLabel (volumeLabel, Fonts::normal, Colours::text);
    styleLabel (volumeValue, Fonts::small, Colours::textDim);
    volumeValue.setInterceptsMouseClicks (true, false);
    volumeValue.setEditable (false, true, false);
    volumeValue.onTextChange = [this, &volumeSpec]
    {
        if (const auto v = parseValue (volumeSpec, volumeValue.getText()))
            volume.setValue (*v, juce::sendNotificationSync);
    };

    volume.setNormalisableRange (makeRange (volumeSpec));
    volume.setDoubleClickReturnValue (true, volumeSpec.defaultValue, juce::ModifierKeys::altModifier);
    volume.setColour (juce::Slider::thumbColourId, Colours::accentSource);

    for (auto* comp : std::initializer_list<juce::Component*> { &volumeLabel, &volume, &volumeValue, &limiter, &vus, &meters })
        addAndMakeVisible (comp);

    volumeBinding = std::make_unique<SliderBinding> (volume, controller.outputParam (OutputParam::volume));
    limiterBinding = std::make_unique<ButtonBinding> (limiter, controller.outputParam (OutputParam::limiter));
    vusBinding = std::make_unique<ButtonBinding> (vus, ParamAccess { nullptr,
        [this] { return controller.showOutputMeters ? 1.0f : 0.0f; },
        [this] (float v, bool) { controller.showOutputMeters = v >= 0.5f; } });
}

void OutputStrip::refresh()
{
    volumeBinding->refresh();
    limiterBinding->refresh();
    vusBinding->refresh();

    if (! volumeValue.isBeingEdited())
        volumeValue.setText (formatValue (getOutputParamSpecs()[OutputParam::volume], volume.getValue()), juce::dontSendNotification);

    meters.setVisible (controller.showOutputMeters);
    meters.repaint();
}

void OutputStrip::resized()
{
    auto area = getLocalBounds().reduced (gap);
    auto row = area.withSizeKeepingCentre (area.getWidth(), Metrics::rowHeight);

    volumeLabel.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (gap);
    volume.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (gap);
    volumeValue.setBounds (row.removeFromLeft (64));
    row.removeFromLeft (16);
    limiter.setBounds (row.removeFromLeft (limiter.getIdealWidth()));
    row.removeFromLeft (16);
    vus.setBounds (row.removeFromLeft (vus.getIdealWidth()));
    row.removeFromLeft (24);

    meters.setBounds (area.withLeft (row.getX()));
}

//==============================================================================
SettingsPanel::SettingsPanel (AppController& c, std::function<void()> changed)
    : controller (c), onChange (std::move (changed)),
      selector (c.getHost().getDeviceManager(), 0, 0, 1, locus::maxSpeakers, false, false, false, false)
{
    addAndMakeVisible (selector);

    styleLabel (linkLabel, Fonts::normal, Colours::text);
    styleLabel (zoomLabel, Fonts::normal, Colours::text);
    addAndMakeVisible (linkLabel);
    addAndMakeVisible (zoomLabel);

    for (const auto frames : { 256, 512, 1024, 2048 })
        link.addItem (juce::String (frames) + " frames", frames);
    link.setSelectedId (controller.getLinkBuffer(), juce::dontSendNotification);
    link.onChange = [this]
    {
        controller.setLinkBuffer (link.getSelectedId());
        if (onChange) onChange();
    };
    addAndMakeVisible (link);

    zoom.addItem ("Fit speakers", 1);
    for (const auto metres : { 5, 10, 25, 50 })
        zoom.addItem (juce::String (metres) + " m", metres + 1);
    zoom.setSelectedId (controller.defaultZoomSetting > 0.0 ? (int) controller.defaultZoomSetting + 1 : 1, juce::dontSendNotification);
    zoom.onChange = [this]
    {
        controller.defaultZoomSetting = zoom.getSelectedId() > 1 ? (double) (zoom.getSelectedId() - 1) : 0.0;
        controller.resetZoom();
        if (onChange) onChange();
    };
    addAndMakeVisible (zoom);

    setSize (520, 440);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto rowZoom = area.removeFromBottom (Metrics::rowHeight);
    area.removeFromBottom (gap);
    auto rowLink = area.removeFromBottom (Metrics::rowHeight);
    area.removeFromBottom (16);
    selector.setBounds (area);

    linkLabel.setBounds (rowLink.removeFromLeft (120));
    link.setBounds (rowLink.removeFromLeft (160));
    zoomLabel.setBounds (rowZoom.removeFromLeft (120));
    zoom.setBounds (rowZoom.removeFromLeft (160));
}
