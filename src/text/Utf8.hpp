#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace brick::text {

// Strict Unicode scalar decoder. Invalid input emits U+FFFD, consumes one byte,
// and returns false. At end it returns false without advancing the offset.
bool nextUtf8(std::string_view value, std::size_t& offset, char32_t& codepoint) noexcept;
bool isValidUtf8(std::string_view value) noexcept;
std::u32string decodeUtf8(std::string_view value);
void appendUtf8(std::string& output, char32_t codepoint);

// XM/XI do not identify a character encoding. Auto display policy: a complete
// valid UTF-8 field is decoded directly; otherwise all bytes use CP437. This is
// a display-only guess for legacy names, never a transformation of model data.
// C0/C1 control characters and DEL become spaces in either display path.
std::string xmDisplayText(std::string_view rawName);

// Preserve complete UTF-8 codepoints when the entire input is valid UTF-8.
// Otherwise preserve the byte semantics of legacy XM names. Neither function
// decodes/re-encodes the retained bytes or normalizes their Unicode characters.
std::string truncateBytes(std::string_view value, std::size_t maximumBytes);
void popBackCharacter(std::string& value);

} // namespace brick::text
