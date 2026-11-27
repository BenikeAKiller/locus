#include "RenderEngine.h"
#include "RoomTimes.h"

namespace locus
{
    namespace
    {
        constexpr double paramRampSeconds = 0.020;
        constexpr double swapFadeSeconds  = 0.015; // fade out, swap, fade in: 30 ms total
        constexpr double limiterCeilingDb = -0.1;
        constexpr double limiterAttackSeconds  = 0.001;
        constexpr double limiterReleaseSeconds = 0.100;

        static_assert (RoomTimes::maxEarlyTaps == maxEarlyTaps && RoomTimes::numClusterTaps == numClusterTaps);

        constexpr double maxAlignSeconds = (maxSpeakerDistance - minSpeakerDistance) / speedOfSound + 0.001;
    }

    RenderEngine::RenderEngine()
    {
        for (auto& p : sourceParams)
            p = std::make_unique<SourceParamStore> (getSourceParamSpecs());

        for (int i = 0; i < maxSpeakers; ++i)
            bus.channels[(size_t) i] = busData[(size_t) i].data();
    }

    RenderEngine::~RenderEngine()
    {
        delete pendingLayout.exchange (nullptr);
        delete currentLayout;
        releaseRetiredLayouts();
    }

    //==============================================================================
    void RenderEngine::setLayout (const SpeakerLayout& layout, bool align)
    {
        auto state = LayoutState::create (layout, align);

        // A layout still pending was never seen by the audio thread, so it is ours to delete.
        delete pendingLayout.exchange (state.release(), std::memory_order_acq_rel);
    }

    void RenderEngine::releaseRetiredLayouts()
    {
        retiredFifo.read (retiredFifo.getNumReady()).forEach ([this] (int index)
        {
            delete retired[(size_t) index];
            retired[(size_t) index] = nullptr;
        });
    }

    float RenderEngine::getAndResetPeak (int deviceChannel) noexcept
    {
        if (deviceChannel < 0 || deviceChannel >= maxSpeakers)
            return 0.0f;

        return peaks[(size_t) deviceChannel].exchange (0.0f, std::memory_order_relaxed);
    }

    //==============================================================================
    void RenderEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;

        for (auto& s : sources)
            s.prepare (sampleRate);

        sourceRunning.fill (false);
        tailRemaining.fill (0);
        lastNumChannels.fill (1);

        for (auto* s : { &room.roomSize, &room.offset, &room.earlyShape, &room.decay, &room.decayLow, &room.decayHigh,
                         &room.freqLow, &room.freqHigh, &room.airFreq, &room.airMix,
                         &room.earlyLevel, &room.clusterLevel, &room.reverbLevel, &volume })
            s->setRampLength (sampleRate, paramRampSeconds);

        setRoomTargets();

        for (auto* s : { &room.roomSize, &room.offset, &room.earlyShape, &room.decay, &room.decayLow, &room.decayHigh,
                         &room.freqLow, &room.freqHigh, &room.airFreq, &room.airMix,
                         &room.earlyLevel, &room.clusterLevel, &room.reverbLevel, &volume })
            s->snapToTarget();

        activeHd = requestedHd;
        hdFade.setRampLength (sampleRate, swapFadeSeconds);
        hdFade.reset (1.0);
        late.prepare (sampleRate);
        late.setNumLines (activeHd ? 16 : 8);
        previousLateLevel = 0.0f;

        layoutFade.setRampLength (sampleRate, swapFadeSeconds);
        layoutFade.reset (0.0);

        limiterAttack  = std::exp (-1.0 / (limiterAttackSeconds * sampleRate));
        limiterRelease = std::exp (-1.0 / (limiterReleaseSeconds * sampleRate));
        limiterGain = 1.0;

        for (auto& l : alignLines)
            l.prepare ((int) std::ceil (maxAlignSeconds * sampleRate) + 4);

        if (currentLayout != nullptr)
            for (auto& s : sources)
                s.invalidatePanning();

        prepared = true;
    }

    void RenderEngine::setRoomTargets() noexcept
    {
        const auto& r = roomParams;

        room.roomSize.setTarget (r.get (RoomParam::roomSize));
        room.offset.setTarget (r.get (RoomParam::offset));
        room.earlyShape.setTarget (r.get (RoomParam::earlyShape) * 0.01);
        room.decay.setTarget (r.get (RoomParam::decay));
        room.decayLow.setTarget (r.get (RoomParam::decayLow));
        room.decayHigh.setTarget (r.get (RoomParam::decayHigh));
        room.freqLow.setTarget (r.get (RoomParam::freqLow));
        room.freqHigh.setTarget (r.get (RoomParam::freqHigh));
        room.airFreq.setTarget (r.get (RoomParam::airFreq));
        room.airMix.setTarget (r.getBool (RoomParam::air) ? 1.0 : 0.0);
        room.earlyLevel.setTarget (r.getBool (RoomParam::earlyOn) ? dbToGain (r.get (RoomParam::earlyLevel)) : 0.0);
        room.clusterLevel.setTarget (r.getBool (RoomParam::clusterOn) ? dbToGain (r.get (RoomParam::clusterLevel)) : 0.0);
        room.reverbLevel.setTarget (r.getBool (RoomParam::reverbOn) ? dbToGain (r.get (RoomParam::reverbLevel)) : 0.0);
        infinite = r.getBool (RoomParam::infinite);
        requestedHd = r.getBool (RoomParam::hd);

        volume.setTarget (dbToGain (outputParams.get (OutputParam::volume)));
        limiterOn = outputParams.getBool (OutputParam::limiter);
    }

    void RenderEngine::buildRoomFrame (int n, double hd) noexcept
    {
        const auto size = room.roomSize.advance (n);
        const auto offsetMs = room.offset.advance (n);
        const auto shape = room.earlyShape.advance (n);
        const auto msToSamples = sampleRate * 0.001;

        // Early taps across the early window (Early Shape tilts their gains), cluster taps
        // across the cluster window: the same numbers the Room plot shows.
        const auto times = computeRoomTimes (size, offsetMs, shape, activeHd);

        roomFrame.numEarlyTaps = times.numEarlyTaps;
        for (int k = 0; k < times.numEarlyTaps; ++k)
        {
            roomFrame.earlyDelay[(size_t) k] = times.earlyMs[(size_t) k] * msToSamples;
            roomFrame.earlyGain[(size_t) k] = (float) times.earlyGain[(size_t) k];
        }

        for (int k = 0; k < numClusterTaps; ++k)
            roomFrame.clusterDelay[(size_t) k] = times.clusterMs[(size_t) k] * msToSamples;

        roomFrame.earlyLevel = (float) (room.earlyLevel.advance (n) * hd);
        roomFrame.clusterLevel = (float) (room.clusterLevel.advance (n) * hd
                                          * std::sqrt (RoomCalibration::clusterEnergy / numClusterTaps));

        const auto scale = times.scale;
        lateSettings.roomScale = scale;
        lateSettings.offsetMs = offsetMs;
        lateSettings.decay = room.decay.advance (n);
        lateSettings.decayLow = room.decayLow.advance (n);
        lateSettings.decayHigh = room.decayHigh.advance (n);
        lateSettings.freqLow = room.freqLow.advance (n);
        lateSettings.freqHigh = room.freqHigh.advance (n);
        lateSettings.infinite = infinite;
        lateSettings.airFreq = room.airFreq.advance (n);
        lateSettings.airMix = room.airMix.advance (n);
    }

    void RenderEngine::handleLayoutSwap (int n) noexcept
    {
        juce::ignoreUnused (n);

        if (pendingLayout.load (std::memory_order_acquire) == nullptr)
        {
            layoutFade.setTarget (1.0);
            return;
        }

        if (currentLayout != nullptr && layoutFade.getCurrent() > 0.0)
        {
            layoutFade.setTarget (0.0);
            return;
        }

        // Silent now: swap. If the message thread has not collected old layouts, try again later.
        if (currentLayout != nullptr && retiredFifo.getFreeSpace() < 1)
            return;

        auto* next = pendingLayout.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr)
            return;

        if (currentLayout != nullptr)
        {
            auto* old = currentLayout;
            retiredFifo.write (1).forEach ([this, old] (int index) { retired[(size_t) index] = old; });
        }

        currentLayout = next;

        for (auto& s : sources)
            s.invalidatePanning();

        for (auto& l : alignLines)
            l.clear();

        limiterGain = 1.0;
        layoutFade.setTarget (1.0);
    }

    //==============================================================================
    void RenderEngine::process (const SourceInputs& inputs, float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        if (! prepared)
        {
            for (int ch = 0; ch < numOutputs; ++ch)
                std::fill (outputs[ch], outputs[ch] + numSamples, 0.0f);
            return;
        }

        setRoomTargets();

        const auto tailSamples = (int) (sourceTailSeconds * sampleRate);

        for (int s = 0; s < maxSources; ++s)
        {
            const auto& in = inputs[(size_t) s];

            if (in.numChannels > 0)
            {
                sources[(size_t) s].setTargets (*sourceParams[(size_t) s]);

                if (! sourceRunning[(size_t) s])
                {
                    sources[(size_t) s].restart();
                    sourceRunning[(size_t) s] = true;
                }

                tailRemaining[(size_t) s] = tailSamples;
                lastNumChannels[(size_t) s] = std::min (in.numChannels, 2);
            }
            else if (sourceRunning[(size_t) s])
            {
                sources[(size_t) s].setTargets (*sourceParams[(size_t) s]);
            }
        }

        for (int offset = 0; offset < numSamples; offset += renderChunk)
        {
            const auto n = std::min (renderChunk, numSamples - offset);

            handleLayoutSwap (n);

            if (currentLayout == nullptr)
            {
                for (int ch = 0; ch < numOutputs; ++ch)
                    std::fill (outputs[ch] + offset, outputs[ch] + offset + n, 0.0f);
                continue;
            }

            // HD change: fade the room out, switch, fade back in.
            hdFade.setTarget (requestedHd == activeHd ? 1.0 : 0.0);
            const auto hd = hdFade.advance (n);
            if (hd <= 0.0 && requestedHd != activeHd)
            {
                activeHd = requestedHd;
                late.setNumLines (activeHd ? 16 : 8);
                hdFade.setTarget (1.0);
            }

            buildRoomFrame (n, hd);

            const auto& layout = *currentLayout;
            bus.numSpeakers = layout.numSpeakers;
            for (int i = 0; i < layout.numSpeakers; ++i)
                std::fill (busData[(size_t) i].begin(), busData[(size_t) i].begin() + n, 0.0f);
            std::fill (lateSend.begin(), lateSend.begin() + n, 0.0f);

            for (int s = 0; s < maxSources; ++s)
            {
                if (! sourceRunning[(size_t) s])
                    continue;

                const auto& in = inputs[(size_t) s];
                std::array<const float*, 2> ch {};
                int numCh;

                if (in.numChannels > 0)
                {
                    numCh = std::min (in.numChannels, 2);
                    ch[0] = in.channels[0] + offset;
                    ch[1] = numCh > 1 ? in.channels[1] + offset : ch[0];
                }
                else
                {
                    numCh = lastNumChannels[(size_t) s];
                    ch = { zeros.data(), zeros.data() };
                }

                sources[(size_t) s].processChunk (ch.data(), numCh, n, layout, roomFrame, bus, lateSend.data());

                if (in.numChannels == 0)
                {
                    tailRemaining[(size_t) s] -= n;
                    if (tailRemaining[(size_t) s] <= 0)
                        sourceRunning[(size_t) s] = false;
                }
            }

            const auto lateLevel = (float) (room.reverbLevel.advance (n) * hd);
            late.processChunk (lateSend.data(), n, lateSettings, layout.lateGains[activeHd ? 1 : 0].data(),
                               previousLateLevel, lateLevel, bus);
            previousLateLevel = lateLevel;

            renderOutputs (outputs, numOutputs, offset, n);
        }
    }

    void RenderEngine::renderOutputs (float* const* outputs, int numOutputs, int offset, int n) noexcept
    {
        const auto& layout = *currentLayout;
        const auto numSpeakers = layout.numSpeakers;

        for (int ch = 0; ch < numOutputs; ++ch)
            std::fill (outputs[ch] + offset, outputs[ch] + offset + n, 0.0f);

        // Align: per-speaker delay and gain.
        for (int i = 0; i < numSpeakers; ++i)
        {
            auto* x = busData[(size_t) i].data();
            auto& line = alignLines[(size_t) i];
            const auto delay = (int) std::lround (layout.alignDelaySeconds[(size_t) i] * sampleRate);
            const auto gain = layout.alignGain[(size_t) i];

            for (int s = 0; s < n; ++s)
            {
                line.push (x[s]);
                x[s] = line.at (delay) * gain;
            }
        }

        // Master volume (including the speaker-setup fade), then the linked limiter.
        const auto v0 = volume.getCurrent() * layoutFade.getCurrent();
        const auto v1 = volume.advance (n) * layoutFade.advance (n);
        const auto ceiling = dbToGain (limiterCeilingDb);

        for (int s = 0; s < n; ++s)
        {
            const auto g = (float) (v0 + (v1 - v0) * (s + 1) / n);
            float peak = 0.0f;

            for (int i = 0; i < numSpeakers; ++i)
            {
                auto& x = busData[(size_t) i][(size_t) s];
                x *= g;
                peak = std::max (peak, std::abs (x));
            }

            if (limiterOn)
            {
                const auto target = peak > ceiling ? ceiling / peak : 1.0;
                const auto coeff = target < limiterGain ? limiterAttack : limiterRelease;
                limiterGain = target + coeff * (limiterGain - target);

                for (int i = 0; i < numSpeakers; ++i)
                {
                    auto& x = busData[(size_t) i][(size_t) s];
                    x = (float) std::clamp ((double) x * limiterGain, -ceiling, ceiling); // no lookahead: clamp what slips through
                }
            }
            else
            {
                limiterGain = 1.0;
            }
        }

        // Write to the device channels and meter.
        for (int i = 0; i < numSpeakers; ++i)
        {
            const auto ch = layout.deviceChannel[(size_t) i];
            if (ch < 0 || ch >= numOutputs)
                continue;

            const auto* x = busData[(size_t) i].data();
            auto* out = outputs[ch] + offset;
            float blockPeak = 0.0f;

            for (int s = 0; s < n; ++s)
            {
                out[s] += x[s];
                blockPeak = std::max (blockPeak, std::abs (x[s]));
            }

            if (ch < maxSpeakers && blockPeak > peaks[(size_t) ch].load (std::memory_order_relaxed))
                peaks[(size_t) ch].store (blockPeak, std::memory_order_relaxed);
        }
    }
}
