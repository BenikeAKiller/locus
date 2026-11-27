#include "SourceProcessor.h"

namespace locus
{
    namespace
    {
        constexpr double paramRampSeconds     = 0.020;
        constexpr double crossfadeSeconds     = 0.030;
        constexpr double panFadeSeconds       = 0.015; // fade out, switch panner, fade in: 30 ms total
        constexpr double maxDopplerSeconds    = 50.0 / speedOfSound + 0.005;
        constexpr double maxRoomDelaySeconds  = 0.5;
        constexpr double directivityShelfHz   = 3000.0;
        constexpr float  clusterAllpassGain   = 0.6f;
        constexpr double clusterAllpassMs[numClusterTaps][2] { { 4.77, 2.31 }, { 5.53, 3.17 }, { 6.31, 2.73 }, { 3.89, 1.93 } };
        constexpr double diffuseClusterAzimuth[numClusterTaps] { -135.0, -45.0, 45.0, 135.0 };
        constexpr double earlySideOffset      = 20.0; // degrees beyond the stereo width
        constexpr double clusterPackedOffset  = 30.0; // degrees beyond half the width

        void panTo (const Panner& panner, Vec3 dir, double spread, float* gains) noexcept
        {
            panner.computeGains (azimuthOf (dir), elevationOf (dir), spread, gains);
        }
    }

    void SourceProcessor::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        for (auto* s : { &distance, &elevation, &aperture, &width, &spread, &panRev, &airFreq, &nearRadius, &drop,
                         &axisLow, &axisMid, &axisHigh, &axisFreqLow, &axisFreqHigh, &axisGain, &axisOn,
                         &omniLow, &omniMid, &omniHigh, &omniFreqLow, &omniFreqHigh, &omniGain, &omniOn, &airMix })
            s->setRampLength (sampleRate, paramRampSeconds);

        azimuth.setRampLength (sampleRate, paramRampSeconds);
        orientation.setRampLength (sampleRate, paramRampSeconds);
        dopplerMix.setRampLength (sampleRate, crossfadeSeconds);
        panFade.setRampLength (sampleRate, panFadeSeconds);

        for (int c = 0; c < 2; ++c)
        {
            dopplerLine[(size_t) c].prepare ((int) std::ceil (maxDopplerSeconds * sampleRate) + renderChunk);
            roomLine[(size_t) c].prepare ((int) std::ceil (maxRoomDelaySeconds * sampleRate) + renderChunk);
        }

        for (int k = 0; k < numClusterTaps; ++k)
            for (int stage = 0; stage < 2; ++stage)
            {
                const auto d = std::max (1, (int) std::lround (clusterAllpassMs[k][stage] * 0.001 * sampleRate));
                allpassDelay[(size_t) k][(size_t) stage] = d;
                clusterAllpass[(size_t) k][(size_t) stage].prepare (d);
            }

        setTargets (SourceParamStore (getSourceParamSpecs()));
        restart();
    }

    void SourceProcessor::setTargets (const SourceParamStore& p) noexcept
    {
        azimuth.setTarget (p.get (SourceParam::azimuth));
        orientation.setTarget (p.get (SourceParam::orientation));
        distance.setTarget (p.get (SourceParam::distance));
        elevation.setTarget (p.get (SourceParam::elevation));
        aperture.setTarget (p.get (SourceParam::aperture));
        width.setTarget (p.get (SourceParam::width));
        spread.setTarget (p.get (SourceParam::spread) * 0.01);
        panRev.setTarget (p.get (SourceParam::panRev) * 0.01);
        airFreq.setTarget (p.get (SourceParam::airFreq));
        nearRadius.setTarget (p.get (SourceParam::nearRadius));
        drop.setTarget (p.get (SourceParam::drop));
        logDrop = p.getChoice (SourceParam::dropMode) == (int) DropMode::logarithmic;

        dopplerMix.setTarget (p.getBool (SourceParam::doppler) ? 1.0 : 0.0);
        airMix.setTarget (p.getBool (SourceParam::air) ? 1.0 : 0.0);
        requestedPanning = p.getChoice (SourceParam::panning);

        const auto axisBypassed = p.getBool (SourceParam::axisBypass);
        axisLow.setTarget (axisBypassed ? 0.0 : p.get (SourceParam::axisLow));
        axisMid.setTarget (axisBypassed ? 0.0 : p.get (SourceParam::axisMid));
        axisHigh.setTarget (axisBypassed ? 0.0 : p.get (SourceParam::axisHigh));
        axisFreqLow.setTarget (p.get (SourceParam::axisFreqLow));
        axisFreqHigh.setTarget (p.get (SourceParam::axisFreqHigh));
        axisGain.setTarget (p.get (SourceParam::axisGain));
        axisOn.setTarget (p.getBool (SourceParam::axisMute) ? 0.0 : 1.0);

        const auto omniBypassed = p.getBool (SourceParam::omniBypass);
        omniLow.setTarget (omniBypassed ? 0.0 : p.get (SourceParam::omniLow));
        omniMid.setTarget (omniBypassed ? 0.0 : p.get (SourceParam::omniMid));
        omniHigh.setTarget (omniBypassed ? 0.0 : p.get (SourceParam::omniHigh));
        omniFreqLow.setTarget (p.get (SourceParam::omniFreqLow));
        omniFreqHigh.setTarget (p.get (SourceParam::omniFreqHigh));
        omniGain.setTarget (p.get (SourceParam::omniGain));
        omniOn.setTarget (p.getBool (SourceParam::omniMute) ? 0.0 : 1.0);
    }

    void SourceProcessor::restart() noexcept
    {
        azimuth.snapToTarget();
        orientation.snapToTarget();

        for (auto* s : { &distance, &elevation, &aperture, &width, &spread, &panRev, &airFreq, &nearRadius, &drop,
                         &axisLow, &axisMid, &axisHigh, &axisFreqLow, &axisFreqHigh, &axisGain, &axisOn,
                         &omniLow, &omniMid, &omniHigh, &omniFreqLow, &omniFreqHigh, &omniGain, &omniOn,
                         &airMix, &dopplerMix })
            s->snapToTarget();

        activePanning = requestedPanning;
        panFade.reset (1.0);

        for (int c = 0; c < 2; ++c)
        {
            dopplerLine[(size_t) c].clear();
            roomLine[(size_t) c].clear();

            for (auto* f : { &airFilter[(size_t) c], &directivityFilter[(size_t) c], &axisLowShelf[(size_t) c],
                             &axisHighShelf[(size_t) c], &omniLowShelf[(size_t) c], &omniHighShelf[(size_t) c] })
                f->reset();
        }

        for (auto& tap : clusterAllpass)
            for (auto& line : tap)
                line.clear();

        lastAirCutoff = -1.0;
        lastDirectivityDb = 1.0e9;
        lastAxis = ShelfEqSettings { 1.0e9 };
        lastOmni = ShelfEqSettings { 1.0e9 };
        invalidatePanning();
    }

    //==============================================================================
    void SourceProcessor::updateFilters (double dist, double nearR, double airF, double orient, double ap) noexcept
    {
        const auto cutoff = airCutoffHz (airF, nearR, dist, sampleRate);
        if (std::abs (cutoff - lastAirCutoff) > 0.5)
        {
            lastAirCutoff = cutoff;
            const auto c = BiquadCoeffs::lowPass (sampleRate, cutoff);
            for (auto& f : airFilter)
                f.setCoefficients (c);
        }

        const auto shelfDb = directivityShelfDb (orient, ap);
        if (std::abs (shelfDb - lastDirectivityDb) > 0.01)
        {
            lastDirectivityDb = shelfDb;
            const auto c = BiquadCoeffs::highShelf (sampleRate, directivityShelfHz, shelfDb);
            for (auto& f : directivityFilter)
                f.setCoefficients (c);
        }

        const ShelfEqSettings axis { axisLow.getCurrent(), axisMid.getCurrent(), axisHigh.getCurrent(),
                                     axisFreqLow.getCurrent(), axisFreqHigh.getCurrent() };
        if (! (axis == lastAxis))
        {
            lastAxis = axis;
            const auto d = DoubleShelfDesign::make (sampleRate, axis);
            for (int c = 0; c < 2; ++c)
            {
                axisLowShelf[(size_t) c].setCoefficients (d.lowShelf);
                axisHighShelf[(size_t) c].setCoefficients (d.highShelf);
            }
        }

        const ShelfEqSettings omni { omniLow.getCurrent(), omniMid.getCurrent(), omniHigh.getCurrent(),
                                     omniFreqLow.getCurrent(), omniFreqHigh.getCurrent() };
        if (! (omni == lastOmni))
        {
            lastOmni = omni;
            const auto d = DoubleShelfDesign::make (sampleRate, omni);
            for (int c = 0; c < 2; ++c)
            {
                omniLowShelf[(size_t) c].setCoefficients (d.lowShelf);
                omniHighShelf[(size_t) c].setCoefficients (d.highShelf);
            }
        }
    }

    void SourceProcessor::updatePanning (const LayoutState& layout, int numChannels, double az, double el,
                                         double w, double spr, double rev) noexcept
    {
        auto unchanged = [] (const auto& key, const auto& cache)
        {
            for (size_t i = 0; i < key.size(); ++i)
                if (std::abs (key[i] - cache[i]) >= 1.0e-4)
                    return false;
            return true;
        };

        const auto layoutKey = (double) (numChannels + 10 * layout.numSpeakers);
        const auto& panner = layout.getPanner (activePanning);

        // Direct: every chunk while the source moves. One point, or two points at
        // azimuth -/+ width / 2 rotated with the source.
        const std::array<double, 6> key { az, el, w, spr, (double) activePanning, layoutKey };
        if (! (panCacheValid && unchanged (key, panCache)))
        {
            panCache = key;
            panCacheValid = true;

            if (numChannels == 2)
            {
                for (int c = 0; c < 2; ++c)
                    panTo (panner, rotateWithSource ((c == 0 ? -0.5 : 0.5) * w, az, el), spr, directBus[(size_t) c].unit.data());
            }
            else
            {
                panner.computeGains (az, el, spr, directBus[0].unit.data());
                directBus[1].unit.fill (0.0f);
            }
        }

        // Room stages: every 4th chunk at most (reflection directions lag by under 3 ms,
        // inaudible, and their spread panning is the most expensive part).
        ++chunksSinceRoomPan;
        const std::array<double, 6> roomKey { az, el, w, rev, (double) activePanning, layoutKey };
        if (roomPanCacheValid && (chunksSinceRoomPan < roomPanInterval || unchanged (roomKey, roomPanCache)))
            return;

        roomPanCache = roomKey;
        roomPanCacheValid = true;
        chunksSinceRoomPan = 0;

        // Early: two sides, beyond the stereo width.
        for (int side = 0; side < 2; ++side)
            panTo (panner, rotateWithSource ((side == 0 ? -1.0 : 1.0) * (w * 0.5 + earlySideOffset), az, el),
                   earlyPanSpread, earlyBus[(size_t) side].unit.data());

        // Cluster: four directions moving from fixed diffuse positions (Pan Rev 0)
        // to a pack around the source (Pan Rev 100), without crossing over.
        std::array<int, numClusterTaps> order { 0, 1, 2, 3 };
        std::sort (order.begin(), order.end(), [az] (int a, int b)
        {
            return wrapAzimuth (diffuseClusterAzimuth[a] - az) < wrapAzimuth (diffuseClusterAzimuth[b] - az);
        });

        const auto packedHalfWidth = w * 0.5 + clusterPackedOffset;
        constexpr double packedPosition[numClusterTaps] { -1.0, -1.0 / 3.0, 1.0 / 3.0, 1.0 };

        for (int i = 0; i < numClusterTaps; ++i)
        {
            const auto diffuse = diffuseClusterAzimuth[order[(size_t) i]];
            const auto packed = az + packedPosition[i] * packedHalfWidth;
            const auto clusterAz = diffuse + rev * wrapAzimuth (packed - diffuse);
            panner.computeGains (clusterAz, rev * el, clusterPanSpread, clusterBus[(size_t) i].unit.data());
        }
    }

    void SourceProcessor::mixBus (const float* signal, Bus& b, int numSpeakers, int numSamples, SpeakerBus& out) noexcept
    {
        const auto invN = 1.0f / (float) numSamples;

        for (int i = 0; i < numSpeakers; ++i)
        {
            const auto g0 = b.previous[(size_t) i], g1 = b.current[(size_t) i];
            if (g0 == 0.0f && g1 == 0.0f)
                continue;

            const auto dg = (g1 - g0) * invN;
            auto g = g0;
            auto* o = out.channels[(size_t) i];

            for (int s = 0; s < numSamples; ++s)
            {
                g += dg;
                o[s] += signal[s] * g;
            }
        }

        b.previous = b.current;
    }

    //==============================================================================
    void SourceProcessor::processChunk (const float* const* input, int numInputChannels, int n,
                                        const LayoutState& layout, const RoomFrame& room,
                                        SpeakerBus& bus, float* lateSend) noexcept
    {
        jassert (n > 0 && n <= renderChunk);
        const auto numCh = std::clamp (numInputChannels, 1, 2);

        // Smoothed values at the end of this chunk
        const auto az = azimuth.advance (n), orient = orientation.advance (n);
        const auto dist = distance.advance (n), el = elevation.advance (n);
        const auto ap = aperture.advance (n), w = width.advance (n);
        const auto spr = spread.advance (n), rev = panRev.advance (n);
        const auto airF = airFreq.advance (n), nearR = nearRadius.advance (n), dropDb = drop.advance (n);
        axisLow.advance (n); axisHigh.advance (n); axisFreqLow.advance (n); axisFreqHigh.advance (n);
        omniLow.advance (n); omniHigh.advance (n); omniFreqLow.advance (n); omniFreqHigh.advance (n);
        const auto axMid = axisMid.advance (n), axGain = axisGain.advance (n), axOn = axisOn.advance (n);
        const auto omMid = omniMid.advance (n), omGain = omniGain.advance (n), omOn = omniOn.advance (n);
        const auto dMix = (float) dopplerMix.advance (n);
        const auto aMix = (float) airMix.advance (n);

        // Panning type change: fade out, switch, fade back in.
        panFade.setTarget (requestedPanning == activePanning ? 1.0 : 0.0);
        const auto fade = panFade.advance (n);
        if (fade <= 0.0 && requestedPanning != activePanning)
        {
            activePanning = requestedPanning;
            panCacheValid = roomPanCacheValid = false;
            panFade.setTarget (1.0);
        }

        const auto effectiveEl = layout.threeD ? el : 0.0;

        // Stage gains
        const auto distDb = distanceGainDb (dist, nearR, dropDb, logDrop);
        const auto directGain   = (float) (dbToGain (distDb + axMid + axGain) * axOn * fade);
        const auto roomGain     = (float) (dbToGain (omMid + omGain) * omOn);
        const auto earlyStage   = (float) (dbToGain (distDb * 0.5) * room.earlyLevel * fade);
        const auto clusterStage = (float) (room.clusterLevel * fade);
        const auto lateGain     = (float) fade * (numCh == 2 ? 0.70710678f : 1.0f);

        updateFilters (dist, nearR, airF, orient, ap);
        updatePanning (layout, numCh, az, effectiveEl, w, spr, rev);

        const auto N = layout.numSpeakers;
        for (int i = 0; i < N; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                directBus[(size_t) c].current[(size_t) i] = c < numCh ? directBus[(size_t) c].unit[(size_t) i] * directGain : 0.0f;
                earlyBus[(size_t) c].current[(size_t) i] = earlyBus[(size_t) c].unit[(size_t) i] * earlyStage;
            }

            for (auto& b : clusterBus)
                b.current[(size_t) i] = b.unit[(size_t) i] * clusterStage;
        }

        const auto dopplerDelay = std::clamp (dist / speedOfSound * sampleRate, 1.0, (double) dopplerLine[0].getMaxDelay() - 2.0);

        if (firstChunk)
        {
            for (auto* b : { &directBus[0], &directBus[1], &earlyBus[0], &earlyBus[1],
                             &clusterBus[0], &clusterBus[1], &clusterBus[2], &clusterBus[3] })
                b->previous = b->current;

            previousDopplerDelay = dopplerDelay;
            previousRoomGain = roomGain;
            previousAirMix = aMix;
            previousDopplerMix = dMix;
            firstChunk = false;
        }

        // Per-sample: Doppler, direct filters, room input
        const auto invN = 1.0f / (float) n;

        for (int c = 0; c < numCh; ++c)
        {
            const auto* x = input[c];
            auto& doppler = dopplerLine[(size_t) c];

            for (int s = 0; s < n; ++s)
            {
                const auto t = (float) (s + 1) * invN;
                const auto in = x[s];
                doppler.push (in);

                auto y = in;
                const auto m = previousDopplerMix + (dMix - previousDopplerMix) * t;
                if (m > 0.0f)
                {
                    const auto delay = previousDopplerDelay + (dopplerDelay - previousDopplerDelay) * (double) t;
                    y = in + m * (doppler.readCubic (delay) - in);
                }

                auto a = airFilter[(size_t) c].process (y);
                a = y + (previousAirMix + (aMix - previousAirMix) * t) * (a - y);
                a = directivityFilter[(size_t) c].process (a);
                a = axisHighShelf[(size_t) c].process (axisLowShelf[(size_t) c].process (a));
                directSignal[(size_t) c][(size_t) s] = a;

                auto r = omniHighShelf[(size_t) c].process (omniLowShelf[(size_t) c].process (y));
                r *= previousRoomGain + (roomGain - previousRoomGain) * t;
                roomLine[(size_t) c].push (r);
                lateSend[s] += r * lateGain;
            }
        }

        // Early reflections and cluster read the room line (the whole chunk is already written).
        const auto chB = numCh > 1 ? 1 : 0;

        for (int s = 0; s < n; ++s)
        {
            const auto age = (double) (n - 1 - s);
            float sideA = 0.0f, sideB = 0.0f;

            for (int k = 0; k < room.numEarlyTaps; ++k)
            {
                const auto odd = (k & 1) != 0;
                const auto v = roomLine[(size_t) (odd ? chB : 0)].readLinear (room.earlyDelay[(size_t) k] + age) * room.earlyGain[(size_t) k];
                (odd ? sideB : sideA) += v;
            }

            earlySignal[0][(size_t) s] = sideA;
            earlySignal[1][(size_t) s] = sideB;

            for (int k = 0; k < numClusterTaps; ++k)
            {
                auto v = roomLine[(size_t) ((k & 1) ? chB : 0)].readLinear (room.clusterDelay[(size_t) k] + age);

                for (int stage = 0; stage < 2; ++stage)
                {
                    auto& line = clusterAllpass[(size_t) k][(size_t) stage];
                    const auto delayed = line.at (allpassDelay[(size_t) k][(size_t) stage] - 1);
                    const auto wv = v + clusterAllpassGain * delayed;
                    v = delayed - clusterAllpassGain * wv;
                    line.push (wv);
                }

                clusterSignal[(size_t) k][(size_t) s] = v;
            }
        }

        // Pan everything onto the speakers.
        for (int c = 0; c < 2; ++c)
        {
            mixBus (directSignal[(size_t) c].data(), directBus[(size_t) c], N, n, bus);
            mixBus (earlySignal[(size_t) c].data(), earlyBus[(size_t) c], N, n, bus);
        }

        for (int k = 0; k < numClusterTaps; ++k)
            mixBus (clusterSignal[(size_t) k].data(), clusterBus[(size_t) k], N, n, bus);

        previousDopplerDelay = dopplerDelay;
        previousRoomGain = roomGain;
        previousAirMix = aMix;
        previousDopplerMix = dMix;
    }
}
