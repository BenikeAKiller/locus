#include <dsp/Panners.h>
#include <numeric>

using namespace locus;

namespace
{
    double powerSum (const float* g, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i)
            s += (double) g[i] * g[i];
        return s;
    }

    int countNonZero (const float* g, int n)
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
            c += std::abs (g[i]) > 1.0e-9f ? 1 : 0;
        return c;
    }

    /** Azimuth of the energy vector rE = sum(g^2 u) / sum(g^2). */
    double energyVectorAzimuth (const SpeakerLayout& layout, const float* g)
    {
        Vec3 v;
        for (int i = 0; i < layout.size(); ++i)
            v = v + layout.speakers[(size_t) i].getDirection() * ((double) g[i] * g[i]);
        return azimuthOf (v);
    }

    std::vector<SpeakerLayout> allPresets()
    {
        std::vector<SpeakerLayout> result;
        for (const auto& p : getSpeakerPresets())
            result.push_back (*makePresetLayout (p.name));
        return result;
    }

    /** Calls fn(az, el) over a sweep of directions covering the sphere, poles included. */
    template <typename Fn>
    void forDirections (Fn&& fn)
    {
        for (double el = -90.0; el <= 90.0; el += 15.0)
            for (double az = -180.0; az < 180.0; az += 7.0)
                fn (az, el);
    }
}

class PannerTests final : public juce::UnitTest
{
public:
    PannerTests() : juce::UnitTest ("Panners", "Locus") {}

    void runTest() override
    {
        std::array<float, maxSpeakers> g {}, g2 {};

        beginTest ("All panners: constant power on every preset");
        for (const auto& layout : allPresets())
            for (auto type : { PanningType::angular, PanningType::nearest, PanningType::ambisonics })
            {
                const auto panner = createPanner (type, layout);
                double worst = 0.0;

                forDirections ([&] (double az, double el)
                {
                    for (double spread : { 0.0, 0.4, 1.0 })
                    {
                        panner->computeGains (az, el, spread, g.data());
                        worst = std::max (worst, std::abs (powerSum (g.data(), layout.size()) - 1.0));
                    }
                });

                expect (worst < 1.0e-5, layout.name + " type " + juce::String ((int) type) + " error " + juce::String (worst));
            }

        beginTest ("VBAP: a source on a speaker gives that speaker gain 1");
        for (const auto& layout : allPresets())
        {
            const VbapPanner panner (layout);
            for (int i = 0; i < layout.size(); ++i)
            {
                const auto& s = layout.speakers[(size_t) i];
                panner.computeGains (s.azimuth, s.elevation, 0.0, g.data());
                expectWithinAbsoluteError (g[(size_t) i], 1.0f, 1.0e-5f, layout.name + " speaker " + s.label);
            }
        }

        beginTest ("VBAP: halfway between L and R of 4.0 splits equally");
        {
            const VbapPanner panner (*makePresetLayout ("4.0"));
            panner.computeGains (0.0, 0.0, 0.0, g.data());
            expectWithinAbsoluteError (g[0], 0.70710678f, 1.0e-5f);
            expectWithinAbsoluteError (g[1], 0.70710678f, 1.0e-5f);
            expectWithinAbsoluteError (g[2] + g[3], 0.0f, 1.0e-6f);
        }

        beginTest ("VBAP: 3D triangulation covers the sphere for every 3D preset");
        for (const auto& layout : allPresets())
        {
            if (! layout.is3D())
                continue;

            const VbapPanner panner (layout);
            expect (panner.usesTriangulation(), layout.name + " fell back to 2D");

            bool allCovered = true;
            forDirections ([&] (double az, double el)
            {
                panner.computeGains (az, el, 0.0, g.data());
                allCovered = allCovered && countNonZero (g.data(), layout.size()) > 0;
            });

            expect (allCovered, layout.name);
        }

        beginTest ("VBAP: spread widens the image");
        {
            const auto layout = *makePresetLayout ("8ch");
            const VbapPanner panner (layout);
            panner.computeGains (0.0, 0.0, 0.0, g.data());
            const auto narrow = countNonZero (g.data(), layout.size());
            panner.computeGains (0.0, 0.0, 1.0, g.data());
            const auto wide = countNonZero (g.data(), layout.size());
            expect (wide > narrow, juce::String (narrow) + " -> " + juce::String (wide));
        }

        beginTest ("2D layouts ignore elevation");
        for (auto type : { PanningType::angular, PanningType::nearest, PanningType::ambisonics })
        {
            const auto panner = createPanner (type, *makePresetLayout ("5.0"));
            panner->computeGains (40.0, 0.0, 0.3, g.data());
            panner->computeGains (40.0, 60.0, 0.3, g2.data());
            for (int i = 0; i < 5; ++i)
                expectWithinAbsoluteError (g[(size_t) i], g2[(size_t) i], 1.0e-6f);
        }

        beginTest ("KNN: k grows with spread and reaches every speaker");
        for (const auto& layout : allPresets())
        {
            const KnnPanner panner (layout);
            const auto base = std::min (layout.is3D() ? 3 : 2, layout.size());
            int previous = 0;

            for (int step = 0; step <= 10; ++step)
            {
                const auto spread = step / 10.0;
                panner.computeGains (33.0, 10.0, spread, g.data());
                const auto active = countNonZero (g.data(), layout.size());

                expectEquals (active, panner.getNumNeighbours (spread), layout.name);
                expect (active >= previous, layout.name);
                previous = active;
            }

            expectEquals (panner.getNumNeighbours (0.0), base, layout.name);
            expectEquals (previous, layout.size(), layout.name);
        }

        beginTest ("HOA: automatic order");
        {
            expectEquals (HoaPanner (*makePresetLayout ("4.0")).getOrder(), 1);
            expectEquals (HoaPanner (*makePresetLayout ("5.0")).getOrder(), 2);
            expectEquals (HoaPanner (*makePresetLayout ("32ch")).getOrder(), 7);
            expectEquals (HoaPanner (*makePresetLayout ("5.0.2")).getOrder(), 1);
            expectEquals (HoaPanner (*makePresetLayout ("7.0.4")).getOrder(), 2);
            expectEquals (HoaPanner (*makePresetLayout ("9.0.6")).getOrder(), 2);
        }

        beginTest ("HOA: decoder power within +/-1 dB over 360 degrees on every circular layout");
        double worstDeviation = 0.0;
        for (int n = 4; n <= 32; ++n)
        {
            const auto layout = *makePresetLayout (juce::String (n) + "ch");
            const HoaPanner panner (layout);
            std::array<double, maxSpeakers> raw {};
            std::vector<double> powerDb;

            for (int az = 0; az < 360; ++az)
            {
                panner.computeRawGains ((double) az, 0.0, 0.0, raw.data());
                double p = 0.0;
                for (int i = 0; i < n; ++i)
                    p += raw[(size_t) i] * raw[(size_t) i];
                powerDb.push_back (10.0 * std::log10 (p));
            }

            const auto mean = std::accumulate (powerDb.begin(), powerDb.end(), 0.0) / (double) powerDb.size();
            const auto [lo, hi] = std::minmax_element (powerDb.begin(), powerDb.end());
            const auto deviation = std::max (mean - *lo, *hi - mean);
            expect (deviation <= 1.0, layout.name + " deviation " + juce::String (deviation, 3) + " dB");
            worstDeviation = std::max (worstDeviation, deviation);
        }
        logMessage ("  worst HOA power deviation: " + juce::String (worstDeviation, 3) + " dB");

        beginTest ("HOA: energy vector points at the source on circular layouts");
        double worstAngle = 0.0;
        for (int n = 4; n <= 32; ++n)
        {
            const auto layout = *makePresetLayout (juce::String (n) + "ch");
            const HoaPanner panner (layout);
            double worst = 0.0;

            for (int az = -180; az < 180; az += 3)
            {
                panner.computeGains ((double) az, 0.0, 0.0, g.data());
                worst = std::max (worst, std::abs (wrapAzimuth (energyVectorAzimuth (layout, g.data()) - az)));
            }

            expect (worst < 5.0, layout.name + " worst error " + juce::String (worst, 2) + " deg");
            worstAngle = std::max (worstAngle, worst);
        }
        logMessage ("  worst HOA energy-vector error: " + juce::String (worstAngle, 2) + " deg");
    }
};

static PannerTests pannerTests;
