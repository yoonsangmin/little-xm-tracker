#include "text/Utf8.hpp"

#include <algorithm>
#include <array>

namespace brick::text {
namespace {
constexpr char32_t replacement = 0xfffd;

// IBM PC code page 437, bytes 0x80..0xff. ASCII controls are intentionally
// rendered as spaces, rather than the old PC control-code pictographs.
constexpr std::array<char32_t, 128> cp437 = {{
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
}};

bool displayControl(char32_t codepoint) noexcept {
    return codepoint < 0x20 || (codepoint >= 0x7f && codepoint <= 0x9f);
}
} // namespace

bool nextUtf8(std::string_view value, std::size_t& offset, char32_t& codepoint) noexcept {
    codepoint = replacement;
    if (offset >= value.size()) return false;
    const auto start = offset++;
    const auto first = static_cast<unsigned char>(value[start]);
    if (first < 0x80) {
        codepoint = first;
        return true;
    }
    unsigned length;
    char32_t result, minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        length = 2; result = first & 0x1f; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3; result = first & 0x0f; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4; result = first & 0x07; minimum = 0x10000;
    } else {
        return false;
    }
    if (value.size() - start < length) return false;
    for (unsigned i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(value[start + i]);
        if ((next & 0xc0) != 0x80) return false;
        result = (result << 6) | (next & 0x3f);
    }
    if (result < minimum || result > 0x10ffff || (result >= 0xd800 && result <= 0xdfff)) return false;
    offset = start + length;
    codepoint = result;
    return true;
}

bool isValidUtf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    char32_t codepoint;
    while (offset < value.size()) {
        if (!nextUtf8(value, offset, codepoint)) return false;
    }
    return true;
}

std::u32string decodeUtf8(std::string_view value) {
    std::u32string result;
    result.reserve(value.size());
    std::size_t offset = 0;
    char32_t codepoint;
    while (offset < value.size()) {
        nextUtf8(value, offset, codepoint);
        result.push_back(codepoint);
    }
    return result;
}

void appendUtf8(std::string& output, char32_t codepoint) {
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) codepoint = replacement;
    if (codepoint < 0x80) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string xmDisplayText(std::string_view rawName) {
    std::string result;
    result.reserve(rawName.size());
    if (isValidUtf8(rawName)) {
        std::size_t offset = 0;
        char32_t codepoint;
        while (offset < rawName.size()) {
            nextUtf8(rawName, offset, codepoint);
            appendUtf8(result, displayControl(codepoint) ? U' ' : codepoint);
        }
    } else {
        for (const auto character : rawName) {
            const auto byte = static_cast<unsigned char>(character);
            const char32_t codepoint = byte < 0x80 ? byte : cp437[byte - 0x80];
            appendUtf8(result, displayControl(codepoint) ? U' ' : codepoint);
        }
    }
    return result;
}

std::string truncateBytes(std::string_view value, std::size_t maximumBytes) {
    if (value.size() <= maximumBytes) return std::string(value);
    if (!isValidUtf8(value)) return std::string(value.substr(0, maximumBytes));
    std::size_t offset = 0, retained = 0;
    char32_t codepoint;
    while (offset < value.size()) {
        nextUtf8(value, offset, codepoint);
        if (offset > maximumBytes) break;
        retained = offset;
    }
    return std::string(value.substr(0, retained));
}

void popBackCharacter(std::string& value) {
    if (value.empty()) return;
    if (!isValidUtf8(value)) {
        value.pop_back();
        return;
    }
    auto offset = value.size() - 1;
    while (offset > 0 && (static_cast<unsigned char>(value[offset]) & 0xc0) == 0x80) --offset;
    value.resize(offset);
}

} // namespace brick::text
