#pragma once

#include "../model/AppController.h"

/** The room view (spec §9.3): top view, or top + front for 3D setups. Shows the speakers,
    listener, sources, and the selected source's near radius, orientation and aperture.
    Drag a source to move it; Shift = elevation, Alt = orientation, Ctrl/Cmd = aperture;
    wheel or pinch zooms; double-click empty space resets the zoom. */
class CanvasView final : public juce::Component
{
public:
    explicit CanvasView (AppController&);

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    struct Pane
    {
        juce::Rectangle<float> bounds;
        bool front = false;
        juce::Point<float> centre;
        float scale = 1.0f; // pixels per metre

        juce::Point<float> toScreen (locus::Vec3 p) const noexcept
        {
            return { centre.x + (float) p.x * scale, centre.y - (float) (front ? p.z : p.y) * scale };
        }
    };

    struct SourceView
    {
        AppController::SourceInfo info;
        double azimuth, elevation, distance, width, orientation, aperture;
        locus::Vec3 position;
    };

    enum class DragMode { none, position, elevation, orientation, aperture };

    std::vector<Pane> getPanes() const;
    SourceView getSourceView (int slot) const;
    std::optional<std::pair<int, int>> hitTest (juce::Point<float>) const; // (slot, pane index)

    void drawPane (juce::Graphics&, const Pane&);
    void drawReadout (juce::Graphics&);
    void setDragged (int param, double value);

    AppController& controller;

    DragMode dragMode = DragMode::none;
    int dragSlot = -1, dragPane = 0;
    juce::Point<float> dragStart;
    double startElevation = 0.0, startAperture = 0.0, startHorizontal = 0.0;
    std::vector<int> touchedParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasView)
};
