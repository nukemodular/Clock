// Original StepOffsetMenu.h (archived)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiTheme.h"

// Animated radial step selector button (1..16)
// - Base: 60x60 kAccent circle with inner (reduced by 10) kBase
// - Cyan number centered showing selected step
// - On click: shows 16 small ring options around, with simple expand/collapse animation
// - onStepChanged callback invoked when a step is selected
class AnimatedStepOffsetMenu : public juce::Component, private juce::Timer
{
public:
    AnimatedStepOffsetMenu()
    {
        setInterceptsMouseClicks(true, true);
        startTimerHz(60);
    }

    void setColours(juce::Colour accent, juce::Colour base, juce::Colour cyan)
    {
        kAccent = accent; kBase = base; kCyan = cyan; repaint();
    }

    void setStep(int newStep)
    {
        newStep = juce::jlimit(1, 16, newStep);
        if (selectedStep != newStep)
        {
            selectedStep = newStep;
            repaint();
        }
    }
    int getStep() const { return selectedStep; }

    std::function<void(int)> onStepChanged;

    void setClickThroughRect(juce::Rectangle<int> localRect)
    {
        clickThroughRect = localRect;
        hasClickThrough = true;
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();

        const float outerD = 50.0f;
        const float innerReduce = 10.0f;
        juce::Rectangle<float> outer(center.x - outerD * 0.5f, center.y - outerD * 0.5f, outerD, outerD);
        juce::Rectangle<float> inner = outer.reduced(innerReduce);

        g.setColour(kAccent); g.fillEllipse(outer);
        g.setColour(kBase);   g.fillEllipse(inner);

        if (openAmount > 0.01f) {
            drawOptions(g, center, outerD - 20.0f);

        }

        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", 16.0f, juce::Font::bold)));
        g.drawFittedText(juce::String(selectedStep), outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = kBaseD;
        const float dx = e.x - center.x;
        const float dy = e.y - center.y;
        const float dist2 = dx*dx + dy*dy;
        const float baseR = (baseD * 0.5f);
        if (dist2 <= baseR * baseR)
        {
            opening = !menuOpen;
            closing = menuOpen;
            menuOpen = !menuOpen;
            if (menuOpen)
                hoverIndex = hitTestOptionHover(e.position);
            return;
        }
        if (opening) return;
        const auto clickPos = e.position;
        int hit = hitTestOptionHover(clickPos);
        if (hit < 1)
            hit = hitTestOption(clickPos);
        if (hit < 1 && hoverIndex >= 1)
            hit = hoverIndex;
        if (hit >= 1 && hit <= 16)
        {
            selectedStep = hit;
            if (onStepChanged)
                onStepChanged(selectedStep);
            opening = false; closing = true; menuOpen = false;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (! menuOpen || openAmount <= 0.01f) return;

        {
            auto r = getLocalBounds().toFloat();
            auto center = r.getCentre();
            const float baseD = 50.0f;
            const float itemD = 20.0f;
            const float itemR = itemD * 0.5f;
            const float s0 = 1.30f;
            const float maxScaleR = itemR * s0;
            const float minRadius = baseD * 0.5f - 2.0f;
            const float tighten = 4.0f * s0;
            const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR + 3.0f - tighten;
            const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

            const float baseRadiusFromCenter = baseD * 0.5f;
            const float distanceFactor       = 3.5f;
            const float outerBound           = radius + maxScaleR + distanceFactor * baseRadiusFromCenter;

            const float dist = center.getDistanceFrom(e.position);
            if (dist > outerBound)
            {
                opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
                return;
            }
        }

        const int idx = hitTestOptionHover(e.position);
        if (idx != hoverIndex)
        {
            hoverIndex = idx;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (menuOpen)
        {
            auto globalPos = juce::Desktop::getInstance().getMousePosition();
            auto localPos = getLocalPoint(nullptr, globalPos).toFloat();

            auto r = getLocalBounds().toFloat();
            auto center = r.getCentre();
            const float baseD = kBaseD;
            const float itemD = kOptionItemD;
            const float itemR = itemD * 0.5f;
            const float s0 = kHoverScale;
            const float maxScaleR = itemR * s0;
            const float minRadius = baseD * 0.5f - 2.0f;
            const float tighten = kMouseTightenMul * s0;
            const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR + 3.0f - tighten;
            const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

            const float baseRadiusFromCenter = baseD * 0.5f;
            const float distanceFactor = 3.5f;
            const float outerBound = radius + maxScaleR + distanceFactor * baseRadiusFromCenter;

            const float slack = 6.0f;
            const float dist = center.getDistanceFrom(localPos);
            if (dist <= outerBound + slack)
            {
                return;
            }

            opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
            return;
        }

        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void resized() override { }

    bool hitTest(int x, int y) override
    {
        if (! menuOpen && openAmount <= 0.01f)
        {
            if (hasClickThrough && clickThroughRect.contains(x, y))
                return false;
            auto lb = getLocalBounds().toFloat();
            auto c  = lb.getCentre();
            const float outerR = kOuterBaseR;
            const float dx = (float) x - c.x;
            const float dy = (float) y - c.y;
            const bool insideBase = (dx*dx + dy*dy) <= (outerR * outerR);
            return insideBase;
        }
        return true;
    }
};
