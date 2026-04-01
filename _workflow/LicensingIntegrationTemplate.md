# Licensing Integration Template

This template extracts the reusable shape of the current licensing flow without tying it to seQOne naming, theme code, or asset names.

Keep the system split into three parts:
- `LicenseManagerTemplate.*`: local file storage plus machine-bound validation.
- `LicenseDialogTemplate.*`: a full-screen activation overlay that blocks interaction until validation succeeds.
- `GumroadLicenseValidatorTemplate.*`: optional provider-specific validator factory for Gumroad.
- project glue in the processor, editor, and build files.

## 1. Core flow

The current project follows this order:

1. Editor starts.
2. Processor exposes `isLicensedUI()` and `getLicenseUserUI()`.
3. If the product is unlicensed, the editor creates a full-bounds activation overlay and keeps it on top.
4. User enters e-mail and license key.
5. The overlay performs an async remote validation call.
6. On success, the overlay saves a local machine-bound license file.
7. The overlay hides itself and the editor resumes normal interaction.
8. On the next launch, local validation runs first and the overlay stays hidden when the stored hash matches the current machine.

That is the reusable procedure. Provider-specific API details belong behind the remote validator callback.

## 2. Local license storage

Use `LicenseManagerTemplate` as the stable local layer.

Replace these config values first:

```cpp
workflow_template::LicenseManagerTemplate::Config licenseConfig{
    .appSupportFolder = "MyCompany/MyPlugin",
    .licenseFileName = "myplugin.lic",
    .hashSalt = "CHANGE_ME_PRODUCT_UNIQUE_SALT",
    .fallbackMachineId = "fallback_machine_id",
};
```

Stored file format:
- `licensed-user|hardware-hash`

Recommended rules:
- keep the file small and plaintext
- hash `machineId + licensedUser + productSalt`
- keep the salt unique per product
- never trust the remote validation result alone on future launches; always re-check the stored hash against the current machine

## 3. Full-screen overlay behavior

The current plugin uses a full-editor overlay instead of a popup window. Keep that pattern.

Why it works well:
- blocks interaction without host-specific modal-window issues
- keeps styling fully inside the plugin editor
- makes resize handling trivial because the overlay simply tracks editor bounds

Minimum behavior worth keeping:
- `setInterceptsMouseClicks(true, true)` while visible
- `addAndMakeVisible(*licenseDialog)` in the editor constructor when unlicensed
- `licenseDialog->toFront(false)` after creation
- `licenseDialog->setBounds(getLocalBounds())` inside the editor `resized()`
- on success: `setInterceptsMouseClicks(false, false)` and `setVisible(false)`

The template dialog already implements that overlay pattern.

## 4. Remote validator example

The current project validates against Gumroad. Keep that integration behind the callback passed into `LicenseDialogTemplate`.

If you want that split out cleanly, use `GumroadLicenseValidatorTemplate` and only replace the product ID and optional endpoint.

```cpp
auto validator = workflow_template::GumroadLicenseValidatorTemplate::makeValidator({
    .productId = "YOUR_GUMROAD_PRODUCT_ID"
});
```

If you need a different provider, keep the same callback shape and replace only the validator helper.

Example shape:

```cpp
workflow_template::LicenseDialogTemplate::RemoteValidator validator =
    [productId](const juce::String& email,
                const juce::String& licenseKey,
                workflow_template::LicenseDialogTemplate::ValidationCallback completion)
{
    juce::ignoreUnused(email);

    juce::Thread::launch([productId, licenseKey, completion = std::move(completion)]() mutable
    {
        workflow_template::LicenseDialogTemplate::ValidationResult result;

        try
        {
            juce::URL url("https://api.gumroad.com/v2/licenses/verify");
            const juce::String postData = "product_id=" + juce::URL::addEscapeChars(productId, true)
                                        + "&license_key=" + juce::URL::addEscapeChars(licenseKey, true)
                                        + "&increment_uses_count=false";

            const auto response = url.withPOSTData(postData).readEntireTextStream(true);
            const auto parsed = juce::JSON::parse(response);

            if (auto* object = parsed.getDynamicObject(); object != nullptr)
            {
                result.ok = object->getProperty("success");
                if (result.ok)
                {
                    if (auto purchase = object->getProperty("purchase"); purchase.isObject())
                        result.licensedUser = purchase.getProperty("email", {}).toString();
                }
                else
                {
                    result.errorMessage = object->getProperty("message", "Invalid license").toString();
                }
            }
            else
            {
                result.errorMessage = "Invalid API response.";
            }
        }
        catch (const std::exception& exception)
        {
            result.errorMessage = exception.what();
        }

        completion(std::move(result));
    });
};
```

Keep the provider call off the message thread.

## 5. Processor hooks

The current processor-side API is intentionally small. Keep it that way.

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
    workflow_template::LicenseManagerTemplate::Config licenseConfig_;
};
```

That keeps editor code simple and avoids mixing validation UI logic into the processor.

## 6. Editor wiring

Minimal editor pattern:

```cpp
if (!processor.isLicensedUI())
{
    workflow_template::LicenseDialogTemplate::Config dialogConfig;
    dialogConfig.title = "Register My Plugin";
    dialogConfig.overlayColour = juce::Colours::black.withAlpha(0.88f);

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
            licensedToLabel.setVisible(true);
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

The current project also shows a passive `licensed to ...` label after activation. That part is optional but useful as a social watermark.

## 7. Assets and styling

The current project uses a plate image plus themed overlay colors. For a reusable project template, keep artwork optional.

A neutral vector starting point is included as `_workflow/assets/LicenseCardScaffold.svg`.
Use it directly as SVG binary data or export it to PNG at the size you need.

Asset checklist if you want the same layered look:
- one background card image at 1x or SVG
- optional 2x raster asset if you do not use SVG
- theme colors for overlay tint, card fill, text, and accent

Do not make the asset mandatory. The template dialog already falls back to a drawn card.

## 8. CMake checklist

Add the source files to your target list:

```cmake
set(MY_PLUGIN_SOURCE_FILES
    Source/Utils/LicenseManager.cpp
    Source/Utils/LicenseManager.h
    Source/Components/LicenseDialog.cpp
    Source/Components/LicenseDialog.h)
```

If you embed artwork, add it to binary data:

```cmake
juce_add_binary_data(MyPluginAssets
    HEADER_NAME MyPluginAssetsBinaryData.h
    SOURCES
        assets/license_plate.png)
```

Then link that binary-data target to the plugin target.

## 9. Project-specific values to replace every time

Before copying this flow into another project, change all of these:
- application support folder name
- local license file name
- hash salt
- validator endpoint and provider product ID
- overlay title and button text
- artwork asset names
- success and error copy shown to the user

## 10. References in this repo

The current concrete implementation lives here:
- `Source/Utils/LicenseManager.h`
- `Source/Utils/LicenseManager.cpp`
- `Source/Components/LicenseDialog.h`
- `Source/Components/LicenseDialog.cpp`
- `Source/PluginEditor.cpp`
- `Source/PluginProcessor.h`
- `LICENSE_SYSTEM_SETUP.md`

## 11. Fastest path

If you want the smallest setup instead of the full template set, use `MinimumLicensingTemplate.md`.