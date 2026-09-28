#pragma once
#include <array>
#include <string>

// Display metadata, deliberately independent of the active preset. Empty cache
// entries are not evidence that a previously observed slot lost its name.
namespace HardwarePresetNames {
using Names = std::array<std::string, 8>;
inline void merge(Names &names, bool connected, bool identityChanged,
                  bool validated, const Names &observed) {
    if (!connected || identityChanged) names.fill("");
    if (!connected || !validated) return;
    for (size_t i = 0; i < names.size(); ++i)
        if (!observed[i].empty()) names[i] = observed[i];
}
inline const char *label(const Names &names, unsigned oneBasedSlot) {
    if (oneBasedSlot < 1 || oneBasedSlot > names.size() || names[oneBasedSlot - 1].empty())
        return "UNKNOWN";
    return names[oneBasedSlot - 1].c_str();
}
}
