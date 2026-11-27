#include "LayoutChecker.h"
#include "LocusLookAndFeel.h"
#include "ToggleDot.h"

namespace locus::ui::LayoutChecker
{
    namespace
    {
        const juce::Identifier overlayFlag ("locusLayoutOverlay");
        const juce::Identifier overflowFlag ("locusLayoutOverflow");

        juce::String describe (const juce::Component& c)
        {
            juce::String s = c.getName().isNotEmpty() ? c.getName() : juce::String (typeid (c).name()).fromLastOccurrenceOf (" ", false, false);

            if (auto* l = dynamic_cast<const juce::Label*> (&c))
                s << " \"" << l->getText() << "\"";
            else if (auto* b = dynamic_cast<const juce::Button*> (&c))
                s << " \"" << b->getButtonText() << "\"";

            return s;
        }

        juce::String path (const juce::Component& c, const juce::Component& root)
        {
            juce::StringArray parts;
            for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
                parts.insert (0, describe (*p));
            return parts.joinIntoString (" / ");
        }

        constexpr float tolerance = 0.5f;

        void checkText (juce::Component& c, juce::Component& root, juce::StringArray& out)
        {
            if (auto* l = dynamic_cast<juce::Label*> (&c))
            {
                if (l->getText().isEmpty() || l->isBeingEdited())
                    return;

                const auto font = l->getFont();
                const auto border = l->getBorderSize();
                const auto textWidth = Fonts::width (font, l->getText());
                const auto available = (float) (l->getWidth() - border.getLeftAndRight());

                if (textWidth > available + tolerance)
                    out.add (path (c, root) + ": text " + juce::String (textWidth, 1) + " px wide in " + juce::String (available, 1) + " px");

                if (font.getHeight() > (float) (l->getHeight() - border.getTopAndBottom()) + tolerance)
                    out.add (path (c, root) + ": text " + juce::String (font.getHeight(), 1) + " px tall in " + juce::String (l->getHeight()) + " px");
            }
            else if (auto* dot = dynamic_cast<ToggleDot*> (&c))
            {
                if (dot->getIdealWidth() > dot->getWidth())
                    out.add (path (c, root) + ": needs " + juce::String (dot->getIdealWidth()) + " px, has " + juce::String (dot->getWidth()));
            }
            else if (auto* b = dynamic_cast<juce::TextButton*> (&c))
            {
                const auto textWidth = Fonts::width (Fonts::regular (Fonts::normal), b->getButtonText());
                const auto available = (float) (b->getWidth() - 2 * LocusLookAndFeel::buttonTextPadding);
                if (textWidth > available + tolerance)
                    out.add (path (c, root) + ": text " + juce::String (textWidth, 1) + " px wide in " + juce::String (available, 1) + " px");
            }
        }

        void walk (juce::Component& c, juce::Component& root, juce::StringArray& out)
        {
            checkText (c, root, out);

            std::vector<juce::Component*> visible;
            for (auto* child : c.getChildren())
                if (child->isVisible())
                    visible.push_back (child);

            for (auto* child : visible)
            {
                if (! child->getProperties().contains (overflowFlag)
                    && ! c.getLocalBounds().contains (child->getBounds()))
                    out.add (path (*child, root) + ": " + child->getBounds().toString() + " outside parent " + c.getLocalBounds().toString());
            }

            for (size_t i = 0; i < visible.size(); ++i)
                for (size_t j = i + 1; j < visible.size(); ++j)
                {
                    auto* a = visible[i];
                    auto* b = visible[j];
                    if (a->getProperties().contains (overlayFlag) || b->getProperties().contains (overlayFlag))
                        continue;

                    if (a->getBounds().intersects (b->getBounds()))
                        out.add (path (*a, root) + " overlaps " + describe (*b));
                }

            for (auto* child : visible)
                walk (*child, root, out);
        }
    }

    void markOverlay (juce::Component& c)  { c.getProperties().set (overlayFlag, true); }
    void markOverflow (juce::Component& c) { c.getProperties().set (overflowFlag, true); }

    juce::StringArray check (juce::Component& root)
    {
        juce::StringArray out;
        walk (root, root, out);
        return out;
    }
}
