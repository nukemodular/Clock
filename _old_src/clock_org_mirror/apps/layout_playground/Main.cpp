// Simple wrapper application that uses the shared PlaygroundComponent implementation
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../Source/PlaygroundComponent.h"

using namespace juce;

class PlaygroundWindow : public juce::DocumentWindow
{
public:
    PlaygroundWindow() : juce::DocumentWindow ("Layout Playground", juce::Colours::black, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (new PlaygroundComponent(), true);
        centreWithSize (300, 240);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class PlaygroundApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Layout Playground"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise (const juce::String&) override { window.reset (new PlaygroundWindow()); }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}
private:
    std::unique_ptr<PlaygroundWindow> window;
};

START_JUCE_APPLICATION (PlaygroundApplication)
