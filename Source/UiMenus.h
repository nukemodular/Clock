// UiMenus.h
// Consolidated menu controls: GridScaleMenu, StepOffsetMenu, ShuffleModeMenu
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiTheme.h"

// --- GridScaleMenu (merged) -------------------------------------------------
class GridScaleMenu : public juce::Component, private juce::Timer
{
public:
    GridScaleMenu()
    {
        setInterceptsMouseClicks(true, true);
        startTimerHz(60);
    }
    void setColours(juce::Colour, juce::Colour, juce::Colour) {} // Deprecated: uses UiThemeColours
    void setIndex(int idx) { idx = juce::jlimit(0, 3, idx); if (currentIndex != idx) { currentIndex = idx; repaint(); } }
    int getIndex() const { return currentIndex; }
    std::function<void(int)> onGridChanged;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override;
    void resized() override {}
private:
    bool menuOpen { false }, opening { false }, closing { false };
    float openAmount { 0.0f };
    int currentIndex { 1 }, hoverIndex { -1 };
    // juce::Colour kAccent, kBase, kCyan removed; use UiThemeColours directly
    static constexpr float kStartDeg = 110.0f, kEndDeg = 250.0f;
    void timerCallback() override;
    void drawOptions(juce::Graphics& g, juce::Point<float> center, float baseD);
    int hitTestOption(juce::Point<float> pos) const;
    int hitTestOptionHover(juce::Point<float> pos) const;
};

// --- StepOffsetMenu (merged) ------------------------------------------------
class AnimatedStepOffsetMenu : public juce::Component, private juce::Timer
{
public:
    AnimatedStepOffsetMenu() { setInterceptsMouseClicks(true, true); startTimerHz(60); }
    void setColours(juce::Colour, juce::Colour, juce::Colour) {} // Deprecated
    void setStep(int newStep) { newStep = juce::jlimit(1, 16, newStep); if (selectedStep != newStep) { selectedStep = newStep; repaint(); } }
    int getStep() const { return selectedStep; }
    std::function<void(int)> onStepChanged;
    void setClickThroughRect(juce::Rectangle<int> localRect) { clickThroughRect = localRect; hasClickThrough = true; }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override;
    void resized() override { }
    bool hitTest(int x, int y) override;
private:
    bool menuOpen { false }, opening { false }, closing { false };
    float openAmount { 0.0f };
    int selectedStep { 1 }, hoverIndex { -1 };
    juce::Rectangle<int> clickThroughRect {}; bool hasClickThrough { false };
    // Colors removed
    static constexpr float kBaseInward = UiLayout::kBaseInward;
    static constexpr float kInwardSmallShift = UiLayout::kInwardSmallShift;
    static constexpr float kInwardHoverExtra = UiLayout::kInwardHoverExtra;
    static constexpr float kBaseD = UiLayout::kBaseD;
    static constexpr float kOptionItemD = UiLayout::kStepOptionItemD;
    static constexpr float kHoverScale = 1.6666667f;
    static constexpr float kMouseTightenMul = UiLayout::kMouseTightenMul;
    static constexpr float kDistanceFactor = UiLayout::kDistanceFactor;
    static constexpr float kInnerBaseRadius = UiLayout::kInnerBaseRadius;
    static constexpr float kHoverExtraRadius = UiLayout::kHoverExtraRadius;
    static constexpr float kOuterBaseR = UiLayout::kOuterBaseR;
    void timerCallback() override;
    struct RingConfig { static constexpr int count = 16; static constexpr float optionItemD = AnimatedStepOffsetMenu::kOptionItemD; static constexpr float clickItemD = 25.0f; static constexpr float hoverItemD = 25.0f; static constexpr float scaleCenter = 1.6666667f; static constexpr float scaleNear1 = 1.3333333f; static constexpr float scaleSmall = 1.0f; static constexpr float scaleNormalShrink=1.0f; static constexpr float inwardSmallShift = AnimatedStepOffsetMenu::kInwardSmallShift; static constexpr float inwardHoverExtra = AnimatedStepOffsetMenu::kInwardHoverExtra; static constexpr float baseInward = AnimatedStepOffsetMenu::kBaseInward; static float computeBaseRadius(float openAmt, int width, int height, float itemR, float maxScale, float spreadExtra, float margin); static int circularDistance(int a, int b); static float scaleFor(int idx1, int hoverIdx); static float inwardFor(int idx1, int hoverIdx); };
    void drawOptions(juce::Graphics& g, juce::Point<float> center, float baseD);
    int hitTestOption(juce::Point<float> pos) const;
    int hitTestOptionHover(juce::Point<float> pos) const;
};

// --- ShuffleModeMenu (merged) ------------------------------------------------
class ShuffleModeMenu : public juce::Component
{
public:
    std::function<void(int)> onButtonClicked;
    std::function<void(int)> onValueChanged;
    void setValue(int v) { int clamped = juce::jlimit(1, 7, v); if (currentIndex != clamped - 1) { currentIndex = clamped - 1; repaint(); } }
    int getValue() const noexcept { return currentIndex + 1; }
    void setManualBounds(const std::array<juce::Rectangle<int>, 7>& rects){ manualMode = true; const auto origin = getPosition(); for (size_t i = 0; i < rects.size(); ++i) buttonBounds[i] = rects[i].translated(-origin.x, -origin.y); repaint(); }
    bool isManualMode() const noexcept { return manualMode; }
    void setColours(juce::Colour, juce::Colour, juce::Colour){ repaint(); }
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool hitTest(int x, int y) override;
private:
    // Colors removed
    std::array<juce::Rectangle<int>, 7> buttonBounds {};
    bool manualMode { false };
    int currentIndex { 3 };
    bool dragging { false };
    int hitTestIndex(juce::Point<float> pos) const;
    int nearestIndex(juce::Point<float> pos) const;
    void applyIndexFromUser(int idx, bool notify);
};
