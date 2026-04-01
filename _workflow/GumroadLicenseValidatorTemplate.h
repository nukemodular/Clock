#pragma once

#include <JuceHeader.h>
#include "LicenseDialogTemplate.h"

namespace workflow_template
{

class GumroadLicenseValidatorTemplate
{
  public:
    struct Config
    {
        juce::String productId;
        juce::String endpoint = "https://api.gumroad.com/v2/licenses/verify";
        bool incrementUsesCount = false;
        juce::String invalidResponseMessage = "Invalid Gumroad API response.";
        juce::String invalidLicenseMessage = "Invalid license key.";
    };

    static LicenseDialogTemplate::RemoteValidator makeValidator(Config config);
};

} // namespace workflow_template