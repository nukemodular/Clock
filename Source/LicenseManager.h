#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

namespace toolboy_license
{

class LicenseManager
{
public:
    struct Config
    {
        juce::String appSupportFolder;
        juce::String licenseFileName;
        juce::String hashSalt;
        juce::String fallbackMachineId;
        juce::String mirrorSupportFolder;
    };

    static juce::File getAppDataDirectory(const juce::String& folderName);
    static juce::File getLicenseFile(const Config& config);
    static juce::File getMirrorLicenseFile(const Config& config);
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

} // namespace toolboy_license