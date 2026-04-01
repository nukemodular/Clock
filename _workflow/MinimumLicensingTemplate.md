# Minimum Licensing Template

This is the shortest version of the licensing setup that still gives you a usable machine-bound activation flow.

Use this when you do not want the full extracted styling/template stack yet.

## Files to copy

Required:
- `LicenseManagerTemplate.h`
- `LicenseManagerTemplate.cpp`
- `LicenseDialogTemplate.h`
- `LicenseDialogTemplate.cpp`

Optional but recommended:
- `GumroadLicenseValidatorTemplate.h`
- `GumroadLicenseValidatorTemplate.cpp`
- `assets/LicenseCardScaffold.svg`

## Minimal processor API

```cpp
class MyProcessor : public juce::AudioProcessor
{
public:
    bool isLicensedUI() const
    {
        return workflow_template::LicenseManagerTemplate::isLicensed(licenseConfig_);
    }

    juce::String getLicenseUserUI() const
    {
        juce::String licensedUser;
        juce::String storedHash;
        if (workflow_template::LicenseManagerTemplate::loadLicense(licenseConfig_, licensedUser, storedHash))
            return licensedUser;
        return {};
    }

private:
    workflow_template::LicenseManagerTemplate::Config licenseConfig_{
        "MyPlugin",
        "myplugin.lic",
        "CHANGE_ME_PRODUCT_UNIQUE_SALT",
        "fallback_machine_id"
    };
};
```

## Minimal editor wiring

```cpp
if (!processor.isLicensedUI())
{
    workflow_template::LicenseDialogTemplate::Config dialogConfig;
    dialogConfig.title = "Register My Plugin";

    auto validator = workflow_template::GumroadLicenseValidatorTemplate::makeValidator({
        .productId = "YOUR_GUMROAD_PRODUCT_ID"
    });

    licenseDialog = std::make_unique<workflow_template::LicenseDialogTemplate>(
        dialogConfig,
        validator,
        [this](const juce::String& licensedUser, const juce::String& providerKey)
        {
            return workflow_template::LicenseManagerTemplate::saveLicense(licenseConfig_, licensedUser, providerKey);
        },
        [this]
        {
            licensedToLabel.setText("licensed to " + processor.getLicenseUserUI(), juce::dontSendNotification);
        });

    addAndMakeVisible(*licenseDialog);
    licenseDialog->toFront(false);
}
```

And in `resized()`:

```cpp
if (licenseDialog)
    licenseDialog->setBounds(getLocalBounds());
```

## Minimal build wiring

```cmake
target_sources(MyPlugin PRIVATE
    Source/Utils/LicenseManager.cpp
    Source/Utils/LicenseManager.h
    Source/Components/LicenseDialog.cpp
    Source/Components/LicenseDialog.h
    Source/Utils/GumroadLicenseValidator.cpp
    Source/Utils/GumroadLicenseValidator.h)
```

If you embed the scaffold image:

```cmake
juce_add_binary_data(MyPluginAssets
    HEADER_NAME MyPluginAssetsBinaryData.h
    SOURCES
        assets/license_card_scaffold.svg)
```

## Replace these values before shipping

- app support folder
- license filename
- product salt
- Gumroad product ID
- overlay title text
- asset filename if you use artwork

## Minimum rules to keep

- do remote validation off the message thread
- store only a small local license file
- revalidate the stored file against the current machine on startup
- keep the activation UI as a full-editor overlay instead of a native modal window