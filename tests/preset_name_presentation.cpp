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
    using PresetNamePresentation::nameTopOffset;
    assert(nameTopOffset(true, 27) == 42);
    assert(nameTopOffset(true, 27) + 27 == 69); // Second line unchanged.
    assert(nameTopOffset(false, 27) == 56); // Half-line rounds up.
    assert(nameTopOffset(false, 26) == 55);
    assert(nameTopOffset(true, 26) == 42);
    for (const char *name : {"New", "American Classic Blues 2"}) {
        const auto pending = fit(name, 66, measure);
        const auto loading = fit(name, 66, measure);
        const auto ready = fit(name, 66, measure);
        const int32_t expected = pending.twoLine ? 42 : 56;
        assert(nameTopOffset(pending.twoLine, 27) == expected);
        assert(nameTopOffset(loading.twoLine, 27) == expected);
        assert(nameTopOffset(ready.twoLine, 27) == expected);
    }
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

    for (const char *space : {u8"\u0085", u8"\u00A0", u8"\u1680",
                             u8"\u2000", u8"\u2001", u8"\u2002", u8"\u2003",
                             u8"\u2004", u8"\u2005", u8"\u2006", u8"\u2007",
                             u8"\u2008", u8"\u2009", u8"\u200A", u8"\u2028",
                             u8"\u2029", u8"\u202F", u8"\u205F", u8"\u3000"}) {
        const std::string name = std::string(u8"é音🎸") + space + u8"é音🎸";
        const auto fitting = fit(name.c_str(), 21, measure);
        assert(fitting.text == name && !fitting.twoLine); // Exact-fit bytes unchanged.
        const auto split = fit(name.c_str(), 9, measure);
        assert(split.text == u8"é音🎸\né音🎸" && split.twoLine);
        const std::string repeated = std::string(space) + u8"é音🎸" + space + " " +
                                     space + u8"é音🎸" + space;
        assert(fit(repeated.c_str(), 9, measure).text == split.text);
        const std::string longName = name + space + u8"é音🎸é音🎸";
        assert(fit(longName.c_str(), 12, measure).text == u8"é音🎸\né音...");
    }
    // Nearby non-whitespace codepoints and incomplete encodings aren't matched.
    for (const std::string &text : {std::string(u8"\u200B"), std::string(u8"\u2060"),
                                   std::string("\xC2"), std::string("\xE2\x80"),
                                   std::string("\xE2\x80X")})
        assert(PresetNamePresentation::whitespaceBytes(text, 0) == 0);

    // Every emitted line fits, including fallback and multibyte boundaries.
    for (const char *name : {"Alpha Beta Gamma Delta Extra Long Suffix 2",
                             "WWWWWWWW", u8"éé音🎸éé音🎸éé音🎸",
                             u8"é音🎸\u00A0é音🎸\u2007é音🎸\u202Fé音🎸\u3000é音🎸"}) {
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
