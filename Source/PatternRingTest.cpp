// PatternRingTest.cpp - lightweight internal test for PatternRing bitmask round-trip.
// Not a full unit test framework; invoked opportunistically from plugin code (add call if desired).

#include "Pattern.h"
#include <juce_core/juce_core.h>

namespace InternalTests
{
    bool testPatternRingBitmask()
    {
        PatternRing pr;
        // Construct a non-trivial bitmask pattern: alternating plus a few extras.
        uint16_t mask = 0;
        for (int i = 0; i < 16; ++i)
        {
            bool on = (i % 2 == 0);
            if (i == 3 || i == 9) on = true; // force a couple additional bits
            if (on) mask |= (uint16_t(1) << i);
        }
        pr.setBitmask(mask);
        const uint16_t round = pr.getBitmask();
        return (round == mask);
    }

    // Optional entry point (call from editor constructor if desired).
    void runAll()
    {
        const bool ok = testPatternRingBitmask();
        if (! ok)
        {
            juce::Logger::writeToLog("PatternRing bitmask test FAILED");
        }
        else
        {
            juce::Logger::writeToLog("PatternRing bitmask test passed");
        }
    }
}
