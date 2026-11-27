#include "RenderTypes.h"
#include "DspUtils.h"

namespace locus
{
    Vec3 lateLineDirection (int line, int numLines, bool threeD) noexcept
    {
        if (! threeD)
            return directionFromAzEl (360.0 * (line + 0.5) / numLines, 0.0);

        const auto golden = pi * (3.0 - std::sqrt (5.0));
        const auto z = (line + 0.5) / numLines;
        const auto r = std::sqrt (1.0 - z * z);
        const auto phi = line * golden;
        return { r * std::cos (phi), r * std::sin (phi), z };
    }

    std::unique_ptr<LayoutState> LayoutState::create (const SpeakerLayout& layout, bool align)
    {
        auto s = std::make_unique<LayoutState>();
        s->layout = layout;
        s->align = align;
        s->numSpeakers = layout.size();
        s->threeD = layout.is3D();

        s->panners[(size_t) PanningChoice::angular]    = createPanner (PanningType::angular, layout);
        s->panners[(size_t) PanningChoice::nearest]    = createPanner (PanningType::nearest, layout);
        s->panners[(size_t) PanningChoice::ambisonics] = createPanner (PanningType::ambisonics, layout);

        const VbapPanner vbap (layout);
        std::array<float, maxSpeakers> g {};

        for (int hd = 0; hd < 2; ++hd)
        {
            const int numLines = hd == 0 ? 8 : 16;
            auto& gains = s->lateGains[(size_t) hd];
            gains.assign ((size_t) (numLines * s->numSpeakers), 0.0f);

            for (int l = 0; l < numLines; ++l)
            {
                const auto d = lateLineDirection (l, numLines, s->threeD);
                vbap.computeGains (azimuthOf (d), elevationOf (d), latePanSpread, g.data());
                std::copy (g.begin(), g.begin() + s->numSpeakers, gains.begin() + l * s->numSpeakers);
            }
        }

        const auto corrections = layout.computeAlign();
        for (int i = 0; i < s->numSpeakers; ++i)
        {
            s->deviceChannel.push_back (layout.speakers[(size_t) i].output - 1);
            s->alignDelaySeconds.push_back (align ? corrections[(size_t) i].delaySeconds : 0.0);
            s->alignGain.push_back (align ? (float) dbToGain (corrections[(size_t) i].gainDb) : 1.0f);
        }

        return s;
    }
}
