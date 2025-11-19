#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Header-only modular expanding/collapsing circular popup menu.
// Implementations are inline so this can be included without modifying build system.

class PopupMenuRing
{
public:
    PopupMenuRing()
    {
        itemR = 12.0f;
        expansionRadius = 6.0f;
        fontSize = 12.0f;
        animDurationMs = 166.0f;
        staggerMs = 25.0f;
    }

    // configuration
    void setAngles (float s, float e) { startAngleDeg = s; endAngleDeg = e; }
    void setCircleRadius (float r) noexcept { itemR = r; }
    void setExpansionRadius (float r) noexcept { expansionRadius = r; }
    void setFontSize (float s) noexcept { fontSize = s; }
    void setAnimDurationMs (float d) noexcept { animDurationMs = d; }
    void setStaggerMs (float s) noexcept { staggerMs = s; }

    void setValues (const juce::Array<int>& vals)
    {
        values.clear(); labels.clear();
        for (int i = 0; i < vals.size(); ++i)
        {
            values.add (vals.getReference(i));
            labels.add (juce::String (vals.getReference(i)));
        }
        progress.clear(); hoverScale.clear(); hoverTarget.clear();
        for (int i = 0; i < values.size(); ++i) { progress.add (0.0f); hoverScale.add (1.0f); hoverTarget.add (1.0f); }
    }

    void setLabels (const juce::StringArray& l)
    {
        labels = l;
        // Preserve existing numeric values if they were set via setValues().
        // If values is shorter than labels, extend with zeros; if longer, truncate.
        if (values.size() < labels.size())
        {
            for (int i = values.size(); i < labels.size(); ++i) values.add (0);
        }
        else if (values.size() > labels.size())
        {
            while (values.size() > labels.size()) values.removeLast();
        }

        progress.clear(); hoverScale.clear(); hoverTarget.clear();
        for (int i = 0; i < labels.size(); ++i) { progress.add (0.0f); hoverScale.add (1.0f); hoverTarget.add (1.0f); }
    }

    // control
    void startExpand()
    {
        animStartMs = juce::Time::getMillisecondCounterHiRes();
        animating = true; expandingTarget = true; visible = true;
        for (int i = 0; i < progress.size(); ++i) progress.set (i, 0.0f);
    }

    void startCollapse()
    {
        animStartMs = juce::Time::getMillisecondCounterHiRes();
        animating = true; expandingTarget = false;
        for (int i = 0; i < progress.size(); ++i) progress.set (i, (i < progress.size()) ? progress.getReference(i) : 1.0f);
    }

    void toggle()
    {
        if (visible && ! animating) startCollapse();
        else if (! visible && ! animating) startExpand();
    }

    // input handling; baseX/baseY/baseR are the base circle center and radius
    // handleMouseDown returns the index clicked (>=0) or -1
    int handleMouseDown (juce::Point<float> pos, float baseX, float baseY, float baseR)
    {
        if (! visible && ! animating) return -1;
        const float finalDist = baseR + expansionRadius;
        const float startAngRad = juce::degreesToRadians (startAngleDeg);
        float spanRad = juce::degreesToRadians (endAngleDeg - startAngleDeg);
        if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
        const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
        const int n = values.size();
        const float denom = fullCircle ? (float) n : (n > 1 ? (float)(n - 1) : 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const float ang = startAngRad + ((float)i / denom) * spanRad;
            const float prog = (i < progress.size()) ? progress.getReference(i) : 1.0f;
            if (prog < 0.6f) continue; // gated
            const float px = baseX + std::cos (ang) * (finalDist * prog);
            const float py = baseY + std::sin (ang) * (finalDist * prog);
            const float dx = pos.x - px; const float dy = pos.y - py;
            const float baseScale = 0.7f + 0.3f * prog;
            const float hoverMult = (i < hoverScale.size()) ? hoverScale.getReference(i) : 1.0f;
            const float curR = itemR * baseScale * hoverMult;
            if (dx*dx + dy*dy <= curR * curR)
                return i;
        }
        return -1;
    }

    // handleMouseMove returns new hover index (-1 if none)
    int handleMouseMove (juce::Point<float> pos, float baseX, float baseY, float baseR)
    {
        if (! visible && ! animating) return -1;
        const float finalDist = baseR + expansionRadius;
        const float startAngRad = juce::degreesToRadians (startAngleDeg);
        float spanRad = juce::degreesToRadians (endAngleDeg - startAngleDeg);
        if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
        const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
        const int n = values.size();
        const float denom = fullCircle ? (float) n : (n > 1 ? (float)(n - 1) : 1.0f);

        int newHover = -1;
        for (int i = 0; i < n; ++i)
        {
            const float ang = startAngRad + ((float)i / denom) * spanRad;
            const float prog = (i < progress.size()) ? progress.getReference(i) : 1.0f;
            if (prog < 0.6f) continue;
            const float px = baseX + std::cos (ang) * (finalDist * prog);
            const float py = baseY + std::sin (ang) * (finalDist * prog);
            const float dx = pos.x - px; const float dy = pos.y - py;
            const float baseScale = 0.7f + 0.3f * prog;
            const float hoverMult = (i < hoverScale.size()) ? hoverScale.getReference(i) : 1.0f;
            const float curR = itemR * baseScale * hoverMult;
            if (dx*dx + dy*dy <= curR * curR) { newHover = i; break; }
        }

        if (newHover != hoverIndex)
        {
            hoverIndex = newHover;
            for (int j = 0; j < hoverTarget.size(); ++j) hoverTarget.set (j, (j == hoverIndex) ? 1.33f : 1.0f);
            // take a small immediate step
            for (int j = 0; j < hoverScale.size(); ++j)
            {
                const float cur = hoverScale.getReference(j);
                const float tgt = hoverTarget.getReference(j);
                hoverScale.set (j, cur + (tgt - cur) * 0.25f);
            }
        }
        return hoverIndex;
    }

    // draw current state
    void draw (juce::Graphics& g, float baseX, float baseY, float baseR)
    {
        if (! visible && ! animating) return;
        const float finalDist = baseR + expansionRadius;
        const float startAngRad = juce::degreesToRadians (startAngleDeg);
        float spanRad = juce::degreesToRadians (endAngleDeg - startAngleDeg);
        if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
        const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
        const float denom = fullCircle ? (float) values.size() : (values.size() > 1 ? (float)(values.size() - 1) : 1.0f);

        // non-hovered
        for (int i = 0; i < values.size(); ++i)
        {
            if (i == hoverIndex) continue;
            const float prog = juce::jlimit (0.0f, 1.0f, (i < progress.size()) ? progress.getReference(i) : 0.0f);
            if (prog <= 0.001f) continue;
            const float ang = startAngRad + ((float)i / denom) * spanRad;
            const float dist = finalDist * prog;
            const float px = baseX + std::cos (ang) * dist;
            const float py = baseY + std::sin (ang) * dist;
            const float scale = 0.7f + 0.3f * prog;
            float r = itemR * scale;
            const float hoverMult = (i < hoverScale.size()) ? hoverScale.getReference(i) : 1.0f;
            r *= hoverMult;
            g.setColour (UiThemeColours::accent());
            g.fillEllipse (px - r, py - r, r*2.0f, r*2.0f);
            if (i < labels.size())
            {
                const float fs = juce::jlimit (8.0f, 22.0f, r * 0.9f);
                g.setColour (UiThemeColours::base());
                g.setFont (juce::Font (juce::FontOptions ("Arial", fs, juce::Font::bold)));
                g.drawFittedText (labels[i], (int)(px - r), (int)(py - r), (int)(r*2.0f), (int)(r*2.0f), juce::Justification::centred, 1);
            }
        }

        // hovered last
        if (hoverIndex >= 0 && hoverIndex < values.size())
        {
            const int i = hoverIndex;
            const float prog = juce::jlimit (0.0f, 1.0f, (i < progress.size()) ? progress.getReference(i) : 0.0f);
            if (prog > 0.001f)
            {
                const float ang = startAngRad + ((float)i / denom) * spanRad;
                const float dist = finalDist * prog;
                const float px = baseX + std::cos (ang) * dist;
                const float py = baseY + std::sin (ang) * dist;
                const float scale = 0.7f + 0.3f * prog;
                float r = itemR * scale;
                const float hoverMult = (i < hoverScale.size()) ? hoverScale.getReference(i) : 1.0f;
                r *= hoverMult;
                g.setColour (UiThemeColours::cyan());
                g.fillEllipse (px - r, py - r, r*2.0f, r*2.0f);
                if (i < labels.size())
                {
                    const float fs = juce::jlimit (8.0f, 26.0f, r * 0.95f);
                    g.setColour (UiThemeColours::base());
                    g.setFont (juce::Font (juce::FontOptions ("Arial", fs, juce::Font::bold)));
                    g.drawFittedText (labels[i], (int)(px - r), (int)(py - r), (int)(r*2.0f), (int)(r*2.0f), juce::Justification::centred, 1);
                }
            }
        }
    }

    // update animation; should be called from parent's timerCallback
    void update (double nowMs, double dtMs)
    {
        if (! animating && hoverScale.size() == hoverTarget.size())
        {
            // still update hover smoothing even when not animating, but caller controls timer
            const double tau = 80.0;
            const float alpha = (float) (1.0 - std::exp (- dtMs / tau));
            for (int i = 0; i < hoverScale.size(); ++i)
            {
                const float cur = hoverScale.getReference(i);
                const float tgt = hoverTarget.getReference(i);
                const float next = cur + (tgt - cur) * alpha;
                hoverScale.set (i, next);
            }
            return;
        }

        if (! animating) return;

        bool allDone = true;
        for (int i = 0; i < progress.size(); ++i)
        {
            const double delayMs = (double)i * (double)staggerMs;
            const double local = (nowMs - animStartMs - delayMs) / (double)animDurationMs;
            const float t = (float) juce::jlimit (0.0, 1.0, local);
            float p = 0.0f;
            if (expandingTarget)
                p = easeOutCubic (t);
            else
                p = 1.0f - easeOutCubic (t);
            progress.set (i, p);
            const bool done = expandingTarget ? (p >= 0.999f) : (p <= 0.001f);
            if (! done) allDone = false;
        }

        // animate hover smoothing
        if (hoverScale.size() == hoverTarget.size())
        {
            const double tau = 80.0;
            const float alpha = (float) (1.0 - std::exp (- dtMs / tau));
            for (int i = 0; i < hoverScale.size(); ++i)
            {
                const float cur = hoverScale.getReference(i);
                const float tgt = hoverTarget.getReference(i);
                const float next = cur + (tgt - cur) * alpha;
                hoverScale.set (i, next);
            }
        }

        if (allDone)
        {
            animating = false;
            if (! expandingTarget)
            {
                visible = false;
                hoverIndex = -1;
            }
        }
    }

    // getters
    bool isAnimating() const noexcept { return animating; }
    bool isVisible() const noexcept { return visible; }
    int getHoverIndex() const noexcept { return hoverIndex; }
    juce::Array<int> getValues() const { return values; }
    juce::StringArray getLabels() const { return labels; }

private:
    float startAngleDeg = -90.0f;
    float endAngleDeg = 270.0f;
    float itemR = 12.0f;
    float expansionRadius = 6.0f; // offset added to base radius when laying out
    float fontSize = 12.0f;
    float animDurationMs = 166.0f;
    float staggerMs = 25.0f;

    juce::StringArray labels;
    juce::Array<int> values;

    juce::Array<float> progress; // 0..1 per item
    juce::Array<float> hoverScale;
    juce::Array<float> hoverTarget;

    bool visible = false;
    bool animating = false;
    bool expandingTarget = false;
    int hoverIndex = -1;

    double animStartMs = 0.0;

    static inline float easeOutCubic (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
};
