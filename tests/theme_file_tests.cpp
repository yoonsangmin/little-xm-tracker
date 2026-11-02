#include "io/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using brick::io::Theme;
using brick::io::decodeTheme;
using brick::io::encodeTheme;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::uint8_t> bytes(const std::string& value) {
    return {value.begin(), value.end()};
}
std::string encoded(const Theme& theme) {
    const auto data = encodeTheme(theme);
    return {data.begin(), data.end()};
}
void reject(const std::string& value, const char* expected) {
    try { (void)decodeTheme(bytes(value)); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(expected) != std::string::npos, "Theme error did not identify the problem");
        return;
    }
    throw std::runtime_error("Malformed theme was accepted");
}
std::string replace(std::string value, const std::string& from, const std::string& to) {
    const auto at = value.find(from);
    require(at != std::string::npos, "Missing test input fragment");
    value.replace(at, from.size(), to);
    return value;
}

void roundTrips() {
    require(decodeTheme(encodeTheme(brick::io::defaultTheme)) == brick::io::defaultTheme, "Default theme round trip changed colors");
    // Every role exercises every value in each RGB component, including 0/255.
    for (std::uint32_t value = 0; value < 256; ++value) {
        Theme theme{};
        for (std::size_t n = 0; n < theme.size(); ++n)
            theme[n] = (value << 16) | (((value + std::uint32_t(n) * 17) & 255u) << 8) |
                       ((255u - value + std::uint32_t(n)) & 255u);
        require(decodeTheme(encodeTheme(theme)) == theme, "RGB components changed during round trip");
    }
    auto invalid = brick::io::defaultTheme;
    invalid[6] = 0x1000000;
    bool rejected = false;
    try { (void)encodeTheme(invalid); }
    catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("RGB24: accent") != std::string::npos; }
    require(rejected, "Encoder silently truncated an invalid RGB value");
}

void textCompatibility() {
    auto text = encoded(brick::io::defaultTheme);
    text = replace(text, "#DEE8F2", "#dee8f2");
    text = replace(text, "panel #151F2C", "\tpanel\t#151F2C  # panel background");
    text = replace(text, "LITTLE_XM_THEME 1", "LITTLE_XM_THEME 1 # format version");
    std::string decorated = "\xef\xbb\xbf\r\n  # Shared theme\r\n\r\n";
    for (char c : text) decorated += c == '\n' ? "\r\n" : std::string(1, c);
    require(decodeTheme(bytes(decorated)) == brick::io::defaultTheme, "BOM, CRLF, whitespace or comments changed the theme");
    text = encoded(brick::io::defaultTheme);
    text.pop_back();
    require(decodeTheme(bytes(text)) == brick::io::defaultTheme, "Final newline was required");

    const auto first = text.find('\n') + 1;
    const auto next = text.find('\n', first) + 1;
    const auto row = text.substr(first, next - first);
    text.erase(first, next - first);
    text += "\n" + row;
    require(decodeTheme(bytes(text)) == brick::io::defaultTheme, "Theme colors depended on key order");
}

void malformedFiles() {
    const auto valid = encoded(brick::io::defaultTheme);
    reject("", "header");
    reject("# Comment only\n", "header");
    reject(replace(valid, "LITTLE_XM_THEME", "NOT_A_THEME"), "header");
    reject(replace(valid, "LITTLE_XM_THEME 1", "LITTLE_XM_THEME 2"), "version");
    reject(replace(valid, "LITTLE_XM_THEME 1", "LITTLE_XM_THEME"), "header");
    reject(replace(valid, "LITTLE_XM_THEME 1", "LITTLE_XM_THEME 1 extra"), "header");
    reject(replace(valid, "background", "unknown_color"), "Unknown theme color");
    reject(valid + "background #000000\n", "Duplicate theme color");
    reject(replace(valid, "overlay #04080F\n", ""), "Missing theme color: overlay");
    for (const std::string color : {"04080F", "#04080", "#04080FF", "#GG0000", "#-12345", "0x04080F", "16777216", ""})
        reject(replace(valid, "#04080F", color), "must be #RRGGBB");
    reject(replace(valid, "overlay #04080F", "overlay #04080F trailing"), "must be #RRGGBB");
    reject(valid + std::string("\0", 1), "Unknown theme color");
    reject(std::string(64u * 1024u + 1, ' '), "64 KiB");
    // The documented size is inclusive, and padding cannot hide missing fields.
    require(decodeTheme(bytes(valid + std::string(64u * 1024u - valid.size(), ' '))) == brick::io::defaultTheme,
            "A valid theme at the size limit was rejected");
    reject("LITTLE_XM_THEME 1\n" + std::string(64000, ' '), "Missing theme color");
}

double luminance(std::uint32_t rgb) {
    const auto channel = [](std::uint32_t value) {
        const double c = double(value & 255u) / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb);
}
double contrast(std::uint32_t a, std::uint32_t b) {
    const double first = luminance(a), second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}
void bundledThemes(const std::filesystem::path& folder) {
    std::array<Theme, 16> themes{};
    const std::array<const char*, 16> names{{"default", "midnight", "amber", "mono",
        "ocean", "forest", "phosphor", "cobalt", "arcade", "ember", "ice", "lavender",
        "rose", "copper", "sand", "paper"}};
    std::size_t themeCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        if (entry.is_regular_file() && entry.path().extension() == ".theme") ++themeCount;
    require(themeCount == names.size(), "Bundled theme count differs from the expected 16 presets");
    for (std::size_t n = 0; n < names.size(); ++n) {
        std::ifstream file(folder / (std::string(names[n]) + ".theme"), std::ios::binary);
        require(bool(file), "Bundled theme is missing");
        const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        themes[n] = decodeTheme(data);
        require(decodeTheme(encodeTheme(themes[n])) == themes[n], "Bundled theme failed export/import");
        for (std::size_t background : {0u, 1u, 2u}) {
            require(contrast(themes[n][3], themes[n][background]) >= 4.5, "Preset text has poor contrast");
            require(contrast(themes[n][6], themes[n][background]) >= 4.5, "Preset accent has poor contrast");
        }
        require(contrast(themes[n][7], themes[n][0]) >= 4.5, "Preset cursor has poor contrast");
    }
    require(themes[0] == brick::io::defaultTheme, "Bundled Default differs from the app default");
    for (std::size_t a = 0; a < themes.size(); ++a)
        for (std::size_t b = a + 1; b < themes.size(); ++b)
            require(themes[a] != themes[b], "Bundled presets are duplicates");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: theme_file_tests <bundled-themes-folder>");
        roundTrips();
        textCompatibility();
        malformedFiles();
        bundledThemes(argv[1]);
        std::cout << "Theme RGB round trips, parsing, rejection and 16 bundled presets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
