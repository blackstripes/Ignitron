#pragma once
#include <cstdint>

// Zero-based physical key / mini display index -> Preset-mode action.
// Slot numbers are one-based within the browsed bank; FX mode uses its own
// direct key-to-slot mapping.
namespace PhysicalPresetLayout {
enum class Role : uint8_t { Slot, BankPrevious, BankNext };
struct Position { Role role; uint8_t slot; };
constexpr Position positions[6] = {
    {Role::Slot, 4}, {Role::BankPrevious, 0}, {Role::BankNext, 0},
    {Role::Slot, 1}, {Role::Slot, 2}, {Role::Slot, 3}
};
} // namespace PhysicalPresetLayout
