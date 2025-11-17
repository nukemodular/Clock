#pragma once

#include <juce_core/juce_core.h>

// Centralized UI layout constants used by multiple menu components.
namespace UiLayout
{
    // Base button diameters
    static constexpr float kBaseD = 50.0f;        // default center button diameter

    // Option item sizes
    static constexpr float kOptionItemD = 20.0f;  // option item diameter for GridScaleMenu
    static constexpr float kStepOptionItemD = 15.0f; // option item diameter for StepOffsetMenu (small circles ~15px)

    // GridScale specific
    static constexpr float kGridOuterD = 53.0f;   // outer accent diameter used when drawing grid button
    static constexpr float kGridOuterR = 30.0f;   // click radius for the central button (px)
    static constexpr float kGridBaseD  = 60.0f;   // base diameter used for radius math in GridScaleMenu
    static constexpr float kGridS0 = 1.33f;        // hover scale multiplier for grid items
    static constexpr float kGridS1 = 1.1f;        // adjacent-item scale multiplier
    static constexpr float kGridTighten = 8.0f;   // tightening margin used in grid radial math
    static constexpr float kGridOuterMargin = 12.0f; // outer margin used to avoid accidental collapse
    static constexpr float kGridInnerReduce = 10.0f; // inner reduction used when drawing the central base circle

    // Hover / scale
    static constexpr float kHoverScale = 1.3f;   // hovered scale multiplier used in hover math

    // Ring placement tweaks
    static constexpr float kInwardWhenOpen = 7.0f; // px the option ring pulls inward at full open (GridScale)
    static constexpr float kHoverOutward = 0.0f;  // px hovered item moves outward at full open (GridScale)

    // StepOffsetMenu specific defaults
    static constexpr float kBaseInward = 13.0f;        // px overall ring inward at full open
    // StepOffsetMenu outward/inward tuning
    // Small shift pulls non-hover items slightly inward to compact the ring (px)
    static constexpr float kInwardSmallShift = 1.5f;
    // Hover extra pushes hovered item outward from the base ring (px). Keep small values here
    // so we can tune the visual feel centrally. Suggested default ~3.0f (adjacent will be half).
    static constexpr float kInwardHoverExtra = 3.0f; // px hovered item pushes outward

    // Mouse interaction tuning
    static constexpr float kMouseTightenMul = 2.0f;  // tighten multiplier applied to s0
    static constexpr float kDistanceFactor = 3.0f;   // used when computing outer interactive radius

    static constexpr float kInnerBaseRadius = 25.0f;  // px: block hover inside this inner radius
    static constexpr float kHoverExtraRadius = 12.0f; // px: hover selection extra radius
    static constexpr float kOuterBaseR = 20.0f;       // px: base click radius when closed

    // General UI metrics
    static constexpr float kFontSmall = 10.0f;
    static constexpr float kFontMedium = 16.0f;
    static constexpr float kFontTooltip = 13.0f;
    static constexpr float kCornerRadius = 6.0f;
    static constexpr float kLedRadius = 6.0f;
    static constexpr float kBarWidth = 18.0f;
    static constexpr float kInnerReduce = 6.0f; // common inner reduction used in some ellipse math

    // Backdrop (decorative) rings configuration
    // Sizes in pixels for each backdrop circle (largest -> smallest)
    static constexpr std::array<int, 6> kBackdropSizes = { { 420, 350, 290, 240, 200, 170 } };

    // Per-ring pulse scale increments (multiplied by progress 0..1)
    static constexpr std::array<float, 6> kBackdropPulseScales = { { 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f } };

    // Pulse decay per frame (multiply progress by this each timer tick). Values closer to 1.0 decay slower.
    static constexpr float kBackdropPulseDecay = 0.95f;

    // Minimum and maximum visual scale applied to backdrop circles (applied to base size)
    static constexpr float kBackdropMinScale = 0.95f;
    static constexpr float kBackdropMaxScale = 1.15f;

    // Pulse timing helpers (ms) - useful if switching to an animator-driven pulse
    static constexpr int kBackdropPulseDurationMs = 250; // nominal pulse duration
}
