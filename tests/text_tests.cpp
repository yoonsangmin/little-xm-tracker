#include "io/Codec.hpp"
#include "model/Document.hpp"
#include "text/Utf8.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void strictUnicode() {
    const std::string valid = u8"A\u00e9\u03a9\ud55c\u65e5\U0001f642";
    const std::u32string expected = U"A\u00e9\u03a9\ud55c\u65e5\U0001f642";
    require(brick::text::isValidUtf8(valid), "multilingual UTF-8 rejected");
    require(brick::text::decodeUtf8(valid) == expected, "Unicode scalar decoding mismatch");
    std::string encoded;
    for (const auto codepoint : expected) brick::text::appendUtf8(encoded, codepoint);
    require(encoded == valid, "Unicode scalar re-encoding mismatch");
    require(brick::text::isValidUtf8(""), "empty UTF-8 rejected");
    require(brick::text::isValidUtf8(std::string("A\0B", 3)), "embedded NUL is valid UTF-8");

    const std::vector<std::string> invalid = {
        "\x80", "\xc0\xaf", "\xc1\xbf", "\xe0\x80\xaf", "\xf0\x80\x80\xaf",
        "\xed\xa0\x80", "\xed\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
        "\xff", "\xc2", "\xe2\x82", "\xf0\x9f\x99", "\xe2!\xa1"
    };
    for (const auto& value : invalid) require(!brick::text::isValidUtf8(value), "invalid UTF-8 accepted");
    require(brick::text::decodeUtf8("\xe2!\xa1") == U"\ufffd!\ufffd", "invalid decoder did not advance safely");
    std::size_t offset = 0;
    char32_t codepoint = 0;
    require(!brick::text::nextUtf8("", offset, codepoint) && offset == 0, "decoder advanced past end");
    encoded.clear();
    brick::text::appendUtf8(encoded, 0xd800);
    brick::text::appendUtf8(encoded, 0x110000);
    require(encoded == u8"\ufffd\ufffd", "encoder emitted an invalid Unicode scalar");
}

void byteLimitsAndBackspace() {
    const std::string name = u8"A\ud55c\u00e9\U0001f642Z";
    const std::vector<std::size_t> boundaries = {0, 1, 4, 6, 10, 11};
    for (std::size_t limit = 0; limit <= name.size() + 2; ++limit) {
        const auto prefix = brick::text::truncateBytes(name, limit);
        std::size_t expected = 0;
        for (const auto boundary : boundaries) if (boundary <= limit) expected = boundary;
        require(prefix == name.substr(0, expected), "UTF-8 byte limit split a codepoint");
        require(brick::text::isValidUtf8(prefix), "truncated name is invalid UTF-8");
    }
    auto edited = name;
    brick::text::popBackCharacter(edited); require(edited == u8"A\ud55c\u00e9\U0001f642", "ASCII backspace failed");
    brick::text::popBackCharacter(edited); require(edited == u8"A\ud55c\u00e9", "4-byte backspace split scalar");
    brick::text::popBackCharacter(edited); require(edited == u8"A\ud55c", "2-byte backspace split scalar");
    brick::text::popBackCharacter(edited); require(edited == "A", "3-byte backspace split scalar");
    brick::text::popBackCharacter(edited); brick::text::popBackCharacter(edited);
    require(edited.empty(), "empty backspace failed");

    std::string legacy = "A\x82\xb3";
    require(brick::text::truncateBytes(legacy, 2) == std::string("A\x82"), "legacy byte truncation changed encoding");
    brick::text::popBackCharacter(legacy);
    require(legacy == std::string("A\x82"), "legacy byte backspace changed retained bytes");
    const auto decomposed = u8"e\u0301";
    require(brick::text::truncateBytes(decomposed, 3) == decomposed, "helper normalized Unicode");
}

void displayOnlyFallback() {
    const std::string unicode = u8"\ud55c\uad6d\uc5b4 \u65e5\u672c\u8a9e \u00e9";
    require(brick::text::xmDisplayText(unicode) == unicode, "valid UTF-8 did not take precedence");
    const std::string legacy = "Caf\x82 \xb3\xdb";
    const auto before = legacy;
    require(brick::text::xmDisplayText(legacy) == u8"Caf\u00e9 \u2502\u2588", "CP437 fallback mapping failed");
    require(legacy == before, "display conversion changed original bytes");
    // One invalid byte selects CP437 for the whole field, not a hybrid encoding.
    require(brick::text::xmDisplayText("\xc3\xa9\xff") == u8"\u251c\u2310\u00a0", "fallback mixed UTF-8 and CP437");
    require(brick::text::xmDisplayText(std::string("A\0\nB\x7f", 5)) == "A  B ", "name control bytes reached layout");
    require(brick::text::xmDisplayText(u8"A\u0085B") == "A B", "Unicode C1 control reached layout");
    require(brick::text::xmDisplayText(std::string("\x82\0\n", 3)) == u8"\u00e9  ", "legacy name control bytes reached layout");
}

void codecNamesRemainBytes() {
    auto document = brick::makeDemoDocument();
    document.name = u8"\ud55c\uae00 \uace1";
    document.instruments[0].name = u8"\u65e5\u672c\u8a9e";
    document.instruments[0].samples[0].name = "Caf\x82";
    const auto initialXm = brick::io::encodeXm(document);
    const auto loaded = brick::io::decodeXm(initialXm);
    require(loaded.name == document.name, "XM UTF-8 song bytes changed");
    require(loaded.instruments[0].name == document.instruments[0].name, "XM UTF-8 instrument bytes changed");
    require(loaded.instruments[0].samples[0].name == document.instruments[0].samples[0].name, "XM CP437 sample bytes changed");
    (void)brick::text::xmDisplayText(loaded.name);
    (void)brick::text::xmDisplayText(loaded.instruments[0].samples[0].name);
    require(brick::io::encodeXm(loaded) == initialXm, "display altered serialized XM bytes");

    auto instrument = loaded.instruments[0];
    instrument.name = std::string("Raw\0\x82", 5);
    const auto initialXi = brick::io::encodeXi(instrument);
    const auto loadedXi = brick::io::decodeXi(initialXi);
    require(loadedXi.name == instrument.name, "XI embedded NUL/legacy name bytes changed");
    (void)brick::text::xmDisplayText(loadedXi.name);
    require(brick::io::encodeXi(loadedXi) == initialXi, "display altered serialized XI bytes");
}
} // namespace

int main() {
    try {
        strictUnicode();
        byteLimitsAndBackspace();
        displayOnlyFallback();
        codecNamesRemainBytes();
        std::cout << "Text decoding, byte limits, display fallback, and codec name preservation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
