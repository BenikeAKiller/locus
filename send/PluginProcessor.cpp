#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <uikit/Formatting.h>

namespace
{
    const juce::Identifier slotProperty ("slot");
    constexpr int timerHz = 30;
    constexpr double keepAlivePublishSeconds = 1.0;

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

LocusSendProcessor::LocusSendProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "LocusSend", createParameterLayout())
{
    for (const auto& spec : locus::getSourceParamSpecs())
    {
        parameters.push_back (state.getParameter (spec.id));
        state.addParameterListener (spec.id, this);
    }

    state.state.setProperty (slotProperty, 0, nullptr);
    startTimerHz (timerHz);
}

LocusSendProcessor::~LocusSendProcessor()
{
    stopTimer();
    for (const auto& spec : locus::getSourceParamSpecs())
        state.removeParameterListener (spec.id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout LocusSendProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : locus::getSourceParamSpecs())
    {
        const juce::ParameterID id { spec.id, 1 };

        switch (spec.type)
        {
            case locus::ParamType::continuous:
            {
                juce::NormalisableRange<float> range (spec.minValue, spec.maxValue);
                if (spec.midpoint > 0.0f)
                    range.setSkewForCentre (spec.midpoint);

                // Same text as the app ("2.10 m", "5.66 kHz") in the DAW's parameter display.
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, spec.name, range, spec.defaultValue,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([spec] (float v, int) { return locus::ui::formatValue (spec, v); })
                        .withValueFromStringFunction ([spec] (const juce::String& t) { return (float) locus::ui::parseValue (spec, t).value_or (spec.defaultValue); })));
                break;
            }

            case locus::ParamType::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, spec.name, spec.defaultValue >= 0.5f));
                break;

            case locus::ParamType::choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, spec.name, spec.choices, (int) spec.defaultValue));
                break;
        }
    }

    return layout;
}

//==============================================================================
void LocusSendProcessor::prepareToPlay (double, int) {}

bool LocusSendProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void LocusSendProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    endpoint.writeAudio (buffer.getArrayOfReadPointers(), getTotalNumInputChannels(), buffer.getNumSamples(),
                         getSampleRate(), isNonRealtime());

    // The track's audio goes to the app; the track itself outputs silence.
    buffer.clear();
}

//==============================================================================
void LocusSendProcessor::parameterChanged (const juce::String&, float)
{
    // May be called on the audio thread (automation): only flag it; the timer publishes.
    parametersDirty.store (true);
}

void LocusSendProcessor::publishParameters()
{
    float values[locus::ipc::numSyncParams];
    for (int p = 0; p < locus::ipc::numSyncParams; ++p)
        values[p] = parameters[(size_t) p]->convertFrom0to1 (parameters[(size_t) p]->getValue());

    endpoint.publishParameters (values);
    lastPublish = nowSeconds();
}

void LocusSendProcessor::timerCallback()
{
    const auto now = nowSeconds();

    endpoint.setRequestedSlot (getRequestedSlot());
    endpoint.tick (now);

    // Edits made in the app, applied with gestures so the host can record automation.
    endpoint.readAppEdits ([this] (const locus::ipc::SendEndpoint::AppEdit& e)
    {
        auto* p = parameters[(size_t) e.param];
        if (e.gestureBegins) p->beginChangeGesture();
        if (e.changed)       p->setValueNotifyingHost (p->convertTo0to1 (e.value));
        if (e.gestureEnds)   p->endChangeGesture();
    });

    if (parametersDirty.exchange (false) || endpoint.needsPublish() || now - lastPublish > keepAlivePublishSeconds)
        publishParameters();

    const auto slot = endpoint.getSlot();
    const auto name = getSourceName();
    if (slot >= 0 && (slot != publishedNameSlot || name != publishedName))
    {
        endpoint.publishName (name);
        publishedName = name;
        publishedNameSlot = slot;
    }
}

//==============================================================================
void LocusSendProcessor::setRequestedSlot (int requested)
{
    state.state.setProperty (slotProperty, juce::jlimit (0, locus::maxSources, requested), nullptr);
}

int LocusSendProcessor::getRequestedSlot() const
{
    return (int) state.state.getProperty (slotProperty, 0);
}

juce::String LocusSendProcessor::getSourceName() const
{
    {
        const juce::ScopedLock sl (nameLock);
        if (trackName.isNotEmpty())
            return trackName;
    }

    const auto slot = endpoint.getSlot();
    return slot >= 0 ? "Source " + juce::String (slot + 1) : juce::String ("Source");
}

juce::String LocusSendProcessor::getStatusText() const
{
    using Status = locus::ipc::SendEndpoint::Status;

    switch (endpoint.getStatus())
    {
        case Status::connected:      return juce::String (juce::CharPointer_UTF8 ("Connected \xc2\xb7 Slot ")) + juce::String (endpoint.getSlot() + 1);
        case Status::appNotRunning:  return "App not running";
        case Status::offline:        return "Offline render not supported";
        case Status::noSharedMemory: return "Cannot connect to the app";
        case Status::slotInUse:      break;
    }

    const auto requested = getRequestedSlot();
    return requested > 0 ? "Slot " + juce::String (requested) + " in use" : juce::String ("All slots in use");
}

void LocusSendProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const juce::ScopedLock sl (nameLock);
    trackName = properties.name.value_or (juce::String());
}

//==============================================================================
void LocusSendProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LocusSendProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));

    parametersDirty.store (true);
}

juce::AudioProcessorEditor* LocusSendProcessor::createEditor()
{
    return new LocusSendEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LocusSendProcessor();
}
