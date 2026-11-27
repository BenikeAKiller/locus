#include "AppController.h"

namespace
{
    constexpr int refreshHz = 30;
    constexpr double meterFallDbPerSecond = 24.0;
    constexpr double peakHoldSeconds = 1.5;

    double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

AppController::AppController (AudioHost& h)
    : host (h), engine (h.getEngine())
{
    meterDb.fill (meterFloorDb);
    holdDb.fill (meterFloorDb);
    applyLayout();
    resetZoom();
    startTimerHz (refreshHz);
}

AppController::~AppController()
{
    stopTimer();
}

//==============================================================================
AppController::SourceInfo AppController::getSourceInfo (int slot) const
{
    SourceInfo info;
    info.slot = slot;

    const auto status = host.getEndpoint().getStatus (slot);
    info.connected = status.connected;
    info.active = status.active;
    info.numChannels = status.numChannels;
    info.underruns = status.underruns;
    info.name = status.name.isNotEmpty() ? status.name : knownNames[(size_t) slot];

    if (slot == 0 && ! status.connected && host.isTestSignalActive())
    {
        info.isTestSignal = info.active = true;
        info.numChannels = 1;
        info.name = "Test signal";
    }

    if (info.name.isEmpty())
        info.name = "Source " + juce::String (slot + 1);

    return info;
}

std::vector<int> AppController::getListedSlots() const
{
    std::vector<int> slots;
    for (int s = 0; s < locus::maxSources; ++s)
        if (known[(size_t) s] || host.getEndpoint().getStatus (s).connected || (s == 0 && host.isTestSignalActive()))
            slots.push_back (s);
    return slots;
}

void AppController::addKnownSource (int slot, const juce::String& name)
{
    if (slot < 0 || slot >= locus::maxSources)
        return;

    known[(size_t) slot] = true;
    if (name.isNotEmpty())
        knownNames[(size_t) slot] = name;
}

void AppController::setSelectedSlot (int slot)
{
    selectedSlot = juce::jlimit (0, locus::maxSources - 1, slot);
}

float AppController::getSourceValue (int slot, int param) const
{
    return engine.getSourceParams (slot).get (param);
}

void AppController::setSourceValue (int slot, int param, float value, bool gestureActive)
{
    auto& store = engine.getSourceParams (slot);
    store.set (param, value);
    host.getEndpoint().sendEdit (slot, param, store.get (param), gestureActive);
}

ParamAccess AppController::selectedSourceParam (int param)
{
    return { &locus::getSourceParamSpecs()[(size_t) param],
             [this, param] { return getSourceValue (selectedSlot, param); },
             [this, param] (float v, bool gesture) { setSourceValue (selectedSlot, param, v, gesture); } };
}

ParamAccess AppController::roomParam (int param)
{
    return { &locus::getRoomParamSpecs()[(size_t) param],
             [this, param] { return engine.getRoomParams().get (param); },
             [this, param] (float v, bool) { engine.getRoomParams().set (param, v); } };
}

ParamAccess AppController::outputParam (int param)
{
    return { &locus::getOutputParamSpecs()[(size_t) param],
             [this, param] { return engine.getOutputParams().get (param); },
             [this, param] (float v, bool) { engine.getOutputParams().set (param, v); } };
}

//==============================================================================
const locus::ParamSpec& AppController::getSpeakerRadiusSpec()
{
    static const locus::ParamSpec spec = []
    {
        locus::ParamSpec s;
        s.id = "speakerRadius";
        s.name = "Radius";
        s.minValue = (float) locus::minSpeakerRadius;
        s.maxValue = (float) locus::maxSpeakerRadius;
        s.defaultValue = (float) locus::defaultSpeakerRadius;
        s.unit = "m";
        s.midpoint = 3.0f;
        return s;
    }();

    return spec;
}

ParamAccess AppController::speakerRadiusParam()
{
    return { &getSpeakerRadiusSpec(),
             [this] { return (float) speakerRadius; },
             [this] (float v, bool gesture) { setSpeakerRadius (v, ! gesture); } };
}

void AppController::setSetup (const juce::String& name)
{
    if (name == locus::customSetupName)
    {
        if (customLayout.speakers.empty()) // first time: start from the current setup
        {
            customLayout = layout;
            customLayout.name = locus::customSetupName;
        }
    }
    else if (! locus::makePresetLayout (name).has_value())
    {
        return;
    }

    setupName = name;
    applyLayout();
    resetZoom();
}

void AppController::setSpeakerRadius (double radius, bool applyNow)
{
    speakerRadius = juce::jlimit (locus::minSpeakerRadius, locus::maxSpeakerRadius, radius);

    if (isCustomSetup())
        return; // Speaker Radius does not apply to Custom (spec §5.3)

    if (auto preset = locus::makePresetLayout (setupName, speakerRadius))
        layout = *preset;

    if (applyNow)
        applyLayout();
}

bool AppController::setCustomLayout (const locus::SpeakerLayout& newLayout)
{
    if (! newLayout.validate().isEmpty())
        return false;

    customLayout = newLayout;
    customLayout.name = locus::customSetupName;

    if (isCustomSetup())
        applyLayout();

    return true;
}

void AppController::beginSpeakerEdit()
{
    if (! isCustomSetup())
        setSetup (locus::customSetupName);

    editingSpeakers = true;
}

//==============================================================================
locus::SessionData AppController::captureSession() const
{
    auto d = locus::SessionData::defaults();
    d.setupName = setupName;
    d.customLayout = customLayout;
    d.speakerRadius = speakerRadius;
    d.align = align;

    for (int p = 0; p < locus::RoomParam::count; ++p)
        d.room[(size_t) p] = engine.getRoomParams().get (p);
    for (int p = 0; p < locus::OutputParam::count; ++p)
        d.output[(size_t) p] = engine.getOutputParams().get (p);

    for (int s = 0; s < locus::maxSources; ++s)
    {
        auto& src = d.sources[(size_t) s];
        const auto status = host.getEndpoint().getStatus (s);
        src.known = known[(size_t) s] || status.connected;
        src.name = status.name.isNotEmpty() ? status.name : knownNames[(size_t) s];
        for (int p = 0; p < locus::SourceParam::count; ++p)
            src.params[(size_t) p] = engine.getSourceParams (s).get (p);
    }

    return d;
}

void AppController::applySession (const locus::SessionData& d)
{
    for (int p = 0; p < locus::RoomParam::count; ++p)
        engine.getRoomParams().set (p, d.room[(size_t) p]);
    for (int p = 0; p < locus::OutputParam::count; ++p)
        engine.getOutputParams().set (p, d.output[(size_t) p]);

    // Stored source values; a connected Send plugin's own values replace them on its next
    // publish, because the plugin is the source of truth (spec §10).
    for (int s = 0; s < locus::maxSources; ++s)
    {
        const auto& src = d.sources[(size_t) s];
        for (int p = 0; p < locus::SourceParam::count; ++p)
            engine.getSourceParams (s).set (p, src.params[(size_t) p]);

        known[(size_t) s] = src.known;
        knownNames[(size_t) s] = src.name;
    }

    speakerRadius = juce::jlimit (locus::minSpeakerRadius, locus::maxSpeakerRadius, d.speakerRadius);
    align = d.align;
    customLayout = d.customLayout;
    setupName = d.setupName == locus::customSetupName && customLayout.validate().isEmpty() ? d.setupName
              : locus::makePresetLayout (d.setupName).has_value() ? d.setupName : juce::String ("5.0");
    editingSpeakers = false;
    applyLayout();
    resetZoom();

    const auto listed = getListedSlots();
    setSelectedSlot (listed.empty() ? 0 : listed.front());
}

void AppController::setAlign (bool shouldAlign)
{
    align = shouldAlign;
    applyLayout();
}

void AppController::resetSpeakers()
{
    setupName = "5.0";
    speakerRadius = locus::defaultSpeakerRadius;
    align = true;
    editingSpeakers = false;
    applyLayout();
    resetZoom();
}

void AppController::applyLayout()
{
    if (isCustomSetup())
        layout = customLayout;
    else if (auto preset = locus::makePresetLayout (setupName, speakerRadius))
        layout = *preset;

    engine.setLayout (layout, align);
}

AppController::CanvasView AppController::getCanvasView() const noexcept
{
    return layout.is3D() ? canvasView : CanvasView::top; // Top+Front only for 3D setups
}

//==============================================================================
void AppController::setZoom (double visibleRadius) noexcept
{
    zoom = juce::jlimit (0.5, 100.0, visibleRadius);
}

double AppController::getDefaultZoom() const noexcept
{
    if (defaultZoomSetting > 0.0)
        return defaultZoomSetting;

    double furthest = 1.0;
    for (const auto& s : layout.speakers)
        furthest = std::max (furthest, s.distance);

    return furthest * 1.4;
}

float AppController::getMeterDb (int ch) const noexcept
{
    return ch >= 0 && ch < locus::maxSpeakers ? meterDb[(size_t) ch] : meterFloorDb;
}

float AppController::getMeterHoldDb (int ch) const noexcept
{
    return ch >= 0 && ch < locus::maxSpeakers ? holdDb[(size_t) ch] : meterFloorDb;
}

juce::String AppController::getDeviceSummary() const
{
    if (auto* device = host.getDeviceManager().getCurrentAudioDevice())
        return device->getName() + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
             + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz";

    return "No audio device";
}

double AppController::getCpuPercent() const
{
    return host.getDeviceManager().getCpuUsage() * 100.0;
}

void AppController::setLinkBuffer (int frames)
{
    linkBuffer = frames == 256 || frames == 512 || frames == 2048 ? frames : 1024;
    host.getEndpoint().setTargetFill (linkBuffer);
}

void AppController::timerCallback()
{
    // Remember every source that has connected, so it stays listed while idle.
    for (int s = 0; s < locus::maxSources; ++s)
    {
        const auto status = host.getEndpoint().getStatus (s);
        if (status.connected)
            addKnownSource (s, status.name);
    }

    // Meters: peak with a falling display and a 1.5 s hold.
    const auto t = now();
    const auto fall = (float) (meterFallDbPerSecond / refreshHz);

    for (int ch = 0; ch < locus::maxSpeakers; ++ch)
    {
        const auto peak = engine.getAndResetPeak (ch);
        const auto peakDb = peak > 0.0f ? juce::jmax (meterFloorDb, juce::Decibels::gainToDecibels (peak)) : meterFloorDb;

        meterDb[(size_t) ch] = juce::jmax (peakDb, meterDb[(size_t) ch] - fall);

        if (peakDb >= holdDb[(size_t) ch] || t - holdTime[(size_t) ch] > peakHoldSeconds)
        {
            holdDb[(size_t) ch] = peakDb;
            holdTime[(size_t) ch] = t;
        }
    }
}
