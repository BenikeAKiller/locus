#pragma once

#include "Theme.h"

namespace locus::ui
{
    /** On/off toggle drawn as a 16 px dot (filled = on) followed by its label (spec §9.1). */
    class ToggleDot : public juce::Button
    {
    public:
        ToggleDot (const juce::String& text, juce::Colour accent);

        /** Width that shows the dot and the whole label. */
        int getIdealWidth() const;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void enablementChanged() override;

    private:
        juce::Colour accent;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleDot)
    };
}
