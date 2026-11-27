#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace locus;
using namespace locus::ui;

namespace
{
    constexpr int margin = Metrics::grid;
}

LocusSendEditor::LocusSendEditor (LocusSendProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    slotBox.addItem ("Auto", 1);
    for (int i = 1; i <= maxSources; ++i)
        slotBox.addItem ("Slot " + juce::String (i), i + 1);
    slotBox.setSelectedId (processor.getRequestedSlot() + 1, juce::dontSendNotification);
    slotBox.onChange = [this] { processor.setRequestedSlot (slotBox.getSelectedId() - 1); };
    addAndMakeVisible (slotBox);

    for (auto* l : { &nameLabel, &statusLabel })
    {
        l->setBorderSize ({});
        l->setMinimumHorizontalScale (1.0f);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (l);
    }
    nameLabel.setFont (Fonts::semibold (Fonts::normal));
    statusLabel.setFont (Fonts::regular (Fonts::small));

    for (const auto param : { SourceParam::azimuth, SourceParam::distance, SourceParam::elevation })
    {
        const auto& spec = getSourceParamSpecs()[(size_t) param];
        knobs.push_back (std::make_unique<Knob> (spec, Colours::accentSource));
        addAndMakeVisible (*knobs.back());
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getState(), spec.id, knobs.back()->getSlider()));
        knobs.back()->applySpecFormatting(); // the attachment installs the host's text functions
    }

    setResizable (false, false);
    setSize (width, height);
    timerCallback();
    startTimerHz (10);
}

LocusSendEditor::~LocusSendEditor()
{
    stopTimer();
    attachments.clear();
    setLookAndFeel (nullptr);
}

void LocusSendEditor::timerCallback()
{
    const auto name = processor.getSourceName();
    if (name != fullName)
    {
        fullName = name;
        resized();
    }

    const auto status = processor.getStatusText();
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, status.startsWith ("Connected") ? Colours::textDim : Colours::warn);

    if (slotBox.getSelectedId() != processor.getRequestedSlot() + 1 && ! slotBox.isPopupActive())
        slotBox.setSelectedId (processor.getRequestedSlot() + 1, juce::dontSendNotification);
}

void LocusSendEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
}

void LocusSendEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto top = area.removeFromTop (Metrics::rowHeight);
    slotBox.setBounds (top.removeFromLeft (104));
    top.removeFromLeft (margin);
    nameLabel.setText (Fonts::elide (nameLabel.getFont(), fullName, (float) top.getWidth()), juce::dontSendNotification);
    nameLabel.setBounds (top);

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (2);

    auto row = area.removeFromTop (Metrics::cellHeight)
                   .withSizeKeepingCentre (3 * Metrics::cellWidth + 2 * margin, Metrics::cellHeight);
    for (auto& k : knobs)
    {
        k->setBounds (row.removeFromLeft (Metrics::cellWidth));
        row.removeFromLeft (margin);
    }
}
