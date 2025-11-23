#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace SvgUtils
{
    // Returns a copy of attributes that are required to render a valid SVG document
    inline void copyEssentialSvgAttributes (const juce::XmlElement& from, juce::XmlElement& to)
    {
        // Copy common sizing attributes if present
        if (from.hasAttribute ("width"))    to.setAttribute ("width",    from.getStringAttribute ("width"));
        if (from.hasAttribute ("height"))   to.setAttribute ("height",   from.getStringAttribute ("height"));
        if (from.hasAttribute ("viewBox"))  to.setAttribute ("viewBox",  from.getStringAttribute ("viewBox"));
        if (from.hasAttribute ("preserveAspectRatio"))
            to.setAttribute ("preserveAspectRatio", from.getStringAttribute ("preserveAspectRatio"));

        // Ensure base namespaces
        to.setAttribute ("xmlns", "http://www.w3.org/2000/svg");
        to.setAttribute ("xmlns:xlink", "http://www.w3.org/1999/xlink");
    }

    inline std::unique_ptr<juce::XmlElement> deepCopyIfExists (const juce::XmlElement* el)
    {
        return el != nullptr ? std::make_unique<juce::XmlElement> (*el) : nullptr;
    }

    inline juce::XmlElement* findFirstChildByName (const juce::XmlElement& parent, const juce::String& name)
    {
        for (auto* c = parent.getFirstChildElement(); c != nullptr; c = c->getNextElement())
            if (c->hasTagName (name))
                return c;
        return nullptr;
    }

    // Heuristic: treat <g> with inkscape:groupmode="layer" as a layer. If not present,
    // use any top-level <g> with either an id or inkscape:label.
    inline bool isLayerGroup (const juce::XmlElement& g)
    {
        if (! g.hasTagName ("g"))
            return false;

        const auto mode  = g.getStringAttribute ("inkscape:groupmode");
        const auto label = g.getStringAttribute ("inkscape:label");
        const auto id    = g.getStringAttribute ("id");

        if (mode.equalsIgnoreCase ("layer"))
            return true;

        return label.isNotEmpty() || id.isNotEmpty();
    }

    // Extract top-level layer groups from an SVG root element and build a Drawable for each.
    // Returns vector of (layerName, Drawable) in document order.
    inline std::vector<std::pair<juce::String, std::unique_ptr<juce::Drawable>>>
    extractLayersAsDrawables (const juce::XmlElement& svgRoot)
    {
        std::vector<std::pair<juce::String, std::unique_ptr<juce::Drawable>>> out;

        if (! svgRoot.hasTagName ("svg"))
            return out;

        // Optional: copy <defs> to preserve gradients, symbols, styles
        auto* defs = findFirstChildByName (svgRoot, "defs");
        auto defsCopy = deepCopyIfExists (defs);

        int layerIndex = 0;
        for (auto* child = svgRoot.getFirstChildElement(); child != nullptr; child = child->getNextElement())
        {
            if (! isLayerGroup (*child))
                continue;

            juce::XmlElement svg ("svg");
            copyEssentialSvgAttributes (svgRoot, svg);

            if (defsCopy)
                svg.addChildElement (new juce::XmlElement (*defsCopy));

            svg.addChildElement (new juce::XmlElement (*child));

            if (auto drawable = juce::Drawable::createFromSVG (svg))
            {
                auto name = child->getStringAttribute ("inkscape:label");
                if (name.isEmpty())
                    name = child->getStringAttribute ("id");
                if (name.isEmpty())
                    name = juce::String ("layer") + juce::String (++layerIndex);

                out.emplace_back (name, std::move (drawable));
            }
        }

        return out;
    }

    // Convenience: Parse from a string
    inline std::vector<std::pair<juce::String, std::unique_ptr<juce::Drawable>>>
    extractLayersFromString (const juce::String& svgText)
    {
        juce::XmlDocument doc (svgText);
        auto root = std::unique_ptr<juce::XmlElement> (doc.getDocumentElement());
        if (root == nullptr)
            return {};
        return extractLayersAsDrawables (*root);
    }

    // Convenience: Parse from a file
    inline std::vector<std::pair<juce::String, std::unique_ptr<juce::Drawable>>>
    extractLayersFromFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return {};
        juce::XmlDocument doc (file);
        auto root = std::unique_ptr<juce::XmlElement> (doc.getDocumentElement());
        if (root == nullptr)
            return {};
        return extractLayersAsDrawables (*root);
    }
}
