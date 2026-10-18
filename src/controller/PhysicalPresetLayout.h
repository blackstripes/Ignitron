#pragma once
#include <cstdint>
#include <array>

// Zero-based physical key / mini display index -> Preset-mode action.
// Slot numbers are one-based within the browsed bank; FX mode uses its own
// direct key-to-slot mapping.
namespace PhysicalPresetLayout {
enum class Role : uint8_t { Slot, BankPrevious, BankNext };
struct Position { Role role; uint8_t slot; };
constexpr Position positions[6] = {
    {Role::BankPrevious, 0}, {Role::Slot, 4}, {Role::BankNext, 0},
    {Role::Slot, 1}, {Role::Slot, 2}, {Role::Slot, 3}
};
inline std::array<uint8_t, 6> serviceOrder(uint8_t bank, uint8_t target, uint8_t confirmed) {
    std::array<uint8_t, 6> order{};
    uint8_t count = 0;
    for (uint8_t global : {target, confirmed}) {
        if (!global || (global == confirmed && confirmed == target)) continue;
        for (uint8_t i = 0; i < 6; ++i)
            if (positions[i].role == Role::Slot && bank * 4 + positions[i].slot == global)
                order[count++] = i;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        bool used = false;
        for (uint8_t j = 0; j < count; ++j) used |= order[j] == i;
        if (!used) order[count++] = i;
    }
    return order;
}
} // namespace PhysicalPresetLayout
