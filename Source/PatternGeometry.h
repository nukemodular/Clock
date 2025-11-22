// ============================================================================
// PatternGeometry.h - centralised pattern ring geometry constants
// ============================================================================
#pragma once
namespace PatternGeometry {
    static constexpr float kPatternInset = 8.0f;            // inward inset from ring inner radius
    static constexpr float kPatternOutwardShift = 5.0f;      // net outward visual shift applied
    static constexpr float kPatternThicknessRatio = 0.775f;  // inner radius = outer * ratio
}
