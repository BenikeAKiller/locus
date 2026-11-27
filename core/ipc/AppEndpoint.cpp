#include "AppEndpoint.h"
#include "AudioRing.h"

namespace locus::ipc
{
    namespace
    {
        // Drift compensation (spec §8): a PI controller on the smoothed fill error drives the
        // resampling ratio, limited to +/-0.5 %.
        constexpr double fillSmoothingSeconds = 0.25;
        constexpr double proportionalGain = 2.0e-6; // per frame of error
        constexpr double integralGain = 2.0e-7;     // per frame-second of error
        constexpr double maxCorrection = 0.005;

        // A slot buffered this far past its target (e.g. after the app stalled) skips ahead.
        constexpr int skipAheadFactor = 3;

        // Extra frames kept beyond one plugin block plus one app block, for scheduling jitter.
        constexpr int adaptiveMargin = 512;

        constexpr double ownerTimeoutSeconds = 2.0;
        constexpr int maxRateRatio = 5; // 192 kHz plugin into a 44.1 kHz app, plus margin
    }

    AppEndpoint::AppEndpoint (const juce::String& sharedMemoryName)
    {
        if (memory.open (sharedMemoryName, sizeof (SharedLayout)))
        {
            auto* candidate = static_cast<SharedLayout*> (memory.getData());
            if (initialiseLayout (*candidate))
                layout = candidate;
        }

        if (layout != nullptr)
            for (int s = 0; s < maxSources; ++s)
                lastAppWrite[(size_t) s] = layout->slots[s].fromApp.lock.current();
    }

    //==============================================================================
    void AppEndpoint::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (1, maxBlockSize);

        for (auto& st : audio)
        {
            for (int c = 0; c < 2; ++c)
            {
                st.output[(size_t) c].assign ((size_t) maxBlock, 0.0f);
                st.scratch[(size_t) c].assign ((size_t) (maxBlock * maxRateRatio + 64), 0.0f);
                st.interpolators[(size_t) c].reset();
            }

            st.wasActive = false;
            st.priming = true;
            st.silentSamples = 1 << 30;
        }
    }

    void AppEndpoint::pullAudio (SourceInputs& inputs, int numSamples) noexcept
    {
        for (int s = 0; s < maxSources; ++s)
        {
            inputs[(size_t) s] = {};

            if (layout != nullptr && numSamples <= maxBlock)
                pullSlot (s, inputs[(size_t) s], numSamples);
        }
    }

    void AppEndpoint::pullSlot (int index, SourceInput& input, int n) noexcept
    {
        auto& sl = layout->slots[index];
        auto& st = audio[(size_t) index];
        AudioRing ring (sl.writeIndex, sl.readIndex, sl.audio[0], sl.audio[1], ringFrames);

        const auto owner = sl.owner.load (std::memory_order_acquire);
        const auto beat = sl.audioBeat.load (std::memory_order_acquire);

        if (beat != st.lastAudioBeat)
        {
            st.lastAudioBeat = beat;
            st.silentSamples = 0;
        }
        else
        {
            st.silentSamples = std::min (st.silentSamples + n, 1 << 30);
        }

        const bool isActive = owner != 0
                           && st.silentSamples < (int) (activityTimeoutSeconds * sampleRate)
                           && sl.nonRealtime.load (std::memory_order_relaxed) == 0;

        if (! isActive)
        {
            if (st.wasActive)
                ring.flush();

            st.wasActive = false;
            active[(size_t) index].store (false, std::memory_order_relaxed);
            channels[(size_t) index].store (0, std::memory_order_relaxed);
            return;
        }

        if (! st.wasActive)
        {
            // Start fresh: drop anything stale and buffer up to the target before playing.
            ring.flush();
            st.wasActive = true;
            st.priming = true;
            st.smoothedFill = st.integral = st.correction = 0.0;
            st.pluginBlock = 0;
            st.blockBeat = beat;
            st.blockWriteIndex = sl.writeIndex.load (std::memory_order_acquire);
            for (auto& i : st.interpolators)
                i.reset();
        }

        // Measure the plugin's block size: frames written / blocks written since the last look.
        {
            const auto w = sl.writeIndex.load (std::memory_order_acquire);
            const auto blocks = beat - st.blockBeat;
            if (blocks > 0 && w > st.blockWriteIndex)
            {
                const auto size = (int) ((w - st.blockWriteIndex) / blocks);
                st.pluginBlock = juce::jlimit (st.pluginBlock, ringFrames / 4, size);
            }
            st.blockBeat = beat;
            st.blockWriteIndex = w;
        }

        active[(size_t) index].store (true, std::memory_order_relaxed);

        const auto numCh = (int) juce::jlimit (1u, 2u, sl.numChannels.load (std::memory_order_relaxed));
        channels[(size_t) index].store (numCh, std::memory_order_relaxed);

        auto* out0 = st.output[0].data();
        auto* out1 = st.output[1].data();
        input.channels = { out0, numCh > 1 ? out1 : out0 };
        input.numChannels = numCh;

        auto silence = [&]
        {
            std::fill (out0, out0 + n, 0.0f);
            std::fill (out1, out1 + n, 0.0f);
        };

        const auto pluginRate = (double) sl.sampleRate.load (std::memory_order_relaxed);
        const auto rateRatio = pluginRate > 0.0 ? pluginRate / sampleRate : 1.0;

        // The plugin delivers audio in bursts of one block, so the fill swings by a whole block.
        // Keep at least one plugin block plus one app block (in plugin frames) plus a margin
        // buffered, whatever the Link buffer setting, so the ring never runs dry between bursts.
        const auto appBlockInPluginFrames = (int) std::ceil (n * rateRatio);
        const auto target = juce::jmin (ringFrames - st.pluginBlock - 512,
                                        juce::jmax (targetFill.load (std::memory_order_relaxed),
                                                    st.pluginBlock + appBlockInPluginFrames + adaptiveMargin));
        lastTarget[(size_t) index].store (target, std::memory_order_relaxed);
        auto available = ring.getNumReady();

        if (st.priming)
        {
            if (available < target)
            {
                silence();
                return;
            }

            st.priming = false;
            st.smoothedFill = available;
        }

        const auto skipThreshold = juce::jmin (ringFrames - 512, target * skipAheadFactor + st.pluginBlock);
        if (available > skipThreshold)
        {
            ring.advance (available - target);
            available = target;
            st.smoothedFill = target;
            st.integral = 0.0;
        }

        const auto ratio = rateRatio * (1.0 + st.correction);
        const auto needed = (int) std::ceil (n * ratio) + 2;

        if (available < needed || needed > (int) st.scratch[0].size())
        {
            underruns[(size_t) index].fetch_add (1, std::memory_order_relaxed);
            st.priming = true;
            silence();
            return;
        }

        ring.peek (st.scratch[0].data(), st.scratch[1].data(), needed);

        const auto used = st.interpolators[0].process (ratio, st.scratch[0].data(), out0, n);
        if (numCh > 1)
            st.interpolators[1].process (ratio, st.scratch[1].data(), out1, n);

        ring.advance (used);

        // PI control of the buffer fill.
        const auto fill = (double) (available - used);
        st.smoothedFill += (1.0 - std::exp (-n / (fillSmoothingSeconds * sampleRate))) * (fill - st.smoothedFill);

        const auto error = st.smoothedFill - target;
        st.integral = juce::jlimit (-maxCorrection / integralGain, maxCorrection / integralGain,
                                    st.integral + error * n / sampleRate);
        st.correction = juce::jlimit (-maxCorrection, maxCorrection,
                                      proportionalGain * error + integralGain * st.integral);
    }

    //==============================================================================
    void AppEndpoint::tick (double nowSeconds, const std::array<SourceParamStore*, maxSources>& stores)
    {
        if (layout == nullptr)
            return;

        now = nowSeconds;
        layout->header.appHeartbeat.fetch_add (1);

        for (int s = 0; s < maxSources; ++s)
        {
            auto& sl = layout->slots[s];
            const auto owner = sl.owner.load();
            const auto aliveBeat = sl.aliveBeat.load();

            if (aliveBeat != seenAlive[(size_t) s] || owner != knownOwner[(size_t) s])
            {
                seenAlive[(size_t) s] = aliveBeat;
                seenAliveTime[(size_t) s] = now;
            }

            if (owner != knownOwner[(size_t) s])
            {
                knownOwner[(size_t) s] = owner;
                lastPluginSequence[(size_t) s] = 0;
                names[(size_t) s].clear();
            }

            alive[(size_t) s] = owner != 0 && now - seenAliveTime[(size_t) s] < ownerTimeoutSeconds;
            if (! alive[(size_t) s])
                continue;

            // Plugin -> app parameters. Accept only once the plugin has applied every edit the
            // app sent, so a slightly stale plugin value never overrides a newer app edit.
            float values[numSyncParams];
            uint32_t applied = 0, sequence = 0;
            uint64_t publisher = 0;

            const auto ok = sl.fromPlugin.lock.read ([&]
            {
                for (int p = 0; p < numSyncParams; ++p)
                    values[p] = sl.fromPlugin.values[p].load (std::memory_order_relaxed);
                applied = sl.fromPlugin.appliedAppSequence.load (std::memory_order_relaxed);
                publisher = sl.fromPlugin.publisher.load (std::memory_order_relaxed);
            }, &sequence);

            const bool caughtUp = (int32_t) (applied - lastAppWrite[(size_t) s]) >= 0;

            if (ok && publisher == owner && sequence != lastPluginSequence[(size_t) s] && caughtUp)
            {
                lastPluginSequence[(size_t) s] = sequence;
                if (auto* store = stores[(size_t) s])
                    for (int p = 0; p < numSyncParams; ++p)
                        store->set (p, values[p]);
            }

            names[(size_t) s] = readName (sl.name);
        }
    }

    void AppEndpoint::sendEdit (int slot, int param, float value, bool gestureActive)
    {
        if (layout == nullptr || slot < 0 || slot >= maxSources || param < 0 || param >= numSyncParams)
            return;

        auto& edits = layout->slots[slot].fromApp;
        const auto bit = 1u << param;

        const auto s = edits.lock.beginWrite();
        edits.values[param].store (value, std::memory_order_relaxed);
        edits.changeCount[param].fetch_add (1, std::memory_order_relaxed);
        const auto mask = edits.gestureMask.load (std::memory_order_relaxed);
        edits.gestureMask.store (gestureActive ? (mask | bit) : (mask & ~bit), std::memory_order_relaxed);
        edits.lock.endWrite (s);

        lastAppWrite[(size_t) slot] = s + 1;
    }

    AppEndpoint::SlotStatus AppEndpoint::getStatus (int slot) const
    {
        SlotStatus st;
        if (slot < 0 || slot >= maxSources)
            return st;

        st.connected = alive[(size_t) slot];
        st.active = active[(size_t) slot].load (std::memory_order_relaxed);
        st.name = names[(size_t) slot];
        st.numChannels = channels[(size_t) slot].load (std::memory_order_relaxed);
        st.underruns = underruns[(size_t) slot].load (std::memory_order_relaxed);
        st.targetFrames = lastTarget[(size_t) slot].load (std::memory_order_relaxed);

        if (layout != nullptr)
        {
            const auto& sl = layout->slots[slot];
            st.bufferedFrames = (int) (sl.writeIndex.load (std::memory_order_acquire) - sl.readIndex.load (std::memory_order_acquire));
        }

        return st;
    }
}
