#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::EquippedItemToCube {
namespace {

constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint64_t MaximumDiagnosticLogs = 8;

constexpr std::uintptr_t SendTwentyOneByteCommandRva = 0x0EC820;
constexpr std::uintptr_t TransferCommandCallSiteRva = 0x160102;
constexpr std::uintptr_t CanQuickMoveItemToCubeRva = 0x15A280;
constexpr std::uintptr_t TransferItemRva = 0x15F8B0;
constexpr std::uintptr_t ResolveHoveredUnitRva = 0x2A7810;
constexpr std::uintptr_t ActualEquippedClickHandlerRva = 0x2CACF0;
constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t GetUnitInventoryRva = 0x34A360;
constexpr std::uintptr_t GetEquippedItemRva = 0x3886D0;
constexpr std::uintptr_t IsVirtualKeyDownRva = 0x120A100;

constexpr std::array<std::uint8_t, 32> SendTwentyOneByteCommandExpected{
    0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x05, 0x9D,
    0xEA, 0x8D, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x44, 0x24, 0x38, 0x8B, 0x44, 0x24, 0x70, 0x89,
    0x44, 0x24, 0x2D, 0x8B, 0x44, 0x24, 0x78, 0x88,
};
constexpr std::array<std::uint8_t, 21> TransferCommandCallSiteExpected{
    0x89, 0x74, 0x24, 0x28, 0x45, 0x8B, 0xCD, 0x89,
    0x5C, 0x24, 0x20, 0x44, 0x8B, 0xC7, 0xB1, 0x54,
    0xE8, 0x09, 0xC7, 0xF8, 0xFF,
};
constexpr std::array<std::uint8_t, 32> ActualEquippedClickHandlerExpected{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x57,
    0x41, 0x55, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B,
    0xD9, 0xE8, 0x0A, 0xCB, 0xFD, 0xFF, 0x41, 0xB8,
    0x1A, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x4D,
};
constexpr std::array<std::uint8_t, 32> CanQuickMoveItemToCubeExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xB9, 0x19, 0x00, 0x00, 0x00, 0xE8, 0x6D,
    0x42, 0xF7, 0xFF, 0x84, 0xC0, 0x75, 0x17, 0xB9,
    0x18, 0x00, 0x00, 0x00, 0xE8, 0x5F, 0x42, 0xF7,
};
constexpr std::array<std::uint8_t, 32> TransferItemExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x08, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
    0xF8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xF5,
};
constexpr std::array<std::uint8_t, 32> ResolveHoveredUnitExpected{
    0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0x81, 0xC4,
    0x05, 0x00, 0x00, 0x41, 0x83, 0xF8, 0xFF, 0x75,
    0x10, 0x83, 0xB9, 0xC8, 0x05, 0x00, 0x00, 0x06,
    0x75, 0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28,
};
constexpr std::array<std::uint8_t, 16> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
    0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19,
};
constexpr std::array<std::uint8_t, 32> GetUnitInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetEquippedItemExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xED,
};
constexpr std::array<std::uint8_t, 21> IsVirtualKeyDownExpected{
    0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x86, 0x6E,
    0xAA, 0x00, 0xC1, 0xE8, 0x0F, 0x83, 0xE0, 0x01,
    0x48, 0x83, 0xC4, 0x28, 0xC3,
};

struct TransferPlacement {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint8_t valid{};
    std::uint8_t padding[3]{};
};
static_assert(sizeof(TransferPlacement) == 12);

using SendTwentyOneByteCommandFn = void(__fastcall*)(
    std::uint8_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t) noexcept;
using ActualEquippedClickHandlerFn = void(__fastcall*)(void*, void*) noexcept;
using CanQuickMoveItemToCubeFn = bool(__fastcall*)(void*) noexcept;
using TransferItemFn = bool(__fastcall*)(
    void*, void*, std::uint8_t, std::uint8_t, std::uint8_t, void*) noexcept;
using ResolveHoveredUnitFn = void*(__fastcall*)(void*) noexcept;
using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using GetUnitInventoryFn = void*(__fastcall*)(void*, const char*, int) noexcept;
using GetEquippedItemFn = void*(__fastcall*)(void*, std::uint32_t) noexcept;
using IsVirtualKeyDownFn = std::uint32_t(__fastcall*)(std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
SendTwentyOneByteCommandFn OriginalSendTwentyOneByteCommand{};
ActualEquippedClickHandlerFn OriginalActualEquippedClickHandler{};
CanQuickMoveItemToCubeFn CanQuickMoveItemToCube{};
TransferItemFn TransferItem{};
ResolveHoveredUnitFn ResolveHoveredUnit{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
GetUnitInventoryFn GetUnitInventory{};
GetEquippedItemFn GetEquippedItem{};
IsVirtualKeyDownFn IsVirtualKeyDown{};
thread_local bool RewriteArmed{};
thread_local std::uint32_t RewriteBodyLocation{};
std::atomic<std::uint64_t> ClickEntries{};
std::atomic<std::uint64_t> NativeControlSamples{};
std::atomic<std::uint64_t> Win32ControlSamples{};
std::atomic<std::uint64_t> NativeLegacyMismatches{};
std::atomic<std::uint64_t> ControlRejected{};
std::atomic<std::uint64_t> ControllerMissing{};
std::atomic<std::uint64_t> InvalidBodyLocation{};
std::atomic<std::uint64_t> HoveredPlayerMissing{};
std::atomic<std::uint64_t> LocalPlayerMismatch{};
std::atomic<std::uint64_t> InventoryMissing{};
std::atomic<std::uint64_t> EquippedItemMissing{};
std::atomic<std::uint64_t> CubeEligibilityRejected{};
std::atomic<std::uint64_t> TransferFailed{};
std::atomic<std::uint64_t> TransferSucceeded{};
std::atomic<std::uint64_t> BuilderCallsWhileArmed{};
std::atomic<std::uint64_t> BuilderWrongOpcode{};
std::atomic<std::uint64_t> BuilderWrongPage{};
std::atomic<std::uint32_t> LastModifierState{};
std::atomic<std::uint64_t> RewrittenTransfers{};
std::atomic<std::uint64_t> DiagnosticLogs{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-equipped-item-to-cube",
    .name = "Equipped Item to Cube",
    .version = "1.0.4",
    .author = "RuffnecKk",
    .description = "Moves Ctrl-clicked equipped items directly to the Horadric Cube.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

auto ValidateComposableInlineHook(
    std::uintptr_t rva,
    const void* expected,
    std::uint32_t expectedSize
) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, expectedSize)) {
        return true;
    }

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (Context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics,
            D2RL::DiagnosticsServiceV1RequiredSize)) {
        return false;
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = rva,
        .expected = expected,
        .expectedSize = expectedSize,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    return diagnostics->queryHookStatus(Context, &query, &status)
            == D2RL::Diagnostics::Result::Success
        && status.state == D2RL::Diagnostics::ModificationState::Tracked
        && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook
        && status.ownerCount == 1;
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "EquippedItemToCube: configuration exceeds 65535 bytes."
            : "EquippedItemToCube: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string(
            "EquippedItemToCube: invalid TOML (")
            + error + "); no hook was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

bool ValidateRuntime() noexcept {
    return Context->CheckExpectedBytes(
            SendTwentyOneByteCommandRva,
            SendTwentyOneByteCommandExpected.data(),
            static_cast<std::uint32_t>(
                SendTwentyOneByteCommandExpected.size()))
        && Context->CheckExpectedBytes(
            TransferCommandCallSiteRva,
            TransferCommandCallSiteExpected.data(),
            static_cast<std::uint32_t>(
                TransferCommandCallSiteExpected.size()))
        && Context->CheckExpectedBytes(
            ActualEquippedClickHandlerRva,
            ActualEquippedClickHandlerExpected.data(),
            static_cast<std::uint32_t>(ActualEquippedClickHandlerExpected.size()))
        && Context->CheckExpectedBytes(
            CanQuickMoveItemToCubeRva,
            CanQuickMoveItemToCubeExpected.data(),
            static_cast<std::uint32_t>(CanQuickMoveItemToCubeExpected.size()))
        && Context->CheckExpectedBytes(
            TransferItemRva,
            TransferItemExpected.data(),
            static_cast<std::uint32_t>(TransferItemExpected.size()))
        && ValidateComposableInlineHook(
            ResolveHoveredUnitRva,
            ResolveHoveredUnitExpected.data(),
            static_cast<std::uint32_t>(ResolveHoveredUnitExpected.size()))
        && Context->CheckExpectedBytes(
            GetLocalDataContextRva,
            GetLocalDataContextExpected.data(),
            static_cast<std::uint32_t>(GetLocalDataContextExpected.size()))
        && Context->CheckExpectedBytes(
            GetLocalPlayerRva,
            GetLocalPlayerExpected.data(),
            static_cast<std::uint32_t>(GetLocalPlayerExpected.size()))
        && Context->CheckExpectedBytes(
            GetUnitInventoryRva,
            GetUnitInventoryExpected.data(),
            static_cast<std::uint32_t>(GetUnitInventoryExpected.size()))
        && Context->CheckExpectedBytes(
            GetEquippedItemRva,
            GetEquippedItemExpected.data(),
            static_cast<std::uint32_t>(GetEquippedItemExpected.size()))
        && Context->CheckExpectedBytes(
            IsVirtualKeyDownRva,
            IsVirtualKeyDownExpected.data(),
            static_cast<std::uint32_t>(IsVirtualKeyDownExpected.size()));
}

auto NativeKeyDown(std::int32_t virtualKey) noexcept -> bool {
    if (!IsVirtualKeyDown) return false;
    __try {
        return IsVirtualKeyDown(virtualKey) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto AsyncKeyDown(std::int32_t virtualKey) noexcept -> bool {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

auto StateKeyDown(std::int32_t virtualKey) noexcept -> bool {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

auto ReadModifierState() noexcept -> std::uint32_t {
    std::uint32_t state{};
    // Sample the legacy 1.0.2 decision first so the additional diagnostic
    // reads cannot change the value used by the existing behavior.
    if (AsyncKeyDown(VK_CONTROL)) state |= Win32AsyncControl;
    if (NativeKeyDown(VK_CONTROL)) state |= NativeControl;
    if (NativeKeyDown(VK_LCONTROL)) state |= NativeLeftControl;
    if (NativeKeyDown(VK_RCONTROL)) state |= NativeRightControl;
    if (AsyncKeyDown(VK_LCONTROL)) state |= Win32AsyncLeftControl;
    if (AsyncKeyDown(VK_RCONTROL)) state |= Win32AsyncRightControl;
    if (StateKeyDown(VK_CONTROL)) state |= Win32StateControl;
    if (StateKeyDown(VK_LCONTROL)) state |= Win32StateLeftControl;
    if (StateKeyDown(VK_RCONTROL)) state |= Win32StateRightControl;
    return state;
}

class RewriteScope {
public:
    explicit RewriteScope(std::uint32_t bodyLocation) noexcept
        : previousArmed_(RewriteArmed),
          previousBodyLocation_(RewriteBodyLocation) {
        RewriteBodyLocation = bodyLocation;
        RewriteArmed = true;
    }

    ~RewriteScope() noexcept {
        RewriteArmed = previousArmed_;
        RewriteBodyLocation = previousBodyLocation_;
    }

private:
    bool previousArmed_{};
    std::uint32_t previousBodyLocation_{};
};

void __fastcall HookSendTwentyOneByteCommand(
    std::uint8_t opcode,
    std::uint32_t field1,
    std::uint32_t field2,
    std::uint32_t field3,
    std::uint32_t field4,
    std::uint32_t field5
) noexcept {
    const TwentyOneByteCommand inventoryCommand{
        .opcode = opcode,
        .field1 = field1,
        .field2 = field2,
        .field3 = field3,
        .field4 = field4,
        .field5 = field5,
    };
    if (RewriteArmed) {
        BuilderCallsWhileArmed.fetch_add(1, std::memory_order_relaxed);
        if (opcode != InventoryTransferOpcode) {
            BuilderWrongOpcode.fetch_add(1, std::memory_order_relaxed);
        } else if (field4 != CubeInventoryPage) {
            BuilderWrongPage.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (ShouldRewriteCubeTransfer(
            RewriteArmed, inventoryCommand, RewriteBodyLocation)) {
        const auto equippedCommand = RewriteAsEquippedTransfer(
            inventoryCommand, RewriteBodyLocation);
        const auto rewritten = RewrittenTransfers.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (Settings.diagnosticsEnabled
            && DiagnosticLogs.fetch_add(1, std::memory_order_relaxed)
                < MaximumDiagnosticLogs) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "EquippedItemToCube: rewrote command for body slot %u (total=%llu).",
                RewriteBodyLocation,
                static_cast<unsigned long long>(rewritten));
            Context->LogInfo(message);
        }
        OriginalSendTwentyOneByteCommand(
            equippedCommand.opcode,
            equippedCommand.field1,
            equippedCommand.field2,
            equippedCommand.field3,
            equippedCommand.field4,
            equippedCommand.field5);
        return;
    }
    OriginalSendTwentyOneByteCommand(
        opcode, field1, field2, field3, field4, field5);
}

void __fastcall HookActualEquippedClickHandler(
    void* controller,
    void* eventState
) noexcept {
    ClickEntries.fetch_add(1, std::memory_order_relaxed);
    const auto modifierState = ReadModifierState();
    LastModifierState.store(modifierState, std::memory_order_relaxed);
    if (HasNativeControl(modifierState)) {
        NativeControlSamples.fetch_add(1, std::memory_order_relaxed);
    }
    if (HasWin32Control(modifierState)) {
        Win32ControlSamples.fetch_add(1, std::memory_order_relaxed);
    }
    if (HasNativeControl(modifierState)
        && !LegacyControlAccepted(modifierState)) {
        NativeLegacyMismatches.fetch_add(1, std::memory_order_relaxed);
    }

    if (!LegacyControlAccepted(modifierState)) {
        ControlRejected.fetch_add(1, std::memory_order_relaxed);
    } else if (!controller) {
        ControllerMissing.fetch_add(1, std::memory_order_relaxed);
    } else {
        std::uint32_t bodyLocation{};
        std::memcpy(
            &bodyLocation,
            static_cast<const std::uint8_t*>(controller) + 0x5D8,
            sizeof(bodyLocation));

        void* player = ResolveHoveredUnit(controller);
        void* localPlayer = GetLocalPlayer(GetLocalDataContext());
        void* inventory = player
            ? GetUnitInventory(player, __FILE__, __LINE__)
            : nullptr;
        void* item = inventory && IsEquippedBodyLocation(bodyLocation)
            ? GetEquippedItem(inventory, bodyLocation)
            : nullptr;

        if (!IsEquippedBodyLocation(bodyLocation)) {
            InvalidBodyLocation.fetch_add(1, std::memory_order_relaxed);
        } else if (!player) {
            HoveredPlayerMissing.fetch_add(1, std::memory_order_relaxed);
        } else if (player != localPlayer) {
            LocalPlayerMismatch.fetch_add(1, std::memory_order_relaxed);
        } else if (!inventory) {
            InventoryMissing.fetch_add(1, std::memory_order_relaxed);
        } else if (!item) {
            EquippedItemMissing.fetch_add(1, std::memory_order_relaxed);
        } else if (!CanQuickMoveItemToCube(item)) {
            CubeEligibilityRejected.fetch_add(1, std::memory_order_relaxed);
        } else {
            TransferPlacement placement{};
            const RewriteScope rewriteScope(bodyLocation);
            if (TransferItem(item, player, 3, 0, 1, &placement)) {
                TransferSucceeded.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            TransferFailed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    OriginalActualEquippedClickHandler(controller, eventState);
}

bool InstallHooks() noexcept {
    if (!Context->InstallInlineHook(
            SendTwentyOneByteCommandRva,
            SendTwentyOneByteCommandExpected.data(),
            static_cast<std::uint32_t>(
                SendTwentyOneByteCommandExpected.size()),
            HookSendTwentyOneByteCommand,
            &OriginalSendTwentyOneByteCommand)) {
        Context->LogError(
            "EquippedItemToCube: 21-byte command builder hook refused.");
        return false;
    }
    if (!Context->InstallInlineHook(
            ActualEquippedClickHandlerRva,
            ActualEquippedClickHandlerExpected.data(),
            static_cast<std::uint32_t>(ActualEquippedClickHandlerExpected.size()),
            HookActualEquippedClickHandler,
            &OriginalActualEquippedClickHandler)) {
        Context->LogError(
            "EquippedItemToCube: equipped-slot click hook refused.");
        return false;
    }
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[384]{};
    std::snprintf(
        message,
        sizeof(message),
        "Equipped Item to Cube 1.0.4: %s; diagnostics=%s; clicks=%llu; rewritten=%llu; lastModifiers=0x%03X.",
        Settings.enabled ? "active" : "disabled",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        static_cast<unsigned long long>(
            ClickEntries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RewrittenTransfers.load(std::memory_order_relaxed)),
        LastModifierState.load(std::memory_order_relaxed));
    command->plugin->WriteConsoleMessage(message);
    std::snprintf(
        message,
        sizeof(message),
        "Ctrl samples: native=%llu; win32=%llu; nativeLegacyMismatch=%llu; rejected=%llu; controllerMissing=%llu.",
        static_cast<unsigned long long>(
            NativeControlSamples.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Win32ControlSamples.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeLegacyMismatches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ControlRejected.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ControllerMissing.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    std::snprintf(
        message,
        sizeof(message),
        "Pre-transfer gates: body=%llu; hovered=%llu; local=%llu; inventory=%llu; item=%llu; cube=%llu; transferFailed=%llu; transferSucceeded=%llu.",
        static_cast<unsigned long long>(
            InvalidBodyLocation.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            HoveredPlayerMissing.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            LocalPlayerMismatch.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            InventoryMissing.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            EquippedItemMissing.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CubeEligibilityRejected.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            TransferFailed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            TransferSucceeded.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    std::snprintf(
        message,
        sizeof(message),
        "Armed builder: calls=%llu; wrongOpcode=%llu; wrongPage=%llu.",
        static_cast<unsigned long long>(
            BuilderCallsWhileArmed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            BuilderWrongOpcode.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            BuilderWrongPage.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetDiagnostics() noexcept {
    ClickEntries.store(0, std::memory_order_relaxed);
    NativeControlSamples.store(0, std::memory_order_relaxed);
    Win32ControlSamples.store(0, std::memory_order_relaxed);
    NativeLegacyMismatches.store(0, std::memory_order_relaxed);
    ControlRejected.store(0, std::memory_order_relaxed);
    ControllerMissing.store(0, std::memory_order_relaxed);
    InvalidBodyLocation.store(0, std::memory_order_relaxed);
    HoveredPlayerMissing.store(0, std::memory_order_relaxed);
    LocalPlayerMismatch.store(0, std::memory_order_relaxed);
    InventoryMissing.store(0, std::memory_order_relaxed);
    EquippedItemMissing.store(0, std::memory_order_relaxed);
    CubeEligibilityRejected.store(0, std::memory_order_relaxed);
    TransferFailed.store(0, std::memory_order_relaxed);
    TransferSucceeded.store(0, std::memory_order_relaxed);
    BuilderCallsWhileArmed.store(0, std::memory_order_relaxed);
    BuilderWrongOpcode.store(0, std::memory_order_relaxed);
    BuilderWrongPage.store(0, std::memory_order_relaxed);
    LastModifierState.store(0, std::memory_order_relaxed);
    RewrittenTransfers.store(0, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);
}

void ResetState() noexcept {
    OriginalSendTwentyOneByteCommand = nullptr;
    OriginalActualEquippedClickHandler = nullptr;
    CanQuickMoveItemToCube = nullptr;
    TransferItem = nullptr;
    ResolveHoveredUnit = nullptr;
    GetLocalDataContext = nullptr;
    GetLocalPlayer = nullptr;
    GetUnitInventory = nullptr;
    GetEquippedItem = nullptr;
    IsVirtualKeyDown = nullptr;
    RewriteArmed = false;
    RewriteBodyLocation = 0;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion < D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = nullptr;
    Settings = {};
    ResetState();
    ResetDiagnostics();

    if (!ReadConfiguration()) return false;
    if (!Settings.enabled) {
        context->LogInfo(
            "EquippedItemToCube 1.0.4 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    if (!Base) {
        context->LogError("EquippedItemToCube: D2R executable base is unavailable.");
        return false;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    char buildMessage[192]{};
    std::snprintf(buildMessage, sizeof(buildMessage),
        "EquippedItemToCube: observed D2R build-name=%s; validating the complete native fingerprint.",
        runtimeBuild && runtimeBuild[0] != '\0' ? runtimeBuild : "unknown");
    context->LogInfo(buildMessage);
    if (!ValidateRuntime()) {
        context->LogError(
            "EquippedItemToCube: native hook or helper fingerprint mismatch; plugin refused.");
        return false;
    }

    CanQuickMoveItemToCube = At<CanQuickMoveItemToCubeFn>(
        CanQuickMoveItemToCubeRva);
    TransferItem = At<TransferItemFn>(TransferItemRva);
    ResolveHoveredUnit = At<ResolveHoveredUnitFn>(ResolveHoveredUnitRva);
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    GetUnitInventory = At<GetUnitInventoryFn>(GetUnitInventoryRva);
    GetEquippedItem = At<GetEquippedItemFn>(GetEquippedItemRva);
    IsVirtualKeyDown = At<IsVirtualKeyDownFn>(IsVirtualKeyDownRva);

    if (!InstallHooks()) return false;

    if (!context->RegisterConsoleCommand(
            "equipped-item-to-cube",
            Status,
            "Show equipped-item Ctrl-click status.")) {
        context->LogWarn(
            "EquippedItemToCube: status command could not be registered.");
    }

    context->LogInfo(
        "EquippedItemToCube 1.0.4 by RuffnecKk active; Ctrl-left-click equipped transfers target the Cube; builder counters are available from the console command.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ResetState();
    ResetDiagnostics();
    Settings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::EquippedItemToCube
