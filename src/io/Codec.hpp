#pragma once

#include "model/Document.hpp"
#include <cstddef>
#include <filesystem>

namespace brick::io {

// Unsupported variants and malformed inputs throw std::runtime_error.
// Read XM 1.02/1.03/1.04; always write the standard 1.04 layout.
std::vector<std::uint8_t> encodeXm(const Document& document);
struct XmImportInfo {
    // Recognized OpenMPT extras are bounded and skipped, not applied or saved.
    bool ignoredOpenMptExtensions = false;
};
Document decodeXm(const std::vector<std::uint8_t>& bytes, XmImportInfo* info = nullptr);
std::vector<std::uint8_t> encodeXi(const Instrument& instrument);
Instrument decodeXi(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> readFile(const std::filesystem::path& path);
void writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);
// PCM WAV only: unsigned 8-bit or signed 16-bit, mono/stereo. Stereo is averaged
// to mono only with explicit caller opt-in (default: reject).
Sample decodeWav(const std::vector<std::uint8_t>& bytes, bool downmixStereo = false);
// Exports signed 16-bit mono PCM at the sample's XM C4 playback rate.
// Instrument envelopes and sample loop metadata are not embedded in WAV.
std::vector<std::uint8_t> encodeWavMono(const Sample& sample);
// Exports the half-open PCM frame range [first, last), without changing the sample.
// Empty ranges are valid; callers can require a nonempty user selection.
std::vector<std::uint8_t> encodeWavMono(const Sample& sample, std::size_t first, std::size_t last);
std::vector<std::uint8_t> encodeWav16(const std::vector<std::int16_t>& interleavedStereo,
                                      std::uint32_t sampleRate);

} // namespace brick::io
