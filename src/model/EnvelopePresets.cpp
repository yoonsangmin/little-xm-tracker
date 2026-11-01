#include "model/EnvelopePresets.hpp"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace brick::edit {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<std::uint8_t, 8> magic{{'B','R','X','E','N','V',0x1a,0}};
constexpr std::size_t headerBytes = 16, recordBytes = 60;
constexpr std::size_t payloadBytes = EnvelopePresetCount * 2 * recordBytes;
constexpr std::size_t fileBytes = headerBytes + payloadBytes + 4;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void validateEnvelope(const Envelope& envelope) {
    require(envelope.count <= envelope.points.size(), "Envelope preset exceeds 12 points");
    require((envelope.flags & ~7) == 0, "Envelope preset has unsupported flags");
    if (envelope.flags & 1) require(envelope.count > 0, "Enabled envelope preset has no points");
    if (envelope.flags & 2) require(envelope.sustain < envelope.count, "Envelope preset sustain point is invalid");
    if (envelope.flags & 4)
        require(envelope.loopStart <= envelope.loopEnd && envelope.loopEnd < envelope.count,
                "Envelope preset loop points are invalid");
    for (std::size_t n = 0; n < envelope.count; ++n) {
        require(envelope.points[n].y <= 64, "Envelope preset value exceeds 64");
        if (n) require(envelope.points[n - 1].x <= envelope.points[n].x, "Envelope preset ticks are not ordered");
    }
}

void validatePreset(const EnvelopePreset& preset, bool envelopeOnly = false) {
    validateEnvelope(preset.envelope);
    if (envelopeOnly) return;
    require(preset.vibratoType <= 3, "Envelope preset vibrato waveform is invalid");
    require(preset.vibratoDepth <= 15, "Envelope preset vibrato depth exceeds 15");
    require(preset.vibratoRate <= 63, "Envelope preset vibrato rate exceeds 63");
}

Envelope shape(std::initializer_list<EnvPoint> points, std::uint8_t flags,
               std::uint8_t sustain = 0, std::uint8_t loopStart = 0, std::uint8_t loopEnd = 0) {
    Envelope result;
    std::copy(points.begin(), points.end(), result.points.begin());
    result.count = std::uint8_t(points.size());result.flags = flags;
    result.sustain = sustain;result.loopStart = loopStart;result.loopEnd = loopEnd;
    return result;
}

void append(Bytes& bytes, std::uint32_t value, unsigned count) {
    for (unsigned n = 0; n < count; ++n) bytes.push_back(std::uint8_t(value >> (n * 8)));
}

std::uint32_t integer(const Bytes& bytes, std::size_t offset, unsigned count) {
    require(offset <= bytes.size() && count <= bytes.size() - offset, "Truncated envelope preset data");
    std::uint32_t result = 0;
    for (unsigned n = 0; n < count; ++n) result |= std::uint32_t(bytes[offset + n]) << (n * 8);
    return result;
}

std::uint32_t checksum(const Bytes& bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t n = 0; n < count; ++n) {
        crc ^= bytes[n];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

void appendPreset(Bytes& bytes, const EnvelopePreset& preset) {
    validatePreset(preset);
    const auto& envelope = preset.envelope;
    bytes.push_back(preset.custom ? 1 : 0);
    bytes.push_back(envelope.count);bytes.push_back(envelope.sustain);
    bytes.push_back(envelope.loopStart);bytes.push_back(envelope.loopEnd);bytes.push_back(envelope.flags);
    append(bytes, preset.fadeout, 2);
    bytes.push_back(preset.vibratoType);bytes.push_back(preset.vibratoSweep);
    bytes.push_back(preset.vibratoDepth);bytes.push_back(preset.vibratoRate);
    for (const auto& point : envelope.points) {append(bytes, point.x, 2);append(bytes, point.y, 2);}
}

EnvelopePreset readPreset(const Bytes& bytes, std::size_t offset) {
    EnvelopePreset result;
    require(bytes[offset] <= 1, "Envelope preset custom flag is invalid");
    result.custom = bytes[offset] != 0;
    auto& envelope = result.envelope;
    envelope.count = bytes[offset + 1];envelope.sustain = bytes[offset + 2];
    envelope.loopStart = bytes[offset + 3];envelope.loopEnd = bytes[offset + 4];envelope.flags = bytes[offset + 5];
    result.fadeout = std::uint16_t(integer(bytes, offset + 6, 2));
    result.vibratoType = bytes[offset + 8];result.vibratoSweep = bytes[offset + 9];
    result.vibratoDepth = bytes[offset + 10];result.vibratoRate = bytes[offset + 11];
    for (std::size_t n = 0; n < envelope.points.size(); ++n) {
        envelope.points[n].x = std::uint16_t(integer(bytes, offset + 12 + n * 4, 2));
        envelope.points[n].y = std::uint16_t(integer(bytes, offset + 14 + n * 4, 2));
    }
    validatePreset(result);
    return result;
}
} // namespace

EnvelopePresets defaultEnvelopePresets() {
    EnvelopePresets presets;
    auto& volume = presets.volume;
    volume[0].envelope = shape({{0,64},{1,64}}, 3, 1);volume[0].fadeout = 512;
    volume[1].envelope = shape({{0,64},{4,48},{12,20},{24,0}}, 1);volume[1].fadeout = 256;
    volume[2].envelope = shape({{0,0},{2,64},{10,40},{24,0}}, 3, 2);volume[2].fadeout = 256;
    volume[3].envelope = shape({{0,0},{32,64},{64,48},{112,0}}, 3, 2);volume[3].fadeout = 192;
    volume[4].envelope = shape({{0,64},{48,0}}, 1);volume[4].fadeout = 256;
    volume[5].envelope = shape({{0,32},{4,64},{8,32},{20,0}}, 5, 0, 0, 2);volume[5].fadeout = 512;
    auto& panning = presets.panning;
    panning[0].envelope = shape({{0,32},{1,32}}, 1);
    panning[1].envelope = shape({{0,0},{1,0}}, 1);
    panning[2].envelope = shape({{0,64},{1,64}}, 1);
    panning[3].envelope = shape({{0,0},{32,64}}, 1);
    panning[4].envelope = shape({{0,64},{32,0}}, 1);
    panning[5].envelope = shape({{0,32},{8,0},{24,64},{32,32}}, 5, 0, 0, 3);
    return presets;
}

const char* envelopePresetName(bool pan, std::size_t slot) {
    static constexpr std::array<const char*, EnvelopePresetCount> volume{{"FULL", "PLUCK", "ADSR", "SOFT PAD", "FADE OUT", "PULSE LOOP"}};
    static constexpr std::array<const char*, EnvelopePresetCount> panning{{"CENTER", "LEFT", "RIGHT", "SWEEP L-R", "SWEEP R-L", "PAN LOOP"}};
    if (slot >= EnvelopePresetCount) throw std::out_of_range("Envelope preset slot");
    return (pan ? panning : volume)[slot];
}

EnvelopePreset captureEnvelopePreset(const Instrument& instrument, bool pan) {
    EnvelopePreset result;result.custom = true;
    result.envelope = pan ? instrument.panningEnvelope : instrument.volumeEnvelope;
    if (!pan) {
        result.fadeout = instrument.fadeout;
        result.vibratoType = instrument.vibratoType;result.vibratoSweep = instrument.vibratoSweep;
        result.vibratoDepth = instrument.vibratoDepth;result.vibratoRate = instrument.vibratoRate;
    }
    validatePreset(result);
    return result;
}

void applyEnvelopePreset(Instrument& instrument, bool pan, const EnvelopePreset& preset) {
    validatePreset(preset, pan);
    if (pan) instrument.panningEnvelope = preset.envelope;
    else {
        instrument.volumeEnvelope = preset.envelope;instrument.fadeout = preset.fadeout;
        instrument.vibratoType = preset.vibratoType;instrument.vibratoSweep = preset.vibratoSweep;
        instrument.vibratoDepth = preset.vibratoDepth;instrument.vibratoRate = preset.vibratoRate;
    }
}

Bytes encodeEnvelopePresets(const EnvelopePresets& presets) {
    Bytes bytes;bytes.reserve(fileBytes);
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    append(bytes, 1, 2);bytes.push_back(std::uint8_t(EnvelopePresetCount));bytes.push_back(0);
    append(bytes, std::uint32_t(payloadBytes), 4);
    for (const auto* bank : {&presets.volume, &presets.panning})
        for (const auto& preset : *bank) appendPreset(bytes, preset);
    append(bytes, checksum(bytes, bytes.size()), 4);
    return bytes;
}

EnvelopePresets decodeEnvelopePresets(const Bytes& bytes) {
    require(bytes.size() == fileBytes, "Envelope preset file has an invalid length");
    require(std::equal(magic.begin(), magic.end(), bytes.begin()), "Unknown envelope preset file signature");
    require(integer(bytes, 8, 2) == 1, "Unsupported envelope preset file version");
    require(bytes[10] == EnvelopePresetCount && bytes[11] == 0 && integer(bytes, 12, 4) == payloadBytes,
            "Envelope preset file header is invalid");
    require(integer(bytes, bytes.size() - 4, 4) == checksum(bytes, bytes.size() - 4), "Envelope preset checksum mismatch");
    EnvelopePresets result;
    std::size_t offset = headerBytes;
    for (auto* bank : {&result.volume, &result.panning})
        for (auto& preset : *bank) {preset = readPreset(bytes, offset);offset += recordBytes;}
    return result;
}

} // namespace brick::edit
