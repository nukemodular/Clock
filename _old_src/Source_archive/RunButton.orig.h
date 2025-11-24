// RunButton.h (archived original wrapper)
// Compatibility wrapper: definitions moved to UiComponents.h
#pragma once
#include "UiComponents.h"

#pragma once

// Archived original RunButton implementation (copied from UiComponents.h)
// Kept here for safety in case restoration is required.

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeels.h"

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

    void setRunning(bool isRunning)
    {
        running = isRunning;
        setVisible(! running);
        repaint();
    }

    bool isRunning() const noexcept { return running; }

    void setDancerComponent(juce::Component* d)
    {
        dancer = d;
        arrangeZOrder();
    }

    void arrangeZOrder()
    {
        if (auto* p = getParentComponent())
        {
            if (dancer && dancer->getParentComponent() == p)
            {
                dancer->toBack();
                toBack();
            }
        }
    }

    void setOnClick(std::function<void()> cb) { onClick = std::move(cb); }

    void parentHierarchyChanged() override
    {
        Component::parentHierarchyChanged();
        arrangeZOrder();
    }

    void resized() override {}

    void paint(juce::Graphics& g) override
    {
        if (running) return;
        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        juce::Path p;
        const float w = rectW;
        const float h = rectH;
        p.addRectangle(-w * 0.5f, -h * 0.5f, w, h);
        juce::AffineTransform t = juce::AffineTransform::rotation(rotation).translated(cx, cy);
        p.applyTransform(t);
        g.setColour(UiThemeColours::accent());
        g.fillPath(p);
    }

    static void drawAt(juce::Graphics& g, float centreX, float centreY, bool running)
    {
        if (running) return;
        const float w = rectW_static;
        const float h = rectH_static;
        juce::Path p;
        p.addRectangle(-w * 0.5f, -h * 0.5f, w, h);
        juce::AffineTransform t = juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f).translated(centreX, centreY);
        p.applyTransform(t);
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

    static juce::Rectangle<int> suggestedBoundsForCentre(int centreX, int centreY)
    {
        const float halfW = rectW_static * 0.5f;
        const float halfH = rectH_static * 0.5f;
        const float halfDiag = std::sqrt(halfW * halfW + halfH * halfH);
        const int size = (int) std::ceil(halfDiag * 2.0f) + 4;
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
