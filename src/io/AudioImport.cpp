#include "io/AudioImport.hpp"
#include "io/AudioDecoder.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace brick::io {
namespace {
constexpr std::size_t maxFileBytes = 128u * 1024u * 1024u;
constexpr std::size_t maxFrames = 32u * 1024u * 1024u;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool tag(const std::vector<std::uint8_t>& bytes, std::size_t at, const char* value) {
    return at <= bytes.size() && bytes.size() - at >= 4 && std::memcmp(bytes.data() + at, value, 4) == 0;
}
std::uint16_t u16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 2, "Truncated audio header");
    return std::uint16_t(bytes[at] | (std::uint16_t(bytes[at + 1]) << 8));
}
std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return std::uint32_t(u16(bytes, at)) | (std::uint32_t(u16(bytes, at + 2)) << 16);
}
struct WaveInfo {
    std::uint16_t bits = 0, channels = 0;
    std::uint32_t rate = 0;
    std::size_t frames = 0;
    bool floating = false;
};

// General decoders can accept a truncated WAV as a shorter file. Validate the
// container and complete sample frames before allowing that data into a song.
WaveInfo validateWave(const std::vector<std::uint8_t>& bytes) {
    require(bytes.size() >= 12 && tag(bytes, 0, "RIFF") && tag(bytes, 8, "WAVE"), "Invalid RIFF WAV header");
    require(std::uint64_t(u32(bytes, 4)) + 8 == bytes.size(), "WAV RIFF length does not match the file");
    WaveInfo info;
    bool hasFormat = false, hasData = false;
    std::uint16_t alignment = 0;
    std::size_t dataBytes = 0;
    for (std::size_t at = 12; at < bytes.size();) {
        require(bytes.size() - at >= 8, "Truncated WAV chunk header");
        const auto length = std::size_t(u32(bytes, at + 4));
        const auto payload = at + 8;
        require(length <= bytes.size() - payload, "Truncated WAV chunk");
        if (tag(bytes, at, "fmt ")) {
            require(!hasFormat && length >= 16, "Invalid or duplicate WAV format chunk");
            auto format = u16(bytes, payload);
            info.channels = u16(bytes, payload + 2);
            info.rate = u32(bytes, payload + 4);
            alignment = u16(bytes, payload + 12);
            info.bits = u16(bytes, payload + 14);
            require(info.channels == 1 || info.channels == 2, "Import supports one or two audio channels");
            require(info.rate >= 1000 && info.rate <= 384000, "Audio sample rate must be 1000..384000 Hz");
            if (format == 0xfffe) {
                require(length >= 40 && u16(bytes, payload + 16) >= 22 &&
                    std::size_t(u16(bytes, payload + 16)) <= length - 18, "Invalid extensible WAV header");
                const auto validBits = u16(bytes, payload + 18);
                require(validBits == 0 || validBits <= info.bits, "Invalid WAV valid-bit count");
                // KSDATAFORMAT_SUBTYPE_PCM / IEEE_FLOAT share this GUID tail.
                const std::array<std::uint8_t, 14> guidTail{{0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
                require(std::equal(guidTail.begin(), guidTail.end(), bytes.begin() + std::ptrdiff_t(payload + 26)),
                    "Unsupported extensible WAV subtype");
                format = u16(bytes, payload + 24);
            } else if (length > 16) {
                require(length >= 18 && std::size_t(u16(bytes, payload + 16)) <= length - 18,
                    "Invalid WAV format extension length");
            }
            info.floating = format == 3;
            require(format == 1 || format == 3, "Unsupported WAV encoding; use PCM or IEEE float");
            require(info.floating ? (info.bits == 32 || info.bits == 64) :
                (info.bits == 8 || info.bits == 16 || info.bits == 24 || info.bits == 32), "Unsupported WAV bit depth");
            require(alignment == info.channels * (info.bits / 8) &&
                u32(bytes, payload + 8) == std::uint64_t(info.rate) * alignment, "Inconsistent WAV alignment or byte rate");
            hasFormat = true;
        } else if (tag(bytes, at, "data")) {
            require(!hasData, "Multiple WAV data chunks are not supported");
            dataBytes = length; hasData = true;
        }
        require((length & 1u) == 0 || payload + length < bytes.size(), "Missing WAV chunk padding");
        at = payload + length + (length & 1u);
    }
    require(hasFormat && hasData, "WAV is missing format or sample data");
    require(alignment != 0 && dataBytes % alignment == 0, "WAV ends in a partial sample frame");
    info.frames = dataBytes / alignment;
    require(info.frames > 0 && info.frames <= maxFrames, "Audio must contain 1..32M frames");
    return info;
}

struct Decoder {
    ma_decoder value{};
    bool ready = false;
    ~Decoder() { if (ready) ma_decoder_uninit(&value); }
};
} // namespace

bool isAudioExtension(std::string_view extension) {
    std::string value(extension);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value == ".wav" || value == ".wave" || value == ".flac" || value == ".mp3" || value == ".ogg" || value == ".oga";
}

DecodedAudio decodeAudio(const std::vector<std::uint8_t>& bytes) {
    require(!bytes.empty() && bytes.size() <= maxFileBytes, "Audio file must be 1 byte..128 MiB");
    DecodedAudio result;
    WaveInfo wave;
    ma_encoding_format encoding = ma_encoding_format_unknown;
    if (tag(bytes, 0, "RIFF")) {
        wave = validateWave(bytes); result.sourceBits = wave.bits;
        result.format = wave.floating ? "WAV FLOAT" : "WAV PCM";
        encoding = ma_encoding_format_wav;
    } else if (tag(bytes, 0, "fLaC")) {
        result.format = "FLAC"; encoding = ma_encoding_format_flac;
    } else if (tag(bytes, 0, "OggS")) {
        result.format = "OGG VORBIS"; encoding = ma_encoding_format_vorbis;
    } else if ((bytes.size() >= 3 && std::memcmp(bytes.data(), "ID3", 3) == 0) ||
               (bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0)) {
        result.format = "MP3"; encoding = ma_encoding_format_mp3;
    } else throw std::runtime_error("Unsupported audio file; use WAV, FLAC, MP3 or Ogg Vorbis");

    Decoder decoder;
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.encodingFormat = encoding;
    const auto initialized = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder.value);
    require(initialized == MA_SUCCESS, "Invalid or unsupported audio stream");
    decoder.ready = true;
    const auto channels = decoder.value.outputChannels;
    require(channels == 1 || channels == 2, "Import supports one or two audio channels");
    require(decoder.value.outputSampleRate >= 1000 && decoder.value.outputSampleRate <= 384000,
        "Audio sample rate must be 1000..384000 Hz");
    result.channels = std::uint16_t(channels);
    result.sampleRate = decoder.value.outputSampleRate;
    ma_uint64 declaredFrames = 0;
    const bool hasLength = ma_decoder_get_length_in_pcm_frames(&decoder.value, &declaredFrames) == MA_SUCCESS && declaredFrames > 0;
    require(!hasLength || declaredFrames <= maxFrames, "Decoded audio exceeds 32M frames");
    if (wave.frames) require(result.channels == wave.channels && result.sampleRate == wave.rate &&
        (!hasLength || declaredFrames == wave.frames), "WAV decoder metadata does not match the file");
    if (hasLength) result.pcm.reserve(std::size_t(declaredFrames) * channels);
    std::array<float, 4096 * 2> block{};
    for (;;) {
        ma_uint64 read = 0;
        const auto status = ma_decoder_read_pcm_frames(&decoder.value, block.data(), 4096, &read);
        require(status == MA_SUCCESS || status == MA_AT_END, "Audio decoding failed");
        require(read <= 4096 && read <= maxFrames - result.frames(), "Decoded audio exceeds 32M frames");
        const auto samples = std::size_t(read) * channels;
        require(std::all_of(block.begin(), block.begin() + std::ptrdiff_t(samples), [](float v) { return std::isfinite(v); }),
            "Audio contains NaN or infinite sample values");
        result.pcm.insert(result.pcm.end(), block.begin(), block.begin() + std::ptrdiff_t(samples));
        if (status == MA_AT_END || read == 0) break;
    }
    require(result.frames() > 0, "Audio contains no decodable sample frames");
    require(!hasLength || result.frames() == declaredFrames, "Truncated audio stream");
    if (wave.frames) require(result.frames() == wave.frames, "Truncated WAV sample data");
    return result;
}

DecodedAudio loadAudio(const std::filesystem::path& path) { return decodeAudio(readFile(path)); }

Sample convertAudio(const DecodedAudio& audio, StereoMode mode, int outputBits) {
    require(audio.channels == 1 || audio.channels == 2, "Import supports one or two audio channels");
    require(audio.sampleRate >= 1000 && audio.sampleRate <= 384000, "Audio sample rate must be 1000..384000 Hz");
    require(outputBits == 8 || outputBits == 16, "XM sample depth must be 8 or 16 bits");
    require(mode == StereoMode::Blend || mode == StereoMode::Left || mode == StereoMode::Right, "Invalid channel mix");
    require(!audio.pcm.empty() && audio.pcm.size() % audio.channels == 0 && audio.frames() <= maxFrames,
        "Invalid decoded audio frame count");
    Sample sample;
    sample.name = "Imported audio";
    sample.sixteenBit = outputBits == 16;
    sample.pcm.reserve(audio.frames());
    const double scale = sample.sixteenBit ? 32768.0 : 128.0;
    const long maximum = sample.sixteenBit ? 32767 : 127;
    for (std::size_t frame = 0; frame < audio.frames(); ++frame) {
        const auto at = frame * audio.channels;
        const double left = audio.pcm[at];
        const double right = audio.channels == 2 ? audio.pcm[at + 1] : left;
        require(std::isfinite(left) && std::isfinite(right), "Audio contains NaN or infinite sample values");
        const double value = audio.channels == 1 || mode == StereoMode::Left ? left :
            mode == StereoMode::Right ? right : left * 0.5 + right * 0.5;
        const long quantized = std::clamp(std::lround(std::clamp(value, -1.0, 1.0) * scale), -maximum - 1, maximum);
        sample.pcm.push_back(std::int16_t(quantized * (sample.sixteenBit ? 1 : 256)));
    }
    const int tuning = int(std::lround(1536.0 * std::log2(double(audio.sampleRate) / 8363.0)));
    const int note = int(std::floor(double(tuning + 64) / 128.0));
    require(note >= -128 && note <= 127, "Audio sample rate cannot be represented by XM tuning");
    sample.relativeNote = std::int8_t(note);
    sample.finetune = std::int8_t(tuning - note * 128);
    return sample;
}

} // namespace brick::io
