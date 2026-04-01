#pragma once

#include <algorithm>
#include <cmath>

namespace workflow_template
{

struct HostBlockTemplate
{
    double bpm = 120.0;
    double ppqStart = 0.0;
    double sampleRate = 44100.0;
    int numSamples = 0;
    bool playing = false;

    bool isValid() const noexcept
    {
        return playing && bpm > 0.0 && sampleRate > 0.0 && numSamples > 0;
    }
};

struct ScheduledStepTemplate
{
    long long absoluteStep = 0;
    long long patternStep = 0;
    int sampleOffset = 0;
    int stepSamples = 0;
    bool isOffbeat = false;
    bool atBarBoundary = false;
};

class HostSyncShuffleTemplate
{
  public:
    void setStepsPerBeat(int value) noexcept
    {
        stepsPerBeat_ = std::max(1, value);
    }

    void setShuffleAmount(int value) noexcept
    {
        shuffleAmount_ = std::clamp(value, 1, 7);
    }

    void setPhaseOffsetSteps(long long value) noexcept
    {
        phaseOffsetSteps_ = value;
    }

    long long getPhaseOffsetSteps() const noexcept
    {
        return phaseOffsetSteps_;
    }

    static int computeShuffleDelaySamples(double sampleRate,
                                          double stepDurationSeconds,
                                          int shuffleAmount,
                                          bool isOffbeat) noexcept
    {
        if (!isOffbeat)
            return 0;

        const int shuffleUnits = std::max(0, std::clamp(shuffleAmount, 1, 7) - 1);
        const double oneClockSeconds = stepDurationSeconds / 12.0;
        return std::max(0, (int)std::llround(oneClockSeconds * sampleRate * (double)shuffleUnits));
    }

    template <typename Callback>
    bool enumerate(const HostBlockTemplate& block, Callback&& callback, int beatsPerBar = 4) const
    {
        if (!block.isValid())
            return false;

        const double ppqPerSecond = block.bpm / 60.0;
        const double blockDurationSeconds = (double)block.numSamples / block.sampleRate;
        const double ppqEnd = block.ppqStart + (blockDurationSeconds * ppqPerSecond);
        const double stepPpq = 1.0 / (double)stepsPerBeat_;
        const double stepDurationSeconds = stepPpq / ppqPerSecond;
        const int stepSamples = std::max(1, (int)std::llround(stepDurationSeconds * block.sampleRate));
        const int barSteps = std::max(1, stepsPerBeat_ * std::max(1, beatsPerBar));

        const long long firstStep = (long long)std::floor(block.ppqStart * (double)stepsPerBeat_);
        const long long lastStep = (long long)std::floor((ppqEnd - 1.0e-9) * (double)stepsPerBeat_);

        if (lastStep < firstStep)
            return false;

        for (long long absoluteStep = firstStep; absoluteStep <= lastStep; ++absoluteStep)
        {
            const double stepPpqPosition = (double)absoluteStep * stepPpq;
            const double stepTimeSeconds = (stepPpqPosition - block.ppqStart) / ppqPerSecond;
            const long long baseOffset = (long long)std::llround(stepTimeSeconds * block.sampleRate);

            if (baseOffset < 0)
                continue;

            const bool isOffbeat = ((absoluteStep & 1LL) != 0);
            int sampleOffset = (int)baseOffset;
            sampleOffset += computeShuffleDelaySamples(block.sampleRate, stepDurationSeconds, shuffleAmount_, isOffbeat);

            if (sampleOffset >= block.numSamples)
                continue;

            ScheduledStepTemplate scheduled;
            scheduled.absoluteStep = absoluteStep;
            scheduled.patternStep = absoluteStep - phaseOffsetSteps_;
            scheduled.sampleOffset = std::clamp(sampleOffset, 0, block.numSamples - 1);
            scheduled.stepSamples = stepSamples;
            scheduled.isOffbeat = isOffbeat;
            scheduled.atBarBoundary = ((absoluteStep % barSteps) == 0);

            callback(scheduled);
        }

        return true;
    }

  private:
    int stepsPerBeat_ = 4;
    int shuffleAmount_ = 1;
    long long phaseOffsetSteps_ = 0;
};

} // namespace workflow_template