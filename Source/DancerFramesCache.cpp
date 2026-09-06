#include "UiAssets.h"
#include "BinaryData.h"

#include <array>
#include <mutex>

namespace
{
    constexpr const char* kCanonicalViewBox = "0 0 520 520";

    struct EmbeddedSvg
    {
        const char* data;
        int size;
    };

    constexpr EmbeddedSvg makeSvg(const char* data, int size) noexcept
    {
        return { data, size };
    }

    void ensureVisibleAndRemoveGuideCircles(juce::XmlElement& element)
    {
        for (auto* child = element.getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            ensureVisibleAndRemoveGuideCircles(*child);

            if (child->hasTagName("circle"))
            {
                const auto radius = child->getDoubleAttribute("r", -1.0);
                if (radius >= 250.0)
                {
                    element.removeChildElement(child, true);
                    child = next;
                    continue;
                }
            }

            if (child->hasAttribute("style"))
            {
                auto style = child->getStringAttribute("style");
                if (style.containsIgnoreCase("visibility") || style.containsIgnoreCase("display"))
                {
                    style = style.replace("visibility: hidden;", "")
                                 .replace("visibility:hidden;", "")
                                 .replace("visibility: hidden", "")
                                 .replace("visibility:hidden", "")
                                 .replace("display: none;", "")
                                 .replace("display:none;", "")
                                 .replace("display: none", "")
                                 .replace("display:none", "");
                    child->setAttribute("style", style);
                }
            }
            if (child->hasAttribute("visibility"))
            {
                child->removeAttribute("visibility");
            }

            child = next;
        }
    }

    std::once_flag loadOnceFlag;
    std::vector<std::unique_ptr<juce::Drawable>> frames;

    const std::array<EmbeddedSvg, 24> embeddedFrames = {
        makeSvg(BinaryData::Dancer_1_svg, BinaryData::Dancer_1_svgSize),
        makeSvg(BinaryData::Dancer_2_svg, BinaryData::Dancer_2_svgSize),
        makeSvg(BinaryData::Dancer_3_svg, BinaryData::Dancer_3_svgSize),
        makeSvg(BinaryData::Dancer_4_svg, BinaryData::Dancer_4_svgSize),
        makeSvg(BinaryData::Dancer_5_svg, BinaryData::Dancer_5_svgSize),
        makeSvg(BinaryData::Dancer_6_svg, BinaryData::Dancer_6_svgSize),
        makeSvg(BinaryData::Dancer_7_svg, BinaryData::Dancer_7_svgSize),
        makeSvg(BinaryData::Dancer_8_svg, BinaryData::Dancer_8_svgSize),
        makeSvg(BinaryData::Dancer_9_svg, BinaryData::Dancer_9_svgSize),
        makeSvg(BinaryData::Dancer_10_svg, BinaryData::Dancer_10_svgSize),
        makeSvg(BinaryData::Dancer_11_svg, BinaryData::Dancer_11_svgSize),
        makeSvg(BinaryData::Dancer_12_svg, BinaryData::Dancer_12_svgSize),
        makeSvg(BinaryData::Dancer_13_svg, BinaryData::Dancer_13_svgSize),
        makeSvg(BinaryData::Dancer_14_svg, BinaryData::Dancer_14_svgSize),
        makeSvg(BinaryData::Dancer_15_svg, BinaryData::Dancer_15_svgSize),
        makeSvg(BinaryData::Dancer_16_svg, BinaryData::Dancer_16_svgSize),
        makeSvg(BinaryData::Dancer_17_svg, BinaryData::Dancer_17_svgSize),
        makeSvg(BinaryData::Dancer_18_svg, BinaryData::Dancer_18_svgSize),
        makeSvg(BinaryData::Dancer_19_svg, BinaryData::Dancer_19_svgSize),
        makeSvg(BinaryData::Dancer_20_svg, BinaryData::Dancer_20_svgSize),
        makeSvg(BinaryData::Dancer_21_svg, BinaryData::Dancer_21_svgSize),
        makeSvg(BinaryData::Dancer_22_svg, BinaryData::Dancer_22_svgSize),
        makeSvg(BinaryData::Dancer_23_svg, BinaryData::Dancer_23_svgSize),
        makeSvg(BinaryData::Dancer_24_svg, BinaryData::Dancer_24_svgSize)
    };
}

void DancerFramesCache::ensureLoaded()
{
    std::call_once(loadOnceFlag, []
    {
        frames.reserve(embeddedFrames.size());

        for (const auto& resource : embeddedFrames)
        {
            if (resource.data == nullptr || resource.size <= 0)
                continue;

            if (auto svgXml = juce::XmlDocument::parse(juce::String::fromUTF8(resource.data, resource.size)))
            {
                ensureVisibleAndRemoveGuideCircles(*svgXml);
                svgXml->setAttribute("viewBox", kCanonicalViewBox);
                svgXml->setAttribute("width", "520");
                svgXml->setAttribute("height", "520");
                svgXml->setAttribute("preserveAspectRatio", "xMidYMid meet");

                if (auto drawable = juce::Drawable::createFromSVGString(svgXml->toString()))
                    frames.push_back(std::move(drawable));
            }
        }
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
    if (index < 0 || index >= (int) frames.size())
        return nullptr;

    return frames[(size_t) index].get();
}