#include "LicenseDialogTemplate.h"

namespace workflow_template
{

namespace
{
bool looksLikeEmail(const juce::String& value)
{
    return value.contains("@")
        && value.fromFirstOccurrenceOf("@", false, false).contains(".");
}
}

LicenseDialogTemplate::LicenseDialogTemplate(Config config,
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
        label->setJustificationType(label == &titleLabel_ ? juce::Justification::centredLeft
                                                          : juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }

    for (auto* field : { &emailField_, &keyField_ })
    {
        field->setMultiLine(false);
        field->setReturnKeyStartsNewLine(false);
        addAndMakeVisible(*field);
    }

    keyField_.setPasswordCharacter(0);

    activateButton_.onClick = [this]
    {
        submit();
    };
    addAndMakeVisible(activateButton_);

    applyTheme();
}

void LicenseDialogTemplate::applyTheme()
{
    titleLabel_.setColour(juce::Label::textColourId, config_.textColour);
    emailLabel_.setColour(juce::Label::textColourId, config_.textColour);
    keyLabel_.setColour(juce::Label::textColourId, config_.textColour);
    activateButton_.setColour(juce::TextButton::buttonColourId, config_.accentColour);
    activateButton_.setColour(juce::TextButton::textColourOffId, config_.panelColour);

    for (auto* field : { &emailField_, &keyField_ })
    {
        field->setColour(juce::TextEditor::backgroundColourId, config_.panelColour.withAlpha(0.9f));
        field->setColour(juce::TextEditor::textColourId, config_.textColour);
        field->setColour(juce::CaretComponent::caretColourId, config_.accentColour);
        field->setColour(juce::TextEditor::outlineColourId, config_.accentColour.withAlpha(0.25f));
        field->setColour(juce::TextEditor::focusedOutlineColourId, config_.accentColour);
    }

    setStatus(statusLabel_.getText(), statusTone_);
}

void LicenseDialogTemplate::setStatus(juce::String message, StatusTone tone)
{
    statusTone_ = tone;
    statusLabel_.setText(std::move(message), juce::dontSendNotification);

    juce::Colour statusColour = config_.textColour;
    switch (tone)
    {
        case StatusTone::busy:
            statusColour = config_.textColour.brighter(0.15f);
            break;
        case StatusTone::success:
            statusColour = juce::Colours::darkgreen;
            break;
        case StatusTone::error:
            statusColour = juce::Colours::darkred;
            break;
        case StatusTone::neutral:
        default:
            break;
    }

    statusLabel_.setColour(juce::Label::textColourId, statusColour);
}

juce::Rectangle<int> LicenseDialogTemplate::getCardBounds() const
{
    const int width = juce::jmin(540, juce::jmax(320, getWidth() - 48));
    const int height = juce::jmin(320, juce::jmax(220, getHeight() - 48));
    juce::Rectangle<int> bounds(width, height);
    bounds.setCentre(getLocalBounds().getCentre());
    return bounds;
}

void LicenseDialogTemplate::paint(juce::Graphics& graphics)
{
    graphics.fillAll(config_.overlayColour);

    const auto cardBounds = getCardBounds();
    graphics.setColour(config_.panelColour);
    graphics.fillRoundedRectangle(cardBounds.toFloat(), 16.0f);

    graphics.setColour(config_.accentColour.withAlpha(0.18f));
    graphics.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), 16.0f, 1.0f);

    if (config_.cardImage.isValid())
        graphics.drawImage(config_.cardImage, cardBounds.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void LicenseDialogTemplate::resized()
{
    const auto cardBounds = getCardBounds().reduced(24, 20);

    auto row = cardBounds;
    titleLabel_.setBounds(row.removeFromTop(42));
    row.removeFromTop(12);

    emailLabel_.setBounds(row.removeFromTop(20));
    emailField_.setBounds(row.removeFromTop(34));
    row.removeFromTop(10);

    keyLabel_.setBounds(row.removeFromTop(20));
    keyField_.setBounds(row.removeFromTop(34));
    row.removeFromTop(14);

    statusLabel_.setBounds(row.removeFromTop(22));
    row.removeFromTop(10);

    activateButton_.setBounds(row.removeFromTop(34).withTrimmedLeft(6).withTrimmedRight(6));
}

bool LicenseDialogTemplate::keyPressed(const juce::KeyPress& key)
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

void LicenseDialogTemplate::submit()
{
    const auto email = emailField_.getText().trim();
    const auto licenseKey = keyField_.getText().trim();

    if (email.isEmpty() || licenseKey.isEmpty())
    {
        setStatus("Please enter e-mail and license key.", StatusTone::error);
        return;
    }

    if (!looksLikeEmail(email))
    {
        setStatus("Please enter a valid e-mail address.", StatusTone::error);
        return;
    }

    if (!remoteValidator_ || !saveLicense_)
    {
        setStatus("License validator is not configured.", StatusTone::error);
        return;
    }

    setStatus("Checking license...", StatusTone::busy);
    activateButton_.setEnabled(false);

    juce::Component::SafePointer<LicenseDialogTemplate> safeThis(this);
    remoteValidator_(email,
                     licenseKey,
                     [safeThis, licenseKey](ValidationResult result)
                     {
                         juce::MessageManager::callAsync([safeThis, result = std::move(result), licenseKey]() mutable
                         {
                             if (safeThis == nullptr)
                                 return;

                             if (!result.ok)
                             {
                                 safeThis->setStatus(result.errorMessage.isNotEmpty() ? result.errorMessage
                                                                                     : "License check failed.",
                                                     StatusTone::error);
                                 safeThis->activateButton_.setEnabled(true);
                                 return;
                             }

                             const auto licensedUser = result.licensedUser.isNotEmpty() ? result.licensedUser
                                                                                         : safeThis->emailField_.getText().trim();
                             if (!safeThis->saveLicense_(licensedUser, licenseKey))
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

} // namespace workflow_template