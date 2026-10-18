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
    using PresetNamePresentation::fit;
    assert(fit("", 20, measure).text.empty());
    assert(fit("iiii", 20, measure).text == "iiii");
    assert(fit("  A  B ", 40, measure).text == "  A  B ");
    assert(fit("WW", 20, measure).text == "WW"); // Exact fit.
    assert(!fit("WW", 20, measure).twoLine);
    assert(fit("WWWW", 20, measure).text == "WW\nWW");
    assert(fit("American Classic Blues 2", 66, measure).text == "American Classic\nBlues 2");
    assert(fit("American   Classic  Blues  2", 66, measure).text == "American Classic\nBlues 2");
    assert(fit("Alpha Beta Gamma Delta", 40, measure).text == "Alpha Beta\nGamma Delta");
    assert(fit("Big Warm Tone 2", 39, measure).text == "Big Warm\nTone 2");
    assert(fit("WWWWWWWW", 20, measure).text == "WW\nW...");
    assert(fit("A\nB\rC\tD", 40, measure).text == "A B C D");
    assert(fit(u8"éééééé", 12, measure).text == u8"ééé\nééé");
    assert(fit(u8"éééééééééé", 12, measure).text == u8"éééé\néé...");
    assert(fit(u8"🎸音é🎸音é", 9, measure).text == u8"🎸音é\n🎸音é");
    assert(fit("WW", 5, measure).text.empty());
    const auto pending = fit("American Classic Blues 2", 66, measure);
    const auto loading = fit("American Classic Blues 2", 66, measure);
    const auto ready = fit("American Classic Blues 2", 66, measure);
    assert(pending.text == loading.text && loading.text == ready.text);
    assert(pending.twoLine && loading.twoLine && ready.twoLine);
    assert(!fit("New", 66, measure).twoLine); // Description restored.
    assert(fit("American Classic Blues 2", 66, measure).text == pending.text);

    // Every emitted line fits, including fallback and multibyte boundaries.
    for (const char *name : {"Alpha Beta Gamma Delta Extra Long Suffix 2",
                             "WWWWWWWW", u8"éé音🎸éé音🎸éé音🎸"}) {
        for (int width = 6; width <= 80; ++width) {
            const auto result = fit(name, width, measure);
            const auto newline = result.text.find('\n');
            assert(result.twoLine == (newline != std::string::npos));
            assert(measure(result.text.substr(0, newline).c_str()) <= width);
            if (result.twoLine) {
                assert(result.text.find('\n', newline + 1) == std::string::npos);
                assert(measure(result.text.substr(newline + 1).c_str()) <= width);
            }
            // For valid input every continuation byte has its full lead byte.
            for (size_t i = 0; i < result.text.size();) {
                const auto c = static_cast<unsigned char>(result.text[i]);
                const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                assert(c < 0x80 || c >= 0xC2);
                assert(i + n <= result.text.size());
                for (size_t j = 1; j < n; ++j)
                    assert((static_cast<unsigned char>(result.text[i + j]) & 0xC0) == 0x80);
                i += n;
            }
        }
    }
}
