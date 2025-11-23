#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Lightweight Run indicator rectangle used in the layout playground.
// - fixed logical size: 120 x 18 (pixels)
// - rotated clockwise by 45 degrees and drawn centered in this component
// - visible when not running (STOP / no clocks sent), hidden when running
// - can be associated with a dancer component so the dancer is placed behind it in z-order

class RunButton : public juce::Component
{
public:
    RunButton()
    {
        rectW = 120.0f;
        rectH = 18.0f;
        rotation = juce::MathConstants<float>::pi * 0.25f; // 45 degrees
        running = false;
        onClick = {};
        setInterceptsMouseClicks(true, true);
    }

    ~RunButton() override = default;

    // Set running state. When running==true the indicator is invisible.
    void setRunning(bool isRunning)
    {
        running = isRunning;
        setVisible(! running);
        repaint();
    }

    bool isRunning() const noexcept { return running; }

    // Optional: associate a dancer component so we can nudge z-order when requested.
    void setDancerComponent(juce::Component* d)
    {
        dancer = d;
        arrangeZOrder();
    }

    // If parent already contains the dancer, make sure dancer is placed behind this indicator.
    void arrangeZOrder()
    {
        if (auto* p = getParentComponent())
        {
            if (dancer && dancer->getParentComponent() == p)
            {
                // send dancer to back then ensure this indicator is behind other
                // sibling children (so ring wedges/chase draw above it). Using
                // toBack() places the indicator behind sibling children but
                // still keeps it above parent-painted content.
                dancer->toBack();
                toBack();
            }
        }
    }

    // Click callback: invoked when indicator is clicked.
    void setOnClick(std::function<void()> cb) { onClick = std::move(cb); }

    void parentHierarchyChanged() override
    {
        Component::parentHierarchyChanged();
        arrangeZOrder();
    }

    void resized() override
    {
        // Nothing to layout: we draw centered in paint().
    }

    void paint(juce::Graphics& g) override
    {
        if (running) return; // invisible by visibility, but guard anyway

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        // Rectangle centered at origin then transformed
        juce::Path p;
        const float w = rectW;
        const float h = rectH;
        p.addRectangle(-w * 0.5f, -h * 0.5f, w, h);

        // Transform: rotate around origin (rectangle is centered at origin),
        // then translate to the component centre. This ensures the rotation
        // pivot is the rectangle centre and avoids an offset when rotating.
        juce::AffineTransform t = juce::AffineTransform::rotation(rotation).translated(cx, cy);
        p.applyTransform(t);

        // Main indicator fill
        g.setColour(UiThemeColours::accent());
        g.fillPath(p);

        // (Diagnostics removed per user request)
    }

    // Static helper: draw the rotated rectangle at arbitrary centre coordinates.
    // Useful when the component isn't added as a child but the editor wants
    // to draw the indicator between dancer and donut.
    static void drawAt(juce::Graphics& g, float centreX, float centreY, bool running)
    {
        if (running) return; // indicator hidden when running
        const float w = rectW_static;
        const float h = rectH_static;
        juce::Path p;
        p.addRectangle(-w * 0.5f, -h * 0.5f, w, h);
        // Rotate around origin then translate so the rotation pivot is the rectangle centre.
        juce::AffineTransform t = juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f).translated(centreX, centreY);
        p.applyTransform(t);
        // Debug logging to inspect where the indicator is drawn at runtime.
        // Guarded to avoid noisy logs in production builds.
    #if JUCE_DEBUG
        juce::Logger::writeToLog("RunButton::drawAt centre: " + juce::String((int)std::round(centreX)) + "," + juce::String((int)std::round(centreY)));
    #endif
        g.setColour(UiThemeColours::accent());
        g.fillPath(p);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onClick) onClick();
    }

    // Helper to size this component to comfortably contain the rotated rectangle
    static juce::Rectangle<int> suggestedBoundsForCentre(int centreX, int centreY)
    {
        // The diagonal of the rectangle defines required bounding square.
        const float halfW = rectW_static * 0.5f;
        const float halfH = rectH_static * 0.5f;
        const float halfDiag = std::sqrt(halfW * halfW + halfH * halfH);
        const int size = (int) std::ceil(halfDiag * 2.0f) + 4; // small padding
        return juce::Rectangle<int>(centreX - size/2, centreY - size/2, size, size);
    }

private:
    bool running { false };
    juce::Component* dancer { nullptr };
    std::function<void()> onClick;

    float rectW;
    float rectH;
    float rotation;

    static inline constexpr float rectW_static = 120.0f;
    static inline constexpr float rectH_static = 18.0f;
};
