#pragma once

#include <juce_core/juce_core.h>
#include "LicenseDialog.h"

namespace toolboy_license
{

class GumroadLicenseValidator
{
public:
    struct Config
    {
        juce::StringArray productIds;
        juce::String endpoint = "https://api.gumroad.com/v2/licenses/verify";
        bool incrementUsesCount = false;
        juce::String invalidResponseMessage = "Invalid Gumroad API response.";
        juce::String invalidLicenseMessage = "Invalid license key.";
    };

    static LicenseDialog::RemoteValidator makeValidator(Config config);
};

} // namespace toolboy_license