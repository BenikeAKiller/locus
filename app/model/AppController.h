#pragma once

#include "Bindings.h"
#include "../engine/AudioHost.h"
#include <session/SessionFile.h>

/** UI-facing state of the app: which source is selected, the speaker setup, canvas view
    options, meters, and access to every parameter. All methods are for the message thread.
    Components read from here on a 30 Hz refresh, since values also change from the Send
    plugins and host automation. */
class AppController final : private juce::Timer
{
public:
    explicit AppController (AudioHost&);
    ~AppController() override;

    AudioHost& getHost() noexcept { return host; }

    //==============================================================================
    struct SourceInfo
    {
        int slot = 0;
        bool connected = false; // a Send plugin holds the slot
        bool active = false;    // audio is arriving
        bool isTestSignal = false;
        juce::String name;      // never empty: falls back to "Source N"
        int numChannels = 0;
        int underruns = 0;
    };

    SourceInfo getSourceInfo (int slot) const;

    /** Slots shown in the source list: connected now, or seen earlier. Ascending. */
    std::vector<int> getListedSlots() const;
    void addKnownSource (int slot, const juce::String& name);

    int getSelectedSlot() const noexcept { return selectedSlot; }
    void setSelectedSlot (int slot);

    float getSourceValue (int slot, int param) const;
    void setSourceValue (int slot, int param, float value, bool gestureActive);

    /** Access that always follows the selected source. */
    ParamAccess selectedSourceParam (int param);
    ParamAccess roomParam (int param);
    ParamAccess outputParam (int param);

    //==============================================================================
    const locus::SpeakerLayout& getLayout() const noexcept { return layout; }
    juce::String getSetupName() const { return setupName; }
    void setSetup (const juce::String& presetName);

    double getSpeakerRadius() const noexcept { return speakerRadius; }
    /** applyNow = false while dragging: the canvas updates at once, the engine on release. */
    void setSpeakerRadius (double radius, bool applyNow);
    bool getAlign() const noexcept { return align; }
    void setAlign (bool shouldAlign);
    void resetSpeakers();

    static const locus::ParamSpec& getSpeakerRadiusSpec();
    ParamAccess speakerRadiusParam();

    /** Custom setup (spec Â§5.3). */
    bool isCustomSetup() const { return setupName == locus::customSetupName; }
    const locus::SpeakerLayout& getCustomLayout() const noexcept { return customLayout; }

    /** Stores and (if Custom is selected) applies a custom setup. Returns false, changing
        nothing, if the layout fails validation. */
    bool setCustomLayout (const locus::SpeakerLayout&);

    /** The Speaker Editor replaces the canvas while open. Opening it switches to Custom,
        starting from the current setup if there is no custom one yet. */
    bool isEditingSpeakers() const noexcept { return editingSpeakers; }
    void beginSpeakerEdit();
    void endSpeakerEdit() noexcept { editingSpeakers = false; }

    //==============================================================================
    /** Session (spec Â§10). */
    locus::SessionData captureSession() const;
    void applySession (const locus::SessionData&);

    //==============================================================================
    enum class CanvasView { top, topFront };
    CanvasView getCanvasView() const noexcept;
    void setCanvasView (CanvasView v) noexcept { canvasView = v; }

    bool showSpeakerLabels = true;
    bool showCanvasMeters = true;
    bool showOutputMeters = true;

    /** Canvas zoom as the radius in metres visible around the listener. */
    double getZoom() const noexcept { return zoom; }
    void setZoom (double visibleRadius) noexcept;
    void resetZoom() noexcept { setZoom (getDefaultZoom()); }

    /** 0 = fit the speakers, otherwise a fixed visible radius in metres. */
    double defaultZoomSetting = 0.0;
    double getDefaultZoom() const noexcept;

    //==============================================================================
    /** Smoothed peak level and 1.5 s peak hold of a device output channel, in dBFS. */
    float getMeterDb (int deviceChannel) const noexcept;
    float getMeterHoldDb (int deviceChannel) const noexcept;

    juce::String getDeviceSummary() const;

    /** 1-based outputs the speaker layout uses that the open device can't play (missing, or
        switched off in Settings), ascending. Empty when everything is playable or no device is open. */
    juce::Array<int> getMissingOutputs() const;
    double getCpuPercent() const;

    /** IPC target buffering per source in frames (Settings: 256, 512, 1024, 2048). */
    int getLinkBuffer() const noexcept { return linkBuffer; }
    void setLinkBuffer (int frames);

    static constexpr float meterFloorDb = -60.0f;

private:
    void timerCallback() override;
    void applyLayout();

    AudioHost& host;
    locus::RenderEngine& engine;

    int selectedSlot = 0;
    std::array<bool, locus::maxSources> known {};
    std::array<juce::String, locus::maxSources> knownNames;

    locus::SpeakerLayout layout, customLayout;
    bool editingSpeakers = false;
    juce::String setupName { "5.0" };
    double speakerRadius = locus::defaultSpeakerRadius;
    bool align = true;

    CanvasView canvasView = CanvasView::top;
    double zoom = 3.0;
    int linkBuffer = 1024;

    std::array<float, locus::maxSpeakers> meterDb {}, holdDb {};
    std::array<double, locus::maxSpeakers> holdTime {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppController)
};
