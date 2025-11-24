// Auto-backup of Source/StepOffsetMenu.h — copied on 2025-11-24
// To restore: copy this file back to Source/StepOffsetMenu.h and re-run CMake

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

    // Color theme (defaults match PluginEditor)
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

    // Allow parent to define a click-through area (in this component's local coords)
    // When menu is closed, mouse events inside this rect will be ignored so controls behind remain clickable.
    void setClickThroughRect(juce::Rectangle<int> localRect)
    {
        clickThroughRect = localRect;
        hasClickThrough = true;
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();

        // Base button at center: 60x60 outer, inner reduced by 10
        const float outerD = 50.0f;
        const float innerReduce = 10.0f;
        juce::Rectangle<float> outer(center.x - outerD * 0.5f, center.y - outerD * 0.5f, outerD, outerD);
        juce::Rectangle<float> inner = outer.reduced(innerReduce);

        // Base button
        g.setColour(kAccent); g.fillEllipse(outer);
        g.setColour(kBase);   g.fillEllipse(inner);


        // Draw options if open
        if (openAmount > 0.01f) {
            // g.setColour(juce::Colours::black.withAlpha(0.2f));
            // g.fillEllipse(outer);
            drawOptions(g, center, outerD - 20.0f);

        }

        // Number
        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", 16.0f, juce::Font::bold)));
        g.drawFittedText(juce::String(selectedStep), outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Toggle menu if clicked within base circle
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = kBaseD;
        const float dx = e.x - center.x;
        const float dy = e.y - center.y;
        const float dist2 = dx*dx + dy*dy;
        const float baseR = (baseD * 0.5f);
        if (dist2 <= baseR * baseR)
        {
            // Toggle open/closed on mouseDown for faster response
            opening = !menuOpen;
            closing = menuOpen;
            menuOpen = !menuOpen;
            // If we just opened, immediately set hoverIndex based on pointer so selection
            // on the following mouseUp prefers the visually-hovered item.
            if (menuOpen)
                hoverIndex = hitTestOptionHover(e.position);
            return;
        }
        // If we're in the opening animation just ignore this mouseUp so
        // a click that triggered opening doesn't immediately select/collapse.
        if (opening) return;
        const auto clickPos = e.position;
        // Prefer the hover-aware test first (accounts for outward-shifted visuals)
        int hit = hitTestOptionHover(clickPos);
        if (hit < 1) // if hover-aware missed, fall back to the geometric hit test
            hit = hitTestOption(clickPos);
        // If both tests missed due to timing/jitter, but we have a hoverIndex, prefer it
        if (hit < 1 && hoverIndex >= 1)
            hit = hoverIndex;
        if (hit >= 1 && hit <= 16)
        {
            selectedStep = hit;
            if (onStepChanged)
                onStepChanged(selectedStep);
            // Collapse
            opening = false; closing = true; menuOpen = false;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (! menuOpen || openAmount <= 0.01f) return;

        // Auto-close if pointer leaves outer bounds of option ring
        {
            auto r = getLocalBounds().toFloat();
            auto center = r.getCentre();
            const float baseD = 50.0f;
            const float itemD = 20.0f; // matches drawOptions
            const float itemR = itemD * 0.5f;
            const float s0 = 1.30f;    // max scale (hover) — slightly bigger
            const float maxScaleR = itemR * s0;
            const float minRadius = baseD * 0.5f - 2.0f;
            const float tighten = 4.0f * s0; // mirror drawOptions tightening
            const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR + 3.0f - tighten;
            const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius),
                                              minRadius + (maxRadius - minRadius) * openAmount);

            // Allow more travel before collapsing (scaled up for bigger bounds)
            const float baseRadiusFromCenter = baseD * 0.5f;
            const float distanceFactor       = 3.5f; // was effectively ~1x
            const float outerBound           = radius + maxScaleR + distanceFactor * baseRadiusFromCenter;

            const float dist = center.getDistanceFrom(e.position);
            if (dist > outerBound)
            {
                opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
                return; // don't process hover when closing
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
            // When menu is open, avoid immediately closing on transient mouseExit events.
            // Query the current global mouse position and only close if the pointer
            // is actually outside an expanded interactive area around the ring.
            auto globalPos = juce::Desktop::getInstance().getMousePosition();
            auto localPos = getLocalPoint(nullptr, globalPos).toFloat();

            // Recompute the same bounds used in mouseMove to determine an outer interactive radius.
            auto r = getLocalBounds().toFloat();
            auto center = r.getCentre();
            const float baseD = kBaseD;
            const float itemD = kOptionItemD; // matches mouseMove's itemD
            const float itemR = itemD * 0.5f;
            const float s0 = kHoverScale;
            const float maxScaleR = itemR * s0;
            const float minRadius = baseD * 0.5f - 2.0f;
            const float tighten = kMouseTightenMul * s0;
            const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR + 3.0f - tighten;
            const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius),
                                              minRadius + (maxRadius - minRadius) * openAmount);

            const float baseRadiusFromCenter = baseD * 0.5f;
            const float distanceFactor = 3.5f;
            const float outerBound = radius + maxScaleR + distanceFactor * baseRadiusFromCenter;

            const float slack = 6.0f; // allow a small safety margin
            const float dist = center.getDistanceFrom(localPos);
            if (dist <= outerBound + slack)
            {
                // Pointer still within an expanded interactive region — ignore exit.
                return;
            }

            // Otherwise, actually close the menu.
            opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
            return;
        }

        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void resized() override { }

    bool hitTest(int x, int y) override
    {
        // When menu closed, only consume clicks inside the base button circle; let others fall through.
        if (! menuOpen && openAmount <= 0.01f)
        {
            // Allow explicit click-through rectangle to pass events
            if (hasClickThrough && clickThroughRect.contains(x, y))
                return false;
            auto lb = getLocalBounds().toFloat();
            auto c  = lb.getCentre();
            const float outerR = kOuterBaseR; // matches paint outerD / 2
            const float dx = (float) x - c.x;
            const float dy = (float) y - c.y;
            const bool insideBase = (dx*dx + dy*dy) <= (outerR * outerR);
            return insideBase; // only ring center blocks
        }
        // When menu open, retain default behaviour (capture for option selection)
        return true;
    }

private:
    // Animation state
    bool menuOpen { false };
    bool opening  { false };
    bool closing  { false };
    float openAmount { 0.0f }; // 0..1

    int selectedStep { 1 };
    int hoverIndex   { -1 }; // 1..16 when hovering, else -1

    // Optional click-through region (in local coords) where this component should not consume mouse when closed
    juce::Rectangle<int> clickThroughRect {};
    bool hasClickThrough { false };

    // Local theme colours (defaults match PluginEditor)
    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    // Use centralized UiLayout values so changes there take effect for StepOffsetMenu
    static constexpr float kBaseInward        = UiLayout::kBaseInward; // px overall ring inward at full open
    static constexpr float kInwardSmallShift  = UiLayout::kInwardSmallShift;  // px non-hover items pull inward
    static constexpr float kInwardHoverExtra  = UiLayout::kInwardHoverExtra;  // px hovered item pushes outward
    // More centralized UI layout constants (mirror UiLayout where appropriate)
    static constexpr float kBaseD             = UiLayout::kBaseD; // base button diameter
    static constexpr float kOptionItemD       = UiLayout::kStepOptionItemD; // option item diameter used for layout math (~15px)
    static constexpr float kHoverScale        = 1.6666667f; // hovered scale multiplier (25px / 15px)
    static constexpr float kMouseTightenMul   = UiLayout::kMouseTightenMul;  // tighten multiplier applied to s0
    static constexpr float kDistanceFactor    = UiLayout::kDistanceFactor;  // used when computing outer interactive radius
    static constexpr float kInnerBaseRadius   = UiLayout::kInnerBaseRadius; // px: block hover inside this inner radius
    static constexpr float kHoverExtraRadius  = UiLayout::kHoverExtraRadius; // px: hover selection extra radius
    static constexpr float kOuterBaseR        = UiLayout::kOuterBaseR; // px: base click radius when closed

    void timerCallback() override
    {
        const float speed = 0.22f; // animation speed
        if (opening && openAmount < 1.0f)
        {
            openAmount = juce::jmin(1.0f, openAmount + speed);
            repaint();
        }
        else if (closing && openAmount > 0.0f)
        {
            openAmount = juce::jmax(0.0f, openAmount - speed);
            repaint();
        }
        if (openAmount <= 0.0f) closing = false;
        if (openAmount >= 1.0f) opening = false;
    }

    // Central configuration for ring layout & interaction (values chosen to preserve existing visuals)
    struct RingConfig
    {
        static constexpr int   count            = 16;
        static constexpr float optionItemD      = AnimatedStepOffsetMenu::kOptionItemD; // base small circle diameter (px)
        static constexpr float clickItemD       = 25.0f; // diameter used for click detection (keep big enough)
        static constexpr float hoverItemD       = 25.0f; // diameter used for hover detection
        // Scales chosen to match requested absolute sizes for a 15px base:
        // hovered => 25px (25/15 = 1.6667), adjacent => 20px (20/15 = 1.3333), small => 15px (1.0)
        static constexpr float scaleCenter      = 1.6666667f; // hovered (25px)
        static constexpr float scaleNear1       = 1.3333333f; // adjacent (20px)
        static constexpr float scaleSmall       = 1.0f;       // non-adjacent (15px)
        static constexpr float scaleNormalShrink= 1.0f;       // no additional shrink so base sizes match exact px

        // Pull whole ring inward by configured amounts (visual and hit/hover)
        // Values are read from the enclosing AnimatedStepOffsetMenu tweak constants
        static constexpr float inwardSmallShift = AnimatedStepOffsetMenu::kInwardSmallShift;    // px: how much non-hover items pull inward
        static constexpr float inwardHoverExtra = AnimatedStepOffsetMenu::kInwardHoverExtra;   // px: how much hovered item pushes outward
        static constexpr float baseInward = AnimatedStepOffsetMenu::kBaseInward;              // px: overall ring inward at full open

        // No additional spread; keep the ring tight
        static constexpr float spreadVisual     = 0.0f;
        static constexpr float spreadHit        = 0.0f;
        static constexpr float spreadHover      = 0.0f;
        static constexpr float visualMargin     = 0.0f;

        static int circularDistance(int a, int b)
        {
            int d = std::abs(a - b);
            return juce::jmin(d, count - d);
        }
        static float scaleFor(int idx1, int hoverIdx)
        {
            if (hoverIdx < 1) return 1.0f;
            const int d = circularDistance(idx1, hoverIdx);
            if (d == 0) return scaleCenter;
            if (d == 1) return scaleNear1;
            return scaleSmall;
        }
        static float inwardFor(int idx1, int hoverIdx)
        {
            // If there's no hover, compact ring inward
            if (hoverIdx < 1)
                return -inwardSmallShift;

            // Compute circular distance and apply outward offsets for hovered+adjacent
            const int d = circularDistance(idx1, hoverIdx);
            if (d == 0)
                return inwardHoverExtra;                // hovered item: push outward fully
            if (d == 1)
                return inwardHoverExtra * 0.5f;        // adjacent items: push outward partially

            // Otherwise pull small items slightly inward to compact the ring
            return -inwardSmallShift;
        }
        static float computeBaseRadius(float openAmt, int width, int height,
                                       float itemR, float maxScale,
                                       float spreadExtra, float margin)
        {
            juce::ignoreUnused(spreadExtra);
            const float minRadius = 40.0f * 0.5f;
            const float maxRadius = (juce::jmin<float>(width, height) * 0.5f)
                                    - (itemR * maxScale) - margin;
            const float raw = juce::jlimit(minRadius,
                                           juce::jmax(minRadius, maxRadius),
                                           minRadius + (maxRadius - minRadius) * openAmt);
            // Pull the entire ring inward when open for a tighter layout, then
            // allow per-circle inward/outward offsets via inwardFor().
            return raw - baseInward * openAmt;
        }
    };

    void drawOptions(juce::Graphics& g, juce::Point<float> center, float baseD)
    {
        // Centralised parameters & derived base radius
        const int count = RingConfig::count;
        const float itemD = RingConfig::optionItemD;
        const float itemR = itemD * 0.5f;
        const float sCenter = RingConfig::scaleCenter;
        const float baseRadius = RingConfig::computeBaseRadius(openAmount, getWidth(), getHeight(), itemR, sCenter, RingConfig::spreadVisual, RingConfig::visualMargin);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;
        auto scaleFor   = [this](int idx0){ return RingConfig::scaleFor(idx0 + 1, hoverIndex); };
        auto inwardFor  = [this](int idx0){ return RingConfig::inwardFor(idx0 + 1, hoverIndex); };

        // Draw non-hovered items first (ripple scaling). Make fully normal items slightly smaller.
        for (int i = 0; i < count; ++i)
        {
            if (i + 1 == hoverIndex) continue;
            const float ang = startAt12 + step * (float) i;
            const float effRadius = baseRadius + inwardFor(i);
            const float cx = center.x + effRadius * std::cos(ang);
            const float cy = center.y + effRadius * std::sin(ang);
            float sc = scaleFor(i);
            if (sc == 1.0f) sc *= RingConfig::scaleNormalShrink; // shrink normal circles a bit when menu is open
            const float d = itemD * sc;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.33f));
            g.fillEllipse(ring.translated(2.33f, 2.33f));
            g.setColour(kAccent); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f * sc, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), ring.toNearestInt(), juce::Justification::centred, 1);
        }
        // Draw hovered last, scaled and on top
        if (hoverIndex >= 1 && hoverIndex <= count)
        {
            const int i = hoverIndex - 1;
            const float ang = startAt12 + step * (float) i;
            const float effRadius = baseRadius + inwardFor(i);
            const float cx = center.x + effRadius * std::cos(ang);
            const float cy = center.y + effRadius * std::sin(ang);
            const float sc = sCenter;
            const float d = itemD * sc;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.33f));
            g.fillEllipse(ring.translated(2.33f, 2.33f));
            g.setColour(kCyan); g.fillEllipse(ring);
            g.setColour(kBase);
            // Use the same scaled font size as non-hovered items so adjacent fonts aren't bigger
            g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f * sc, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), ring.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    int hitTestOption(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = RingConfig::count;
        const float itemD = RingConfig::clickItemD;
        const float itemR = itemD * 0.5f;
        const auto r = getLocalBounds().toFloat();
        const auto center = r.getCentre();
        const float sCenter = RingConfig::scaleCenter;
        const float baseRadius = RingConfig::computeBaseRadius(
            openAmount, getWidth(), getHeight(), itemR, sCenter,
            RingConfig::spreadHit, RingConfig::visualMargin);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const float ang = startAt12 + step * (float) i;
            // Use correct 1-based index for inwardFor
            float inward = RingConfig::inwardFor(i + 1, hoverIndex);
            const float effRadius = baseRadius + inward;
            const float cx = center.x + effRadius * std::cos(ang);
            const float cy = center.y + effRadius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            const float rr = itemR;
            if ((dx*dx + dy*dy) <= (rr * rr))
                return i + 1;
        }
        return -1;
    }

    // Hover detection: expanded radius and choose the nearest circle among candidates
    int hitTestOptionHover(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = RingConfig::count;
        const float itemD = RingConfig::hoverItemD;
        const float itemR = itemD * 0.5f;
        const float extra = 12.0f; // expanded hover radius for smoother transitions
        const auto r = getLocalBounds().toFloat();
        const auto center = r.getCentre();

        // Block hover inside inner base ellipse of the central button
        const float innerBaseRadius = kInnerBaseRadius;
        if (center.getDistanceFrom(pos) <= innerBaseRadius)
            return -1;

        const float sCenter = RingConfig::scaleCenter;
        const float baseRadius = RingConfig::computeBaseRadius(
            openAmount, getWidth(), getHeight(), itemR, sCenter,
            RingConfig::spreadHover, RingConfig::visualMargin);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;

        int bestIndex = -1;
        float bestDist2 = std::numeric_limits<float>::max();
        const float rr = itemR + kHoverExtraRadius;
        const float rr2 = rr * rr;
        for (int i = 0; i < count; ++i)
        {
            const float ang = startAt12 + step * (float) i;
            float inward = RingConfig::inwardFor(i + 1, hoverIndex);
            const float effRadius = baseRadius + inward;
            const float cx = center.x + effRadius * std::cos(ang);
            const float cy = center.y + effRadius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            const float d2 = dx*dx + dy*dy;
            if (d2 <= rr2 && d2 < bestDist2)
            {
                bestDist2 = d2;
                bestIndex = i + 1;
            }
        }
        return bestIndex;
    }
};
