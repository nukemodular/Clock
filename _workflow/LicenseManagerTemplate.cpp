#include "LicenseManagerTemplate.h"

namespace workflow_template
{

juce::String LicenseManagerTemplate::hashLicensePayload(const juce::String& payload)
{
    return juce::MD5(payload).toHexString();
}

juce::File LicenseManagerTemplate::getAppDataDirectory(const Config& config)
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile(config.appSupportFolder);
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(config.appSupportFolder);
#endif
}

juce::File LicenseManagerTemplate::getLicenseFile(const Config& config)
{
    return getAppDataDirectory(config).getChildFile(config.licenseFileName);
}

juce::String LicenseManagerTemplate::getMachineId(const Config& config)
{
    auto machineIds = juce::SystemStats::getMachineIdentifiers(juce::SystemStats::MachineIdFlags::uniqueId);
    return machineIds.isEmpty() ? config.fallbackMachineId : machineIds[0];
}

bool LicenseManagerTemplate::loadLicense(const Config& config,
                                         juce::String& licensedUser,
                                         juce::String& storedHash)
{
    const auto licenseFile = getLicenseFile(config);
    if (!licenseFile.existsAsFile())
        return false;

    const auto parts = juce::StringArray::fromTokens(licenseFile.loadFileAsString(), "|", "");
    if (parts.size() < 2)
        return false;

    licensedUser = parts[0].trim();
    storedHash = parts[1].trim();
    return licensedUser.isNotEmpty() && storedHash.isNotEmpty();
}

bool LicenseManagerTemplate::validateHardware(const Config& config,
                                              const juce::String& licensedUser,
                                              const juce::String& storedHash)
{
    const auto payload = getMachineId(config) + licensedUser + config.hashSalt;
    return storedHash == hashLicensePayload(payload);
}

bool LicenseManagerTemplate::isLicensed(const Config& config)
{
    juce::String licensedUser;
    juce::String storedHash;
    return loadLicense(config, licensedUser, storedHash)
        && validateHardware(config, licensedUser, storedHash);
}

bool LicenseManagerTemplate::saveLicense(const Config& config,
                                         const juce::String& licensedUser,
                                         const juce::String& providerKey)
{
    juce::ignoreUnused(providerKey);

    const auto licenseFile = getLicenseFile(config);
    const auto parentDir = licenseFile.getParentDirectory();
    if (!parentDir.exists() && !parentDir.createDirectory())
        return false;

    const auto payload = getMachineId(config) + licensedUser + config.hashSalt;
    const auto hardwareHash = hashLicensePayload(payload);
    return licenseFile.replaceWithText(licensedUser + "|" + hardwareHash);
}

} // namespace workflow_template