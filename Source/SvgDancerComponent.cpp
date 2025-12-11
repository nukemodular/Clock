#include "SvgDancerComponent.h"
#include "BinaryData.h"

SvgDancerComponent::SvgDancerComponent()
{
    // Load SVG from BinaryData
    auto svgXml = juce::XmlDocument::parse(juce::String::createStringFromData(BinaryData::dancer_fix_svg, BinaryData::dancer_fix_svgSize));
    if (svgXml)
    {
        rootDrawable = juce::Drawable::createFromSVG(*svgXml);
        parseLayers();
    }
}

void SvgDancerComponent::parseLayers()
{
    if (auto* composite = dynamic_cast<juce::DrawableComposite*>(rootDrawable.get()))
    {
        // Iterate children to find layers
        auto children = composite->getChildren();
        
        // Resize layers vector to accommodate potential IDs
        // We assume IDs are layer0..layer23
        layers.resize(24, nullptr);
        
        for (auto* child : children)
        {
             juce::String id = child->getComponentID();
             if (id.startsWith("layer"))
             {
                 int index = id.substring(5).getIntValue();
                 if (index >= 0 && index < 24)
                 {
                     if (auto* d = dynamic_cast<juce::Drawable*>(child))
                     {
                         layers[index] = d;
                         d->setVisible(false); // Hide all initially
                     }
                 }
             }
        }
    }
}

void SvgDancerComponent::setFrame(int frameIndex)
{
    if (frameIndex < 0) frameIndex = 0;
    if (frameIndex >= layers.size()) frameIndex = (int)layers.size() - 1;
    
    if (currentFrame == frameIndex) return;
    
    currentFrame = frameIndex;
    repaint();
}

void SvgDancerComponent::setTint(juce::Colour c)
{
    if (tintColour != c)
    {
        tintColour = c;
        // Invalidate cache
        for (auto& img : frameCache)
            img = juce::Image();
        repaint();
    }
}

void SvgDancerComponent::resized()
{
    // Invalidate cache on resize
    for (auto& img : frameCache)
        img = juce::Image();
}

void SvgDancerComponent::paint(juce::Graphics& g)
{
    if (useCache)
    {
        auto img = getCachedFrame(currentFrame, getWidth(), getHeight());
        if (img.isValid())
            g.drawImage(img, getLocalBounds().toFloat());
    }
    else
    {
        // Direct drawing (fallback)
        if (currentFrame >= 0 && currentFrame < layers.size())
        {
            if (auto* d = layers[currentFrame])
            {
                // Hide all, show one, draw root
                for (auto* l : layers) if (l) l->setVisible(false);
                d->setVisible(true);
                
                if (rootDrawable)
                {
                    auto bounds = getLocalBounds().toFloat();
                    auto scaled = bounds.withSizeKeepingCentre(bounds.getWidth() * 0.85f, bounds.getHeight() * 0.85f);
                    rootDrawable->drawWithin(g, scaled, juce::RectanglePlacement::centred, 1.0f);
                }
                    
                d->setVisible(false);
            }
        }
    }
}

juce::Image SvgDancerComponent::getCachedFrame(int index, int w, int h)
{
    if (index < 0 || index >= layers.size()) return {};
    if (w <= 0 || h <= 0) return {};
    
    // Resize cache if needed
    if (frameCache.size() != layers.size()) frameCache.resize(layers.size());
    
    auto& img = frameCache[index];
    if (img.isNull() || img.getWidth() != w || img.getHeight() != h)
    {
        // Rasterize
        img = juce::Image(juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g(img);
            
            if (auto* d = layers[index])
            {
                // Hide all, show one, draw root
                for (auto* l : layers) if (l) l->setVisible(false);
                d->setVisible(true);
                
                if (rootDrawable)
                {
                    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h);
                    auto scaled = bounds.withSizeKeepingCentre(bounds.getWidth() * 0.75f, bounds.getHeight() * 0.75f);
                    rootDrawable->drawWithin(g, scaled, juce::RectanglePlacement::centred, 1.0f);
                }
                    
                d->setVisible(false);
            }
        }
        
        // Apply tint if needed
        if (!tintColour.isTransparent())
        {
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                {
                    auto col = bd.getPixelColour(x, y);
                    if (col.getAlpha() > 0)
                        bd.setPixelColour(x, y, tintColour.withAlpha(col.getFloatAlpha()));
                }
        }
    }
    return img;
}
