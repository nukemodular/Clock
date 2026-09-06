
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
    HeaderSwitchToggle(UiThemeColours& t) : theme(t) { setSize(10, 20); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        // Outline-only frame + indicator (no inner fill).
        g.setColour(theme.cyan());
        g.drawRect(b, 1.0f);

        // Draw indicator
        const float indW = 6.0f, indH = 8.0f;
        float indX = (b.getWidth() - indW) * 0.5f;
        float indY = getToggleState() ? (b.getHeight() - indH - 2.0f) : 2.0f ;
        g.setColour(theme.cyan());
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
    UiThemeColours& theme;
};


// ---------------- FullWidthComboBox -----------------
class FullWidthComboBox : public juce::ComboBox
{
public:
    FullWidthComboBox(UiThemeColours& t) : theme(t) {}
    enum ColourIds {
        overlayTextColourId = 0x2000100
    };
    void paint(juce::Graphics& g) override
    {
        const auto middleEllipsize = [](const juce::String& s, const juce::Font& font, int maxWidth) -> juce::String
        {
            if (maxWidth <= 0)
                return {};

            if (juce::GlyphArrangement::getStringWidthInt(font, s) <= maxWidth)
                return s;

            const juce::String dots("...");
            if (juce::GlyphArrangement::getStringWidthInt(font, dots) >= maxWidth)
                return dots;

            const int len = s.length();
            int low = 1, high = len, bestKeep = 0;
            while (low <= high)
            {
                const int mid = (low + high) / 2;
                const int prefix = (mid + 1) / 2;
                const int suffix = mid / 2;
                const int suffixStart = juce::jmax(0, len - suffix);
                const juce::String cand = s.substring(0, prefix) + dots + s.substring(suffixStart);
                if (juce::GlyphArrangement::getStringWidthInt(font, cand) <= maxWidth)
                {
                    bestKeep = mid;
                    low = mid + 1;
                }
                else
                {
                    high = mid - 1;
                }
            }
            if (bestKeep > 0)
            {
                const int prefix = (bestKeep + 1) / 2;
                const int suffix = bestKeep / 2;
                const int suffixStart = juce::jmax(0, len - suffix);
                return s.substring(0, prefix) + dots + s.substring(suffixStart);
            }
            return dots;
        };

        // Draw combo background (no text) to avoid LookAndFeel text drawing
        auto bounds = getLocalBounds();
        g.setColour(theme.fixedBase());
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(theme.accent());
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.5f);
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
                const auto font = juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold));
                g.setFont(font);
                const auto textArea = getLocalBounds().reduced(6, 0);
                const auto txt = middleEllipsize(placeholder, font, textArea.getWidth());
                g.drawText(txt, textArea, juce::Justification::centred, true);
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
                    const auto font = juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold));
                    g.setFont(font);
                    const auto textArea = getLocalBounds().reduced(6, 0);
                    const auto txt = middleEllipsize(t, font, textArea.getWidth());
                    g.drawText(txt, textArea, juce::Justification::centred, true);
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
    UiThemeColours& theme;
};

class NameComboBox : public FullWidthComboBox
{
public:
    explicit NameComboBox(UiThemeColours& t) : FullWidthComboBox(t) {}

    std::function<void()> onEditCurrentRequest;

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onEditCurrentRequest)
            onEditCurrentRequest();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onEditCurrentRequest)
            onEditCurrentRequest();
    }

    void showPopup() override
    {
        if (onEditCurrentRequest)
            onEditCurrentRequest();
    }
};

// Reentrancy guard member added to FullWidthComboBox above; declare here to avoid
// altering public API ordering in patches. (No further changes required.)

// ---------------- SmallDotToggle -----------------
class SmallDotToggle : public juce::ToggleButton {
public:
    SmallDotToggle(UiThemeColours& t) : theme(t) {}
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
    UiThemeColours& theme;
};

// ---------------- HelpButton (SVG icon) -----------------
class HelpButton : public juce::DrawableButton {
public:
    HelpButton(UiThemeColours& t)
        : juce::DrawableButton("help", juce::DrawableButton::ImageFitted), theme(t) {}
    UiThemeColours& theme;
};

// ---------------- ArrowDownComponent -----------------
class ArrowDownComponent : public juce::Component
{
public:
    ArrowDownComponent(UiThemeColours& t) : theme(t) {}
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
        g.setColour(theme.fixedBase());
        g.fillPath(p);
        // Draw a smaller accent triangle inside the base triangle
        juce::Path p2;
        float inset = 6.0f;
        p2.startNewSubPath(w * 0.5f, h - inset);
        p2.lineTo(w * 0.0f + 6.0f, 2.0f );
        p2.lineTo(w * 1.0f - 6.0f, 2.0f );
        p2.closeSubPath();
        g.setColour(theme.cyan());
        g.fillPath(p2);
    }
private:
    UiThemeColours& theme;
};

// ---------------- RunButton (copied, unchanged API) -----------------
class RunButton : public juce::Component
{
public:
    RunButton(UiThemeColours& t) : theme(t)
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
        g.setColour(theme.accent());
        g.fillPath(p);
    }

    static void drawAt(juce::Graphics& g, float centreX, float centreY, bool running, UiThemeColours& theme)
    {
        if (running) return;
        const float w = rectW_static;
        const float h = rectH_static;
        juce::Path p;
        p.addRectangle(-w * 0.5f, -h * 0.5f, w, h);
        juce::AffineTransform t = juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f).translated(centreX, centreY);
        p.applyTransform(t);

        g.setColour(theme.accent());
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
    UiThemeColours& theme;
};

// ---------------- StatusBarComponent (copied, unchanged API) -----------------
class StatusBarComponent : public juce::Component
{
public:
    StatusBarComponent(UiThemeColours& t) : theme(t) {}
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
        // Leave enough room so the LED shadow isn't clipped by our own bounds.
        auto textArea = b.withTrimmedLeft(12);
        g.setColour(theme.cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
        g.drawFittedText(statusText, textArea, juce::Justification::centredLeft, 1);

        const float ledR = 3.0f;
        auto cx = (float) (b.getX() + 5);
        auto cy = (float) b.getCentreY();
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(cx - ledR - 1.0f, cy - ledR + 1.0f, ledR * 2.0f, ledR * 2.0f);
        auto ledColour = theme.cyan().withAlpha(0.33f).interpolatedWith(theme.cyan().withAlpha(0.99f), ledLevel);
        g.setColour(ledColour);
        g.fillEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);
    }

private:
    juce::String statusText { "" };
    float ledLevel { 0.0f };
    UiThemeColours& theme;
};

// ---------------- SimpleColorPicker -----------------
class SimpleColorPicker : public juce::Component
{
public:
    explicit SimpleColorPicker(UiThemeColours& themeToUse, float uiScale = 1.0f) : theme(themeToUse)
    {
        setScaleFactor(uiScale);
    }

    void setScaleFactor(float newScale)
    {
        scaleFactor = juce::jmax(0.5f, newScale);
        setSize((int) std::round(96.0f * scaleFactor),
                (int) std::round(110.0f * scaleFactor));
        repaint();
    }

    void setCurrentColour(juce::Colour c)
    {
        currentHue = c.getHue();
        currentSat = c.getSaturation();
        currentBri = c.getBrightness();
        currentColour = c;
        repaint();
    }

    std::function<void(juce::Colour)> onColorChanged;

    void mouseDown(const juce::MouseEvent& e) override
    {
        handleMouse(e);
    }
    
    void mouseDrag(const juce::MouseEvent& e) override
    {
        handleMouse(e);
    }
    
    void handleMouse(const juce::MouseEvent& e)
    {
        const int columnWidth = juce::jmax(1, getWidth() / 3);
        const int col = juce::jlimit(0, 2, e.x / columnWidth);
        float normY = (float)e.y / (float)getHeight();
        normY = juce::jlimit(0.0f, 1.0f, normY);
        
        if (col == 0) currentHue = normY;
        else if (col == 1) currentSat = 1.0f - normY;
        else if (col == 2) currentBri = 1.0f - normY;
        
        updateTarget();
        repaint();
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto popupBase = theme.base();
        g.setColour(popupBase);
        g.fillRect(bounds);

        const float marginX = 2.0f * scaleFactor;
        const float marginY = 6.0f * scaleFactor;
        const float columnGap = 4.0f * scaleFactor;
        const float usableHeight = juce::jmax(1.0f, bounds.getHeight() - marginY * 2.0f);
        const float columnWidth = (bounds.getWidth() - marginX * 2.0f - columnGap * 2.0f) / 3.0f;
        const float markerHeight = 6.0f * scaleFactor;
        const float markerCorner = 2.0f * scaleFactor;
        const float markerStroke = juce::jmax(1.0f, scaleFactor);

        const float hueX = marginX;
        const float satX = hueX + columnWidth + columnGap;
        const float briX = satX + columnWidth + columnGap;
        
        // Hue (Col 0)
        {
            juce::ColourGradient grad;
            grad.point1 = { hueX + columnWidth * 0.5f, marginY };
            grad.point2 = { hueX + columnWidth * 0.5f, marginY + usableHeight };
            for(float i=0.0f; i<=1.0f; i+=0.1f) grad.addColour(i, juce::Colour::fromHSV(i, 1.0f, 1.0f, 1.0f));
            g.setGradientFill(grad);
            g.fillRect(hueX, marginY, columnWidth, usableHeight);
            
            float y = marginY + currentHue * usableHeight;
            g.setColour(theme.accent().brighter(0.55f));
            g.drawRoundedRectangle(hueX + scaleFactor,
                                   y - markerHeight * 0.5f,
                                   columnWidth - 2.0f * scaleFactor,
                                   markerHeight,
                                   markerCorner,
                                   markerStroke);
        }
        
        // Sat (Col 1)
        {
            juce::ColourGradient grad;
            grad.point1 = { satX + columnWidth * 0.5f, marginY };
            grad.point2 = { satX + columnWidth * 0.5f, marginY + usableHeight };
            // Top: Pure Color (Sat 1), Bottom: White (Sat 0)
            grad.addColour(0.0f, juce::Colour::fromHSV(currentHue, 1.0f, 1.0f, 1.0f));
            grad.addColour(1.0f, juce::Colour::fromHSV(currentHue, 0.0f, 1.0f, 1.0f));
            g.setGradientFill(grad);
            g.fillRect(satX, marginY, columnWidth, usableHeight);
            
            float y = marginY + (1.0f - currentSat) * usableHeight;
            g.setColour(theme.accent().brighter(0.35f));
            g.drawRoundedRectangle(satX + scaleFactor,
                                   y - markerHeight * 0.5f,
                                   columnWidth - 2.0f * scaleFactor,
                                   markerHeight,
                                   markerCorner,
                                   markerStroke);
        }
        
        // Bri (Col 2)
        {
            juce::ColourGradient grad;
            grad.point1 = { briX + columnWidth * 0.5f, marginY };
            grad.point2 = { briX + columnWidth * 0.5f, marginY + usableHeight };
            // Top: Pure Color (Bri 1), Bottom: Black (Bri 0)
            grad.addColour(0.0f, juce::Colour::fromHSV(currentHue, currentSat, 1.0f, 1.0f));
            grad.addColour(1.0f, juce::Colours::black);
            g.setGradientFill(grad);
            g.fillRect(briX, marginY, columnWidth, usableHeight);
            
            float y = marginY + (1.0f - currentBri) * usableHeight;
            g.setColour(theme.accent().brighter(0.25f));
            g.drawRoundedRectangle(briX + scaleFactor,
                                   y - markerHeight * 0.5f,
                                   columnWidth - 2.0f * scaleFactor,
                                   markerHeight,
                                   markerCorner,
                                   markerStroke);
        }
    }

private:
    juce::Colour currentColour;
    float currentHue = 0.0f, currentSat = 0.0f, currentBri = 1.0f;
    float scaleFactor = 1.0f;
    UiThemeColours& theme;

    void updateTarget()
    {
        currentColour = juce::Colour::fromHSV(currentHue, currentSat, currentBri, 1.0f);
        if (onColorChanged)
            onColorChanged(currentColour);
    }
};
