#pragma once

#include <cstdint>
#include <string>

// Presentation only. Measure the complete candidate (including dots) so glyph
// widths, letter spacing and kerning, rather than character counts, decide fit.
namespace PresetNamePresentation {
template <typename Measure>
std::string singleLine(const char *name, int32_t width, Measure measure) {
    std::string text(name);
    for (char &c : text) {
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    }
    if (measure(text.c_str()) <= width) return text;
    const std::string dots = "...";
    if (measure(dots.c_str()) > width) return {};
    while (!text.empty()) {
        // Remove one complete UTF-8 codepoint, never leave a partial suffix.
        size_t last = text.size() - 1;
        while (last && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80) --last;
        text.resize(last);
        const std::string candidate = text + dots;
        if (measure(candidate.c_str()) <= width) return candidate;
    }
    return dots;
}
} // namespace PresetNamePresentation
