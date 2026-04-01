#include "PatternStoreTemplate.h"

namespace workflow_template
{

namespace ids
{
static const juce::Identifier root{"PATTERNS"};
static const juce::Identifier pattern{"PATTERN"};
static const juce::Identifier track{"TRACK"};
static const juce::Identifier step{"STEP"};
static const juce::Identifier index{"index"};
static const juce::Identifier current{"current"};
static const juce::Identifier value{"value"};
static const juce::Identifier length{"length"};
static const juce::Identifier shuffle{"shuffle"};
static const juce::Identifier scale{"scale"};
static const juce::Identifier name{"name"};
} // namespace ids

PatternStoreTemplate::PatternStoreTemplate()
    : root_(ids::root)
{
    initialiseDefaults();
    root_.addListener(this);
}

PatternStoreTemplate::~PatternStoreTemplate()
{
    root_.removeListener(this);
}

void PatternStoreTemplate::initialiseDefaults()
{
    root_.removeAllChildren(nullptr);
    root_.setProperty(ids::current, 0, nullptr);
    currentPattern_ = 0;

    for (int pattern = 0; pattern < kNumPatterns; ++pattern)
    {
        juce::ValueTree patternTree{ids::pattern};
        patternTree.setProperty(ids::index, pattern, nullptr);
        patternTree.setProperty(ids::name, "Pattern " + juce::String(pattern + 1), nullptr);
        patternTree.setProperty(ids::shuffle, 1, nullptr);
        patternTree.setProperty(ids::scale, 3, nullptr);

        for (int track = 0; track < kNumTracks; ++track)
        {
            juce::ValueTree trackTree{ids::track};
            trackTree.setProperty(ids::index, track, nullptr);
            trackTree.setProperty(ids::length, 16, nullptr);

            for (int step = 0; step < kNumSteps; ++step)
            {
                juce::ValueTree stepTree{ids::step};
                stepTree.setProperty(ids::index, step, nullptr);
                stepTree.setProperty(ids::value, 0, nullptr);
                trackTree.appendChild(stepTree, nullptr);
            }

            patternTree.appendChild(trackTree, nullptr);
        }

        root_.appendChild(patternTree, nullptr);
    }
}

juce::ValueTree PatternStoreTemplate::getPatternTree(int pattern) const
{
    if (pattern < 0 || pattern >= kNumPatterns)
        return {};
    return root_.getChild(pattern);
}

juce::ValueTree PatternStoreTemplate::getTrackTree(int pattern, int track) const
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid() || track < 0 || track >= kNumTracks)
        return {};
    return patternTree.getChild(track);
}

juce::ValueTree PatternStoreTemplate::getStepTree(int pattern, int track, int step) const
{
    auto trackTree = getTrackTree(pattern, track);
    if (!trackTree.isValid() || step < 0 || step >= kNumSteps)
        return {};
    return trackTree.getChild(step);
}

void PatternStoreTemplate::setCurrentPattern(int zeroBasedPattern)
{
    zeroBasedPattern = juce::jlimit(0, kNumPatterns - 1, zeroBasedPattern);
    if (currentPattern_ == zeroBasedPattern)
        return;

    currentPattern_ = zeroBasedPattern;
    root_.setProperty(ids::current, currentPattern_, nullptr);
}

int PatternStoreTemplate::getStep(int pattern, int track, int step) const
{
    auto stepTree = getStepTree(pattern, track, step);
    if (!stepTree.isValid())
        return 0;
    return (int)stepTree.getProperty(ids::value, 0);
}

void PatternStoreTemplate::setStep(int pattern, int track, int step, int value)
{
    auto stepTree = getStepTree(pattern, track, step);
    if (!stepTree.isValid())
        return;

    stepTree.setProperty(ids::value, juce::jmax(0, value), nullptr);
}

void PatternStoreTemplate::copyPattern(int fromPattern, int toPattern)
{
    fromPattern = juce::jlimit(0, kNumPatterns - 1, fromPattern);
    toPattern = juce::jlimit(0, kNumPatterns - 1, toPattern);
    if (fromPattern == toPattern)
        return;

    setPatternData(toPattern, getPatternData(fromPattern));
}

void PatternStoreTemplate::clearPattern(int pattern)
{
    pattern = juce::jlimit(0, kNumPatterns - 1, pattern);

    PatternData cleared;
    cleared.name = "Pattern " + juce::String(pattern + 1);
    cleared.shuffle = 1;
    cleared.scale = 3;

    for (int track = 0; track < kNumTracks; ++track)
        cleared.lengths[track] = 16;

    setPatternData(pattern, cleared);
}

int PatternStoreTemplate::getTrackLength(int pattern, int track) const
{
    auto trackTree = getTrackTree(pattern, track);
    if (!trackTree.isValid())
        return 16;
    return (int)trackTree.getProperty(ids::length, 16);
}

void PatternStoreTemplate::setTrackLength(int pattern, int track, int length)
{
    auto trackTree = getTrackTree(pattern, track);
    if (!trackTree.isValid())
        return;

    trackTree.setProperty(ids::length, juce::jlimit(1, kNumSteps, length), nullptr);
}

int PatternStoreTemplate::getShuffle(int pattern) const
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return 1;
    return (int)patternTree.getProperty(ids::shuffle, 1);
}

void PatternStoreTemplate::setShuffle(int pattern, int value)
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return;

    patternTree.setProperty(ids::shuffle, juce::jlimit(1, 7, value), nullptr);
}

int PatternStoreTemplate::getScale(int pattern) const
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return 3;
    return (int)patternTree.getProperty(ids::scale, 3);
}

void PatternStoreTemplate::setScale(int pattern, int value)
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return;

    patternTree.setProperty(ids::scale, juce::jlimit(0, 3, value), nullptr);
}

juce::String PatternStoreTemplate::getName(int pattern) const
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return {};
    return patternTree.getProperty(ids::name).toString();
}

void PatternStoreTemplate::setName(int pattern, const juce::String& name)
{
    auto patternTree = getPatternTree(pattern);
    if (!patternTree.isValid())
        return;

    patternTree.setProperty(ids::name, name, nullptr);
}

bool PatternStoreTemplate::isPatternOccupied(int pattern) const
{
    for (int track = 0; track < kNumTracks; ++track)
        for (int step = 0; step < kNumSteps; ++step)
            if (getStep(pattern, track, step) > 0)
                return true;

    return false;
}

PatternStoreTemplate::PatternData PatternStoreTemplate::getPatternData(int pattern) const
{
    PatternData data;
    data.name = getName(pattern);
    data.shuffle = getShuffle(pattern);
    data.scale = getScale(pattern);

    for (int track = 0; track < kNumTracks; ++track)
    {
        data.lengths[track] = getTrackLength(pattern, track);
        for (int step = 0; step < kNumSteps; ++step)
            data.steps[track][step] = getStep(pattern, track, step);
    }

    return data;
}

void PatternStoreTemplate::setPatternData(int pattern, const PatternData& data)
{
    setName(pattern, data.name);
    setShuffle(pattern, data.shuffle);
    setScale(pattern, data.scale);

    for (int track = 0; track < kNumTracks; ++track)
    {
        setTrackLength(pattern, track, data.lengths[track]);
        for (int step = 0; step < kNumSteps; ++step)
            setStep(pattern, track, step, data.steps[track][step]);
    }
}

juce::ValueTree PatternStoreTemplate::copyState() const
{
    return root_.createCopy();
}

void PatternStoreTemplate::restoreState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType(ids::root))
        return;

    root_.removeListener(this);
    root_ = state.createCopy();
    currentPattern_ = juce::jlimit(0, kNumPatterns - 1, (int)root_.getProperty(ids::current, 0));
    root_.addListener(this);

    listeners_.call([this](Listener& listener)
                    { listener.currentPatternChanged(currentPattern_); });
}

void PatternStoreTemplate::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void PatternStoreTemplate::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void PatternStoreTemplate::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == root_ && property == ids::current)
    {
        currentPattern_ = juce::jlimit(0, kNumPatterns - 1, (int)root_.getProperty(ids::current, 0));
        listeners_.call([this](Listener& listener)
                        { listener.currentPatternChanged(currentPattern_); });
        return;
    }

    if (tree.hasType(ids::step) && property == ids::value)
    {
        const auto trackTree = tree.getParent();
        const auto patternTree = trackTree.getParent();
        const int pattern = (int)patternTree.getProperty(ids::index, -1);
        const int track = (int)trackTree.getProperty(ids::index, -1);
        const int step = (int)tree.getProperty(ids::index, -1);
        const int value = (int)tree.getProperty(ids::value, 0);
        listeners_.call([=](Listener& listener)
                        { listener.stepChanged(pattern, track, step, value); });
        return;
    }

    if (tree.hasType(ids::track) && property == ids::length)
    {
        const auto patternTree = tree.getParent();
        const int pattern = (int)patternTree.getProperty(ids::index, -1);
        const int track = (int)tree.getProperty(ids::index, -1);
        const int length = (int)tree.getProperty(ids::length, 16);
        listeners_.call([=](Listener& listener)
                        { listener.trackLengthChanged(pattern, track, length); });
        return;
    }

    if (tree.hasType(ids::pattern) && (property == ids::name || property == ids::shuffle || property == ids::scale))
    {
        const int pattern = (int)tree.getProperty(ids::index, -1);
        listeners_.call([=](Listener& listener)
                        { listener.patternMetaChanged(pattern); });
    }
}

} // namespace workflow_template