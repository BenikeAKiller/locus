#include "SpeakerEditor.h"
#include <session/SessionFile.h>
#include <uikit/LayoutChecker.h>
#include <map>

using namespace locus;
using namespace locus::ui;

namespace
{
    constexpr int gap = Metrics::grid;
    constexpr int visibleTableRows = 8;
    constexpr int headerHeight = 20, problemHeight = 18;
    constexpr float squareSize = 14.0f;

    enum Column { colIndex, colLabel, colX, colY, colZ, colAz, colEl, colDist, colOut, colDelay, colGain };

    const char* const columnNames[] { "#", "Label", "X", "Y", "Z", "Azimuth", "Elevation", "Distance", "Output", "Delay", "Gain" };

    juce::String degrees (double v) { return juce::String (v, 1) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }
    juce::String metres (double v)  { return juce::String (v, 2) + " m"; }

    bool isEditable (int column) { return column != colIndex && column != colDelay && column != colGain; }

    std::optional<double> parseNumber (const juce::String& text)
    {
        const auto numeric = text.trim().initialSectionContainingOnly ("+-.0123456789");
        if (numeric.isEmpty() || numeric == "-" || numeric == "+" || numeric == ".")
            return std::nullopt;
        return numeric.getDoubleValue();
    }

    void styleCell (juce::Label& l, juce::Colour colour)
    {
        l.setFont (Fonts::regular (Fonts::small));
        l.setColour (juce::Label::textColourId, colour);
        l.setBorderSize ({});
        l.setMinimumHorizontalScale (1.0f);
        l.setJustificationType (juce::Justification::centred);
    }
}

const int SpeakerEditor::columnWidths[SpeakerEditor::numColumns] { 24, 64, 52, 52, 52, 56, 60, 60, 48, 52, 52 };

//==============================================================================
struct SpeakerEditor::Row final : juce::Component
{
    Row()
    {
        for (int c = 0; c < numColumns; ++c)
        {
            auto& cell = cells[(size_t) c];
            styleCell (cell, isEditable (c) ? Colours::text : Colours::textDim);

            if (isEditable (c))
            {
                cell.setEditable (true, false, false);
                cell.onTextChange = [this, c] { if (onEdit) onEdit (c, cells[(size_t) c].getText()); };
                cell.onEditorShow = [this, c]
                {
                    if (onSelect) onSelect();
                    if (auto* editor = cells[(size_t) c].getCurrentTextEditor(); editor != nullptr && c == colLabel)
                        editor->setInputRestrictions (maxSpeakerLabelChars);
                };
            }
            else
            {
                cell.setInterceptsMouseClicks (false, false);
            }

            addAndMakeVisible (cell);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect(); }

    void paint (juce::Graphics& g) override
    {
        if (selected)
        {
            g.setColour (Colours::stroke);
            g.fillRect (getLocalBounds());
        }

        for (int c = 0; c < numColumns; ++c)
        {
            const auto r = cells[(size_t) c].getBounds().toFloat().reduced (1.0f, 2.0f);
            if (invalid[(size_t) c])
            {
                g.setColour (Colours::warn.withAlpha (0.35f));
                g.fillRoundedRectangle (r, (float) Metrics::controlCorner);
            }
            else if (isEditable (c))
            {
                g.setColour (Colours::panel);
                g.fillRoundedRectangle (r, (float) Metrics::controlCorner);
            }
        }
    }

    void resized() override
    {
        int x = 0;
        for (int c = 0; c < numColumns; ++c)
        {
            cells[(size_t) c].setBounds (x, 0, columnWidths[c], getHeight());
            x += columnWidths[c];
        }
    }

    std::array<juce::Label, numColumns> cells;
    std::array<bool, numColumns> invalid {};
    bool selected = false;
    std::function<void (int, const juce::String&)> onEdit;
    std::function<void()> onSelect;
};

//==============================================================================
class SpeakerEditor::Plot final : public juce::Component
{
public:
    Plot (SpeakerEditor& e, bool frontView) : editor (e), front (frontView) {}

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (Colours::bg);
        g.fillRect (area);

        const auto s = scale();
        const auto c = area.getCentre();
        const auto& layout = editor.working;

        // Grid lines every 1 m (5 or 10 m when dense), polar or cartesian.
        double step = 1.0;
        if (step * s < 14.0f) step = 5.0;
        if (step * s < 14.0f) step = 10.0;
        const auto stepPx = (float) step * s;
        g.setColour (Colours::stroke);

        if (editor.cartesian.getToggleState())
        {
            for (auto d = stepPx; d < area.getWidth(); d += stepPx)
            {
                g.drawVerticalLine ((int) (c.x + d), area.getY(), area.getBottom());
                g.drawVerticalLine ((int) (c.x - d), area.getY(), area.getBottom());
            }
            for (auto d = stepPx; d < area.getHeight(); d += stepPx)
            {
                g.drawHorizontalLine ((int) (c.y + d), area.getX(), area.getRight());
                g.drawHorizontalLine ((int) (c.y - d), area.getX(), area.getRight());
            }
        }
        else
        {
            const auto reach = juce::Point<float> (area.getWidth(), area.getHeight()).getDistanceFromOrigin() * 0.5f;
            for (auto r = stepPx; r < reach; r += stepPx)
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
            for (int a = 0; a < 360; a += 30)
                g.drawLine ({ c, c.getPointOnCircumference (reach, juce::degreesToRadians ((float) a)) }, 1.0f);
        }

        g.setColour (Colours::textDim.withAlpha (0.5f));
        g.drawHorizontalLine ((int) c.y, area.getX(), area.getRight());
        g.drawVerticalLine ((int) c.x, area.getY(), area.getBottom());

        // Title and scale bar take space first, so speaker numbers never cover them.
        const auto font = Fonts::regular (Fonts::small);
        g.setFont (font);
        std::vector<juce::Rectangle<float>> occupied;

        const auto title = juce::String (front ? "Front (x / z)" : "Top (x / y)");
        const auto titleBox = juce::Rectangle<float> (6.0f, 4.0f, std::ceil (Fonts::width (font, title)) + 2.0f, 14.0f);
        g.setColour (Colours::textDim);
        g.drawText (title, titleBox, juce::Justification::centredLeft, false);
        occupied.push_back (titleBox);

        double barMetres = 0.5;
        for (const auto candidate : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0 })
            if (candidate * s <= 100.0f)
                barMetres = candidate;
        const auto barPx = (float) barMetres * s;
        const auto barLabel = juce::String (barMetres, barMetres < 1.0 ? 1 : 0) + " m";
        const auto barBox = juce::Rectangle<float> (area.getRight() - 12.0f - barPx, area.getBottom() - 26.0f, barPx, 20.0f);
        g.setColour (Colours::textDim);
        g.drawText (barLabel, barBox.withHeight (14.0f), juce::Justification::centred, false);
        g.fillRect (barBox.getX(), barBox.getBottom() - 3.0f, barPx, 1.5f);
        occupied.push_back (barBox.expanded (0.0f, 2.0f).withWidth (juce::jmax (barPx, Fonts::width (font, barLabel))));

        // Speakers
        std::vector<std::pair<juce::Rectangle<float>, juce::String>> labels;
        for (int i = 0; i < layout.size(); ++i)
        {
            const auto box = juce::Rectangle<float> (squareSize, squareSize).withCentre (toScreen (layout.speakers[(size_t) i].getPosition()));
            const bool selected = i == editor.selectedRow;
            g.setColour (selected ? Colours::accentSource.withAlpha (Colours::plotFillAlpha) : Colours::panel);
            g.fillRect (box);
            g.setColour (selected ? Colours::text : Colours::textDim);
            g.drawRect (box, 1.0f);
            occupied.push_back (box);

            const auto text = juce::String (i + 1);
            labels.push_back ({ juce::Rectangle<float> (std::ceil (Fonts::width (font, text)) + 2.0f, 14.0f)
                                    .withCentre ({ box.getCentreX(), box.getBottom() + 9.0f }), text });
        }

        g.setColour (Colours::textDim);
        for (const auto& [rect, text] : labels)
            if (area.contains (rect) && std::none_of (occupied.begin(), occupied.end(), [&r = rect] (const auto& o) { return o.intersects (r.expanded (3.0f, 0.0f)); }))
            {
                g.drawText (text, rect, juce::Justification::centred, false);
                occupied.push_back (rect);
            }

        // Coordinates of the speaker being dragged
        if (dragIndex >= 0 && dragIndex < layout.size())
        {
            const auto p = layout.speakers[(size_t) dragIndex].getPosition();
            const auto text = "x " + juce::String (p.x, 2) + "  y " + juce::String (p.y, 2) + "  z " + juce::String (p.z, 2) + " m";
            const auto anchor = toScreen (p);
            auto box = juce::Rectangle<float> (std::ceil (Fonts::width (font, text)) + 12.0f, 20.0f).withPosition (anchor.x + 12.0f, anchor.y - 30.0f);
            box = box.constrainedWithin (area.reduced (2.0f));
            g.setColour (Colours::panel);
            g.fillRoundedRectangle (box, (float) Metrics::controlCorner);
            g.setColour (Colours::stroke);
            g.drawRoundedRectangle (box, (float) Metrics::controlCorner, 1.0f);
            g.setColour (Colours::text);
            g.drawText (text, box, juce::Justification::centred, false);
        }

        g.setColour (Colours::stroke);
        g.drawRect (area, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& layout = editor.working;
        for (int i = layout.size(); --i >= 0;)
            if (toScreen (layout.speakers[(size_t) i].getPosition()).getDistanceFrom (e.position) <= squareSize * 0.75f)
            {
                frozenScale = scale(); // keep the view still while dragging
                dragIndex = i;
                editor.selectRow (i);
                return;
            }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex < 0)
            return;

        auto& speaker = editor.working.speakers[(size_t) dragIndex];
        auto p = speaker.getPosition();
        const auto s = scale();
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto mx = (double) ((e.position.x - c.x) / s);
        const auto my = (double) ((c.y - e.position.y) / s);

        p.x = mx;
        (front ? p.z : p.y) = my;
        speaker.setPosition (p);
        editor.workingChanged (false); // applied on release, not on every step
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex >= 0)
        {
            dragIndex = -1;
            frozenScale = 0.0f;
            editor.workingChanged (true);
        }
    }

private:
    float scale() const
    {
        if (frozenScale > 0.0f)
            return frozenScale;

        double furthest = 1.0;
        for (const auto& s : editor.working.speakers)
            furthest = std::max (furthest, s.distance);

        const auto half = juce::jmin (getWidth(), getHeight()) * 0.5f - 16.0f;
        return juce::jmax (1.0f, half / (float) (furthest * 1.25));
    }

    juce::Point<float> toScreen (Vec3 p) const
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto s = scale();
        return { c.x + (float) p.x * s, c.y - (float) (front ? p.z : p.y) * s };
    }

    SpeakerEditor& editor;
    const bool front;
    int dragIndex = -1;
    float frozenScale = 0.0f;
};

//==============================================================================
SpeakerEditor::SpeakerEditor (AppController& c)
    : controller (c), cartesian ("Cartesian", Colours::accentSource)
{
    for (auto* b : { &add, &remove, &load, &save })
        addAndMakeVisible (b);

    add.onClick = [this] { addSpeaker(); };
    remove.onClick = [this] { removeSpeaker(); };
    load.onClick = [this] { loadFile(); };
    save.onClick = [this] { saveFile(); };

    fromPreset.setTextWhenNothingSelected ("Start from preset");
    juce::String group;
    const auto& presets = getSpeakerPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].group != group)
        {
            group = presets[(size_t) i].group;
            fromPreset.addSectionHeading (group);
        }
        fromPreset.addItem (presets[(size_t) i].name, i + 1);
    }
    fromPreset.onChange = [this]
    {
        const auto index = fromPreset.getSelectedId() - 1;
        fromPreset.setSelectedId (0, juce::dontSendNotification);
        if (index >= 0 && index < (int) getSpeakerPresets().size())
            if (auto preset = makePresetLayout (getSpeakerPresets()[(size_t) index].name, controller.getSpeakerRadius()))
                setWorkingLayout (*preset);
    };
    addAndMakeVisible (fromPreset);

    cartesian.onClick = [this] { topPlot->repaint(); frontPlot->repaint(); };
    addAndMakeVisible (cartesian);

    styleCell (problem, Colours::warn);
    problem.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (problem);

    for (int column = 0; column < numColumns; ++column)
    {
        styleCell (header[(size_t) column], Colours::textDim);
        header[(size_t) column].setText (columnNames[column], juce::dontSendNotification);
        addAndMakeVisible (header[(size_t) column]);
    }

    viewport.setViewedComponent (&tableContent, false);
    viewport.setScrollBarsShown (true, false);
    LayoutChecker::markOverflow (tableContent);
    addAndMakeVisible (viewport);

    topPlot = std::make_unique<Plot> (*this, false);
    frontPlot = std::make_unique<Plot> (*this, true);
    addAndMakeVisible (*topPlot);
    addAndMakeVisible (*frontPlot);
}

SpeakerEditor::~SpeakerEditor() = default;

void SpeakerEditor::open()
{
    working = controller.getCustomLayout();
    selectedRow = 0;
    rebuildRows();
    workingChanged (false);
}

void SpeakerEditor::setWorkingLayout (const SpeakerLayout& layout)
{
    working = layout;
    working.name = customSetupName;
    selectedRow = 0;
    rebuildRows();
    workingChanged (true);
}

void SpeakerEditor::workingChanged (bool apply)
{
    const auto problems = working.validate();
    problemText = problems.isEmpty() ? juce::String()
                                     : problems[0] + (problems.size() > 1 ? " (and " + juce::String (problems.size() - 1) + " more)" : juce::String())
                                         + ". Not applied until fixed.";
    problem.setText (Fonts::elide (problem.getFont(), problemText, (float) problem.getWidth()), juce::dontSendNotification);

    if (apply && problems.isEmpty())
        controller.setCustomLayout (working);

    if ((int) rows.size() != working.size())
        rebuildRows();
    else
        updateRows();

    add.setEnabled (working.size() < maxSpeakers);
    add.setAlpha (add.isEnabled() ? 1.0f : Colours::disabledAlpha);
    remove.setEnabled (working.size() > 1);
    remove.setAlpha (remove.isEnabled() ? 1.0f : Colours::disabledAlpha);

    topPlot->repaint();
    frontPlot->repaint();
}

void SpeakerEditor::rebuildRows()
{
    rows.clear();
    for (int i = 0; i < working.size(); ++i)
    {
        auto row = std::make_unique<Row>();
        row->onSelect = [this, i] { selectRow (i); };
        row->onEdit = [this, i] (int column, const juce::String& text) { editCell (i, column, text); };
        tableContent.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    int width = 0;
    for (const auto w : columnWidths)
        width += w;

    tableContent.setSize (width, (int) rows.size() * Metrics::rowHeight);
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds (0, i * Metrics::rowHeight, width, Metrics::rowHeight);

    updateRows();
}

void SpeakerEditor::updateRows()
{
    const auto corrections = working.computeAlign();
    std::map<int, int> outputCount;
    for (const auto& s : working.speakers)
        ++outputCount[s.output];

    for (int i = 0; i < (int) rows.size() && i < working.size(); ++i)
    {
        auto& row = *rows[(size_t) i];
        const auto& s = working.speakers[(size_t) i];
        const auto p = s.getPosition();

        const juce::String texts[numColumns] {
            juce::String (i + 1), s.label, metres (p.x), metres (p.y), metres (p.z),
            degrees (s.azimuth), degrees (s.elevation), metres (s.distance), juce::String (s.output),
            juce::String (corrections[(size_t) i].delaySeconds * 1000.0, 1) + " ms",
            juce::String (corrections[(size_t) i].gainDb, 1) + " dB"
        };

        for (int c = 0; c < numColumns; ++c)
            if (! row.cells[(size_t) c].isBeingEdited())
                row.cells[(size_t) c].setText (texts[c], juce::dontSendNotification);

        row.invalid.fill (false);
        row.invalid[colOut] = outputCount[s.output] > 1 || s.output < 1 || s.output > maxSpeakers;
        row.invalid[colDist] = s.distance < minSpeakerDistance || s.distance > maxSpeakerDistance;
        row.selected = i == selectedRow;
        row.repaint();
    }
}

void SpeakerEditor::selectRow (int index)
{
    selectedRow = juce::jlimit (0, juce::jmax (0, working.size() - 1), index);
    updateRows();
    topPlot->repaint();
    frontPlot->repaint();
}

void SpeakerEditor::editCell (int row, int column, const juce::String& text)
{
    if (row < 0 || row >= working.size())
        return;

    auto& s = working.speakers[(size_t) row];

    if (column == colLabel)
    {
        s.label = text.trim().substring (0, maxSpeakerLabelChars);
    }
    else if (const auto v = parseNumber (text))
    {
        auto p = s.getPosition();
        switch (column)
        {
            case colX:    p.x = *v; s.setPosition (p); break;
            case colY:    p.y = *v; s.setPosition (p); break;
            case colZ:    p.z = *v; s.setPosition (p); break;
            case colAz:   s.azimuth = wrapAzimuth (*v); break;
            case colEl:   s.elevation = juce::jlimit (-90.0, 90.0, *v); break;
            case colDist: s.distance = juce::jmax (0.01, *v); break;
            case colOut:  s.output = juce::roundToInt (*v); break;
            default: break;
        }
    }

    workingChanged (true);
}

void SpeakerEditor::addSpeaker()
{
    if (working.size() >= maxSpeakers)
        return;

    int output = 1;
    while (std::any_of (working.speakers.begin(), working.speakers.end(), [output] (const Speaker& s) { return s.output == output; }))
        ++output;

    Speaker s;
    s.label = juce::String (working.size() + 1);
    s.distance = controller.getSpeakerRadius();
    s.output = output;
    working.speakers.push_back (s);
    selectedRow = working.size() - 1;
    workingChanged (true);
}

void SpeakerEditor::removeSpeaker()
{
    if (working.size() <= 1)
        return;

    working.speakers.erase (working.speakers.begin() + juce::jlimit (0, working.size() - 1, selectedRow));
    selectedRow = juce::jmin (selectedRow, working.size() - 1);
    workingChanged (true);
}

void SpeakerEditor::loadFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load speaker setup", juce::File(), juce::String ("*") + speakerFileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (auto json = readJsonFile (file, error))
            if (auto layout = speakersFromJson (*json, error))
            {
                setWorkingLayout (*layout);
                return;
            }

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load speaker setup", error);
    });
}

void SpeakerEditor::saveFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Save speaker setup", juce::File(), juce::String ("*") + speakerFileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (! writeJsonFile (file.withFileExtension (speakerFileExtension), speakersToJson (working), error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save speaker setup", error);
    });
}

//==============================================================================
void SpeakerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
}

void SpeakerEditor::resized()
{
    auto area = getLocalBounds();

    auto buttons = area.removeFromTop (Metrics::rowHeight);
    add.setBounds (buttons.removeFromLeft (56));
    buttons.removeFromLeft (gap);
    remove.setBounds (buttons.removeFromLeft (80));
    buttons.removeFromLeft (gap);
    fromPreset.setBounds (buttons.removeFromLeft (168));
    buttons.removeFromLeft (gap);
    load.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (gap);
    save.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (16);
    cartesian.setBounds (buttons.removeFromLeft (juce::jmin (buttons.getWidth(), cartesian.getIdealWidth())));

    area.removeFromTop (gap);
    problem.setBounds (area.removeFromTop (problemHeight));
    problem.setText (Fonts::elide (problem.getFont(), problemText, (float) problem.getWidth()), juce::dontSendNotification);

    int tableWidth = 0;
    for (const auto w : columnWidths)
        tableWidth += w;

    area.removeFromTop (4);
    auto headerRow = area.removeFromTop (headerHeight);
    for (int c = 0; c < numColumns; ++c)
        header[(size_t) c].setBounds (headerRow.removeFromLeft (columnWidths[c]));

    viewport.setBounds (area.removeFromTop (visibleTableRows * Metrics::rowHeight)
                            .withWidth (juce::jmin (area.getWidth(), tableWidth + viewport.getScrollBarThickness())));

    area.removeFromTop (gap);
    const auto half = (area.getWidth() - gap) / 2;
    topPlot->setBounds (area.removeFromLeft (half));
    area.removeFromLeft (gap);
    frontPlot->setBounds (area);
}
