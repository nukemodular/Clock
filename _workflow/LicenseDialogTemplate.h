#pragma once

#include <JuceHeader.h>

namespace workflow_template
{

class LicenseDialogTemplate : public juce::Component
{
  public:
    struct Config
    {
        juce::String title = "Register Plugin";
        juce::String emailLabel = "E-mail";
        juce::String keyLabel = "License Key";
        juce::String activateButtonText = "Activate Online";
        juce::Colour overlayColour = juce::Colours::black.withAlpha(0.82f);
        juce::Colour panelColour = juce::Colour(0xfff0f0f0);
        juce::Colour textColour = juce::Colours::black;
        juce::Colour accentColour = juce::Colour(0xff202020);
        juce::Image cardImage;
    };

    struct ValidationResult
    {
        bool ok = false;
        juce::String licensedUser;
        juce::String errorMessage;
    };

    using ValidationCallback = std::function<void(ValidationResult)>;
    using RemoteValidator = std::function<void(const juce::String& email,
                                               const juce::String& licenseKey,
                                               ValidationCallback completion)>;
    using SaveLicense = std::function<bool(const juce::String& licensedUser,
                                           const juce::String& providerKey)>;
    using OnValidated = std::function<void()>;

    LicenseDialogTemplate(Config config,
                          RemoteValidator remoteValidator,
                          SaveLicense saveLicense,
                          OnValidated onValidated);
    ~LicenseDialogTemplate() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    enum class StatusTone
    {
        neutral,
        busy,
        success,
        error
    };

    void applyTheme();
    void setStatus(juce::String message, StatusTone tone);
    void submit();
    juce::Rectangle<int> getCardBounds() const;

    Config config_;
    RemoteValidator remoteValidator_;
    SaveLicense saveLicense_;
    OnValidated onValidated_;

    juce::Label titleLabel_;
    juce::Label emailLabel_;
    juce::Label keyLabel_;
    juce::Label statusLabel_;
    juce::TextEditor emailField_;
    juce::TextEditor keyField_;
    juce::TextButton activateButton_;
    StatusTone statusTone_ = StatusTone::neutral;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseDialogTemplate)
};

} // namespace workflow_template