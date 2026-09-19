#pragma once

#include "config.hpp"

#include <cstdint>

namespace RuffnecKk::EquippedItemToCube {

inline constexpr std::uint8_t InventoryTransferOpcode = 0x54;
inline constexpr std::uint8_t EquippedTransferOpcode = 0x58;
inline constexpr std::uint32_t SelfTargetGuid = 0xFFFFFFFFu;
inline constexpr std::uint32_t CubeInventoryPage = 3;
inline constexpr std::uint32_t BodyLocationCount = 11;

enum ModifierState : std::uint32_t {
    NativeControl = 1U << 0,
    NativeLeftControl = 1U << 1,
    NativeRightControl = 1U << 2,
    Win32AsyncControl = 1U << 3,
    Win32AsyncLeftControl = 1U << 4,
    Win32AsyncRightControl = 1U << 5,
    Win32StateControl = 1U << 6,
    Win32StateLeftControl = 1U << 7,
    Win32StateRightControl = 1U << 8,
};

inline constexpr std::uint32_t NativeControlStates = NativeControl
    | NativeLeftControl | NativeRightControl;
inline constexpr std::uint32_t Win32ControlStates = Win32AsyncControl
    | Win32AsyncLeftControl | Win32AsyncRightControl
    | Win32StateControl | Win32StateLeftControl | Win32StateRightControl;

constexpr auto HasNativeControl(std::uint32_t state) noexcept -> bool {
    return (state & NativeControlStates) != 0;
}

constexpr auto HasWin32Control(std::uint32_t state) noexcept -> bool {
    return (state & Win32ControlStates) != 0;
}

// Diagnostic 1.0.3 proved this legacy decision agrees with D2R's native Ctrl
// state on the failing global-stack route, so preserve it unchanged.
constexpr auto LegacyControlAccepted(std::uint32_t state) noexcept -> bool {
    return (state & Win32AsyncControl) != 0;
}

struct TwentyOneByteCommand {
    std::uint8_t opcode{};
    std::uint32_t field1{};
    std::uint32_t field2{};
    std::uint32_t field3{};
    std::uint32_t field4{};
    std::uint32_t field5{};
};

constexpr auto IsEquippedBodyLocation(std::uint32_t bodyLocation) noexcept -> bool {
    return bodyLocation > 0 && bodyLocation < BodyLocationCount;
}

constexpr auto ShouldRewriteCubeTransfer(
    bool rewriteArmed,
    const TwentyOneByteCommand& command,
    std::uint32_t bodyLocation
) noexcept -> bool {
    return rewriteArmed
        && IsEquippedBodyLocation(bodyLocation)
        && command.opcode == InventoryTransferOpcode
        && command.field4 == CubeInventoryPage;
}

constexpr auto RewriteAsEquippedTransfer(
    const TwentyOneByteCommand& inventoryCommand,
    std::uint32_t bodyLocation
) noexcept -> TwentyOneByteCommand {
    return TwentyOneByteCommand{
        .opcode = EquippedTransferOpcode,
        .field1 = inventoryCommand.field1,
        .field2 = SelfTargetGuid,
        .field3 = bodyLocation,
        .field4 = inventoryCommand.field4,
        .field5 = inventoryCommand.field5,
    };
}

} // namespace RuffnecKk::EquippedItemToCube
