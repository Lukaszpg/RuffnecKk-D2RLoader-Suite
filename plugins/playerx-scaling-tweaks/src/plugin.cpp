#include <D2RLPlugin/api.h>
#include <RuffnecKk/native_stat_compat.hpp>
#include <RuffnecKk/tracked_native_transform_d2rl.hpp>

#include "default_config.hpp"
#include "native_fingerprint.hpp"
#include "player_scaling_policy.hpp"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#pragma intrinsic(_AddressOfReturnAddress)

namespace {

using namespace ruffneckk::player_scaling;

constexpr wchar_t ConfigFileName[] =
    L"ruffneckk-playerx-scaling-tweaks.toml";
constexpr std::size_t RelayStride = 32;

using PlayersAtoiFn = std::int32_t(__fastcall*)(const char*) noexcept;
using SetPlayerCountFn = void(__fastcall*)(void*, std::int32_t) noexcept;
using SetOfflineDifficultyRangeFn = void(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using GetOfflineDifficultySettingFn = void*(__fastcall*)() noexcept;
using GetPlayerCountBonusFn = void(__fastcall*)(
    void*, std::int32_t*, void*, void*) noexcept;
using GetPlayerCountFn = std::int32_t(__fastcall*)(void*) noexcept;

enum class RelayTarget : std::size_t {
    PlayersAtoi,
    SetPlayerCount,
    MonsterOffense,
    NoDropPlayerCount,
    NoDropMonsterCap,
    Count,
};

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};
HANDLE OwnershipMutex{};
void* RelayPage{};
std::array<std::uint64_t, static_cast<std::size_t>(RelayTarget::Count)>
    RelayRvas{};
PlayersAtoiFn RealPlayersAtoi{};
SetPlayerCountFn RealSetPlayerCount{};
SetOfflineDifficultyRangeFn RealSetOfflineDifficultyRange{};
GetOfflineDifficultySettingFn RealGetOfflineDifficultySetting{};
GetPlayerCountBonusFn RealGetPlayerCountBonus{};
RuffnecKk::NativeStatCompat::Adapter NativeStats{};
GetPlayerCountFn RealGetPlayerCount{};
std::atomic_bool Operational{};
std::atomic<std::uint64_t> CommandClamps{};
std::atomic<std::uint64_t> BonusAdjustments{};
std::atomic<std::uint64_t> OffenseAdjustments{};
std::atomic<std::uint64_t> NoDropAdjustments{};
thread_local std::int32_t RequestedPlayers{1};
thread_local bool HasRequestedPlayers{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-playerx-scaling-tweaks",
    .name = "PlayerX Scaling Tweaks",
    .version = "1.0.1",
    .author = "RuffnecKk",
    .description = "Tweaks player-count floors and independent scaling caps.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto IsReadableRange(
        std::uintptr_t address,
        std::size_t size) noexcept -> bool {
    if (size == 0) return true;
    if (address > (std::numeric_limits<std::uintptr_t>::max)() - size) {
        return false;
    }
    const auto end = address + size;
    auto cursor = address;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(cursor), &memory,
                sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || memory.RegionSize == 0) {
            return false;
        }
        const auto baseProtection = memory.Protect & 0xFF;
        if (baseProtection != PAGE_READONLY
                && baseProtection != PAGE_READWRITE
                && baseProtection != PAGE_WRITECOPY
                && baseProtection != PAGE_EXECUTE_READ
                && baseProtection != PAGE_EXECUTE_READWRITE
                && baseProtection != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        const auto regionBase = reinterpret_cast<std::uintptr_t>(
            memory.BaseAddress);
        if (regionBase > (std::numeric_limits<std::uintptr_t>::max)()
                - memory.RegionSize) {
            return false;
        }
        const auto regionEnd = regionBase + memory.RegionSize;
        if (cursor < regionBase || cursor >= regionEnd) return false;
        cursor = (std::min)(end, regionEnd);
    }
    return true;
}

template<class Value>
auto TryRead(
        const void* base,
        std::size_t offset,
        Value& value) noexcept -> bool {
    const auto address = reinterpret_cast<std::uintptr_t>(base);
    if (!address
            || address > (std::numeric_limits<std::uintptr_t>::max)()
                - offset) {
        return false;
    }
    const auto source = address + offset;
    if (!IsReadableRange(source, sizeof(Value))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(source), sizeof(Value));
    return true;
}

template<std::size_t Size>
auto Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    if (!Base
            || Base > (std::numeric_limits<std::uintptr_t>::max)() - rva) {
        return false;
    }
    const auto address = Base + rva;
    return IsReadableRange(address, expected.size())
        && std::memcmp(reinterpret_cast<const void*>(address),
            expected.data(), expected.size()) == 0;
}

auto IsMonsterPlayerCountExempt(void* monster) noexcept -> bool {
    void* unitData{};
    void* monsterRecord{};
    std::uint8_t nonEvilAlignment{};
    return !TryRead(monster, 0x10, unitData)
        || !TryRead(unitData, 0, monsterRecord)
        || !TryRead(monsterRecord, 0x87, nonEvilAlignment)
        || nonEvilAlignment != 0;
}

auto ConfigCandidates() -> std::vector<std::filesystem::path> {
    std::filesystem::path activeModConfigDirectory;
    std::filesystem::path scopeConfigDirectory;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        activeModConfigDirectory =
            std::filesystem::path(Context->modSupportDirectory) / L"config";
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        scopeConfigDirectory =
            std::filesystem::path(Context->pluginConfigPath).parent_path();
    }
    std::error_code currentPathError;
    const auto currentPath = std::filesystem::current_path(currentPathError);
    const auto globalConfigDirectory = currentPathError
        ? std::filesystem::path{}
        : currentPath / L"d2rloader" / L"config";
    return BuildConfigCandidates(
        activeModConfigDirectory,
        scopeConfigDirectory,
        globalConfigDirectory,
        ConfigFileName);
}

auto MaterializeDefaultConfig(
        const std::vector<std::filesystem::path>& candidates) noexcept -> bool {
    std::filesystem::path path;
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        path = std::filesystem::path(
            Context->pluginConfigPath).parent_path() / ConfigFileName;
    } else if (!candidates.empty()) {
        path = candidates.front();
    }
    if (path.empty()) return false;
    try {
        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError) return false;
        const auto handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const auto size = static_cast<DWORD>(sizeof(DefaultConfig) - 1);
        const auto ok = WriteFile(
            handle, DefaultConfig, size, &written, nullptr) != FALSE
            && written == size;
        CloseHandle(handle);
        if (!ok) {
            (void)DeleteFileW(path.c_str());
            return false;
        }
        LoadedConfigPath = path.string();
        return true;
    } catch (...) {
        return false;
    }
}

auto LoadConfig() noexcept -> bool {
    Settings = {};
    LoadedConfigPath = "embedded defaults";
    const auto candidates = ConfigCandidates();
    for (const auto& path : candidates) {
        std::error_code statusError;
        if (!std::filesystem::is_regular_file(path, statusError)) continue;
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string error;
            if (!ParseToml(text, parsed, error)) {
                throw std::invalid_argument(error);
            }
            Settings = parsed;
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            const auto message = std::string("PlayerX Scaling Tweaks: invalid ")
                + path.string() + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
            return false;
        }
    }
    if (MaterializeDefaultConfig(candidates)) {
        const auto message = std::string(
            "PlayerX Scaling Tweaks: created default configuration at ")
            + LoadedConfigPath + ".";
        Context->LogInfo(message.c_str());
    } else {
        Context->LogWarn(
            "PlayerX Scaling Tweaks: no TOML was found or created; embedded defaults are active.");
    }
    return true;
}

auto AcquireOwnership() noexcept -> bool {
    wchar_t ownershipMutexName[96]{};
    if (swprintf_s(
            ownershipMutexName,
            L"Local\\RuffnecKk.PlayerXScalingTweaks.NativeOwner.v1.%lu",
            static_cast<unsigned long>(GetCurrentProcessId())) < 0) {
        return false;
    }
    OwnershipMutex = CreateMutexW(nullptr, FALSE, ownershipMutexName);
    if (!OwnershipMutex) return false;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
    CloseHandle(OwnershipMutex);
    OwnershipMutex = nullptr;
    Context->LogError(
        "PlayerX Scaling Tweaks: another global or mod-local instance already owns the native hooks.");
    return false;
}

void ReleaseOwnership() noexcept {
    if (!OwnershipMutex) return;
    CloseHandle(OwnershipMutex);
    OwnershipMutex = nullptr;
}

auto NativeStatFailureLabel() noexcept -> const char* {
    using Failure = RuffnecKk::NativeStatCompat::Failure;
    switch (NativeStats.LastFailure()) {
    case Failure::ReadFailed: return "memory read failed";
    case Failure::CanonicalMismatch: return "canonical entry mismatch";
    case Failure::ProviderEncoding: return "provider relay encoding mismatch";
    case Failure::ProviderPointer: return "provider relay target mismatch";
    case Failure::ProviderWitness: return "provider witness mismatch";
    default: return "invalid compatibility contract";
    }
}

auto ValidateNativeFingerprint() noexcept -> bool {
    if (!NativeStats.BindCurrentProcess(
            Base,
            RuffnecKk::NativeStatCompat::ToMask(
                RuffnecKk::NativeStatCompat::Helper::GetUnitStat),
            RuffnecKk::TrackedNativeTransform::D2RLDiagnosticsContext(Context))) {
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "PlayerX Scaling Tweaks: stat compatibility admission failed (%s); plugin refused.",
            NativeStatFailureLabel());
        Context->LogError(message);
        return false;
    }
    struct Check {
        bool matched;
        const char* label;
    };
    const Check checks[]{
        {Matches(native::PlayersAtoiCallRva, native::PlayersAtoiCall),
         "/players parser callsite"},
        {Matches(native::PlayersApplyCallRva, native::PlayersApplyCall),
         "/players apply callsite"},
        {Matches(native::PlayersRecognitionGateRva,
            native::PlayersRecognitionGateContext),
         "/players recognition dispatcher"},
        {Matches(native::PlayersAtoiRva, native::PlayersAtoiEntry),
         "/players numeric parser"},
        {Matches(native::SetPlayerCountRva, native::SetPlayerCountEntry),
         "player-count setter"},
        {Matches(native::SetOfflineDifficultyRangeRva,
            native::SetOfflineDifficultyRangeEntry),
         "Offline Difficulty range setter"},
        {Matches(native::SetOfflineDifficultyRangeArgumentContextRva,
            native::SetOfflineDifficultyRangeArgumentContext),
         "Offline Difficulty range-setter ABI"},
        {Matches(native::OfflineDifficultySecondaryGetterRva,
            native::OfflineDifficultySecondaryGetterEntry),
         "Offline Difficulty setting getter"},
        {Matches(native::OfflineDifficultySecondaryGetterReturnRva,
            native::OfflineDifficultySecondaryGetterReturnContext),
         "Offline Difficulty setting return"},
        {Matches(native::PlayersOfflineDifficultySetterContextRva,
            native::PlayersOfflineDifficultySetterContext),
         "Offline Difficulty getter-to-setter ABI"},
        {Matches(native::GetPlayerCountBonusRva,
            native::GetPlayerCountBonusEntry),
         "monster player-count bonus"},
        {Matches(native::MonsterScalingEligibilityContextRva,
            native::MonsterScalingEligibilityContext),
         "monster scaling eligibility layout"},
        {Matches(native::PlayerCountBonusOutputContextRva,
            native::PlayerCountBonusOutputContext),
         "monster player-count output layout"},
        {Matches(native::PlayerCountDifficultyOutputContextRva,
            native::PlayerCountDifficultyOutputContext),
         "monster difficulty output layout"},
        {Matches(native::GetPlayerCountRva, native::GetPlayerCountEntry),
         "game player-count getter"},
        {Matches(native::GetPlayerCountRealCountContextRva,
            native::GetPlayerCountRealCountContext),
         "real connected-player count dataflow"},
        {Matches(native::GetPlayerCountArtificialCapRva,
            native::GetPlayerCountArtificialCap),
         "artificial player-count maximum"},
        {Matches(native::GetPlayerCountArtificialCapTailRva,
            native::GetPlayerCountArtificialCapTail),
         "real player-count return tail"},
        {Matches(native::OfflineDifficultyPrimaryRangeRva,
            native::OfflineDifficultyPrimaryRange),
         "primary Offline Difficulty initializer"},
        {Matches(native::OfflineDifficultyPrimaryFallbackRva,
            native::OfflineDifficultyPrimaryFallback),
         "primary Offline Difficulty fallback"},
        {Matches(native::OfflineDifficultySecondaryRangeRva,
            native::OfflineDifficultySecondaryRange),
         "secondary Offline Difficulty initializer"},
        {Matches(native::OfflineDifficultySecondaryFallbackRva,
            native::OfflineDifficultySecondaryFallback),
         "secondary Offline Difficulty fallback"},
        {Matches(native::MonsterDamageStatCallRva,
            native::MonsterDamageStatCall),
         "monster offense player-count callsite"},
        {Matches(0x588EEF, native::MonsterDamageContext),
         "monster offense ABI context"},
        {Matches(native::NoDropPlayerCountCallRva,
            native::NoDropPlayerCountCall),
         "NoDrop player-count callsite"},
        {Matches(native::NoDropMonsterCapCallRva,
            native::NoDropMonsterCapCall),
         "NoDrop monster-count cap callsite"},
        {Matches(0x440A76, native::NoDropPlayerCountContext),
         "NoDrop player-count ABI context"},
        {Matches(0x440A8C, native::NoDropFormulaContext),
         "NoDrop source-combination formula"},
        {Matches(native::NoDropMonsterCapContextRva,
            native::NoDropMonsterCapContext),
         "NoDrop monster-count cap context"},
    };
    for (const auto& check : checks) {
        if (check.matched) continue;
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "PlayerX Scaling Tweaks: %s signature mismatch; plugin refused.",
            check.label);
        Context->LogError(message);
        return false;
    }
    char message[256]{};
    const auto* build = D2RL::GetBuildName(Context);
    std::snprintf(
        message, sizeof(message),
        "PlayerX Scaling Tweaks: validating the complete native fingerprint succeeded; observed build-name=%s is diagnostic only.",
        build && build[0] != '\0' ? build : "<unavailable>");
    Context->LogInfo(message);
    return true;
}

auto __fastcall HookPlayersAtoi(const char* text) noexcept -> std::int32_t {
    const auto parsed = RealPlayersAtoi(text);
    RequestedPlayers = parsed;
    HasRequestedPlayers = true;
    return parsed;
}

void __fastcall HookSetPlayerCount(
        void* session,
        std::int32_t nativeCount) noexcept {
    const auto requested = HasRequestedPlayers ? RequestedPlayers : nativeCount;
    HasRequestedPlayers = false;
    const auto clamped = ClampPlayersCommand(requested, Settings);
    if (clamped != requested) {
        CommandClamps.fetch_add(1, std::memory_order_relaxed);
    }
    RealSetPlayerCount(session, clamped);
}

void __fastcall HookGetPlayerCountBonus(
        void* game,
        std::int32_t* output,
        void* room,
        void* monster) noexcept {
    RealGetPlayerCountBonus(game, output, room, monster);
    if (!Operational.load(std::memory_order_acquire) || !output) return;

    // A non-positive native count is an explicit engine exemption. Preserve
    // all five outputs exactly as returned instead of turning that exemption
    // into the configured baseline.
    if (!HasNativePlayerCountScaling(output[4])
            || IsMonsterPlayerCountExempt(monster)) {
        return;
    }

    const auto observed = NormalizePlayerCount(output[4]);
    const auto baseline = (std::max)(
        observed, Settings.minimumScalingPlayers);
    const auto lifeCount = ResolveScalingCount(
        observed, Settings.minimumScalingPlayers, Settings.monsterLife);
    const auto experienceCount = ResolveScalingCount(
        observed, Settings.minimumScalingPlayers,
        Settings.monsterExperience);
    const auto newLife = MonsterLifeBonusPercent(lifeCount);
    const auto newExperience = MonsterExperienceBonusPercent(experienceCount);
    if (output[0] != newLife || output[1] != newExperience
            || output[4] != baseline) {
        BonusAdjustments.fetch_add(1, std::memory_order_relaxed);
    }
    output[0] = newLife;
    output[1] = newExperience;
    output[4] = baseline;
}

auto __fastcall HookMonsterOffensePlayerCount(
        void* monster,
        std::int32_t statId,
        std::int32_t layer) noexcept -> std::int32_t {
    const auto observed = NativeStats.GetUnitStat(
        monster, statId, static_cast<std::uint16_t>(layer));
    if (!Operational.load(std::memory_order_acquire)) return observed;
    const auto adjusted = ResolveScalingCount(
        observed, Settings.minimumScalingPlayers, Settings.monsterOffense);
    if (adjusted != observed) {
        OffenseAdjustments.fetch_add(1, std::memory_order_relaxed);
    }
    return adjusted;
}

__declspec(noinline) auto __fastcall HookNoDropPlayerCount(void* game) noexcept
        -> std::int32_t {
    const auto observedPlayers = RealGetPlayerCount(game);
    if (!Operational.load(std::memory_order_acquire)
            || !IsNoDropPartySimulationEnabled(Settings)) {
        return observedPlayers;
    }

    auto* const returnAddressSlot = static_cast<std::byte*>(
        _AddressOfReturnAddress());
    auto* const nearbyPartySlot = reinterpret_cast<std::int32_t*>(
        returnAddressSlot + 0x48);
    const auto observedNearby = *nearbyPartySlot;
    const auto adjusted = ResolveNoDropCounts(
        observedPlayers, observedNearby, Settings.noDrop);
    if (adjusted.playersCommand != observedPlayers
            || adjusted.nearbyPartyMembers != observedNearby) {
        NoDropAdjustments.fetch_add(1, std::memory_order_relaxed);
    }
    *nearbyPartySlot = adjusted.nearbyPartyMembers;
    return adjusted.playersCommand;
}

__declspec(noinline) auto __fastcall HookNoDropMonsterCap(
        void* monster,
        std::int32_t statId,
        std::int32_t layer) noexcept -> std::int32_t {
    const auto observed = NativeStats.GetUnitStat(
        monster, statId, static_cast<std::uint16_t>(layer));
    if (!Operational.load(std::memory_order_acquire)
            || !IsNoDropPartySimulationEnabled(Settings)) {
        return observed;
    }

    const auto* const returnAddressSlot = static_cast<const std::byte*>(
        _AddressOfReturnAddress());
    const auto* const effectiveNoDropSlot =
        reinterpret_cast<const std::int32_t*>(returnAddressSlot + 0x48);
    const auto effectiveNoDrop = NormalizePlayerCount(*effectiveNoDropSlot);
    const auto adjusted = ResolveNoDropMonsterCap(
        observed, effectiveNoDrop,
        IsNoDropPartySimulationEnabled(Settings));
    if (adjusted != observed) {
        NoDropAdjustments.fetch_add(1, std::memory_order_relaxed);
    }
    return adjusted;
}

auto AllocateNearCallsites(std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto origin = (Base + native::PlayersAtoiCallRva)
        & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
         delta < 0x70000000ULL;
         delta += granularity) {
        if (origin > (std::numeric_limits<std::uintptr_t>::max)() - delta) {
            break;
        }
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(origin + delta), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto PrepareRelays() noexcept -> bool {
    constexpr auto targetCount = static_cast<std::size_t>(RelayTarget::Count);
    const std::array<void*, targetCount> targets{
        reinterpret_cast<void*>(&HookPlayersAtoi),
        reinterpret_cast<void*>(&HookSetPlayerCount),
        reinterpret_cast<void*>(&HookMonsterOffensePlayerCount),
        reinterpret_cast<void*>(&HookNoDropPlayerCount),
        reinterpret_cast<void*>(&HookNoDropMonsterCap),
    };
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    RelayPage = AllocateNearCallsites(systemInfo.dwPageSize);
    if (!RelayPage) return false;
    auto* bytes = static_cast<std::uint8_t*>(RelayPage);
    for (std::size_t index{}; index < targets.size(); ++index) {
        auto* relay = bytes + index * RelayStride;
        relay[0] = 0xFF;
        relay[1] = 0x25;
        std::memset(relay + 2, 0, 4);
        const auto target = reinterpret_cast<std::uint64_t>(targets[index]);
        std::memcpy(relay + 6, &target, sizeof(target));
        const auto relayAddress = reinterpret_cast<std::uintptr_t>(relay);
        if (relayAddress < Base) return false;
        RelayRvas[index] = relayAddress - Base;
    }
    DWORD previousProtection{};
    if (!VirtualProtect(
            RelayPage, targetCount * RelayStride, PAGE_EXECUTE_READ,
            &previousProtection)) {
        return false;
    }
    return FlushInstructionCache(
        GetCurrentProcess(), RelayPage,
        targetCount * RelayStride) != FALSE;
}

auto InstallBattleNetSimulationPatches() noexcept -> bool {
    const auto patchBytes = [](std::uintptr_t rva, const auto& expected,
            const auto& replacement) noexcept {
        return Context->PatchBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()),
            replacement.data(),
            static_cast<std::uint32_t>(replacement.size()));
    };
    if (patchBytes(
            native::PlayersRecognitionGatePatchRva,
            native::PlayersRecognitionGateOriginal,
            native::PlayersRecognitionGatePatched)
            && patchBytes(
                native::GetPlayerCountArtificialCapRva,
                native::GetPlayerCountArtificialCap,
                native::GetPlayerCountArtificialCapPatched)
            && patchBytes(
                native::OfflineDifficultyPrimaryRangePatchRva,
                native::OfflineDifficultyRangeOriginal,
                native::OfflineDifficultyRangePatched)
            && patchBytes(
                native::OfflineDifficultyPrimaryFallbackRva,
                native::OfflineDifficultyFallbackOriginal,
                native::OfflineDifficultyFallbackPatched)
            && patchBytes(
                native::OfflineDifficultySecondaryRangePatchRva,
                native::OfflineDifficultyRangeOriginal,
                native::OfflineDifficultyRangePatched)
            && patchBytes(
                native::OfflineDifficultySecondaryFallbackRva,
                native::OfflineDifficultyFallbackOriginal,
                native::OfflineDifficultyFallbackPatched)) {
        return true;
    }
    Context->LogError(
        "PlayerX Scaling Tweaks: Battle.net simulation patch reservation failed; Loader rollback required.");
    return false;
}

auto InstallHooks() noexcept -> bool {
    const auto patchCall = [](std::uintptr_t rva, const auto& expected,
            RelayTarget target) noexcept {
        return Context->PatchCallRel32(
            rva, expected.data(),
            static_cast<std::uint32_t>(expected.size()),
            RelayRvas[static_cast<std::size_t>(target)],
            static_cast<std::uint32_t>(expected.size()));
    };
    if (!patchCall(native::PlayersAtoiCallRva, native::PlayersAtoiCall,
            RelayTarget::PlayersAtoi)
            || !patchCall(native::PlayersApplyCallRva,
                native::PlayersApplyCall, RelayTarget::SetPlayerCount)
            || !patchCall(native::MonsterDamageStatCallRva,
                native::MonsterDamageStatCall, RelayTarget::MonsterOffense)
            || !patchCall(native::NoDropPlayerCountCallRva,
                native::NoDropPlayerCountCall,
                RelayTarget::NoDropPlayerCount)
            || !patchCall(native::NoDropMonsterCapCallRva,
                native::NoDropMonsterCapCall,
                RelayTarget::NoDropMonsterCap)) {
        Context->LogError(
            "PlayerX Scaling Tweaks: managed callsite reservation failed; Loader rollback required.");
        return false;
    }
    if (!Context->InstallInlineHook(
            native::GetPlayerCountBonusRva,
            native::GetPlayerCountBonusEntry.data(),
            static_cast<std::uint32_t>(
                native::GetPlayerCountBonusEntry.size()),
            HookGetPlayerCountBonus,
            &RealGetPlayerCountBonus)) {
        Context->LogError(
            "PlayerX Scaling Tweaks: monster player-count bonus hook failed; Loader rollback required.");
        return false;
    }
    if (Settings.battleNetSimulationEnabled
            && !InstallBattleNetSimulationPatches()) {
        return false;
    }
    return true;
}

auto ResetOfflineDifficultyToP1() noexcept -> bool {
    if (!Settings.battleNetSimulationEnabled) return true;
    auto* const setting = RealGetOfflineDifficultySetting();
    if (!setting) {
        Context->LogError(
            "PlayerX Scaling Tweaks: Offline Difficulty setting was unavailable after the Battle.net simulation patches; Loader rollback required.");
        return false;
    }
    RealSetOfflineDifficultyRange(setting, 1, 1);
    RealSetPlayerCount(setting, 1);
    return true;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[1024]{};
    const auto writeBaseStatus = [&](const char* suffix) {
        return std::snprintf(
            message, sizeof(message),
        "PlayerX Scaling Tweaks 1.0.1: active=%s; source=%s; players=%d..%d; life=%s/%d; xp=%s/%d; offense=%s/%d; nodrop=%s; config=%s%s",
            Operational.load(std::memory_order_acquire) ? "true" : "false",
            Settings.battleNetSimulationEnabled ? "battle-net-simulation" : "native-command",
            Settings.minimumScalingPlayers, Settings.maximumCommandPlayers,
            Settings.monsterLife.enabled ? "scale" : "baseline",
            Settings.monsterLife.maximumPlayers,
            Settings.monsterExperience.enabled ? "scale" : "baseline",
            Settings.monsterExperience.maximumPlayers,
            Settings.monsterOffense.enabled ? "scale" : "baseline",
            Settings.monsterOffense.maximumPlayers,
            IsNoDropPartySimulationEnabled(Settings)
                ? "nearby-party-simulation" : "native",
            LoadedConfigPath.c_str(), suffix);
    };
    const auto length = writeBaseStatus(
        Settings.showUsageCounters ? "; counters=" : ".");
    if (Settings.showUsageCounters && length > 0
            && static_cast<std::size_t>(length) < sizeof(message)) {
        std::snprintf(
            message + length, sizeof(message) - static_cast<std::size_t>(length),
            "%llu/%llu/%llu/%llu.",
            static_cast<unsigned long long>(
                CommandClamps.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                BonusAdjustments.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                OffenseAdjustments.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                NoDropAdjustments.load(std::memory_order_relaxed)));
    }
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    NativeStats.Reset();
    CommandClamps.store(0, std::memory_order_relaxed);
    BonusAdjustments.store(0, std::memory_order_relaxed);
    OffenseAdjustments.store(0, std::memory_order_relaxed);
    NoDropAdjustments.store(0, std::memory_order_relaxed);
    HasRequestedPlayers = false;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = context->exeBase;
    ResetState();
    if (!Base || !LoadConfig()) return false;

    if (!context->RegisterConsoleCommand(
            "playerx-scaling-tweaks", Status,
            "Show the active player-count floors and scaling caps.")) {
        context->LogWarn(
            "PlayerX Scaling Tweaks: optional status command was not registered.");
    }
    if (!Settings.enabled) {
        context->LogInfo(
            "PlayerX Scaling Tweaks 1.0.1 by RuffnecKk loaded disabled; no hook was installed.");
        return true;
    }
    if (!AcquireOwnership() || !ValidateNativeFingerprint()) {
        ReleaseOwnership();
        return false;
    }

    RealPlayersAtoi = At<PlayersAtoiFn>(native::PlayersAtoiRva);
    RealSetPlayerCount = At<SetPlayerCountFn>(native::SetPlayerCountRva);
    RealSetOfflineDifficultyRange = At<SetOfflineDifficultyRangeFn>(
        native::SetOfflineDifficultyRangeRva);
    RealGetOfflineDifficultySetting = At<GetOfflineDifficultySettingFn>(
        native::OfflineDifficultySecondaryGetterRva);
    RealGetPlayerCount = At<GetPlayerCountFn>(native::GetPlayerCountRva);
    if (!PrepareRelays() || !InstallHooks()
            || !ResetOfflineDifficultyToP1()) {
        ReleaseOwnership();
        return false;
    }

    Operational.store(true, std::memory_order_release);
    char message[768]{};
    const auto* build = D2RL::GetBuildName(context);
    std::snprintf(
        message, sizeof(message),
        "PlayerX Scaling Tweaks 1.0.1 by RuffnecKk active; build-name=%s is diagnostic only; source=%s; players=%d..%d; HP cap=%d; XP cap=%d; offense cap=%d; NoDrop=%s; installation=%s; TOML=%s.",
        build && build[0] != '\0' ? build : "<unavailable>",
        Settings.battleNetSimulationEnabled ? "battle-net-simulation" : "native-command",
        Settings.minimumScalingPlayers, Settings.maximumCommandPlayers,
        Settings.monsterLife.maximumPlayers,
        Settings.monsterExperience.maximumPlayers,
        Settings.monsterOffense.maximumPlayers,
        IsNoDropPartySimulationEnabled(Settings)
            ? "nearby-party-simulation" : "native",
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    RealPlayersAtoi = nullptr;
    RealSetPlayerCount = nullptr;
    RealSetOfflineDifficultyRange = nullptr;
    RealGetOfflineDifficultySetting = nullptr;
    RealGetPlayerCountBonus = nullptr;
    NativeStats.Reset();
    RealGetPlayerCount = nullptr;
    ReleaseOwnership();
    Context = nullptr;
    Base = 0;
    // Relay memory intentionally remains valid until process exit. D2RLoader
    // restores managed callsites before unloading this DLL.
}
