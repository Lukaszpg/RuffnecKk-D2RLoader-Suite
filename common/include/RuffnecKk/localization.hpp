#pragma once

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/localization.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RuffnecKk::Localization {

// Suite-owned descriptor for the proven Localization V2 service. The public
// SDK remains pinned. V2 preserves the callback ABI but namespaces string keys.
struct ServiceV2 {
    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    D2RL::Localization::GetStringByIdFn getStringById;
    D2RL::Localization::GetStringByKeyFn getStringByKey;
};
static_assert(std::is_standard_layout_v<ServiceV2>);
static_assert(sizeof(ServiceV2) == 24);
static_assert(offsetof(ServiceV2, getStringById) == 8);
static_assert(offsetof(ServiceV2, getStringByKey) == 16);

class Service final {
public:
    void Reset() noexcept {
        version_ = 0;
        getStringByKey_ = nullptr;
    }

    [[nodiscard]] auto Bind(const D2RL::PluginContext* context) noexcept -> bool {
        Reset();
        if (context == nullptr) return false;

        const ServiceV2* v2{};
        const auto result = context->QueryService(
            D2RL::ServiceId::Localization, 2, &v2);
        if (result == D2RL::ServiceQueryResult::Success) {
            if (v2 == nullptr || v2->serviceSize < sizeof(ServiceV2)
                || v2->serviceVersion != 2 || v2->getStringById == nullptr
                || v2->getStringByKey == nullptr) {
                return false;
            }
            version_ = 2;
            getStringByKey_ = v2->getStringByKey;
            return true;
        }
        if (result != D2RL::ServiceQueryResult::UnsupportedVersion) return false;

        const D2RL::LocalizationServiceV1* v1{};
        if (context->QueryService(D2RL::ServiceId::Localization,
                D2RL::LocalizationServiceV1Version, &v1)
                != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceV1Field(
                v1, D2RL::LocalizationServiceV1RequiredSize)
            || v1->getStringById == nullptr || v1->getStringByKey == nullptr) {
            return false;
        }
        version_ = 1;
        getStringByKey_ = v1->getStringByKey;
        return true;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return getStringByKey_ != nullptr;
    }

    [[nodiscard]] auto Version() const noexcept -> std::uint32_t {
        return version_;
    }

    // Callers supply both proven names: e.g. ItemStats1h / d2r:ItemStats1h.
    // Configurable or already-qualified keys are passed explicitly, unchanged.
    // No default namespace or JSON-layout '@' marker is added by this adapter.
    [[nodiscard]] auto GetStringByKey(
        const D2RL::PluginContext* context,
        const char* legacyKey,
        const char* v2Key,
        char* output,
        std::uint32_t outputSize,
        std::uint32_t* requiredSize) const noexcept -> D2RL::Localization::Result {
        if (getStringByKey_ == nullptr) return D2RL::Localization::Result::Unavailable;
        const auto* key = version_ == 2 ? v2Key : legacyKey;
        if (context == nullptr || key == nullptr) return D2RL::Localization::Result::InvalidArgument;
        return getStringByKey_(context, key, output, outputSize, requiredSize);
    }

private:
    std::uint32_t version_{};
    D2RL::Localization::GetStringByKeyFn getStringByKey_{};
};

} // namespace RuffnecKk::Localization
