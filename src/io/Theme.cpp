#include "io/Theme.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace brick::io {
namespace {

bool extraData(std::istringstream& line) {
    std::string token;
    return bool(line >> token) && token.front() != '#';
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

} // namespace

Theme decodeTheme(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > 64u * 1024u)
        throw std::runtime_error("Theme file exceeds 64 KiB");
    std::string contents(bytes.begin(), bytes.end());
    if (contents.compare(0, 3, "\xef\xbb\xbf") == 0) contents.erase(0, 3);
    std::istringstream input(contents);
    Theme result{};
    std::array<bool, themeColorKeys.size()> seen{};
    bool header = false;
    std::string text;
    while (std::getline(input, text)) {
        std::istringstream line(text);
        std::string key, value;
        if (!(line >> key) || key.front() == '#') continue;
        if (!header) {
            if (key != "LITTLE_XM_THEME" || !(line >> value) || extraData(line))
                throw std::runtime_error("Theme header must be LITTLE_XM_THEME 1");
            if (value != "1") throw std::runtime_error("Unsupported theme version: " + value);
            header = true;
            continue;
        }
        const auto found = std::find(themeColorKeys.begin(), themeColorKeys.end(), key);
        if (found == themeColorKeys.end()) throw std::runtime_error("Unknown theme color: " + key);
        const auto index = std::size_t(found - themeColorKeys.begin());
        if (seen[index]) throw std::runtime_error("Duplicate theme color: " + key);
        if (!(line >> value) || value.size() != 7 || value.front() != '#' || extraData(line))
            throw std::runtime_error("Theme color " + key + " must be #RRGGBB");
        std::uint32_t color = 0;
        for (std::size_t n = 1; n < value.size(); ++n) {
            const int digit = hexDigit(value[n]);
            if (digit < 0) throw std::runtime_error("Theme color " + key + " must be #RRGGBB");
            color = (color << 4) | std::uint32_t(digit);
        }
        result[index] = color;
        seen[index] = true;
    }
    if (!header) throw std::runtime_error("Theme header must be LITTLE_XM_THEME 1");
    for (std::size_t n = 0; n < seen.size(); ++n)
        if (!seen[n]) throw std::runtime_error(std::string("Missing theme color: ") + themeColorKeys[n]);
    return result;
}

std::vector<std::uint8_t> encodeTheme(const Theme& theme) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result = "LITTLE_XM_THEME 1\n";
    for (std::size_t n = 0; n < theme.size(); ++n) {
        if (theme[n] > 0xffffff)
            throw std::runtime_error(std::string("Theme color exceeds RGB24: ") + themeColorKeys[n]);
        result += themeColorKeys[n];
        result += " #";
        for (int shift = 20; shift >= 0; shift -= 4) result += digits[(theme[n] >> shift) & 15u];
        result += '\n';
    }
    return {result.begin(), result.end()};
}

} // namespace brick::io
