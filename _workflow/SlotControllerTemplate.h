#pragma once

#include <functional>
#include <vector>

#include "PatternStoreTemplate.h"

namespace workflow_template
{

class SlotControllerTemplate
{
  public:
    using ApplySelectionFn = std::function<void(int /*zeroBasedPattern*/)>;

    explicit SlotControllerTemplate(PatternStoreTemplate& store)
        : store_(store)
    {
    }

    void captureCurrentAsPrevious() noexcept
    {
        previousPattern_ = store_.getCurrentPattern();
    }

    int getPreviousPattern() const noexcept
    {
        return previousPattern_;
    }

    void selectSlot(int oneBasedSlot, const ApplySelectionFn& applySelection)
    {
        captureCurrentAsPrevious();
        const int target = clampSlot(oneBasedSlot);
        store_.setCurrentPattern(target);

        if (applySelection)
            applySelection(target);
    }

    void storeCurrentIntoSlot(int oneBasedSlot, const ApplySelectionFn& applySelection)
    {
        const int source = store_.getCurrentPattern();
        storeIntoSlot(source, oneBasedSlot, applySelection);
    }

    void storePreviousIntoSlot(int oneBasedSlot, const ApplySelectionFn& applySelection)
    {
        storeIntoSlot(previousPattern_, oneBasedSlot, applySelection);
    }

    void clearSlot(int oneBasedSlot, const ApplySelectionFn& applySelection)
    {
        const int target = clampSlot(oneBasedSlot);
        const bool wasCurrent = (target == store_.getCurrentPattern());
        store_.clearPattern(target);

        if (wasCurrent && applySelection)
            applySelection(target);
    }

    std::vector<bool> buildOccupancy() const
    {
        std::vector<bool> occupied((size_t)PatternStoreTemplate::kNumPatterns, false);
        for (int pattern = 0; pattern < PatternStoreTemplate::kNumPatterns; ++pattern)
            occupied[(size_t)pattern] = store_.isPatternOccupied(pattern);
        return occupied;
    }

  private:
    PatternStoreTemplate& store_;
    int previousPattern_ = 0;

    static int clampSlot(int oneBasedSlot) noexcept
    {
        return juce::jlimit(0, PatternStoreTemplate::kNumPatterns - 1, oneBasedSlot - 1);
    }

    void storeIntoSlot(int sourcePattern, int oneBasedSlot, const ApplySelectionFn& applySelection)
    {
        sourcePattern = juce::jlimit(0, PatternStoreTemplate::kNumPatterns - 1, sourcePattern);
        const int target = clampSlot(oneBasedSlot);
        const bool wasCurrent = (target == store_.getCurrentPattern());

        store_.copyPattern(sourcePattern, target);

        if (!wasCurrent)
            store_.setCurrentPattern(target);

        if (applySelection)
            applySelection(target);
    }
};

} // namespace workflow_template