#include "ui/BitmapFont.hpp"
#include "text/Utf8.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace brick::ui {
namespace {
using Glyph = std::array<unsigned char, 7>;
const std::map<char, Glyph> glyphs = {
 {' ',{0,0,0,0,0,0,0}}, {'A',{14,17,17,31,17,17,17}},
 {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
 {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}},
 {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}},
 {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}},
 {'J',{7,2,2,2,2,18,12}}, {'K',{17,18,20,24,20,18,17}},
 {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}},
 {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
 {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
 {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}},
 {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}},
 {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
 {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}},
 {'Z',{31,1,2,4,8,16,31}},
 // Original 5x7 lowercase forms keep effect signatures and names distinct.
 {'a',{0,0,14,1,15,17,15}}, {'b',{16,16,30,17,17,17,30}},
 {'c',{0,0,14,16,16,17,14}}, {'d',{1,1,15,17,17,17,15}},
 {'e',{0,0,14,17,31,16,14}}, {'f',{6,8,8,28,8,8,8}},
 {'g',{0,0,15,17,15,1,14}}, {'h',{16,16,30,17,17,17,17}},
 {'i',{4,0,12,4,4,4,14}}, {'j',{2,0,6,2,2,18,12}},
 {'k',{16,16,18,20,24,20,18}}, {'l',{12,4,4,4,4,4,14}},
 {'m',{0,0,26,21,21,21,21}}, {'n',{0,0,30,17,17,17,17}},
 {'o',{0,0,14,17,17,17,14}}, {'p',{0,0,30,17,30,16,16}},
 {'q',{0,0,15,17,15,1,1}}, {'r',{0,0,22,25,16,16,16}},
 {'s',{0,0,15,16,14,1,30}}, {'t',{8,8,30,8,8,9,6}},
 {'u',{0,0,17,17,17,19,13}}, {'v',{0,0,17,17,17,10,4}},
 {'w',{0,0,17,17,21,21,10}}, {'x',{0,0,17,10,4,10,17}},
 {'y',{0,0,17,17,15,1,14}}, {'z',{0,0,31,2,4,8,31}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
 {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
 {'=',{0,0,31,0,31,0,0}}, {'_',{0,0,0,0,0,0,31}},
 {'.',{0,0,0,0,0,12,12}}, {',',{0,0,0,0,4,4,8}},
 {':',{0,12,12,0,12,12,0}}, {';',{0,12,12,0,4,4,8}},
 {'/',{1,2,2,4,8,8,16}}, {'\\',{16,8,8,4,2,2,1}},
 {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
 {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
 {'<',{2,4,8,16,8,4,2}}, {'>',{8,4,2,1,2,4,8}},
 {'!',{4,4,4,4,4,0,4}}, {'?',{14,17,1,2,4,0,4}},
 {'#',{10,31,10,10,31,10,0}}, {'%',{17,2,4,4,8,17,0}},
 {'*',{0,21,14,31,14,21,0}}, {'|',{4,4,4,4,4,4,4}},
 {'"',{10,10,10,0,0,0,0}}, {'\'',{4,4,8,0,0,0,0}},
 {'@',{14,17,23,21,23,16,14}}, {'^',{4,10,17,0,0,0,0}},
 {'~',{0,0,9,22,0,0,0}}
};
struct UnicodeGlyph {
    std::array<std::uint16_t, 16> rows{};
    std::uint8_t width = 0;
};
using UnicodeTable = std::array<UnicodeGlyph, 65536>;
std::unique_ptr<const UnicodeTable> unicodeFont;
std::mutex fontLoadMutex;
// Visible replacement box also works before initialization or outside the BMP.
const UnicodeGlyph missingGlyph{{0, 0x7e, 0x42, 0x5a, 0x66, 0x66, 0x5a, 0x42,
                                0x42, 0x5a, 0x66, 0x66, 0x5a, 0x42, 0x7e, 0}, 8};

const UnicodeGlyph& unicodeGlyph(char32_t codepoint) {
    if (unicodeFont) {
        if (codepoint < unicodeFont->size() && (*unicodeFont)[codepoint].width)
            return (*unicodeFont)[codepoint];
        if ((*unicodeFont)[0xfffd].width) return (*unicodeFont)[0xfffd];
    }
    return missingGlyph;
}

const Glyph* asciiGlyph(char32_t codepoint) {
    if (codepoint >= 128) return nullptr;
    const auto found = glyphs.find(static_cast<char>(codepoint));
    return found == glyphs.end() ? nullptr : &found->second;
}

bool combiningMark(char32_t codepoint) {
    return (codepoint >= 0x0300 && codepoint <= 0x036f) ||
           (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
           (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
           (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
           (codepoint >= 0xfe20 && codepoint <= 0xfe2f) ||
           codepoint == 0x3099 || codepoint == 0x309a;
}

// Compose only modern Hangul L+V(+T) and LV+T. This changes display output,
// never the bytes in the document or filesystem names.
char32_t nextDisplayScalar(std::string_view value, std::size_t& offset) {
    char32_t codepoint = 0xfffd;
    brick::text::nextUtf8(value, offset, codepoint);
    if (codepoint >= 0x1100 && codepoint < 0x1113) {
        std::size_t lookahead = offset;
        char32_t vowel = 0;
        if (brick::text::nextUtf8(value, lookahead, vowel) && vowel >= 0x1161 && vowel < 0x1176) {
            codepoint = 0xac00 + (codepoint - 0x1100) * 588 + (vowel - 0x1161) * 28;
            offset = lookahead;
        }
    }
    if (codepoint >= 0xac00 && codepoint <= 0xd7a3 && (codepoint - 0xac00) % 28 == 0) {
        std::size_t lookahead = offset;
        char32_t trailing = 0;
        if (brick::text::nextUtf8(value, lookahead, trailing) && trailing > 0x11a7 && trailing < 0x11c3) {
            codepoint += trailing - 0x11a7;
            offset = lookahead;
        }
    }
    return codepoint;
}

int advance(char32_t codepoint, bool hasBase) {
    if (hasBase && combiningMark(codepoint) && hasUnicodeGlyph(codepoint)) return 0;
    if (asciiGlyph(codepoint)) return 6;
    return unicodeGlyph(codepoint).width == 16 ? 9 : 6;
}

std::size_t lineWidth(std::string_view value) {
    std::size_t offset = 0, width = 0;
    bool hasBase = false;
    while (offset < value.size()) {
        const auto codepoint = nextDisplayScalar(value, offset);
        width += static_cast<std::size_t>(advance(codepoint, hasBase));
        hasBase = true;
    }
    return width;
}

void fitLine(std::string& output, std::string_view value, std::size_t columns) {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const auto budget = columns > maximum / 6 ? maximum : columns * 6;
    const bool clipped = lineWidth(value) > budget;
    const bool ellipsis = clipped && columns > 3;
    const auto available = ellipsis ? budget - 18 : budget;
    std::size_t offset = 0, used = 0;
    bool hasBase = false;
    while (offset < value.size()) {
        const auto codepoint = nextDisplayScalar(value, offset);
        const auto amount = static_cast<std::size_t>(advance(codepoint, hasBase));
        if (amount > available - used) break;
        brick::text::appendUtf8(output, codepoint);
        used += amount;
        hasBase = true;
    }
    if (ellipsis) output += "...";
}

unsigned hexDigit(char digit) {
    if (digit >= '0' && digit <= '9') return static_cast<unsigned>(digit - '0');
    if (digit >= 'A' && digit <= 'F') return static_cast<unsigned>(digit - 'A' + 10);
    if (digit >= 'a' && digit <= 'f') return static_cast<unsigned>(digit - 'a' + 10);
    throw std::runtime_error("Malformed hexadecimal digit in bundled Unicode font");
}
} // namespace

void loadUnicodeFont(const std::filesystem::path& path) {
    // The older ARM64 SDK's call_once can deadlock when retrying after an
    // exception. A scoped mutex keeps a failed load retryable on that runtime.
    std::lock_guard<std::mutex> lock(fontLoadMutex);
    if (unicodeFont) return;
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open bundled Unicode font: " + path.u8string());
        auto table = std::make_unique<UnicodeTable>();
        std::array<char, 80> line{};
        std::size_t count = 0;
        while (input.getline(line.data(), static_cast<std::streamsize>(line.size()))) {
            std::string_view record(line.data());
            if (!record.empty() && record.back() == '\r') record.remove_suffix(1);
            if (record.empty()) continue;
            const auto colon = record.find(':');
            if (colon != 4 || (record.size() != 37 && record.size() != 69))
                throw std::runtime_error("Malformed BMP Unicode font record");
            unsigned codepoint = 0;
            for (std::size_t i = 0; i < colon; ++i) codepoint = codepoint * 16 + hexDigit(record[i]);
            auto& glyph = (*table)[codepoint];
            if (glyph.width) throw std::runtime_error("Duplicate glyph in bundled Unicode font");
            glyph.width = record.size() == 37 ? 8 : 16;
            const std::size_t rowDigits = glyph.width / 4;
            for (std::size_t row = 0; row < 16; ++row) {
                unsigned bits = 0;
                for (std::size_t digit = 0; digit < rowDigits; ++digit)
                    bits = bits * 16 + hexDigit(record[5 + row * rowDigits + digit]);
                glyph.rows[row] = static_cast<std::uint16_t>(bits);
            }
            ++count;
        }
        if (!input.eof()) throw std::runtime_error("Unreadable or oversized bundled Unicode font record");
        if (!count || !(*table)[0xfffd].width) throw std::runtime_error("Unicode font lacks a replacement glyph");
        unicodeFont = std::move(table);
    }
}

bool hasUnicodeGlyph(char32_t codepoint) noexcept {
    return unicodeFont && codepoint < unicodeFont->size() &&
        !(codepoint >= 0xd800 && codepoint <= 0xdfff) && (*unicodeFont)[codepoint].width != 0;
}

void text(SDL_Renderer* renderer, int x, int y, std::string_view value, SDL_Color color, int scale) {
    if (scale <= 0) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int origin = x;
    int baseX = x, baseAdvance = 0;
    bool hasBase = false, baseAscii = false;
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto codepoint = nextDisplayScalar(value, offset);
        if (codepoint == U'\n') {
            y += 9 * scale;
            x = baseX = origin;
            hasBase = false;
            continue;
        }
        const int units = advance(codepoint, hasBase);
        if (const auto* tinyGlyph = asciiGlyph(codepoint)) {
            for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
                if ((*tinyGlyph)[row] & (1 << (4 - col))) {
                    SDL_Rect pixel{x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        } else {
            const auto& glyph = unicodeGlyph(codepoint);
            const int glyphWidth = glyph.width * scale / 2;
            const int drawWidth = units == 0 ? std::min(glyphWidth, baseAdvance) : glyphWidth;
            const int drawX = units == 0 ? baseX + (baseAdvance - drawWidth) / 2 : x;
            const int drawY = units == 0 && baseAscii ? y - 2 * scale : y;
            for (int row = 0; row < 16; ++row) for (int col = 0; col < glyph.width; ++col) {
                if (!(glyph.rows[row] & (1u << (glyph.width - 1 - col)))) continue;
                const int left = col * drawWidth / glyph.width;
                const int right = (col + 1) * drawWidth / glyph.width;
                const int top = row * scale / 2, bottom = (row + 1) * scale / 2;
                if (right <= left || bottom <= top) continue;
                SDL_Rect pixel{drawX + left, drawY + top, right - left, bottom - top};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        if (units) {
            baseX = x;
            baseAdvance = units * scale;
            baseAscii = asciiGlyph(codepoint) != nullptr;
            x += baseAdvance;
        }
        hasBase = true;
    }
}

int textWidth(std::string_view value, int scale) {
    if (scale <= 0) return 0;
    std::size_t offset = 0, current = 0, widest = 0;
    bool hasBase = false;
    while (offset < value.size()) {
        const auto codepoint = nextDisplayScalar(value, offset);
        if (codepoint == U'\n') {
            widest = std::max(widest, current);
            current = 0;
            hasBase = false;
        } else {
            current += static_cast<std::size_t>(advance(codepoint, hasBase));
            hasBase = true;
        }
    }
    widest = std::max(widest, current);
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return widest > maximum / static_cast<std::size_t>(scale) ? std::numeric_limits<int>::max()
        : static_cast<int>(widest * static_cast<std::size_t>(scale));
}

std::string fit(std::string_view value, std::size_t columns) {
    std::string result;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto newline = value.find('\n', begin);
        const auto end = newline == std::string_view::npos ? value.size() : newline;
        fitLine(result, value.substr(begin, end - begin), columns);
        if (newline == std::string_view::npos) break;
        result += '\n';
        begin = newline + 1;
    }
    return result;
}
} // namespace brick::ui
