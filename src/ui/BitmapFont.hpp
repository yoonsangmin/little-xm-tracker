#pragma once
#include <SDL.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace brick::ui {
// Load the bundled BMP GNU Unifont HEX once, before drawing UI text. Throws if
// the asset is missing or malformed; later calls retain the first loaded font.
void loadUnicodeFont(const std::filesystem::path& path);
bool hasUnicodeGlyph(char32_t codepoint) noexcept;

// ASCII retains the original pixel alphabet; other Unicode scalars use Unifont.
// Modern decomposed Hangul is composed for display and basic combining marks
// overlay their preceding glyph. This is not a bidi/complex-script shaper.
void text(SDL_Renderer* renderer, int x, int y, std::string_view value,
          SDL_Color color, int scale = 2);
int textWidth(std::string_view value, int scale = 2); // Widest line, in pixels.
// Each line fits columns * 6 logical units. Truncation preserves full UTF-8
// scalars/combining sequences; malformed bytes become visible U+FFFD glyphs.
std::string fit(std::string_view value, std::size_t columns);
} // namespace brick::ui
