#include "ui/BitmapFont.hpp"
#include "text/Utf8.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void fontCoverage(const std::filesystem::path& fontPath) {
    bool failed = false;
    auto absent = fontPath;
    absent += ".missing-font-test";
    try { brick::ui::loadUnicodeFont(absent); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "A missing font asset must produce an explicit error");
    brick::ui::loadUnicodeFont(fontPath);
    const auto coverage = U"\uD55C\uAE00\u4E2D\u6587\u65E5\u672C\u8A9E\u3042\u30A2\u00E9\u00F1\u042F\u0416\uFFFD";
    for (const char32_t* character = coverage; *character; ++character)
        require(brick::ui::hasUnicodeGlyph(*character), "The bundled font must cover the selected multilingual glyphs");
    require(!brick::ui::hasUnicodeGlyph(0x1f600), "A BMP font must report supplementary glyphs as unavailable");
    require(!brick::ui::hasUnicodeGlyph(0xd800), "Surrogates are not Unicode scalar glyphs");
}

void measuredLayout() {
    using brick::ui::textWidth;
    require(textWidth("ABC") == 36, "ASCII spacing must remain unchanged");
    require(textWidth(u8"\uD55C") == 18, "A wide Unicode glyph must occupy 18 pixels at scale two");
    require(textWidth(u8"A\uD55CB") == 42, "Mixed text must use the actual narrow/wide advances");
    require(textWidth(u8"\u00E9\u042F") == 24, "Narrow Latin and Cyrillic glyphs must use narrow advances");
    require(textWidth(u8"A\n\uD55C\uAE00") == 36, "Multiline width must measure the widest line");
    require(textWidth(u8"\uD55C", 3) == 27, "Odd integer scales must retain consistent wide spacing");
    require(textWidth("ABC", 0) == 0, "A nonpositive scale must not draw or occupy width");
    require(textWidth(u8"A\u0301") == textWidth("A"), "A supported combining mark must overlay its base");
    require(textWidth(u8"\u1100\u1161\u11A8") == textWidth(u8"\uAC01"),
            "Modern decomposed Hangul must compose for consistent display width");
    require(textWidth(u8"\U0001F600") > 0, "An unavailable scalar must occupy a visible replacement cell");
}

void fittingAndMalformedInput() {
    using brick::ui::fit;
    require(fit(u8"\uD55C\uAE00", 3) == u8"\uD55C\uAE00", "Fitting must preserve complete CJK UTF-8 text");
    require(fit(u8"A\uD55CB", 3) == u8"A\uD55C", "A wide character must not overrun a short fit budget");
    require(fit(u8"\uD55C", 1).empty(), "A wide glyph must not be squeezed into one ASCII column");
    require(fit(u8"A\uD55CB\uAE00C", 4) == "A...", "Ellipsis width must be included in the fit budget");
    require(fit(u8"A\u0301B", 1) == u8"A\u0301", "Fitting must retain trailing combining marks on a kept base");
    require(fit(u8"\u1100\u1161\u11A8", 2) == u8"\uAC01", "Display fitting may compose modern Hangul");
    require(fit(u8"\U0001F600", 3) == u8"\U0001F600", "An unavailable glyph must not rewrite the valid scalar to a question mark");

    const std::vector<std::string> malformed{
        std::string("\x80\x80", 2), std::string("\xe3\x81", 2),
        std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3)
    };
    for (const auto& bytes : malformed) {
        std::string expected;
        for (std::size_t i = 0; i < bytes.size(); ++i) expected += u8"\uFFFD";
        const auto sanitized = fit(bytes, 100);
        require(sanitized == expected, "Each malformed byte must yield an explicit replacement, including stray continuations");
        require(brick::text::isValidUtf8(sanitized), "Fitting must always produce well-formed UTF-8");
    }
    const std::vector<std::string> values{
        "ASCII tracker", u8"\uD55C\uAE00 XM \u4E2D\u6587", u8"\u65E5\u672C\u8A9E\nABCDE",
        u8"A\u0301B\u00E9\u042F", u8"\u1100\u1161\u11A8\uD55C", malformed[0]
    };
    for (const auto& value : values) for (std::size_t columns = 0; columns < 12; ++columns) {
        const auto fitted = fit(value, columns);
        require(brick::text::isValidUtf8(fitted), "Every fitted output must preserve UTF-8 boundaries");
        require(brick::ui::textWidth(fitted) <= static_cast<int>(columns * 12),
                "Every fitted line must remain within its measured pixel budget");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Pass the bundled Unifont HEX path as the first argument");
        fontCoverage(std::filesystem::u8path(argv[1]));
        measuredLayout();
        fittingAndMalformedInput();
        std::cout << "Font tests passed: glyph coverage, Unicode widths, UTF-8 fitting, malformed input, Hangul and combining marks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Font test failure: " << error.what() << '\n';
        return 1;
    }
}
