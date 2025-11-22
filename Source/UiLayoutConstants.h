#pragma once

#include <juce_core/juce_core.h>

// Centralized UI layout constants used by multiple menu components.
namespace UiLayout
{
    // (Legacy GridScaleMenu / StepOffsetMenu constants removed – playground & new UI no longer reference them.)

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
     static constexpr float kBackdropMultiplier = 1.25f;

    static constexpr float kBackdropBaseSize = 55.55f * kBackdropMultiplier;
   
    // Largest -> smallest
    static constexpr std::array<float, 8> kBackdropSizes = [] {
        std::array<float, 8> a{};
        a[7] = kBackdropBaseSize;
        for (int i = 6; i >= 0; --i)
            a[i] = a[i + 1] * kBackdropMultiplier;
        return a;
    }();

    // Per-ring pulse scale increments (multiplied by progress 0..1)
    static constexpr std::array<float, 8> kBackdropPulseScales = { { 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f } };

    // Pulse decay per frame (multiply progress by this each timer tick). Values closer to 1.0 decay slower.
    static constexpr float kBackdropPulseDecay = 0.933f;

    // Minimum and maximum visual scale applied to backdrop circles (applied to base size)
    static constexpr float kBackdropMinScale = 0.90f;
    static constexpr float kBackdropMaxScale = 1.2f;

    // Pulse timing helpers (ms) - useful if switching to an animator-driven pulse
    static constexpr float kBackdropPulseDurationMs = 333.333f; // nominal pulse duration
}
