#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

// Lightweight 16-step inward pattern ring used for bar-based retrigger scheduling.
// Visible only in EDIT/autofill mode (idx2 ON) but active even when hidden.
class PatternRing
{
public:
    PatternRing() { steps.fill(false); }

    void setStep(int index, bool on) { if (index >=0 && index < 16) steps[(size_t)index] = on; }
    bool getStep(int index) const { return (index >=0 && index < 16) ? steps[(size_t)index] : false; }
    void toggleStep(int index) { if (index >=0 && index < 16) steps[(size_t)index] = ! steps[(size_t)index]; }
    uint16_t getBitmask() const {
        uint16_t m = 0; for (int i=0;i<16;++i) if (steps[(size_t)i]) m |= (uint16_t(1) << i); return m;
    }
    void setBitmask(uint16_t m) {
        for (int i=0;i<16;++i) steps[(size_t)i] = (m & (uint16_t(1) << i)) != 0; }

    // Draw an inward ring of 16 wedges inside the provided centre with given outer & inner radius.
    // activeColour used for active steps; baseColour for inactive if hideInactive == false.
    // When hideInactive is true, inactive wedges are not filled (still hittable for editing).
    // Draw an inward ring of 16 wedges inside the provided centre with given outer & inner radius.
    // activeColour used for active steps; baseColour for inactive if hideInactive == false.
    // When hideInactive is true, inactive wedges are not filled (still hittable for editing).
    void draw(juce::Graphics& g, juce::Point<float> centre, float outerRadius, float innerRadius,
              juce::Colour activeColour, juce::Colour baseColour, int hoverIndex, UiThemeColours& theme, bool hideInactive=false) const
    {
        if (centre != cachedCentre || outerRadius != cachedOuterRadius || innerRadius != cachedInnerRadius)
        {
            cachedCentre = centre;
            cachedOuterRadius = outerRadius;
            cachedInnerRadius = innerRadius;

            juce::Rectangle<float> outer (centre.x - outerRadius, centre.y - outerRadius, outerRadius*2.0f, outerRadius*2.0f);
            const float stepAngle = juce::MathConstants<float>::twoPi / 16.0f;
            const float startAngle = 0.0f; // 12 o'clock in JUCE addPieSegment
            const float gapPx = 3.0f;
            float anglePad = gapPx / juce::jmax(1.0f, outerRadius);
            anglePad = std::min(anglePad, stepAngle * 0.40f);

            for (int i=0; i<16; ++i)
            {
                const float a0Full = startAngle + i*stepAngle;
                const float a1Full = a0Full + stepAngle;
                const float a0 = a0Full + anglePad * 0.5f;
                const float a1 = a1Full - anglePad * 0.5f;
                cachedWedges[(size_t) i].clear();
                cachedWedges[(size_t) i].addPieSegment(outer, a0, a1, innerRadius/outerRadius);
            }
        }

        for (int i=0;i<16;++i)
        {
            const auto& seg = cachedWedges[(size_t) i];
            const bool on = steps[(size_t)i];
            bool drewFill = false;
            if (on || !hideInactive)
            {
                juce::Colour fill = on ? activeColour.withAlpha(0.66f) : baseColour.withAlpha(0.33f);
                if (!on && i==hoverIndex)
                    fill = fill.brighter(0.4f);
                g.setColour(fill);
                g.fillPath(seg);
                drewFill = true;
            }
            // Always draw outline: active = cyan (high alpha), inactive = accent (fainter)
            juce::Colour outline = on ? theme.cyan().withAlpha(0.95f)
                                      : theme.accent().withAlpha(drewFill ? 0.45f : 0.35f);
            g.setColour(outline);
            g.strokePath(seg, juce::PathStrokeType(1.4f));
        }
    }

    // Hit test for wedge index; returns -1 if none.
    int hitTest(juce::Point<float> p, juce::Point<float> centre, float outerRadius, float innerRadius) const
    {
        const float dx = p.x - centre.x; const float dy = p.y - centre.y;
        const float d2 = dx*dx + dy*dy;
        if (d2 > outerRadius*outerRadius || d2 < innerRadius*innerRadius) return -1;
        const float stepAngle = juce::MathConstants<float>::twoPi / 16.0f;
        const float startAngle = -juce::MathConstants<float>::halfPi;
        float ang = std::atan2(dy, dx); // -pi..pi
        // Normalize to [0,2pi)
        if (ang < startAngle) ang += juce::MathConstants<float>::twoPi;
        float rel = ang - startAngle;
        if (rel < 0.0f) rel += juce::MathConstants<float>::twoPi;
        int idx = (int) std::floor(rel / stepAngle) & 15;
        
        // Apply gap: if click lies within the angular gap region for that wedge, treat as no hit.
        const float gapPx = 3.0f;
        float anglePad = gapPx / juce::jmax(1.0f, outerRadius);
        anglePad = std::min(anglePad, stepAngle * 0.40f);
        // Compute local angle within the wedge (pre-rotation) to determine if inside padded interior.
        // Undo rotation to find original wedge number for angular bounds.
        int rawIdx = idx;
        float wedgeStart = rawIdx * stepAngle;
        float wedgeEnd = wedgeStart + stepAngle;
        float angInWedge = rel - wedgeStart;
        if (angInWedge < 0.0f || angInWedge > stepAngle) return -1;
        if (angInWedge < anglePad * 0.5f || angInWedge > stepAngle - anglePad * 0.5f) return -1; // in gap area
        return idx;
    }

private:
    std::array<bool,16> steps;
    mutable std::array<juce::Path, 16> cachedWedges {};
    mutable juce::Point<float> cachedCentre { -1.0f, -1.0f };
    mutable float cachedOuterRadius { -1.0f };
    mutable float cachedInnerRadius { -1.0f };
};
