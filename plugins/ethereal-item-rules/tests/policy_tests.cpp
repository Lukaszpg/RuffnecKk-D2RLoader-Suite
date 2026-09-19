#include "policy.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

auto ReadAll(const char* path) -> std::string {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    auto text = stream.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

int main(int argc, char** argv) {
    using namespace RuffnecKk::EtherealItemRules;

    ItemTypeCode belt{};
    REQUIRE(NormalizeItemTypeCode(" BeLt ", belt));
    REQUIRE(belt.text[0] == 'b' && belt.text[3] == 't');

    ItemTypeCode gem{};
    REQUIRE(NormalizeItemTypeCode("gem", gem));
    REQUIRE(gem.bytes[3] == ' ');

    ItemTypeCode invalid{};
    REQUIRE(!NormalizeItemTypeCode("too-long", invalid));
    REQUIRE(!NormalizeItemTypeCode("a-b", invalid));

    struct Record {
        std::array<char, 4> code{};
        std::array<std::uint8_t, ItemTypeRecordStride - 4> padding{};
    };
    static_assert(sizeof(Record) == ItemTypeRecordStride);
    std::array<Record, 3> records{};
    std::memcpy(records[0].code.data(), "armo", 4);
    std::memcpy(records[1].code.data(), "belt", 4);
    std::memcpy(records[2].code.data(), "gem ", 4);
    REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), belt) == 1);
    REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), gem) == 2);
    REQUIRE(FindItemTypeId(nullptr, records.size(), sizeof(Record), belt) == -1);
    REQUIRE(FindItemTypeId(records.data(), 4097, sizeof(Record), belt) == -1);

    REQUIRE(argc == 3);
    const auto configText = ReadAll(argv[1]);
    REQUIRE(!configText.empty());
    Config config{};
    std::string error;
    REQUIRE(ParseConfig(configText, config, error));
    REQUIRE(config.enabled);
    REQUIRE(!config.exclusions.enabled);
    REQUIRE(config.exclusions.itemTypeCount == 0);
    REQUIRE(!config.generation.enabled);
    REQUIRE(config.generation.chancePercent == VanillaChancePercent);
    REQUIRE(!config.generation.allowSetItems);
    REQUIRE(!config.generation.allowIndestructibleItems);
    REQUIRE(!config.diagnosticsEnabled);
    REQUIRE(!HasExcludedItemTypes(config));
    REQUIRE(!HasDirectRulePatches(config));

    const auto configured = R"toml(
[exclusions]
enabled = true
item_types = [
    "belt",
    "BELT",
    "armo",
]

[generation]
enabled = true
chance_percent = 6
allow_set_items = true
allow_indestructible_items = true
)toml";
    REQUIRE(ParseConfig(configured, config, error));
    REQUIRE(config.enabled);
    REQUIRE(config.exclusions.enabled);
    REQUIRE(config.exclusions.itemTypeCount == 2);
    REQUIRE(config.generation.enabled);
    REQUIRE(config.generation.chancePercent == 6);
    REQUIRE(HasExcludedItemTypes(config));
    REQUIRE(PatchChance(config));
    REQUIRE(PatchSetItems(config));
    REQUIRE(PatchIndestructibleItems(config));

    const auto disabled = R"toml(
[plugin]
enabled = false
[exclusions]
enabled = true
item_types = ["armo"]
[generation]
enabled = true
chance_percent = 100
allow_set_items = true
allow_indestructible_items = true
[diagnostics]
enabled = true
)toml";
    REQUIRE(ParseConfig(disabled, config, error));
    REQUIRE(!config.enabled);
    REQUIRE(config.diagnosticsEnabled);
    REQUIRE(!HasExcludedItemTypes(config));
    REQUIRE(!HasDirectRulePatches(config));

    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=true\nitem_types=[]\n"
        "[generation]\nenabled=false\nchance_percent=5\n"
        "allow_set_items=false\nallow_indestructible_items=false\nextra=1\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=true\nitem_types=[\"too-long\"]\n"
        "[generation]\nenabled=false\nchance_percent=5\n"
        "allow_set_items=false\nallow_indestructible_items=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=false\nitem_types=[]\n"
        "[generation]\nenabled=true\nchance_percent=101\n"
        "allow_set_items=false\nallow_indestructible_items=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=false\nitem_types=[]\n",
        config,
        error));

    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Disabled, ExclusionState::Pending));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Disabled, ExclusionState::Refused));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Pending, ExclusionState::Installing));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Installing, ExclusionState::Stopping));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Installing, ExclusionState::Active));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Installing, ExclusionState::Refused));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Active, ExclusionState::Stopping));
    REQUIRE(IsValidExclusionTransition(
        ExclusionState::Refused, ExclusionState::Stopping));
    REQUIRE(!IsValidExclusionTransition(
        ExclusionState::Refused, ExclusionState::Installing));
    REQUIRE(!IsValidExclusionTransition(
        ExclusionState::Stopping, ExclusionState::Active));
    REQUIRE(!IsExclusionHookActive(ExclusionState::Disabled));
    REQUIRE(!IsExclusionHookActive(ExclusionState::Pending));
    REQUIRE(!IsExclusionHookActive(ExclusionState::Installing));
    REQUIRE(IsExclusionHookActive(ExclusionState::Active));
    REQUIRE(!IsExclusionHookActive(ExclusionState::Refused));
    REQUIRE(!IsExclusionHookActive(ExclusionState::Stopping));

    const auto source = ReadAll(argv[2]);
    REQUIRE(!source.empty());
    REQUIRE(source.find("ExclusionState::Pending") != std::string::npos);
    REQUIRE(source.find("ExclusionState::Installing") != std::string::npos);
    REQUIRE(source.find("ExclusionState::Active") != std::string::npos);
    REQUIRE(source.find("ExclusionState::Refused") != std::string::npos);
    REQUIRE(source.find("ExclusionState::Stopping") != std::string::npos);
    REQUIRE(source.find("registerDataTablesLoadedListener") != std::string::npos);
    REQUIRE(source.find("unregisterDataTablesLoadedListener") != std::string::npos);

    const auto load = source.find("D2RLoaderLoadPlugin");
    const auto loadEnd = source.find("D2RLoaderUnloadPlugin", load);
    const auto registration = source.find(
        "RegisterExclusionLifecycle()", load);
    const auto validation = source.find("if (!ValidateRuntime())", load);
    const auto rulePatches = source.find("InstallRulePatches()", load);
    REQUIRE(load != std::string::npos);
    REQUIRE(loadEnd != std::string::npos);
    REQUIRE(validation > load && validation < loadEnd);
    REQUIRE(registration > validation && registration < loadEnd);
    REQUIRE(rulePatches > registration && rulePatches < loadEnd);
    REQUIRE(source.find("InstallExclusionHook()", load) == std::string::npos);
    const auto patchFailure = source.find(
        "if (!InstallRulePatches())", load);
    REQUIRE(patchFailure != std::string::npos);
    const auto patchFailureCleanup = source.find(
        "UnregisterExclusionListener()", patchFailure);
    REQUIRE(patchFailureCleanup > patchFailure && patchFailureCleanup < loadEnd);
    const auto unloadCleanup = source.find(
        "UnregisterExclusionListener()", loadEnd);
    const auto unloadReset = source.find("ResetState()", loadEnd);
    REQUIRE(unloadReset != std::string::npos);
    REQUIRE(unloadCleanup > loadEnd && unloadCleanup < unloadReset);

    const auto validationDefinition = source.find("auto ValidateRuntime()");
    REQUIRE(validationDefinition != std::string::npos);
    const auto validationEnd = source.find(
        "auto ShouldLogDiagnostic", validationDefinition);
    REQUIRE(validationEnd != std::string::npos);
    const auto validationBody = std::string_view(
        source.data() + validationDefinition,
        validationEnd - validationDefinition);
    REQUIRE(validationBody.find("CheckItemTypeRva") == std::string_view::npos);
    const auto callback = source.find("void __cdecl OnDataTablesLoaded(");
    REQUIRE(callback != std::string::npos);
    const auto callbackClaim = source.find(
        "TryTransitionExclusionState(", callback);
    const auto callbackContract = source.find("context != Context", callback);
    REQUIRE(callbackClaim != std::string::npos);
    REQUIRE(callbackContract > callbackClaim);
    const auto callbackEnd = source.find(
        "auto RegisterExclusionLifecycle()", callback);
    REQUIRE(callbackEnd > callback);
    const auto callbackCleanup = source.find(
        "UnregisterExclusionListener()", callback);
    REQUIRE(callbackCleanup == std::string::npos || callbackCleanup >= callbackEnd);
    REQUIRE(source.find("event->revision == 0", callback) != std::string::npos);
    REQUIRE(source.find("CheckItemTypeRva", callback) != std::string::npos);
    REQUIRE(source.find("InstallExclusionHook()", callback) != std::string::npos);
    const auto hook = source.find(
        "std::int32_t __fastcall HookCheckItemType(");
    const auto hookReturn = source.find("returnRva", hook);
    const auto hookGate = source.find("IsExclusionHookActive", hook);
    REQUIRE(hook != std::string::npos);
    REQUIRE(hookGate > hook && hookGate < hookReturn);
    REQUIRE(hookReturn > hook);
    REQUIRE(source.find("TryTransitionExclusionState", callback)
        != std::string::npos);
    REQUIRE(source.find("ExclusionLifecycleMutex", callback)
        != std::string::npos);
    REQUIRE(source.find("StopExclusions()", loadEnd)
        != std::string::npos);
    REQUIRE(source.find(
        "StopExclusions();\n"
        "        }\n"
        "        UnregisterExclusionListener();",
        loadEnd) != std::string::npos);
    return 0;
}
