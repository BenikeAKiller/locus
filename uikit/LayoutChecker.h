#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace locus::ui::LayoutChecker
{
    /** Allows a component to overlap its siblings (e.g. a floating readout). */
    void markOverlay (juce::Component&);

    /** Allows a component to extend past its parent (e.g. content scrolled in a Viewport). */
    void markOverflow (juce::Component&);

    /** Walks the visible component tree under root and reports every violation of the layout
        rules (spec §9.1): a child outside its parent, visible siblings that intersect, and text
        in a Label, TextButton, ComboBox or ToggleDot wider or taller than its bounds.
        Returns an empty list when the layout is clean. */
    juce::StringArray check (juce::Component& root);
}
