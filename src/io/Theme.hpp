#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace brick::io {

using Theme = std::array<std::uint32_t, 13>; // RGB24; alpha belongs to the UI.

inline constexpr std::array<const char*, 13> themeColorKeys{{
    "background", "panel", "highlight", "text", "secondary_text", "dim_text",
    "accent", "cursor", "volume", "warning", "selection", "sample_selection", "overlay"
}};

inline constexpr Theme defaultTheme{{
    0x0d131d, 0x151f2c, 0x1a2736, 0xdee8f2, 0x7a93a6, 0x405465,
    0x56deda, 0xffc768, 0x82dd8f, 0xf68080, 0x22414c, 0x273640, 0x04080f
}};

// LITTLE_XM_THEME 1, followed by all 13 keys as "key #RRGGBB".
// Accepts UTF-8 BOM, CRLF, blank lines and # comments. Malformed or oversized
// files (over 64 KiB) throw std::runtime_error; no partial theme is returned.
Theme decodeTheme(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encodeTheme(const Theme& theme);

} // namespace brick::io
