#pragma once

#include <JuceHeader.h>

namespace workflow_template
{

class PatternStoreTemplate : private juce::ValueTree::Listener
{
  public:
    static constexpr int kNumPatterns = 64;
    static constexpr int kNumTracks = 12;
    static constexpr int kNumSteps = 64;

    struct PatternData
    {
        int steps[kNumTracks][kNumSteps]{};
        int lengths[kNumTracks]{};
        int shuffle = 1;
        int scale = 3;
        juce::String name;
    };

    class Listener
    {
      public:
        virtual ~Listener() = default;
        virtual void currentPatternChanged(int /*zeroBasedPattern*/) {}
        virtual void stepChanged(int /*pattern*/, int /*track*/, int /*step*/, int /*value*/) {}
        virtual void trackLengthChanged(int /*pattern*/, int /*track*/, int /*length*/) {}
        virtual void patternMetaChanged(int /*pattern*/) {}
    };

    PatternStoreTemplate();
    ~PatternStoreTemplate() override;

    int getCurrentPattern() const noexcept { return currentPattern_; }
    void setCurrentPattern(int zeroBasedPattern);

    int getStep(int pattern, int track, int step) const;
    void setStep(int pattern, int track, int step, int value);

    void copyPattern(int fromPattern, int toPattern);
    void clearPattern(int pattern);

    int getTrackLength(int pattern, int track) const;
    void setTrackLength(int pattern, int track, int length);

    int getShuffle(int pattern) const;
    void setShuffle(int pattern, int value);

    int getScale(int pattern) const;
    void setScale(int pattern, int value);

    juce::String getName(int pattern) const;
    void setName(int pattern, const juce::String& name);

    bool isPatternOccupied(int pattern) const;

    PatternData getPatternData(int pattern) const;
    void setPatternData(int pattern, const PatternData& data);

    juce::ValueTree copyState() const;
    void restoreState(const juce::ValueTree& state);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

  private:
    juce::ValueTree root_;
    juce::ListenerList<Listener> listeners_;
    int currentPattern_ = 0;

    juce::ValueTree getPatternTree(int pattern) const;
    juce::ValueTree getTrackTree(int pattern, int track) const;
    juce::ValueTree getStepTree(int pattern, int track, int step) const;
    void initialiseDefaults();

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
};

} // namespace workflow_template