#include "CanvasView.h"
#include <uikit/Theme.h>

using namespace locus;
using namespace locus::ui;

namespace
{
    constexpr float paneMargin = 14.0f;
    constexpr float speakerSize = 14.0f;
    constexpr float sourceSize = 18.0f;
    constexpr float stereoDotSize = 6.0f;
    constexpr float wedgeRadius = 30.0f;
    constexpr float hitRadius = 12.0f;
    constexpr float vuWidth = 3.0f, vuGap = 2.0f;
}

CanvasView::CanvasView (AppController& c) : controller (c)
{
    setOpaque (true);
}

std::vector<CanvasView::Pane> CanvasView::getPanes() const
{
    const auto area = getLocalBounds().toFloat();
    std::vector<juce::Rectangle<float>> rects;

    if (controller.getCanvasView() == AppController::CanvasView::topFront)
    {
        auto left = area;
        const auto right = left.removeFromRight (area.getWidth() * 0.5f);
        rects = { left, right };
    }
    else
    {
        rects = { area };
    }

    std::vector<Pane> panes;
    for (size_t i = 0; i < rects.size(); ++i)
    {
        Pane p;
        p.bounds = rects[i];
        p.front = i == 1;
        p.centre = p.bounds.getCentre();
        p.scale = juce::jmax (1.0f, (juce::jmin (p.bounds.getWidth(), p.bounds.getHeight()) * 0.5f - paneMargin) / (float) controller.getZoom());
        panes.push_back (p);
    }
    return panes;
}

CanvasView::SourceView CanvasView::getSourceView (int slot) const
{
    SourceView v;
    v.info = controller.getSourceInfo (slot);
    v.azimuth = controller.getSourceValue (slot, SourceParam::azimuth);
    v.elevation = controller.getLayout().is3D() ? controller.getSourceValue (slot, SourceParam::elevation) : 0.0;
    v.distance = controller.getSourceValue (slot, SourceParam::distance);
    v.width = controller.getSourceValue (slot, SourceParam::width);
    v.orientation = controller.getSourceValue (slot, SourceParam::orientation);
    v.aperture = controller.getSourceValue (slot, SourceParam::aperture);
    v.position = directionFromAzEl (v.azimuth, v.elevation) * v.distance;
    return v;
}

//==============================================================================
void CanvasView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    const auto panes = getPanes();
    for (const auto& p : panes)
        drawPane (g, p);

    if (panes.size() > 1)
    {
        g.setColour (Colours::stroke);
        g.drawVerticalLine ((int) panes[1].bounds.getX(), 0.0f, (float) getHeight());
    }

    g.setColour (Colours::stroke);
    g.drawRect (getLocalBounds(), 1);

    drawReadout (g);
}

void CanvasView::drawPane (juce::Graphics& g, const Pane& pane)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (pane.bounds.toNearestInt());

    const auto& layout = controller.getLayout();

    // Selected source's near radius: a filled disc round the listener (top view).
    if (! pane.front)
    {
        const auto r = controller.getSourceValue (controller.getSelectedSlot(), SourceParam::nearRadius) * pane.scale;
        g.setColour (Colours::panel);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pane.centre));
    }

    // Distance rings: every 1 m, or every 5 / 10 m when zoomed out.
    const auto reach = juce::Point<float> (pane.bounds.getWidth(), pane.bounds.getHeight()).getDistanceFromOrigin() * 0.5f;
    double spacing = 1.0;
    if (spacing * pane.scale < 16.0f) spacing = 5.0;
    if (spacing * pane.scale < 16.0f) spacing = 10.0;

    g.setColour (Colours::stroke);
    for (double m = spacing; m * pane.scale < reach; m += spacing)
    {
        const auto r = (float) (m * pane.scale);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pane.centre), 1.0f);
    }

    g.drawLine ({ pane.bounds.getTopLeft(), pane.bounds.getBottomRight() }, 1.0f);
    g.drawLine ({ pane.bounds.getTopRight(), pane.bounds.getBottomLeft() }, 1.0f);

    if (pane.front)
        g.drawHorizontalLine ((int) pane.centre.y, pane.bounds.getX(), pane.bounds.getRight());

    // Speaker Radius ring
    if (! controller.isCustomSetup()) // Speaker Radius does not apply to Custom
    {
        const auto r = (float) controller.getSpeakerRadius() * pane.scale;
        g.setColour (Colours::textDim.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pane.centre), 1.0f);
    }

    // Listener
    g.setColour (Colours::text);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (pane.centre));

    // Speakers, their meters, then labels where they fit without touching anything.
    std::vector<juce::Rectangle<float>> occupied;
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> labels;
    const auto labelFont = Fonts::regular (Fonts::small);

    for (const auto& s : layout.speakers)
    {
        const auto box = juce::Rectangle<float> (speakerSize, speakerSize).withCentre (pane.toScreen (s.getPosition()));
        g.setColour (Colours::panel);
        g.fillRect (box);
        g.setColour (Colours::textDim);
        g.drawRect (box, 1.0f);
        occupied.push_back (box);

        if (controller.showCanvasMeters)
        {
            const auto level = juce::jlimit (0.0f, 1.0f, 1.0f - controller.getMeterDb (s.output - 1) / AppController::meterFloorDb);
            const auto bar = juce::Rectangle<float> (box.getRight() + vuGap, box.getY(), vuWidth, speakerSize);
            g.setColour (Colours::stroke);
            g.fillRect (bar);
            g.setColour (Colours::accentRoom);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * level));
            occupied.push_back (bar);
        }

        if (controller.showSpeakerLabels)
        {
            const auto w = std::ceil (Fonts::width (labelFont, s.label)) + 2.0f;
            labels.push_back ({ juce::Rectangle<float> (w, 14.0f).withCentre ({ box.getCentreX(), box.getBottom() + 9.0f }), s.label });
        }
    }

    // Sources (selected last, so it is drawn on top)
    auto slots = controller.getListedSlots();
    if (std::find (slots.begin(), slots.end(), controller.getSelectedSlot()) == slots.end())
        slots.push_back (controller.getSelectedSlot());
    std::stable_partition (slots.begin(), slots.end(), [this] (int s) { return s != controller.getSelectedSlot(); });

    for (const auto slot : slots)
    {
        const auto v = getSourceView (slot);
        const auto colour = Colours::source (slot).withMultipliedAlpha (v.info.active ? 1.0f : Colours::disabledAlpha);
        const auto centre = pane.toScreen (v.position);
        const bool isSelected = slot == controller.getSelectedSlot();

        if (isSelected && ! pane.front)
        {
            // Orientation (0 = facing the listener) and aperture, as an outline wedge.
            const auto facing = v.azimuth + 180.0 + v.orientation;
            juce::Path wedge;
            const auto box = juce::Rectangle<float> (wedgeRadius * 2.0f, wedgeRadius * 2.0f).withCentre (centre);
            if (v.aperture >= 359.0)
                wedge.addEllipse (box);
            else
                wedge.addPieSegment (box, (float) degToRad (facing - v.aperture * 0.5), (float) degToRad (facing + v.aperture * 0.5), 0.0f);
            g.setColour (colour);
            g.strokePath (wedge, juce::PathStrokeType (1.0f));
        }

        if (v.info.numChannels == 2)
        {
            for (const auto side : { -0.5, 0.5 })
            {
                const auto p = rotateWithSource (side * v.width, v.azimuth, v.elevation) * v.distance;
                g.setColour (colour);
                g.fillEllipse (juce::Rectangle<float> (stereoDotSize, stereoDotSize).withCentre (pane.toScreen (p)));
            }
        }

        const auto circle = juce::Rectangle<float> (sourceSize, sourceSize).withCentre (centre);
        g.setColour (colour);
        g.fillEllipse (circle);
        if (isSelected)
        {
            g.setColour (Colours::text);
            g.drawEllipse (circle.expanded (2.0f), 1.0f);
        }

        g.setColour (Colours::bg);
        g.setFont (Fonts::semibold (Fonts::small));
        g.drawText (juce::String (slot + 1), circle, juce::Justification::centred, false);
        occupied.push_back (circle.expanded (2.0f));
    }

    g.setFont (labelFont);
    g.setColour (Colours::textDim);
    for (const auto& [rect, text] : labels)
    {
        const bool fits = pane.bounds.contains (rect)
                       && std::none_of (occupied.begin(), occupied.end(), [&rect] (const auto& o) { return o.intersects (rect.expanded (3.0f, 0.0f)); });
        if (fits)
        {
            g.drawText (text, rect, juce::Justification::centred, false);
            occupied.push_back (rect);
        }
    }
}

void CanvasView::drawReadout (juce::Graphics& g)
{
    if (dragMode == DragMode::none || dragSlot < 0)
        return;

    const auto v = getSourceView (dragSlot);
    const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
    const auto text = "az " + juce::String (v.azimuth, 1) + deg + "  dist " + juce::String (v.distance, 2) + " m  el "
                    + juce::String (controller.getSourceValue (dragSlot, SourceParam::elevation), 1) + deg;

    const auto font = Fonts::regular (Fonts::small);
    const auto w = std::ceil (Fonts::width (font, text)) + 12.0f;
    const auto panes = getPanes();
    const auto& pane = panes[(size_t) juce::jlimit (0, (int) panes.size() - 1, dragPane)];
    const auto anchor = pane.toScreen (v.position);

    // Above-right of the source, kept inside the canvas.
    auto box = juce::Rectangle<float> (w, 20.0f).withPosition (anchor.x + 14.0f, anchor.y - 34.0f);
    box = box.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));

    g.setColour (Colours::panel);
    g.fillRoundedRectangle (box, (float) Metrics::controlCorner);
    g.setColour (Colours::stroke);
    g.drawRoundedRectangle (box, (float) Metrics::controlCorner, 1.0f);
    g.setColour (Colours::text);
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred, false);
}

//==============================================================================
std::optional<std::pair<int, int>> CanvasView::hitTest (juce::Point<float> p) const
{
    const auto panes = getPanes();
    auto slots = controller.getListedSlots();
    const auto selected = controller.getSelectedSlot();
    slots.insert (slots.begin(), selected); // the selected source wins when sources overlap

    for (int paneIndex = 0; paneIndex < (int) panes.size(); ++paneIndex)
    {
        if (! panes[(size_t) paneIndex].bounds.contains (p))
            continue;

        for (const auto slot : slots)
            if (panes[(size_t) paneIndex].toScreen (getSourceView (slot).position).getDistanceFrom (p) <= hitRadius)
                return std::pair { slot, paneIndex };
    }

    return std::nullopt;
}

void CanvasView::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    if (! hit)
        return;

    dragSlot = hit->first;
    dragPane = hit->second;
    controller.setSelectedSlot (dragSlot);

    const bool threeD = controller.getLayout().is3D();
    if (e.mods.isShiftDown() && threeD)  dragMode = DragMode::elevation;
    else if (e.mods.isAltDown())         dragMode = DragMode::orientation;
    else if (e.mods.isCommandDown())     dragMode = DragMode::aperture;
    else                                 dragMode = DragMode::position;

    const auto v = getSourceView (dragSlot);
    dragStart = e.position;
    startElevation = controller.getSourceValue (dragSlot, SourceParam::elevation);
    startAperture = v.aperture;
    startHorizontal = juce::jmax (0.05, v.distance * std::cos (degToRad (v.elevation)));
    touchedParams.clear();
    repaint();
}

void CanvasView::setDragged (int param, double value)
{
    controller.setSourceValue (dragSlot, param, (float) value, true);
    if (std::find (touchedParams.begin(), touchedParams.end(), param) == touchedParams.end())
        touchedParams.push_back (param);
}

void CanvasView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    const auto panes = getPanes();
    const auto& pane = panes[(size_t) juce::jlimit (0, (int) panes.size() - 1, dragPane)];
    const auto dy = e.position.y - dragStart.y;
    const bool threeD = controller.getLayout().is3D();
    const auto el = threeD ? controller.getSourceValue (dragSlot, SourceParam::elevation) : 0.0;

    switch (dragMode)
    {
        case DragMode::position:
        {
            const auto local = (e.position - pane.centre) / pane.scale;

            if (! pane.front)
            {
                const Vec3 horizontal { local.x, -local.y, 0.0 };
                const auto h = length (horizontal);
                setDragged (SourceParam::azimuth, azimuthOf (horizontal));
                setDragged (SourceParam::distance, juce::jlimit (0.0, 50.0, h / std::cos (degToRad (el))));
            }
            else
            {
                // Front view: move straight up or down at the same horizontal distance.
                const auto z = (double) -local.y;
                setDragged (SourceParam::elevation, juce::jlimit (-90.0, 90.0, radToDeg (std::atan2 (z, startHorizontal))));
                setDragged (SourceParam::distance, juce::jlimit (0.0, 50.0, std::hypot (startHorizontal, z)));
            }
            break;
        }

        case DragMode::elevation:
            setDragged (SourceParam::elevation, juce::jlimit (-90.0, 90.0, startElevation - dy * 0.5));
            break;

        case DragMode::orientation:
        {
            const auto v = getSourceView (dragSlot);
            const auto toMouse = e.position - pane.toScreen (v.position);
            if (toMouse.getDistanceFromOrigin() > 2.0f)
            {
                const auto facing = azimuthOf ({ toMouse.x, -toMouse.y, 0.0 });
                setDragged (SourceParam::orientation, wrapAzimuth (facing - (v.azimuth + 180.0)));
            }
            break;
        }

        case DragMode::aperture:
            setDragged (SourceParam::aperture, juce::jlimit (10.0, 360.0, startAperture - dy));
            break;

        case DragMode::none:
            break;
    }

    repaint();
}

void CanvasView::mouseUp (const juce::MouseEvent&)
{
    if (dragMode != DragMode::none && dragSlot >= 0)
        for (const auto param : touchedParams) // close the host gestures
            controller.setSourceValue (dragSlot, param, controller.getSourceValue (dragSlot, param), false);

    dragMode = DragMode::none;
    touchedParams.clear();
    repaint();
}

void CanvasView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! hitTest (e.position))
    {
        controller.resetZoom();
        repaint();
    }
}

void CanvasView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    controller.setZoom (controller.getZoom() * std::pow (1.15, -wheel.deltaY * 4.0));
    repaint();
}

void CanvasView::mouseMagnify (const juce::MouseEvent&, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        controller.setZoom (controller.getZoom() / scaleFactor);
    repaint();
}
