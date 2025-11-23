﻿#include "DancerFramesCache.h"
#include "BinaryData.h"
#include <mutex>

namespace {
    std::once_flag loadOnceFlag;
    std::vector<std::unique_ptr<juce::Drawable>> frames;
    bool loaded = false;
    const juce::Colour accentColour = juce::Colour(0xFF4E5B); // #FF4E5B

    void scrubColours(juce::XmlElement& el)
    {
        if (el.hasTagName("path") || el.hasTagName("rect") || el.hasTagName("circle") || el.hasTagName("ellipse") || el.hasTagName("polygon") || el.hasTagName("polyline") || el.hasTagName("g"))
        {
            auto hex = accentColour.toDisplayString(false);
            if (! hex.startsWithChar('#')) hex = "#" + hex.removeCharacters("#");
            el.setAttribute("fill", hex);
            el.setAttribute("stroke", hex);
            el.removeAttribute("style"); // drop embedded styling to reduce parse overhead
        }
        forEachXmlChildElement(el, child) scrubColours(*child);
    }
}

void DancerFramesCache::ensureLoaded()
{
    std::call_once(loadOnceFlag, []
    {
        const char* ptr = BinaryData::dancer_all_svg;
        const size_t sz = BinaryData::dancer_all_svgSize;
        if (ptr == nullptr || sz == 0) { loaded = true; return; }
        juce::String svgText = juce::String::fromUTF8(ptr, (int) sz);
        juce::XmlDocument doc(svgText);
        std::unique_ptr<juce::XmlElement> root(doc.getDocumentElement());
        if (! root) { loaded = true; return; }
        scrubColours(*root); // pre-colour everything

        // Gather layer groups
        std::vector<juce::XmlElement*> layerGroups;
        for (auto* child = root->getFirstChildElement(); child != nullptr; child = child->getNextElement())
        {
            if (child->hasTagName("g"))
            {
                auto id = child->getStringAttribute("id");
                if (id.startsWithIgnoreCase("Layer"))
                    layerGroups.push_back(child);
            }
        }

        if (layerGroups.empty())
        {
            // Fallback: single drawable from whole root
            if (auto d = juce::Drawable::createFromSVG(*root))
                frames.push_back(std::move(d));
            loaded = true;
            return;
        }

        frames.reserve(layerGroups.size());
        for (auto* g : layerGroups)
        {
            juce::XmlElement svg("svg");
            if (root->hasAttribute("viewBox")) svg.setAttribute("viewBox", root->getStringAttribute("viewBox"));
            if (root->hasAttribute("width"))   svg.setAttribute("width", root->getStringAttribute("width"));
            if (root->hasAttribute("height"))  svg.setAttribute("height", root->getStringAttribute("height"));
            svg.setAttribute("xmlns", "http://www.w3.org/2000/svg");
            svg.addChildElement(new juce::XmlElement(*g));
            if (auto d = juce::Drawable::createFromSVG(svg))
                frames.push_back(std::move(d));
        }
        loaded = true;
    });
}

int DancerFramesCache::getFrameCount()
{
    ensureLoaded();
    return (int) frames.size();
}

juce::Drawable* DancerFramesCache::getFrame(int index)
{
    ensureLoaded();
    if (index < 0 || index >= (int) frames.size()) return nullptr;
    return frames[(size_t) index].get();
}
