#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ipc/SendEndpoint.h>

/** Locus Send (spec §8): streams this track's audio to the Locus app, exposes the source's
    parameters for automation and keeps them in sync with the app. The track outputs silence. */
class LocusSendProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::Timer
{
public:
    LocusSendProcessor();
    ~LocusSendProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void updateTrackProperties (const TrackProperties& properties) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    /** 0 = Auto, 1..16. */
    void setRequestedSlot (int requested);
    int getRequestedSlot() const;

    /** Status line text for the editor (spec §9.5). */
    juce::String getStatusText() const;
    juce::String getSourceName() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void publishParameters();

    juce::AudioProcessorValueTreeState state;
    std::vector<juce::RangedAudioParameter*> parameters; // in locus::SourceParam order
    locus::ipc::SendEndpoint endpoint;

    std::atomic<bool> parametersDirty { true };
    double lastPublish = 0.0;

    juce::CriticalSection nameLock;
    juce::String trackName, publishedName;
    int publishedNameSlot = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocusSendProcessor)
};
