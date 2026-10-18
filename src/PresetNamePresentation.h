#pragma once

#include <cstdint>
#include <limits>
#include <string>

// Presentation only: no cache phase, clock or UI state. Complete candidates are
// measured so kerning/letter spacing (including the ellipsis) decide fit.
namespace PresetNamePresentation {
struct Layout {
    std::string text;
    bool twoLine;
};

inline bool whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline size_t previousCodepoint(const std::string &text, size_t end) {
    --end;
    while (end && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return end;
}

template <typename Measure>
std::string ellipsize(std::string text, int32_t width, Measure measure) {
    if (measure(text.c_str()) <= width) return text;
    if (measure("...") > width) return {};
    while (!text.empty()) {
        text.resize(previousCodepoint(text, text.size()));
        while (!text.empty() && text.back() == ' ') text.pop_back();
        text += "...";
        if (measure(text.c_str()) <= width) return text;
        text.resize(text.size() - 3);
    }
    return "...";
}

template <typename Measure>
Layout fit(const char *name, int32_t width, Measure measure) {
    std::string text(name);
    // Preserve fitting names byte-for-byte, including repeated spaces. Embedded
    // line controls must instead be normalized to enforce at most two lines.
    if (text.find_first_of("\r\n\t\f\v") == std::string::npos &&
        measure(text.c_str()) <= width) return {text, false};
    std::string normalized;
    normalized.reserve(text.size());
    for (char c : text) {
        if (whitespace(c)) {
            if (!normalized.empty() && normalized.back() != ' ') normalized += ' ';
        } else normalized += c;
    }
    if (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    text.swap(normalized);
    if (measure(text.c_str()) <= width) return {text, false};

    size_t best = std::string::npos;
    int64_t bestScore = std::numeric_limits<int64_t>::max();
    std::string left, right;
    left.reserve(text.size());
    right.reserve(text.size());
    // Two passes: preserve whole words whenever possible; only then consider
    // breaking a token. Linear scratch storage, no array of candidate strings.
    for (int wordOnly = 1; wordOnly >= 0; --wordOnly) {
        for (size_t split = 1; split < text.size(); ++split) {
            if ((static_cast<unsigned char>(text[split]) & 0xC0) == 0x80) continue;
            const bool space = text[split] == ' ';
            if (wordOnly && !space) continue;
            if (text[split - 1] == ' ') continue;
            left.assign(text, 0, split);
            right.assign(text, split + (space ? 1 : 0), std::string::npos);
            const int64_t a = measure(left.c_str()), b = measure(right.c_str());
            if (a > width || b > width) continue;
            int64_t score = a > b ? a - b : b - a;
            // Discourage a short orphan on either line, particularly suffixes
            // such as "2". Width, not byte/character count, defines short.
            if (left.find(' ') == std::string::npos && a * 3 < width) score += width;
            if (right.find(' ') == std::string::npos && b * 3 < width) score += width;
            if (left.find(' ') == std::string::npos && a < b) score += width / 4;
            if (right.find(' ') == std::string::npos && b < a) score += width / 4;
            if (score < bestScore) { bestScore = score; best = split; }
        }
        if (best != std::string::npos) {
            const size_t skip = text[best] == ' ' ? 1 : 0;
            return {text.substr(0, best) + "\n" + text.substr(best + skip), true};
        }
    }

    // More than two lines: fill line one at a word boundary and truncate only
    // line two. An oversized first token is split at a UTF-8 boundary instead.
    size_t prefix = 0, wordPrefix = 0;
    for (size_t end = 1; end <= text.size(); ++end) {
        if (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) continue;
        left.assign(text, 0, end);
        if (measure(left.c_str()) > width) continue;
        prefix = end;
        if (end < text.size() && text[end] == ' ') wordPrefix = end;
    }
    if (wordPrefix) prefix = wordPrefix;
    if (!prefix) return {ellipsize(text, width, measure), false};
    size_t rest = prefix;
    while (rest < text.size() && text[rest] == ' ') ++rest;
    left.assign(text, 0, prefix);
    while (!left.empty() && left.back() == ' ') left.pop_back();
    right = ellipsize(text.substr(rest), width, measure);
    return {left + "\n" + right, true};
}
} // namespace PresetNamePresentation
