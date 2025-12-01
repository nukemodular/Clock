
#pragma once


#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include "UiTheme.h"
#include <cmath>

// Consolidated small UI components: RunButton + StatusBarComponent.
// This header groups two tiny helper components to reduce the number of
// very small headers included around the codebase.

// ---------------- HeaderSwitchToggle -----------------
class HeaderSwitchToggle : public juce::Component {
public:
    HeaderSwitchToggle() { setSize(10, 20); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        // Draw outer rectangle border
        g.setColour(UiThemeColours::base().darker(1.5f));
        g.fillRect(b);
        // Draw indicator
        const float indW = 6.0f, indH = 8.0f;
        float indX = (b.getWidth() - indW) * 0.5f;
        float indY = getToggleState() ? (b.getHeight() - indH - 2.0f) : 2.0f ;
        g.setColour(UiThemeColours::base());
        g.fillRect(indX, indY, indW, indH);
    }
    void mouseDown(const juce::MouseEvent&) override {
        setToggleState(!getToggleState());
    }
    bool getToggleState() const { return toggleState; }
    void setToggleState(bool state) {
        if (toggleState != state) { toggleState = state; repaint(); if (onToggle) onToggle(toggleState); }
    }
    std::function<void(bool)> onToggle;
private:
    bool toggleState { true };
};


// ---------------- FullWidthComboBox -----------------
class FullWidthComboBox : public juce::ComboBox
{
public:
    enum ColourIds {
        overlayTextColourId = 0x2000100
    };
    void paint(juce::Graphics& g) override
    {
        // Draw combo background (no text) to avoid LookAndFeel text drawing
        auto bounds = getLocalBounds();
        g.setColour(UiThemeColours::base());
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(UiThemeColours::accent());
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.5f);
        // Ensure any internal editor/label children are positioned to cover
        // this combo's area so in-place name editing appears exactly where
        // the combo sits (not below it). Also propagate the combo's text
        // colour into the editor for consistent theming.
        for (auto* c : getChildren())
        {
            if (auto* te = dynamic_cast<juce::TextEditor*>(c))
            {
                te->setBounds(getLocalBounds().reduced(6, 0));
                te->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
                te->setColour(juce::TextEditor::textColourId, findColour(overlayTextColourId));
            }
            else if (auto* lb = dynamic_cast<juce::Label*>(c))
            {
                lb->setBounds(getLocalBounds().reduced(6, 0));
                lb->setJustificationType(juce::Justification::centred);
                lb->setColour(juce::Label::textColourId, findColour(overlayTextColourId));
                // We draw the combo text ourselves in paint(); hide the internal
                // label child so it doesn't also draw and produce doubled text.
                lb->setVisible(false);
                lb->toBack();
            }
        }
        // If an inline editor exists and is visible (either parented to
        // this combo or to the same parent and overlapping), skip drawing
        // the combo's placeholder/selected text to avoid visual overlap.
        bool hasVisibleEditor = false;
        // Check children of this combo
        for (auto* c : getChildren())
            if (auto* te = dynamic_cast<juce::TextEditor*>(c))
                if (te->isVisible()) { hasVisibleEditor = true; break; }
        // Also check sibling editors that may be parented to the same
        // parent (common pattern when editor is created elsewhere).
        if (! hasVisibleEditor)
        {
            if (auto* p = getParentComponent())
            {
                for (auto* c : p->getChildren())
                    if (auto* te = dynamic_cast<juce::TextEditor*>(c))
                        if (te->isVisible() && te->getBounds().intersects(getBounds())) { hasVisibleEditor = true; break; }
            }
        }

        // Show placeholder when no selection (only when no inline editor)
        if (! hasVisibleEditor && getSelectedId() == 0)
        {
            juce::String placeholder = getTextWhenNothingSelected();
            if (placeholder.isNotEmpty())
            {
                auto col = findColour(overlayTextColourId);
                if (col.isTransparent()) col = findColour(juce::ComboBox::textColourId);
                g.setColour(col);
                g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawFittedText(placeholder, getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
            }
        }
        else
        {
            // Draw the selected text centered using this combo's text colour.
            // Only draw when there's no inline editor visible to avoid
            // double-drawing under the editor.
            if (! hasVisibleEditor)
            {
                juce::String t = getText();
                if (t.isNotEmpty())
                {
                    auto col = findColour(overlayTextColourId);
                    if (col.isTransparent()) col = findColour(juce::ComboBox::textColourId);
                    g.setColour(col);
                    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
                    g.drawFittedText(t, getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
                }
            }
        }
    }
    void resized() override
    {
        juce::ComboBox::resized();
        for (auto* c : getChildren())
        {
            if (auto* te = dynamic_cast<juce::TextEditor*>(c))
                te->setBounds(getLocalBounds().reduced(6, 0));
            else if (auto* lb = dynamic_cast<juce::Label*>(c))
            {
                lb->setBounds(getLocalBounds().reduced(6, 0));
                lb->setJustificationType(juce::Justification::centred);
            }
        }
    }

    void childrenChanged() override
    {
        if (childrenChangeGuard)
            return;
        childrenChangeGuard = true;
        juce::ComboBox::childrenChanged();
        // Ensure any editor/label created dynamically is positioned and on top
        for (auto* c : getChildren())
        {
            if (auto* te = dynamic_cast<juce::TextEditor*>(c))
            {
                te->setBounds(getLocalBounds().reduced(6, 0));
                    te->setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.08f));
                    te->setColour(juce::TextEditor::textColourId, findColour(overlayTextColourId));
                // Defer bringing to front and focusing to avoid re-entrancy
                te->setWantsKeyboardFocus(true);
                juce::MessageManager::callAsync([this, te]() {
                    if (te->getParentComponent() == this)
                    {
                        te->toFront(true);
                        te->setVisible(true);
                        te->grabKeyboardFocus();
                    }
                    else if (auto* p = getParentComponent())
                    {
                        // If editor is sibling, still bring it front and ensure visibility
                        te->toFront(true);
                        te->setVisible(true);
                        te->grabKeyboardFocus();
                    }
                });
            }
            else if (auto* lb = dynamic_cast<juce::Label*>(c))
            {
                lb->setBounds(getLocalBounds().reduced(6, 0));
                lb->setJustificationType(juce::Justification::centred);
                lb->setColour(juce::Label::textColourId, findColour(overlayTextColourId));
                // Always keep the internal label hidden — paint() handles the
                // visible text so the label must not draw itself (avoids double
                // text rendering when both are visible).
                lb->setVisible(false);
                lb->toBack();
            }
        }
        childrenChangeGuard = false;
    }
private:
    bool childrenChangeGuard { false };
};

// Reentrancy guard member added to FullWidthComboBox above; declare here to avoid
// altering public API ordering in patches. (No further changes required.)

// ---------------- SmallDotToggle -----------------
class SmallDotToggle : public juce::ToggleButton {
public:
    void setColours(juce::Colour offCol, juce::Colour onCol) { off = offCol; on = onCol; repaint(); }
    void paintButton(juce::Graphics& g, bool, bool) override {
        auto b = getLocalBounds().toFloat();
        auto d = std::min(b.getWidth(), b.getHeight());
        auto r = juce::Rectangle<float>(b.getCentreX() - d * 0.5f, b.getCentreY() - d * 0.5f, d, d);
        g.setColour(getToggleState() ? on : off);
        g.fillEllipse(r);
    }
    void mouseDown(const juce::MouseEvent& /*e*/) override { setToggleState(! getToggleState(), juce::sendNotification); }
    void mouseUp(const juce::MouseEvent& /*e*/) override {}
private:
    juce::Colour off { juce::Colours::red };
    juce::Colour on  { juce::Colours::cyan };
};

// ---------------- HelpButton -----------------
class HelpButton : public juce::TextButton {
public:
    HelpButton() : juce::TextButton("?") {}
    void paintButton(juce::Graphics& g, bool, bool) override {
        juce::Colour col = getToggleState() ? UiThemeColours::cyan() : UiThemeColours::cyan().darker(1.0f);
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions("Arial", 18.0f, juce::Font::bold)));
        g.drawFittedText(getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
    }
};

// ---------------- ArrowDownComponent -----------------
class ArrowDownComponent : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::Path p;
        float w = b.getWidth();
        float h = b.getHeight();
        // Triangle points: bottom center, top left, top right
        p.startNewSubPath(w * 0.5f, h);
        p.lineTo(w * 0.0f, 0.0f);
        p.lineTo(w * 1.0f, 0.0f);
        p.closeSubPath();
        g.setColour(UiThemeColours::base());
        g.fillPath(p);
        // Draw a smaller accent triangle inside the base triangle
        juce::Path p2;
        float inset = 6.0f;
        p2.startNewSubPath(w * 0.5f, h - inset);
        p2.lineTo(w * 0.0f + 6.0f, 2.0f );
        p2.lineTo(w * 1.0f - 6.0f, 2.0f );
        p2.closeSubPath();
        g.setColour(UiThemeColours::cyan());
        g.fillPath(p2);
    }
};

// ---------------- RunButton (copied, unchanged API) -----------------
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

// ---------------- StatusBarComponent (copied, unchanged API) -----------------
class StatusBarComponent : public juce::Component
{
public:
    void setStatusText(const juce::String& s)
    {
        if (statusText != s)
        {
            statusText = s;
            repaint();
        }
    }

    void setLedLevel(float v)
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        if (std::abs(ledLevel - v) > 0.001f)
        {
            ledLevel = v;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        auto textArea = b.withTrimmedLeft(12);
        g.setColour(UiThemeColours::cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
        g.drawFittedText(statusText, textArea, juce::Justification::centredLeft, 1);

        const float ledR = 3.0f;
        auto cx = (float) (b.getX() + 2);
        auto cy = (float) b.getCentreY();
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(cx - ledR - 1.5f, cy - ledR + 1.5f, ledR * 2.0f, ledR * 2.0f);
        auto ledColour = UiThemeColours::cyan().withAlpha(0.33f).interpolatedWith(UiThemeColours::cyan().withAlpha(0.99f), ledLevel);
        g.setColour(ledColour);
        g.fillEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);
    }

private:
    juce::String statusText { "" };
    float ledLevel { 0.0f };
};
