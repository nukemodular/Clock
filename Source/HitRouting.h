// ============================================================================
// HitRouting.h
// ---------------------------------------------------------------------------
// Unified single-pass hit-test for the Clock plugin UI. The router converts a
// raw mouse position + contextual state (popups, pattern edit mode, ring
// geometry) into a compact HitResult. This eliminates the previous layered,
// order-sensitive geometry checks scattered across PluginEditor and
// PlaygroundComponent.
//
// Priority (first match wins): (UPDATED)
//   1. PatternWedge        (only when pattern edit mode active)
//   2. PopupItem3 / PopupItem6 (expanded popup menu items take precedence over ring)
//   3. RingCenter          (inner circle toggle / play)
//   4. ShufflePoint        (discrete shuffle dots / linear handle - before ring wedges to avoid annulus swallow)
//   5. RingWedge           (offset start step selection 1..16)
//   6. SmallCircle[i]      (canonical circles excluding ring idx0)
//   7. RotaryHandle        (division rotary inside circle idx7)
//   8. ClickPulseButton    (small inner button over rotary)
//   9. None                (no actionable zone)
//
// Notes:
// - PatternWedge swallows underlying ring center / wedge while editing so that
//   gaps or inner hole clicks don't trigger play / offset changes.
// - Popup items take precedence over their underlying circle area while open.
// - RotaryHandle vs ClickPulseButton ordering ensures the smaller pulse toggle
//   remains independently clickable.
// - Shuffle points are only considered if no higher-priority zone matched.
// - Returned indices are semantic: PatternWedge 0..15, RingWedge 1..16,
//   SmallCircle is circle idx, PopupItem index into popup arrays, ShufflePoint
//   shuffle id (1..7). RotaryHandle & ClickPulseButton use -1.
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Pattern.h"
#include "PlaygroundComponent.h" // require full definition for member access
#include "PatternGeometry.h"

// Interaction modes gate which zones are considered during hit-test.
enum class InteractionMode { Normal, PatternEdit, Popup3Open, Popup6Open, SubmenuActive };

enum class ZoneType {
    None,
    PatternWedge, // index => pattern wedge 0..15
    RingCenter,   // index = -1
    RingWedge,    // index => logical step 1..16
    PopupItem3,   // index => popup3 item
    PopupItem6,   // index => popup6 item
    SmallCircle,  // index => circle idx (1..N minus ring)
    ShufflePoint, // index => shuffle id (1..7)
    RotaryHandle, // index = -1
    ClickPulseButton // index = -1
};

struct HitResult {
    ZoneType type { ZoneType::None };
    int index { -1 };       // semantic per zone (step1..16, circle idx, popup item, etc.)
    int subIndex { -1 };    // optional secondary index (e.g. raw wedge) if needed later
    bool inDonut { false }; // ring annulus test result
    bool insideInner { false }; // inside inner circle
};

// Context struct assembled by caller (editor) so router stays header-only.
struct HitContext {
    PlaygroundComponent* playground { nullptr }; // provides circles, popups, shuffle positions
    PatternRing* pattern { nullptr };            // pattern ring data (when editing)
    juce::Rectangle<int> ringArea;               // ring bounding box
    bool patternEditMode { false };
    bool submenuActive { false };                // setup submenu
    int outerDiameter { 0 };                     // ring outer diameter (pixels)
    int innerDiameter { 0 };                     // ring inner diameter (pixels)
};

// Perform prioritized hit test for a given point. Caller supplies interaction mode.
inline HitResult performHitTest(const juce::Point<float>& pt, const HitContext& ctx, InteractionMode mode)
{
    HitResult r; // default None
    auto* pg = ctx.playground;
    if (! pg) return r;
    // Ring geometry (always active; staging removed)
    const int cx = ctx.ringArea.getCentreX();
    const int cy = ctx.ringArea.getCentreY();
    const float outerR = juce::jmax(1.0f, (float) ctx.outerDiameter * 0.5f);
    const float innerR = juce::jmax(1.0f, (float) ctx.innerDiameter * 0.5f);
    const float dx = pt.x - (float) cx;
    const float dy = pt.y - (float) cy;
    const float dist2 = dx*dx + dy*dy;
    const bool inDonut = (dist2 <= outerR*outerR && dist2 >= innerR*innerR);
    const bool insideInner = (dist2 < innerR*innerR);
    r.inDonut = inDonut; r.insideInner = insideInner;

    const bool popup3Active = pg->isPopup3Active();
    const bool popup6Active = pg->isPopup6Active();

    // Pattern wedge only when pattern edit active
    if (mode == InteractionMode::PatternEdit && ctx.pattern && ctx.patternEditMode)
    {
        float innerHoleR = innerR; // base inner hole from ring
        float patternOuterR = innerHoleR - PatternGeometry::kPatternInset + PatternGeometry::kPatternOutwardShift;
        float patternInnerR = patternOuterR * PatternGeometry::kPatternThicknessRatio;
        juce::Point<float> centre((float) cx, (float) cy);
        int pw = ctx.pattern->hitTest(pt, centre, patternOuterR, patternInnerR);
        if (pw >= 0)
        {
            r.type = ZoneType::PatternWedge;
            r.index = pw; // 0..15 pattern wedge
            return r;
        }
        // If inside pattern ring band but between wedges (gap), treat as None unless inside inner circle.
        const float d = std::sqrt(dist2);
        if (d >= patternInnerR && d <= patternOuterR)
        {
            // Swallow gap clicks during edit: do not fall through to popups or ring offset.
            r.type = ZoneType::None; return r;
        }
        if (d < patternInnerR)
        {
            // Block run toggle while editing pattern; treat as None.
            r.type = ZoneType::None; return r;
        }
    }

    // Popup items (always when visible)
    if (popup3Active)
    {
        if (pg->getCircleCount() > 3)
        {
            float x,y,radius; if (pg->getCircleInfo(3,x,y,radius))
            {
                int hi3 = pg->popup3Hit(pt, x, y, radius);
                if (hi3 >= 0) { r.type = ZoneType::PopupItem3; r.index = hi3; return r; }
            }
        }
    }
    if (popup6Active)
    {
        if (pg->getCircleCount() > 6)
        {
            float x,y,radius; if (pg->getCircleInfo(6,x,y,radius))
            {
                int hi6 = pg->popup6Hit(pt, x, y, radius);
                if (hi6 >= 0) { r.type = ZoneType::PopupItem6; r.index = hi6; return r; }
            }
        }
    }
    // Ring center (not while pattern editing)
    if (insideInner && mode != InteractionMode::PatternEdit)
    {
        r.type = ZoneType::RingCenter; r.index = -1; return r;
    }

    // 4. Shuffle points before ring wedges.
    //    Previous implementation returned the FIRST matching shuffle circle which caused
    //    larger circles to "occlude" smaller ones when radii overlapped (user perceived
    //    z‑stack queue jumping, notably circle 1 losing hover/click precedence near circle 7).
    //    Revised logic collects ALL hits and chooses the smallest visual radius; ties fall back
    //    to the smallest shuffle id. This makes smaller circles effectively "frontmost" for hit detection.
    if (pg->getShuffleCount() > 0)
    {
        // Enhanced shuffle hit logic:
        //  - Use nearest-center circle hit (distance to centre within radius+buffer) for baseline.
        //  - Add CAPSULE extension for endpoints (sid1 & sidN) along the segment toward their immediate neighbour.
        //    This captures tangential sweeps that skim just outside the circular area.
        //  - Larger endpoint buffers (+6px) and mid buffers (+2px).
        //  - Capsule half-width = 5px; length spans gap between endpoint and neighbour minus their radii.
        //  - If point falls inside capsule, treat as hit for that endpoint.
        const int nSh = pg->getShuffleCount();
        struct ShInfo { int sid; float x,y,r; };
        juce::Array<ShInfo> infos; infos.ensureStorageAllocated(nSh);
        for (int i=0;i<nSh;++i){ int sid; float sx,sy,sr; if (pg->getShuffleInfo(i,sid,sx,sy,sr)) infos.add({sid,sx,sy,sr}); }
        auto getInfoBySid = [&infos](int sid)->ShInfo{ for (auto& s: infos) if (s.sid==sid) return s; return ShInfo{sid,0,0,0}; };
        int bestSid=-1; float bestMetric=1e12f; // metric = centre distance squared (or capsule axis distance squared when capsule hit)
        for (auto& sh : infos)
        {
            float buffer = (sh.sid == 1 || sh.sid == nSh) ? 6.0f : 2.0f;
            float hitR = sh.r + buffer;
            float dx = pt.x - sh.x; float dy = pt.y - sh.y; float d2 = dx*dx + dy*dy;
            bool circleHit = (d2 <= hitR*hitR);
            bool capsuleHit = false; float capsuleMetric = 1e12f;
            if ((sh.sid == 1 || sh.sid == nSh) && nSh > 1)
            {
                int neighSid = (sh.sid == 1 ? 2 : nSh-1);
                auto neigh = getInfoBySid(neighSid);
                // Vector from endpoint to neighbour
                juce::Point<float> A(sh.x, sh.y), B(neigh.x, neigh.y);
                juce::Point<float> P(pt.x, pt.y);
                auto AB = B - A; auto AP = P - A;
                float abLen2 = AB.x*AB.x + AB.y*AB.y;
                float u = (abLen2 > 0.0001f)? (AP.x*AB.x + AP.y*AB.y)/abLen2 : 0.0f;
                u = juce::jlimit(0.0f,1.0f,u);
                juce::Point<float> closest = A + AB * u;
                float dxCaps = P.x - closest.x; float dyCaps = P.y - closest.y;
                float halfWidth = 5.0f; // capsule radius
                float distCaps2 = dxCaps*dxCaps + dyCaps*dyCaps;
                // Only accept capsule hits that fall outside the raw circle (to avoid double-weighting circle interior)
                if (distCaps2 <= halfWidth*halfWidth && d2 > sh.r*sh.r)
                {
                    capsuleHit = true;
                    capsuleMetric = distCaps2 + 0.0001f; // prefer real circle hits (larger weight) by keeping metric small but distinct
                }
            }
            if (circleHit)
            {
                if (d2 < bestMetric){ bestMetric = d2; bestSid = sh.sid; }
            }
            else if (capsuleHit)
            {
                if (capsuleMetric < bestMetric){ bestMetric = capsuleMetric; bestSid = sh.sid; }
            }
        }
        if (bestSid > 0)
        {
            r.type = ZoneType::ShufflePoint; r.index = bestSid; return r;
        }
    }

    // Ring wedge (offset selection)
    if (inDonut && mode != InteractionMode::PatternEdit)
    {
        const float angle = std::atan2(dy, dx); // -pi..pi
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        float rel = angle - startAt12;
        while (rel < 0.0f) rel += juce::MathConstants<float>::twoPi;
        const float slice = juce::MathConstants<float>::twoPi / 16.0f;
        int rawIdx = (int) std::floor(rel / slice) & 15;
        r.type = ZoneType::RingWedge; r.index = rawIdx + 1; r.subIndex = rawIdx; return r;
    }

    // 6. Small circles (skip ring idx0). Choose smallest containing radius (nested safety).
    int bestCircle = -1; float bestR = 1e9f;
    for (int i = 1; i < pg->getCircleCount(); ++i)
    {
        float x,y,radius; if (!pg->getCircleInfo(i,x,y,radius)) continue;
        const float dxC = pt.x - x; const float dyC = pt.y - y;
        if (dxC*dxC + dyC*dyC <= radius*radius)
        {
            if (radius < bestR) { bestR = radius; bestCircle = i; }
        }
    }
    if (bestCircle >= 0)
    {
        // Rotary handle / click pulse button special zones inside circle 7 region.
        if (bestCircle == 7)
        {
            float x,y,rad; if (pg->getCircleInfo(7,x,y,rad))
            {
                const float smallR = 8.0f;
                const float dxb = pt.x - x; const float dyb = pt.y - y;
                if (dxb*dxb + dyb*dyb <= smallR*smallR) { r.type = ZoneType::ClickPulseButton; r.index = -1; return r; }
                const float insetR = 17.0f;
                if (dxb*dxb + dyb*dyb <= insetR*insetR) { r.type = ZoneType::RotaryHandle; r.index = -1; return r; }
            }
        }
        r.type = ZoneType::SmallCircle; r.index = bestCircle; return r;
    }

    // (Shuffle points already handled earlier)

    // Nothing matched.
    return r;
}
