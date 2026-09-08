#include "LicenseDialog.h"

namespace toolboy_license
{
namespace
{
bool looksLikeEmail(const juce::String& value)
{
    return value.contains("@")
        && value.fromFirstOccurrenceOf("@", false, false).contains(".");
}
}

LicenseDialog::LicenseDialog(Config config,
                             RemoteValidator remoteValidator,
                             SaveLicense saveLicense,
                             OnValidated onValidated)
    : config_(std::move(config)),
      remoteValidator_(std::move(remoteValidator)),
      saveLicense_(std::move(saveLicense)),
      onValidated_(std::move(onValidated))
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);

    titleLabel_.setText(config_.title, juce::dontSendNotification);
    emailLabel_.setText(config_.emailLabel, juce::dontSendNotification);
    keyLabel_.setText(config_.keyLabel, juce::dontSendNotification);
    activateButton_.setButtonText(config_.activateButtonText);

    for (auto* label : { &titleLabel_, &emailLabel_, &keyLabel_, &statusLabel_ })
    {
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }

    for (auto* field : { &emailField_, &keyField_ })
    {
        field->setMultiLine(false);
        field->setReturnKeyStartsNewLine(false);
        addAndMakeVisible(*field);
    }

    activateButton_.onClick = [this]() { submit(); };
    addAndMakeVisible(activateButton_);

    applyTheme();
}

void LicenseDialog::applyTheme()
{
    titleLabel_.setColour(juce::Label::textColourId, config_.textColour);
    emailLabel_.setColour(juce::Label::textColourId, config_.textColour.withAlpha(0.8f));
    keyLabel_.setColour(juce::Label::textColourId, config_.textColour.withAlpha(0.8f));
    activateButton_.setColour(juce::TextButton::buttonColourId, config_.accentColour);
    activateButton_.setColour(juce::TextButton::textColourOffId, config_.panelColour);

    for (auto* field : { &emailField_, &keyField_ })
    {
        field->setColour(juce::TextEditor::backgroundColourId, config_.panelColour.brighter(0.08f));
        field->setColour(juce::TextEditor::textColourId, config_.textColour);
        field->setColour(juce::CaretComponent::caretColourId, config_.accentColour);
        field->setColour(juce::TextEditor::outlineColourId, config_.accentColour.withAlpha(0.25f));
        field->setColour(juce::TextEditor::focusedOutlineColourId, config_.accentColour);
    }

    titleLabel_.setFont(juce::Font(juce::FontOptions("Arial", 18.0f, juce::Font::bold)));
    emailLabel_.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    keyLabel_.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    statusLabel_.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::plain)));

    setStatus(statusLabel_.getText(), statusTone_);
}

void LicenseDialog::setStatus(juce::String message, StatusTone tone)
{
    statusTone_ = tone;
    statusLabel_.setText(std::move(message), juce::dontSendNotification);

    juce::Colour statusColour = config_.textColour.withAlpha(0.75f);
    switch (tone)
    {
        case StatusTone::busy: statusColour = config_.accentColour; break;
        case StatusTone::success: statusColour = juce::Colour(0xff6ce091); break;
        case StatusTone::error: statusColour = juce::Colour(0xffff7373); break;
        case StatusTone::neutral: default: break;
    }

    statusLabel_.setColour(juce::Label::textColourId, statusColour);
}

juce::Rectangle<int> LicenseDialog::getCardBounds() const
{
    const int width = juce::jmin(300, juce::jmax(260, getWidth() - 24));
    const int height = juce::jmin(240, juce::jmax(220, getHeight() - 24));
    juce::Rectangle<int> bounds(width, height);
    bounds.setCentre(getLocalBounds().getCentre());
    return bounds;
}

void LicenseDialog::paint(juce::Graphics& graphics)
{
    graphics.fillAll(config_.overlayColour);

    const auto cardBounds = getCardBounds();
    graphics.setColour(config_.panelColour.withAlpha(0.97f));
    graphics.fillRoundedRectangle(cardBounds.toFloat(), 16.0f);

    graphics.setColour(config_.accentColour.withAlpha(0.18f));
    graphics.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), 16.0f, 2.0f);
}

void LicenseDialog::resized()
{
    auto cardBounds = getCardBounds().reduced(16, 16);

    titleLabel_.setBounds(cardBounds.removeFromTop(24));
    cardBounds.removeFromTop(8);

    emailLabel_.setBounds(cardBounds.removeFromTop(14));
    emailField_.setBounds(cardBounds.removeFromTop(26));
    cardBounds.removeFromTop(8);

    keyLabel_.setBounds(cardBounds.removeFromTop(14));
    keyField_.setBounds(cardBounds.removeFromTop(26));
    cardBounds.removeFromTop(8);

    statusLabel_.setBounds(cardBounds.removeFromTop(24));
    cardBounds.removeFromTop(10);

    activateButton_.setBounds(cardBounds.removeFromTop(30));
}

bool LicenseDialog::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        submit();
        return true;
    }

    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::tabKey)
        return true;

    return false;
}

void LicenseDialog::submit()
{
    const auto email = emailField_.getText().trim();
    const auto licenseKey = keyField_.getText().trim();

    if (email.isEmpty() || licenseKey.isEmpty())
    {
        setStatus("Please enter e-mail and license key.", StatusTone::error);
        return;
    }

    if (! looksLikeEmail(email))
    {
        setStatus("Please enter a valid e-mail address.", StatusTone::error);
        return;
    }

    if (! remoteValidator_ || ! saveLicense_)
    {
        setStatus("License validator is not configured.", StatusTone::error);
        return;
    }

    setStatus("Checking license...", StatusTone::busy);
    activateButton_.setEnabled(false);

    juce::Component::SafePointer<LicenseDialog> safeThis(this);
    remoteValidator_(email,
                     licenseKey,
                     [safeThis, licenseKey](ValidationResult result)
                     {
                         juce::MessageManager::callAsync([safeThis, result = std::move(result), licenseKey]() mutable
                         {
                             if (safeThis == nullptr)
                                 return;

                             if (! result.ok)
                             {
                                 safeThis->setStatus(result.errorMessage.isNotEmpty() ? result.errorMessage
                                                                                     : "License check failed.",
                                                     StatusTone::error);
                                 safeThis->activateButton_.setEnabled(true);
                                 return;
                             }

                             const auto licensedUser = result.licensedUser.isNotEmpty()
                                                         ? result.licensedUser
                                                         : safeThis->emailField_.getText().trim();

                             if (! safeThis->saveLicense_(licensedUser, licenseKey))
                             {
                                 safeThis->setStatus("License was valid but could not be saved.", StatusTone::error);
                                 safeThis->activateButton_.setEnabled(true);
                                 return;
                             }

                             safeThis->setStatus("License activated.", StatusTone::success);
                             safeThis->setInterceptsMouseClicks(false, false);
                             safeThis->setVisible(false);

                             if (safeThis->onValidated_)
                                 safeThis->onValidated_();
                         });
                     });
}

} // namespace toolboy_license