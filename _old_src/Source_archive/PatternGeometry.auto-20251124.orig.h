// Auto-backup of Source/PatternGeometry.h — copied on 2025-11-24
// To restore: copy this file back to Source/PatternGeometry.h and re-run CMake
// ============================================================================
// PatternGeometry.h - centralised pattern ring geometry constants
// ============================================================================
#pragma once
namespace PatternGeometry {
    static constexpr float kPatternInset = 8.0f;            // inward inset from ring inner radius
    static constexpr float kPatternOutwardShift = 5.0f;      // net outward visual shift applied
    static constexpr float kPatternThicknessRatio = 0.775f;  // inner radius = outer * ratio
}
