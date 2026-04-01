#pragma once

#include <JuceHeader.h>

namespace workflow_template
{

class LicenseManagerTemplate
{
  public:
    struct Config
    {
        juce::String appSupportFolder = "MyPlugin";
        juce::String licenseFileName = "myplugin.lic";
        juce::String hashSalt = "CHANGE_ME_PRODUCT_UNIQUE_SALT";
        juce::String fallbackMachineId = "fallback_machine_id";
    };

    static juce::File getAppDataDirectory(const Config& config);
    static juce::File getLicenseFile(const Config& config);
    static juce::String getMachineId(const Config& config);

    static bool loadLicense(const Config& config,
                            juce::String& licensedUser,
                            juce::String& storedHash);

    static bool validateHardware(const Config& config,
                                 const juce::String& licensedUser,
                                 const juce::String& storedHash);

    static bool isLicensed(const Config& config);

    static bool saveLicense(const Config& config,
                            const juce::String& licensedUser,
                            const juce::String& providerKey);

  private:
    static juce::String hashLicensePayload(const juce::String& payload);
};

} // namespace workflow_template