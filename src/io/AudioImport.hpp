#pragma once

#include "model/Document.hpp"
#include <filesystem>
#include <string_view>

namespace brick::io {

enum class StereoMode { Blend, Left, Right };

// Native-rate interleaved PCM. Decode first; the document is only replaced
// after the user chooses the channel mix and XM sample bit depth.
struct DecodedAudio {
    std::vector<float> pcm;
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t sourceBits = 0; // 0 for compressed sources without a PCM depth.
    std::string format;
    std::size_t frames() const { return channels ? pcm.size() / channels : 0; }
};

// WAV PCM 8/16/24/32, WAV float 32/64 (including extensible headers),
// FLAC, MP3 and Ogg Vorbis. Only mono/stereo sources; no automatic resampling.
// Files are limited to 128 MiB and decoded audio to 32M frames.
DecodedAudio decodeAudio(const std::vector<std::uint8_t>& bytes);
DecodedAudio loadAudio(const std::filesystem::path& path);
bool isAudioExtension(std::string_view extension);

// Blend is (L+R)/2. Mono ignores the mix choice. Finite values outside full
// scale are clipped on conversion; nonfinite PCM is rejected. Output is
// quantized to signed XM 8/16-bit PCM, with native-rate tuning retained.
Sample convertAudio(const DecodedAudio& audio, StereoMode mode, int outputBits);

} // namespace brick::io
