#pragma once

#include <array>

#include <JuceHeader.h>

#include "HostSyncShuffleTemplate.h"
#include "PatternStoreTemplate.h"

namespace workflow_template
{

class RuntimeSequencerTemplate
{
  public:
    static constexpr int kNumTracks = PatternStoreTemplate::kNumTracks;
    static constexpr int kNumSteps = PatternStoreTemplate::kNumSteps;

    struct RuntimeState
    {
        std::array<std::array<uint8_t, kNumSteps>, kNumTracks> steps{};
        std::array<int, kNumTracks> lengths{};
        int shuffle = 1;
        int scale = 3;
    };

    void applyPatternFromStore(const PatternStoreTemplate& store, int zeroBasedPattern)
    {
        const auto pattern = store.getPatternData(zeroBasedPattern);

        for (int track = 0; track < kNumTracks; ++track)
        {
            state_.lengths[(size_t)track] = juce::jlimit(1, kNumSteps, pattern.lengths[track]);
            for (int step = 0; step < kNumSteps; ++step)
                state_.steps[(size_t)track][(size_t)step] = (uint8_t)juce::jlimit(0, 127, pattern.steps[track][step]);
        }

        state_.shuffle = juce::jlimit(1, 7, pattern.shuffle);
        state_.scale = juce::jlimit(0, 3, pattern.scale);
    }

    void updateStep(int track, int absStep, int value)
    {
        if (track < 0 || track >= kNumTracks)
            return;

        absStep = juce::jlimit(0, kNumSteps - 1, absStep);
        state_.steps[(size_t)track][(size_t)absStep] = (uint8_t)juce::jlimit(0, 127, value);
    }

    void updateTrackLength(int track, int length)
    {
        if (track < 0 || track >= kNumTracks)
            return;

        state_.lengths[(size_t)track] = juce::jlimit(1, kNumSteps, length);
    }

    void setShuffle(int value) noexcept
    {
        state_.shuffle = juce::jlimit(1, 7, value);
    }

    const RuntimeState& getState() const noexcept
    {
        return state_;
    }

    template <typename TriggerCallback>
    bool processHostBlock(const HostBlockTemplate& block,
                          HostSyncShuffleTemplate& scheduler,
                          TriggerCallback&& trigger,
                          int beatsPerBar = 4) const
    {
        scheduler.setShuffleAmount(state_.shuffle);

        return scheduler.enumerate(
            block,
            [this, &trigger](const ScheduledStepTemplate& scheduled)
            {
                for (int track = 0; track < kNumTracks; ++track)
                {
                    const int length = juce::jlimit(1, kNumSteps, state_.lengths[(size_t)track]);
                    const int wrappedStep = wrapStep(scheduled.patternStep, length);
                    const int value = (int)state_.steps[(size_t)track][(size_t)wrappedStep];
                    if (value <= 0)
                        continue;

                    trigger(track, wrappedStep, value, scheduled.sampleOffset, scheduled);
                }
            },
            beatsPerBar);
    }

  private:
    RuntimeState state_;

    static int wrapStep(long long patternStep, int trackLength) noexcept
    {
        trackLength = juce::jlimit(1, kNumSteps, trackLength);
        const long long mod = patternStep % (long long)trackLength;
        return (int)(mod < 0 ? (mod + trackLength) : mod);
    }
};

} // namespace workflow_template