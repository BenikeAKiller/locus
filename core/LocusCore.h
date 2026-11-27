#pragma once

#include <juce_core/juce_core.h>

namespace locus
{
    /** Product version, from the top-level CMake project version. */
    juce::String getVersionString();

    /** Maximum counts fixed by the spec. */
    constexpr int maxSources  = 16;
    constexpr int maxSpeakers = 32;
}
