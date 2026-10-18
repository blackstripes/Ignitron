#pragma once
#include "MiniActionPresentation.h"
#include <array>

// Display-only data. Never feed this back into ControllerState or action routing.
namespace PresetPreviewPresentation {
struct Preview {
    bool valid = false;
    uint8_t target = 0;
    std::string name, description;
    std::array<bool, 6> enabled{};
};

// The caller captures the cache generation and identity before reading the
// slot, and checks them again after the read. A cache miss supplies no preset.
struct Evidence {
    bool checksumsReady = false;
    bool slotPresent = false;
    uint8_t checksum = 0;
    uint32_t generationBefore = 0, generationAfter = 0;
    std::string cacheAmpName, cacheAmpSerial;
};

template <typename PresetType>
Preview select(const ControllerSnapshot &s, const Evidence &e, const PresetType &preset) {
    Preview result;
    const auto card = MiniActionPresentation::mainPresetCard(s);
    if (card.phase != MiniActionPresentation::MainPresetPhase::Pending &&
        card.phase != MiniActionPresentation::MainPresetPhase::Loading) return result;
    if (!s.identityKnown || s.ampName.empty() || s.ampSerial.empty() ||
        s.ampName != e.cacheAmpName || s.ampSerial != e.cacheAmpSerial ||
        !e.checksumsReady || !e.slotPresent || e.generationBefore != e.generationAfter ||
        card.target < 1 || card.target > s.hardwarePresetNames.size() ||
        preset.isEmpty || preset.name.empty() || preset.checksum != e.checksum ||
        preset.pedals.size() <= 6) return result;
    constexpr uint8_t indices[6] = {0, 1, 2, 4, 5, 6};
    for (uint8_t i : indices)
        if (preset.pedals[i].name.empty() || !preset.pedals[i].isOnKnown) return result;
    result.valid = true;
    result.target = card.target;
    result.name = preset.name;
    result.description = preset.description;
    for (uint8_t i = 0; i < 6; ++i) result.enabled[i] = preset.pedals[indices[i]].isOn;
    return result;
}
} // namespace PresetPreviewPresentation
