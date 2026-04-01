#include "LicenseManager.h"

namespace toolboy_license
{
namespace
{
bool writeLicenseFile(const juce::File& licenseFile, const juce::String& contents)
{
    const auto parentDir = licenseFile.getParentDirectory();
    if (! parentDir.exists() && ! parentDir.createDirectory())
        return false;

    return licenseFile.replaceWithText(contents);
}
}

juce::String LicenseManager::hashLicensePayload(const juce::String& payload)
{
    const auto utf8 = payload.toUTF8();
    return juce::MD5(utf8.getAddress(), utf8.sizeInBytes() - 1).toHexString();
}

juce::File LicenseManager::getAppDataDirectory(const juce::String& folderName)
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile(folderName);
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(folderName);
#endif
}

juce::File LicenseManager::getLicenseFile(const Config& config)
{
    return getAppDataDirectory(config.appSupportFolder).getChildFile(config.licenseFileName);
}

juce::File LicenseManager::getMirrorLicenseFile(const Config& config)
{
    if (config.mirrorSupportFolder.isEmpty())
        return {};

    return getAppDataDirectory(config.mirrorSupportFolder).getChildFile(config.licenseFileName);
}

juce::String LicenseManager::getMachineId(const Config& config)
{
    auto machineIds = juce::SystemStats::getMachineIdentifiers(juce::SystemStats::MachineIdFlags::uniqueId);
    return machineIds.isEmpty() ? config.fallbackMachineId : machineIds[0];
}

bool LicenseManager::loadLicense(const Config& config,
                                 juce::String& licensedUser,
                                 juce::String& storedHash)
{
    const auto tryLoad = [&licensedUser, &storedHash](const juce::File& licenseFile) -> bool
    {
        if (! licenseFile.existsAsFile())
            return false;

        const auto parts = juce::StringArray::fromTokens(licenseFile.loadFileAsString(), "|", "");
        if (parts.size() < 2)
            return false;

        licensedUser = parts[0].trim();
        storedHash = parts[1].trim();
        return licensedUser.isNotEmpty() && storedHash.isNotEmpty();
    };

    if (tryLoad(getLicenseFile(config)))
        return true;

    const auto mirrorFile = getMirrorLicenseFile(config);
    return mirrorFile != juce::File() && tryLoad(mirrorFile);
}

bool LicenseManager::validateHardware(const Config& config,
                                      const juce::String& licensedUser,
                                      const juce::String& storedHash)
{
    const auto payload = getMachineId(config) + licensedUser + config.hashSalt;
    return storedHash == hashLicensePayload(payload);
}

bool LicenseManager::isLicensed(const Config& config)
{
    juce::String licensedUser;
    juce::String storedHash;
    return loadLicense(config, licensedUser, storedHash)
        && validateHardware(config, licensedUser, storedHash);
}

bool LicenseManager::saveLicense(const Config& config,
                                 const juce::String& licensedUser,
                                 const juce::String& providerKey)
{
    juce::ignoreUnused(providerKey);

    const auto payload = getMachineId(config) + licensedUser + config.hashSalt;
    const auto hardwareHash = hashLicensePayload(payload);
    const auto contents = licensedUser + "|" + hardwareHash;

    const bool primaryOk = writeLicenseFile(getLicenseFile(config), contents);

    const auto mirrorFile = getMirrorLicenseFile(config);
    const bool needsMirror = mirrorFile != juce::File() && mirrorFile != getLicenseFile(config);
    const bool mirrorOk = ! needsMirror || writeLicenseFile(mirrorFile, contents);

    return primaryOk && mirrorOk;
}

} // namespace toolboy_license