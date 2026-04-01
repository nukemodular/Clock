#include "SvgDancerComponent.h"

#include "UiAssets.h"

namespace
{
    constexpr float kDancerAnchorSize = 520.0f;
}

SvgDancerComponent::SvgDancerComponent()
{
    rebuildTintedFrames();
}

void SvgDancerComponent::setFrame(int frameIndex)
{
    const auto totalFrames = DancerFramesCache::getFrameCount();
    if (totalFrames <= 0)
        return;

    frameIndex = juce::jlimit(0, totalFrames - 1, frameIndex);

    if (currentFrame == frameIndex)
        return;

    currentFrame = frameIndex;

    repaint();
}

void SvgDancerComponent::setTint(juce::Colour c)
{
    if (tintColour == c)
        return;

    tintColour = c;
    rebuildTintedFrames();
    repaint();
}

void SvgDancerComponent::resized()
{
}

void SvgDancerComponent::paint(juce::Graphics& g)
{
    const auto frameCount = DancerFramesCache::getFrameCount();
    if (currentFrame < 0 || currentFrame >= frameCount)
        return;

    if ((int) tintedFrames.size() != frameCount)
        rebuildTintedFrames();

    if (currentFrame < 0 || currentFrame >= (int) tintedFrames.size())
        return;

    if (auto* drawable = tintedFrames[(size_t) currentFrame].get())
    {
        auto scaled = getLocalBounds().toFloat().reduced(15.0f);

        const auto sourceBounds = juce::Rectangle<float>(0.0f, 0.0f, kDancerAnchorSize, kDancerAnchorSize);
        const auto transform = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                   .getTransformToFit(sourceBounds, scaled);

        drawable->draw(g, 1.0f, transform);
    }
}

void SvgDancerComponent::rebuildTintedFrames()
{
    const auto frameCount = DancerFramesCache::getFrameCount();
    tintedFrames.clear();
    tintedFrames.resize((size_t) juce::jmax(0, frameCount));

    const auto replacement = tintColour.isTransparent() ? juce::Colour::fromRGB(0xFF, 0x14, 0x00)
                                                         : tintColour;

    for (int index = 0; index < frameCount; ++index)
    {
        if (auto* original = DancerFramesCache::getFrame(index))
        {
            auto copy = original->createCopy();
            if (copy)
            {
                copy->replaceColour(juce::Colour::fromRGB(0xFF, 0x14, 0x00), replacement);
                copy->replaceColour(juce::Colour::fromRGB(0xFF, 0x00, 0x06), replacement);
                copy->replaceColour(juce::Colour::fromRGBA(0xFA, 0x22, 0x03, 0xFF), replacement);
                copy->replaceColour(juce::Colour::fromRGBA(0x7C, 0x8B, 0xFF, 0x87), replacement.withAlpha(0.53f));
                copy->replaceColour(juce::Colour::fromRGBA(0x75, 0xFF, 0xC5, 0xBF), replacement.withAlpha(0.75f));
                tintedFrames[(size_t) index] = std::move(copy);
            }
        }
    }
}