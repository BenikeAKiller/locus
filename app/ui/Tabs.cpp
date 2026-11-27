#include "Tabs.h"
#include <dsp/RoomTimes.h>

using namespace locus;
using namespace locus::ui;

namespace
{
    constexpr int gap = Metrics::grid;
    constexpr int resetWidth = 64;

    const ParamSpec& sourceSpec (int p) { return getSourceParamSpecs()[(size_t) p]; }
    const ParamSpec& roomSpec (int p)   { return getRoomParamSpecs()[(size_t) p]; }

    void placeCells (juce::Rectangle<int> row, std::initializer_list<juce::Component*> cells)
    {
        for (auto* c : cells)
        {
            c->setBounds (row.removeFromLeft (Metrics::cellWidth));
            row.removeFromLeft (gap);
        }
    }

    void styleLabel (juce::Label& l, juce::Justification j = juce::Justification::centredLeft)
    {
        l.setFont (Fonts::regular (Fonts::normal));
        l.setBorderSize ({});
        l.setMinimumHorizontalScale (1.0f);
        l.setJustificationType (j);
        l.setColour (juce::Label::textColourId, Colours::text);
    }

    /** Resets a contiguous range of parameters to their defaults through the given access. */
    template <typename AccessFn>
    void resetRange (int first, int last, AccessFn&& access)
    {
        for (int p = first; p <= last; ++p)
        {
            auto a = access (p);
            a.set (a.spec->defaultValue, false);
        }
    }
}

//==============================================================================
TabHeader::TabHeader()
{
    title.setFont (Fonts::semibold (Fonts::title));
    title.setBorderSize ({});
    title.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (title);
    addAndMakeVisible (reset);
}

void TabHeader::setTitle (const juce::String& text, juce::Colour colour)
{
    title.setColour (juce::Label::textColourId, colour);
    if (text != fullTitle)
    {
        fullTitle = text;
        resized();
    }
}

void TabHeader::resized()
{
    auto area = getLocalBounds();
    reset.setBounds (area.removeFromRight (resetWidth));
    area.removeFromRight (gap);

    int extrasWidth = 0;
    for (auto* e : extras)
        extrasWidth += 12 + e->getIdealWidth();

    const auto font = title.getFont();
    const auto titleSpace = (float) juce::jmax (0, area.getWidth() - extrasWidth);
    const auto shown = Fonts::elide (font, fullTitle, titleSpace);
    title.setText (shown, juce::dontSendNotification);
    title.setBounds (area.removeFromLeft ((int) std::ceil (Fonts::width (font, shown)) + 1));

    for (auto* e : extras)
    {
        area.removeFromLeft (12);
        e->setBounds (area.removeFromLeft (e->getIdealWidth()));
    }
}

//==============================================================================
namespace
{
    constexpr int sourceKnobParams[] {
        SourceParam::orientation, SourceParam::aperture, SourceParam::width, SourceParam::spread,
        SourceParam::azimuth, SourceParam::distance, SourceParam::elevation, SourceParam::panRev,
        SourceParam::airFreq, SourceParam::nearRadius, SourceParam::drop
    };
}

SourceTab::SourceTab (AppController& c)
    : controller (c),
      doppler ("Doppler", Colours::accentSource),
      air ("Air", Colours::accentSource)
{
    addAndMakeVisible (header);
    header.reset.onClick = [this]
    {
        resetRange (SourceParam::azimuth, SourceParam::drop, [this] (int p) { return controller.selectedSourceParam (p); });
    };

    for (const auto p : sourceKnobParams)
    {
        knobs.push_back (std::make_unique<Knob> (sourceSpec (p), Colours::accentSource));
        addAndMakeVisible (*knobs.back());
        sliderBindings.push_back (std::make_unique<SliderBinding> (knobs.back()->getSlider(), controller.selectedSourceParam (p)));
    }

    styleLabel (dropModeLabel, juce::Justification::centred);
    styleLabel (panningLabel);
    for (auto* comp : std::initializer_list<juce::Component*> { &dropModeLabel, &panningLabel, &dropMode, &panning, &doppler, &air })
        addAndMakeVisible (comp);

    choiceBindings.push_back (std::make_unique<ChoiceBinding> (dropMode, controller.selectedSourceParam (SourceParam::dropMode)));
    choiceBindings.push_back (std::make_unique<ChoiceBinding> (panning, controller.selectedSourceParam (SourceParam::panning)));
    buttonBindings.push_back (std::make_unique<ButtonBinding> (doppler, controller.selectedSourceParam (SourceParam::doppler)));
    buttonBindings.push_back (std::make_unique<ButtonBinding> (air, controller.selectedSourceParam (SourceParam::air)));
}

Knob* SourceTab::knob (int param) const
{
    for (size_t i = 0; i < std::size (sourceKnobParams); ++i)
        if (sourceKnobParams[i] == param)
            return knobs[i].get();
    return nullptr;
}

void SourceTab::refresh()
{
    const auto slot = controller.getSelectedSlot();
    const auto info = controller.getSourceInfo (slot);
    header.setTitle (info.name, Colours::source (slot));

    for (auto& b : sliderBindings) b->refresh();
    for (auto& b : buttonBindings) b->refresh();
    for (auto& b : choiceBindings) b->refresh();

    knob (SourceParam::elevation)->setEnabled (controller.getLayout().is3D()); // azimuth only for 2D setups
    knob (SourceParam::width)->setEnabled (info.numChannels != 1);           // no width for mono sources
    knob (SourceParam::airFreq)->setEnabled (controller.getSourceValue (slot, SourceParam::air) >= 0.5f);
}

int SourceTab::getPreferredHeight() const
{
    return Metrics::rowHeight + 3 * (gap + Metrics::cellHeight) + gap + Metrics::rowHeight;
}

void SourceTab::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (Metrics::rowHeight));

    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight),
                { knob (SourceParam::orientation), knob (SourceParam::aperture), knob (SourceParam::width), knob (SourceParam::spread) });

    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight),
                { knob (SourceParam::azimuth), knob (SourceParam::distance), knob (SourceParam::elevation), knob (SourceParam::panRev) });

    area.removeFromTop (gap);
    auto row3 = area.removeFromTop (Metrics::cellHeight);
    placeCells (row3, { knob (SourceParam::airFreq), knob (SourceParam::nearRadius), knob (SourceParam::drop) });
    auto dropCell = row3.withTrimmedLeft (3 * (Metrics::cellWidth + gap)).withWidth (Metrics::cellWidth);
    dropModeLabel.setBounds (dropCell.removeFromTop (Metrics::cellLabelHeight));
    dropCell.removeFromTop (4);
    dropMode.setBounds (dropCell.removeFromTop (Metrics::rowHeight));

    area.removeFromTop (gap);
    auto row4 = area.removeFromTop (Metrics::rowHeight);
    panningLabel.setBounds (row4.removeFromLeft (56));
    row4.removeFromLeft (gap);
    panning.setBounds (row4.removeFromLeft (128));
    row4.removeFromLeft (16);
    doppler.setBounds (row4.removeFromLeft (doppler.getIdealWidth()));
    row4.removeFromLeft (16);
    air.setBounds (row4.removeFromLeft (air.getIdealWidth()));
}

//==============================================================================
void EqPlot::setResponse (const ShelfEqSettings& s, bool isBypassed, bool isMuted, juce::Colour c)
{
    if (s == settings && isBypassed == bypassed && isMuted == muted && c == accent)
        return;

    settings = s;
    bypassed = isBypassed;
    muted = isMuted;
    accent = c;
    repaint();
}

void EqPlot::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRect (area);

    const auto inner = area.reduced (1.0f);
    auto xOf = [&] (double hz) { return inner.getX() + inner.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yOf = [&] (double db) { return inner.getCentreY() - (float) (juce::jlimit (-24.0, 24.0, db) / 24.0) * inner.getHeight() * 0.5f; };

    // Grid: decades and 0 / +-12 dB.
    g.setFont (Fonts::regular (Fonts::small));
    for (const auto& [hz, text] : { std::pair { 100.0, "100" }, std::pair { 1000.0, "1k" }, std::pair { 10000.0, "10k" } })
    {
        const auto x = xOf (hz);
        g.setColour (Colours::stroke);
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        g.setColour (Colours::textDim);
        g.drawText (text, juce::Rectangle<float> (x + 3.0f, inner.getY() + 2.0f, 28.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    g.setColour (Colours::stroke);
    g.drawHorizontalLine ((int) yOf (0.0), inner.getX(), inner.getRight());
    g.setColour (Colours::stroke.withAlpha (0.5f));
    g.drawHorizontalLine ((int) yOf (12.0), inner.getX(), inner.getRight());
    g.drawHorizontalLine ((int) yOf (-12.0), inner.getX(), inner.getRight());

    // Response: Low / Mid / High shelves (Gain is a separate level, not drawn).
    const auto design = DoubleShelfDesign::make (48000.0, bypassed ? ShelfEqSettings { 0.0, 0.0, 0.0, settings.freqLow, settings.freqHigh } : settings);
    juce::Path curve;
    constexpr int points = 160;
    for (int i = 0; i <= points; ++i)
    {
        const auto hz = 20.0 * std::pow (1000.0, (double) i / points);
        const auto p = juce::Point<float> (xOf (hz), yOf (design.magnitudeDb (48000.0, hz)));
        if (i == 0) curve.startNewSubPath (p);
        else        curve.lineTo (p);
    }

    auto fill = curve;
    fill.lineTo (inner.getRight(), inner.getBottom());
    fill.lineTo (inner.getX(), inner.getBottom());
    fill.closeSubPath();

    const auto alpha = muted ? Colours::disabledAlpha : 1.0f;
    g.setColour (accent.withAlpha (Colours::plotFillAlpha * alpha));
    g.fillPath (fill);
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    g.setColour (accent.withAlpha (0.6f * alpha));
    g.drawVerticalLine ((int) xOf (settings.freqLow), inner.getY(), inner.getBottom());
    g.drawVerticalLine ((int) xOf (settings.freqHigh), inner.getY(), inner.getBottom());

    g.setColour (Colours::stroke);
    g.drawRect (area, 1.0f);
}

//==============================================================================
EqTab::EqTab (AppController& c, int firstParam, juce::Colour accentColour)
    : controller (c), first (firstParam), accent (accentColour),
      bypass ("Bypass", accentColour), mute ("Mute", accentColour)
{
    addAndMakeVisible (header);
    header.reset.onClick = [this]
    {
        resetRange (first, first + 7, [this] (int p) { return controller.selectedSourceParam (p); });
    };

    addAndMakeVisible (plot);

    const char* labels[] { "Low", "Mid", "High", "Freq Low", "Freq High", "Gain" };
    for (int i = 0; i < 6; ++i)
    {
        const auto style = i == 5 ? Knob::Style::horizontal : Knob::Style::rotary;
        knobs.push_back (std::make_unique<Knob> (sourceSpec (first + i), accent, labels[i], style));
        addAndMakeVisible (*knobs.back());
        sliderBindings.push_back (std::make_unique<SliderBinding> (knobs.back()->getSlider(), controller.selectedSourceParam (first + i)));
    }

    addAndMakeVisible (bypass);
    addAndMakeVisible (mute);
    buttonBindings.push_back (std::make_unique<ButtonBinding> (bypass, controller.selectedSourceParam (first + 6)));
    buttonBindings.push_back (std::make_unique<ButtonBinding> (mute, controller.selectedSourceParam (first + 7)));
}

void EqTab::refresh()
{
    const auto slot = controller.getSelectedSlot();
    header.setTitle (controller.getSourceInfo (slot).name, Colours::source (slot));

    for (auto& b : sliderBindings) b->refresh();
    for (auto& b : buttonBindings) b->refresh();

    auto v = [&] (int offset) { return (double) controller.getSourceValue (slot, first + offset); };
    plot.setResponse ({ v (0), v (1), v (2), v (3), v (4) }, v (6) >= 0.5, v (7) >= 0.5, accent);
}

int EqTab::getPreferredHeight() const
{
    return Metrics::rowHeight + gap + 112 + 2 * (gap + Metrics::cellHeight);
}

void EqTab::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (Metrics::rowHeight));
    area.removeFromTop (gap);
    plot.setBounds (area.removeFromTop (112));

    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight), { knobs[0].get(), knobs[1].get(), knobs[2].get(), knobs[3].get() });

    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (Metrics::cellHeight);
    knobs[4]->setBounds (row2.removeFromLeft (Metrics::cellWidth));
    row2.removeFromLeft (gap);
    knobs[5]->setBounds (row2.removeFromLeft (2 * Metrics::cellWidth + gap)); // Gain slider spans two cells
    row2.removeFromLeft (16);
    row2.removeFromTop (Metrics::cellLabelHeight);
    bypass.setBounds (row2.removeFromTop (Metrics::rowHeight).withWidth (bypass.getIdealWidth()));
    row2.removeFromTop (gap);
    mute.setBounds (row2.removeFromTop (Metrics::rowHeight).withWidth (mute.getIdealWidth()));
}

//==============================================================================
void RoomPlot::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRect (area);
    const auto inner = area.reduced (4.0f, 4.0f);

    auto& r = controller.getHost().getEngine().getRoomParams();
    const auto times = computeRoomTimes (r.get (RoomParam::roomSize), r.get (RoomParam::offset),
                                         r.get (RoomParam::earlyShape) * 0.01, r.getBool (RoomParam::hd));
    const bool infinite = r.getBool (RoomParam::infinite);
    const double decay = r.get (RoomParam::decay);
    const double t60[] { decay * r.get (RoomParam::decayLow), decay, decay * r.get (RoomParam::decayHigh) };
    const auto longest = infinite ? 20.0 : *std::max_element (std::begin (t60), std::end (t60));
    const auto tMax = times.lateOnsetMs + 1000.0 * longest;

    // Square-root time axis, so the early structure stays visible next to a long tail.
    auto xOf = [&] (double ms) { return inner.getX() + inner.getWidth() * (float) std::sqrt (juce::jlimit (0.0, 1.0, ms / tMax)); };
    auto yOf = [&] (double db) { return inner.getY() + inner.getHeight() * (float) juce::jlimit (0.0, 1.0, -db / 60.0); };

    auto levelDb = [&] (int levelParam, int onParam)
    {
        return r.getBool (onParam) ? (double) r.get (levelParam) : -200.0;
    };

    // Direct sound
    g.setColour (Colours::textDim);
    g.drawLine (xOf (0.0), yOf (0.0), xOf (0.0), inner.getBottom(), 1.5f);

    // Early reflections
    const auto early = levelDb (RoomParam::earlyLevel, RoomParam::earlyOn);
    g.setColour (Colours::accentRoom);
    for (int k = 0; k < times.numEarlyTaps; ++k)
    {
        const auto db = gainToDb (times.earlyGain[(size_t) k]) + early;
        const auto x = xOf (times.earlyMs[(size_t) k]);
        g.drawLine (x, yOf (db), x, inner.getBottom(), 1.0f);
    }

    // Cluster block
    const auto cluster = levelDb (RoomParam::clusterLevel, RoomParam::clusterOn) + gainToDb (std::sqrt (RoomCalibration::clusterEnergy));
    if (cluster > -60.0)
    {
        const auto block = juce::Rectangle<float>::leftTopRightBottom (xOf (times.clusterStartMs), yOf (cluster),
                                                                      xOf (times.clusterEndMs), inner.getBottom());
        g.setColour (Colours::accentRoom.withAlpha (Colours::plotFillAlpha));
        g.fillRect (block);
        g.setColour (Colours::accentRoom);
        g.drawRect (block, 1.0f);
    }

    // Late decay: low, mid, high
    const auto start = levelDb (RoomParam::reverbLevel, RoomParam::reverbOn) - 6.0;
    if (start > -60.0)
    {
        const juce::Colour colours[] { Colours::textDim, Colours::accentRoom, Colours::accentRoom.withAlpha (0.5f) };
        for (int band = 0; band < 3; ++band)
        {
            juce::Path p;
            for (int i = 0; i <= 40; ++i)
            {
                const auto ms = times.lateOnsetMs + (tMax - times.lateOnsetMs) * i / 40.0;
                const auto db = infinite ? start : start - 60.0 * (ms - times.lateOnsetMs) / (1000.0 * t60[band]);
                const juce::Point<float> pt (xOf (ms), yOf (db));
                if (i == 0) p.startNewSubPath (pt);
                else        p.lineTo (pt);
                if (db < -60.0)
                    break;
            }
            g.setColour (colours[band]);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
    }

    g.setColour (Colours::stroke);
    g.drawRect (area, 1.0f);
}

//==============================================================================
RoomTab::RoomTab (AppController& c)
    : controller (c),
      hd ("HD", Colours::accentRoom), infinite ("Infinite", Colours::accentRoom), air ("Air", Colours::accentRoom),
      plot (c)
{
    addAndMakeVisible (header);
    header.setTitle ("Room", Colours::text);
    for (auto* t : { &hd, &infinite, &air })
    {
        header.addAndMakeVisible (t);
        header.extras.push_back (t);
    }
    header.reset.onClick = [this]
    {
        resetRange (0, RoomParam::count - 1, [this] (int p) { return controller.roomParam (p); });
    };

    addAndMakeVisible (plot);

    for (const auto p : { RoomParam::roomSize, RoomParam::offset, RoomParam::earlyShape, RoomParam::decay,
                          RoomParam::decayLow, RoomParam::decayHigh, RoomParam::freqLow, RoomParam::freqHigh, RoomParam::airFreq })
    {
        knobs.push_back (std::make_unique<Knob> (roomSpec (p), Colours::accentRoom));
        addAndMakeVisible (*knobs.back());
        sliderBindings.push_back (std::make_unique<SliderBinding> (knobs.back()->getSlider(), controller.roomParam (p)));
    }

    const std::pair<int, int> mixParams[] { { RoomParam::earlyLevel, RoomParam::earlyOn },
                                            { RoomParam::clusterLevel, RoomParam::clusterOn },
                                            { RoomParam::reverbLevel, RoomParam::reverbOn } };
    for (const auto& [level, on] : mixParams)
    {
        mixes.push_back (std::make_unique<Knob> (roomSpec (level), Colours::accentRoom, juce::String(), Knob::Style::horizontal));
        auto& toggle = mixes.back()->addHeaderToggle (roomSpec (level).name);
        addAndMakeVisible (*mixes.back());
        sliderBindings.push_back (std::make_unique<SliderBinding> (mixes.back()->getSlider(), controller.roomParam (level)));
        buttonBindings.push_back (std::make_unique<ButtonBinding> (toggle, controller.roomParam (on)));
    }

    buttonBindings.push_back (std::make_unique<ButtonBinding> (hd, controller.roomParam (RoomParam::hd)));
    buttonBindings.push_back (std::make_unique<ButtonBinding> (infinite, controller.roomParam (RoomParam::infinite)));
    buttonBindings.push_back (std::make_unique<ButtonBinding> (air, controller.roomParam (RoomParam::air)));
}

void RoomTab::refresh()
{
    for (auto& b : sliderBindings) b->refresh();
    for (auto& b : buttonBindings) b->refresh();

    knobs.back()->setEnabled (controller.getHost().getEngine().getRoomParams().getBool (RoomParam::air)); // Air Freq
    plot.repaint();
}

int RoomTab::getPreferredHeight() const
{
    return Metrics::rowHeight + gap + 72 + 3 * (gap + Metrics::cellHeight);
}

void RoomTab::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (Metrics::rowHeight));
    area.removeFromTop (gap);
    plot.setBounds (area.removeFromTop (72));

    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight), { knobs[0].get(), knobs[1].get(), knobs[2].get(), knobs[3].get() });
    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight), { knobs[4].get(), knobs[5].get(), knobs[6].get(), knobs[7].get() });
    area.removeFromTop (gap);
    placeCells (area.removeFromTop (Metrics::cellHeight), { knobs[8].get(), mixes[0].get(), mixes[1].get(), mixes[2].get() });
}

//==============================================================================
SpeakersTab::SpeakersTab (AppController& c)
    : controller (c),
      radius (AppController::getSpeakerRadiusSpec(), Colours::accentSource),
      align ("Align", Colours::accentSource)
{
    addAndMakeVisible (header);
    header.setTitle ("Speakers", Colours::text);
    header.reset.onClick = [this] { controller.resetSpeakers(); };

    styleLabel (setupLabel);
    addAndMakeVisible (setupLabel);

    juce::String group;
    const auto& presets = getSpeakerPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].group != group)
        {
            group = presets[(size_t) i].group;
            setup.addSectionHeading (group);
        }
        setup.addItem (presets[(size_t) i].name, i + 1);
    }
    setup.addSectionHeading (customSetupName);
    setup.addItem (customSetupName, (int) presets.size() + 1);

    setup.onChange = [this]
    {
        const auto index = setup.getSelectedId() - 1;
        if (index >= 0 && index < (int) getSpeakerPresets().size())
            controller.setSetup (getSpeakerPresets()[(size_t) index].name);
        else if (index == (int) getSpeakerPresets().size())
            controller.setSetup (customSetupName);
    };
    addAndMakeVisible (setup);

    addAndMakeVisible (radius);
    radiusBinding = std::make_unique<SliderBinding> (radius.getSlider(), controller.speakerRadiusParam());

    addAndMakeVisible (align);
    alignBinding = std::make_unique<ButtonBinding> (align, ParamAccess { nullptr,
        [this] { return controller.getAlign() ? 1.0f : 0.0f; },
        [this] (float v, bool) { controller.setAlign (v >= 0.5f); } });

    editCustom.onClick = [this]
    {
        if (controller.isEditingSpeakers())
            controller.endSpeakerEdit();
        else
            controller.beginSpeakerEdit();
    };
    addAndMakeVisible (editCustom);
}

void SpeakersTab::refresh()
{
    const auto& presets = getSpeakerPresets();
    auto id = controller.isCustomSetup() ? (int) presets.size() + 1 : 0;
    for (int i = 0; i < (int) presets.size() && id == 0; ++i)
        if (presets[(size_t) i].name == controller.getSetupName())
            id = i + 1;

    if (setup.getSelectedId() != id && ! setup.isPopupActive())
        setup.setSelectedId (id, juce::dontSendNotification);

    radiusBinding->refresh();
    alignBinding->refresh();
    radius.setEnabled (! controller.isCustomSetup()); // Speaker Radius does not apply to Custom

    const auto buttonText = controller.isEditingSpeakers() ? juce::String ("Done")
                                                           : juce::String (juce::CharPointer_UTF8 ("Edit custom\xe2\x80\xa6"));
    if (editCustom.getButtonText() != buttonText)
        editCustom.setButtonText (buttonText);
}

int SpeakersTab::getPreferredHeight() const
{
    return Metrics::rowHeight + gap + Metrics::rowHeight + gap + Metrics::cellHeight;
}

void SpeakersTab::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (Metrics::rowHeight));

    area.removeFromTop (gap);
    auto row = area.removeFromTop (Metrics::rowHeight);
    setupLabel.setBounds (row.removeFromLeft (48));
    row.removeFromLeft (gap);
    setup.setBounds (row.removeFromLeft (176));

    area.removeFromTop (gap);
    auto cells = area.removeFromTop (Metrics::cellHeight);
    radius.setBounds (cells.removeFromLeft (Metrics::cellWidth));
    cells.removeFromLeft (16);
    cells.removeFromTop (Metrics::cellLabelHeight);
    align.setBounds (cells.removeFromTop (Metrics::rowHeight).withWidth (align.getIdealWidth()));
    cells.removeFromTop (gap);
    editCustom.setBounds (cells.removeFromTop (Metrics::rowHeight).withWidth (128));
}
