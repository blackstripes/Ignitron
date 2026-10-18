#include "PresetNamePresentation.h"
#include <cassert>
#include <cstring>

// Unequal glyph widths catch character-count truncation. Production supplies
// lv_text_get_size with the actual label font and letter spacing.
static int32_t measure(const char *text) {
    int32_t width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if ((*p & 0xC0) == 0x80) continue;
        width += *p == 'W' ? 10 : *p == '.' ? 2 : 3;
    }
    return width;
}

int main() {
    using PresetNamePresentation::singleLine;
    assert(singleLine("", 20, measure).empty());
    assert(singleLine("iiii", 20, measure) == "iiii");
    assert(singleLine("WWWW", 20, measure) == "W...");
    assert(singleLine("WW", 20, measure) == "WW"); // Exact fit.
    assert(singleLine("WiiW", 20, measure) == "Wi...");
    assert(singleLine("A\nB\rC\tD", 40, measure) == "A B C D");
    assert(singleLine(u8"éééééé", 12, measure) == u8"éé...");
    assert(singleLine("WW", 5, measure).empty());
    const auto pending = singleLine("WWWW", 20, measure);
    const auto ready = singleLine("WWWW", 20, measure);
    assert(pending == ready); // No phase, elapsed time or view-dependent state.
    assert(singleLine("New", 20, measure) == "New");
    assert(singleLine("WWWW", 20, measure) == pending);
}
