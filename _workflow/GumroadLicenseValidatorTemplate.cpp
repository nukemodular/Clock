#include "GumroadLicenseValidatorTemplate.h"

namespace workflow_template
{

LicenseDialogTemplate::RemoteValidator GumroadLicenseValidatorTemplate::makeValidator(Config config)
{
    return [config = std::move(config)](const juce::String& email,
                                        const juce::String& licenseKey,
                                        LicenseDialogTemplate::ValidationCallback completion)
    {
        juce::Thread::launch([config, email, licenseKey, completion = std::move(completion)]() mutable
        {
            LicenseDialogTemplate::ValidationResult result;

            try
            {
                juce::URL url(config.endpoint);
                const juce::String postData = "product_id=" + juce::URL::addEscapeChars(config.productId, true)
                                            + "&license_key=" + juce::URL::addEscapeChars(licenseKey, true)
                                            + "&increment_uses_count=" + juce::String(config.incrementUsesCount ? "true" : "false");

                const auto response = url.withPOSTData(postData).readEntireTextStream(true);
                const auto parsed = juce::JSON::parse(response);

                auto* object = parsed.getDynamicObject();
                if (object == nullptr)
                {
                    result.errorMessage = config.invalidResponseMessage;
                    completion(std::move(result));
                    return;
                }

                const auto successVar = object->getProperty("success");
                result.ok = successVar.isBool() ? (bool) successVar : false;
                if (!result.ok)
                {
                    const auto message = object->getProperty("message").toString().trim();
                    result.errorMessage = message.isNotEmpty() ? message : config.invalidLicenseMessage;
                    completion(std::move(result));
                    return;
                }

                result.licensedUser = email;
                const auto purchase = object->getProperty("purchase");
                if (purchase.isObject())
                {
                    if (auto* purchaseObject = purchase.getDynamicObject())
                    {
                        const auto purchaseEmail = purchaseObject->getProperty("email").toString().trim();
                        if (purchaseEmail.isNotEmpty())
                            result.licensedUser = purchaseEmail;
                    }
                }
            }
            catch (const std::exception& exception)
            {
                result.errorMessage = exception.what();
            }

            completion(std::move(result));
        });
    };
}

} // namespace workflow_template