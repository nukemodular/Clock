#pragma once

#include <utility>

#include <JuceHeader.h>

#include "HostSyncShuffleTemplate.h"

namespace workflow_template
{

class JucePlayheadHostSyncTemplate
{
  public:
    static bool makeHostBlock(juce::AudioPlayHead* playHead,
                              double sampleRate,
                              int numSamples,
                              HostBlockTemplate& block)
    {
        block = {};

        if (playHead == nullptr || sampleRate <= 0.0 || numSamples <= 0)
            return false;

        auto position = playHead->getPosition();
        if (!position)
            return false;

        auto bpm = position->getBpm();
        auto ppq = position->getPpqPosition();
        if (!bpm.hasValue() || *bpm <= 0.0 || !ppq.hasValue())
            return false;

        block.playing = position->getIsPlaying();
        block.bpm = *bpm;
        block.ppqStart = *ppq;
        block.sampleRate = sampleRate;
        block.numSamples = numSamples;
        return block.isValid();
    }

    template <typename Callback>
    static bool enumerateFromPlayhead(juce::AudioPlayHead* playHead,
                                      double sampleRate,
                                      int numSamples,
                                      HostSyncShuffleTemplate& scheduler,
                                      Callback&& callback)
    {
        HostBlockTemplate block;
        if (!makeHostBlock(playHead, sampleRate, numSamples, block))
            return false;

        return scheduler.enumerate(block, std::forward<Callback>(callback));
    }
};

} // namespace workflow_template