#include "model/EnvelopePresets.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class Fn> void reject(Fn fn, const char* message) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
bool sameEnvelope(const brick::Envelope& a, const brick::Envelope& b) {
    if (a.count != b.count || a.sustain != b.sustain || a.loopStart != b.loopStart || a.loopEnd != b.loopEnd || a.flags != b.flags) return false;
    for (std::size_t n = 0; n < a.points.size(); ++n)
        if (a.points[n].x != b.points[n].x || a.points[n].y != b.points[n].y) return false;
    return true;
}
bool samePreset(const brick::edit::EnvelopePreset& a, const brick::edit::EnvelopePreset& b) {
    return sameEnvelope(a.envelope, b.envelope) && a.fadeout == b.fadeout && a.vibratoType == b.vibratoType &&
        a.vibratoSweep == b.vibratoSweep && a.vibratoDepth == b.vibratoDepth && a.vibratoRate == b.vibratoRate && a.custom == b.custom;
}
bool sameBank(const brick::edit::EnvelopePresets& a, const brick::edit::EnvelopePresets& b) {
    for (std::size_t n = 0; n < brick::edit::EnvelopePresetCount; ++n)
        if (!samePreset(a.volume[n], b.volume[n]) || !samePreset(a.panning[n], b.panning[n])) return false;
    return true;
}
brick::Instrument instrumentFixture() {
    brick::Instrument result; result.name = "KEEP INSTRUMENT";
    brick::Sample sample; sample.name = "KEEP PCM"; sample.pcm = {0,256,-512,1024};
    sample.volume = 43; sample.panning = 201; sample.relativeNote = -7; sample.finetune = 35;
    sample.loopType = 1; sample.loopStart = 1; sample.loopLength = 3;
    result.samples.push_back(sample);
    sample.name = "KEEP 8 BIT"; sample.sixteenBit = false; sample.relativeNote = 7;
    result.samples.push_back(sample);
    for (std::size_t n = 0; n < result.noteToSample.size(); ++n) result.noteToSample[n] = std::uint8_t(n % 2);
    auto& volume = result.volumeEnvelope;
    volume.count = 3; volume.points[0] = {0,64}; volume.points[1] = {12,48}; volume.points[2] = {30,0};
    volume.flags = 7; volume.sustain = 1; volume.loopEnd = 2;
    auto& pan = result.panningEnvelope;
    pan.count = 2; pan.points[0] = {0,24}; pan.points[1] = {20,40}; pan.flags = 3; pan.sustain = 1;
    result.fadeout = 0x1234; result.vibratoType = 2; result.vibratoSweep = 165; result.vibratoDepth = 10; result.vibratoRate = 50;
    result.extension[0] = 1; result.extension[21] = 42;
    return result;
}

void defaultsAndCapture() {
    using namespace brick;
    const auto defaults = edit::defaultEnvelopePresets();
    check(edit::EnvelopePresetCount == 6 && defaults.volume.size() == 6 && defaults.panning.size() == 6,
          "Default banks contain six volume and six panning presets");
    for (bool pan : {false,true}) {
        for (std::size_t slot = 0; slot < edit::EnvelopePresetCount; ++slot) {
            const auto& preset = pan ? defaults.panning[slot] : defaults.volume[slot];
            const std::string name = edit::envelopePresetName(pan, slot);
            check(!name.empty() && name.size() <= 10 && std::all_of(name.begin(), name.end(), [](unsigned char c){return c >= 32 && c <= 126;}),
                  "Every factory preset has a short printable ASCII name");
            check(!preset.custom && preset.envelope.count > 0 && (preset.envelope.flags & 1), "Every factory preset is an enabled usable shape rather than a custom slot");
            auto instrument = instrumentFixture();
            edit::applyEnvelopePreset(instrument, pan, preset);
            const auto xi = io::encodeXi(instrument);
            check(io::encodeXi(io::decodeXi(xi)) == xi, "Every default preset applies as valid XI instrument data");
        }
    }
    reject([&]{edit::envelopePresetName(false, 6);}, "Preset names reject a slot outside the six-entry bank");
    const auto source = instrumentFixture(); const auto before = io::encodeXi(source);
    const auto volume = edit::captureEnvelopePreset(source, false);
    check(volume.custom && sameEnvelope(volume.envelope, source.volumeEnvelope) && volume.fadeout == source.fadeout &&
          volume.vibratoType == source.vibratoType && volume.vibratoSweep == source.vibratoSweep && volume.vibratoDepth == source.vibratoDepth &&
          volume.vibratoRate == source.vibratoRate, "Volume capture stores the envelope, fadeout and all auto-vibrato values as custom data");
    const auto pan = edit::captureEnvelopePreset(source, true);
    check(pan.custom && sameEnvelope(pan.envelope, source.panningEnvelope) && pan.fadeout == 0 && pan.vibratoType == 0 &&
          pan.vibratoSweep == 0 && pan.vibratoDepth == 0 && pan.vibratoRate == 0, "Panning capture stores only its envelope and clears volume-only payload fields");
    check(io::encodeXi(source) == before, "Capturing presets leaves all source instrument data unchanged");
    auto invalidSource = source; invalidSource.vibratoType = 4; invalidSource.vibratoDepth = 16; invalidSource.vibratoRate = 64;
    check(samePreset(edit::captureEnvelopePreset(invalidSource, true), pan), "Panning capture does not copy or validate unrelated volume auto-vibrato fields");
    reject([&]{edit::captureEnvelopePreset(invalidSource, false);}, "Volume capture rejects invalid auto-vibrato instead of storing it");
}

void applyAndRollback() {
    using namespace brick;
    const auto source = instrumentFixture();
    const auto volume = edit::captureEnvelopePreset(source, false);
    auto target = instrumentFixture(); target.name = "DESTINATION";
    target.volumeEnvelope = edit::defaultEnvelopePresets().volume[1].envelope;
    target.panningEnvelope = edit::defaultEnvelopePresets().panning[0].envelope;
    target.fadeout = 17; target.vibratoType = 1; target.vibratoSweep = 2; target.vibratoDepth = 3; target.vibratoRate = 9;
    auto expected = target;
    expected.volumeEnvelope = volume.envelope; expected.fadeout = volume.fadeout;
    expected.vibratoType = volume.vibratoType; expected.vibratoSweep = volume.vibratoSweep;
    expected.vibratoDepth = volume.vibratoDepth; expected.vibratoRate = volume.vibratoRate;
    edit::applyEnvelopePreset(target, false, volume);
    check(io::encodeXi(target) == io::encodeXi(expected), "Volume apply changes its full payload while preserving samples, key map, panning envelope, name and extensions");
    auto pan = edit::captureEnvelopePreset(source, true);
    // These fields are deliberately invalid as volume settings, but unused
    // when applying a panning envelope to an instrument.
    pan.fadeout = 65535; pan.vibratoType = 255; pan.vibratoSweep = 255; pan.vibratoDepth = 255; pan.vibratoRate = 255;
    expected.panningEnvelope = pan.envelope;
    edit::applyEnvelopePreset(target, true, pan);
    check(io::encodeXi(target) == io::encodeXi(expected), "Panning apply changes only the panning envelope and ignores all volume-only metadata");
    const auto appliedBytes = io::encodeXi(target);
    check(io::encodeXi(io::decodeXi(appliedBytes)) == appliedBytes, "Custom envelope application survives an exact XI round trip");
    const auto rejectApply = [&](edit::EnvelopePreset bad, bool panning, const char* message) {
        reject([&]{edit::applyEnvelopePreset(target, panning, bad);}, message);
        check(io::encodeXi(target) == appliedBytes, "Invalid preset application cannot partially alter an instrument");
    };
    auto bad = volume; bad.envelope.count = 13; rejectApply(bad, false, "Apply rejects more than twelve envelope points");
    bad = volume; bad.envelope.flags = 8; rejectApply(bad, false, "Apply rejects unknown envelope flags");
    bad = volume; bad.envelope.count = 0; bad.envelope.flags = 1; rejectApply(bad, false, "Apply rejects an enabled empty envelope");
    bad = volume; bad.envelope.points[1].y = 65; rejectApply(bad, false, "Apply rejects an active point outside the 0..64 range");
    bad = volume; bad.envelope.points[2].x = 11; rejectApply(bad, false, "Apply rejects decreasing active envelope ticks");
    bad = volume; bad.envelope.sustain = bad.envelope.count; rejectApply(bad, false, "Apply rejects an enabled sustain marker outside the points");
    bad = volume; bad.envelope.loopStart = 2; bad.envelope.loopEnd = 1; rejectApply(bad, false, "Apply rejects reversed active loop markers");
    bad = volume; bad.envelope.loopEnd = bad.envelope.count; rejectApply(bad, true, "Panning apply also validates its active envelope markers");
    bad = volume; bad.vibratoType = 4; rejectApply(bad, false, "Volume apply rejects an unsupported vibrato waveform");
    bad = volume; bad.vibratoDepth = 16; rejectApply(bad, false, "Volume apply rejects auto-vibrato depth above fifteen");
    bad = volume; bad.vibratoRate = 64; rejectApply(bad, false, "Volume apply rejects auto-vibrato rate above sixty-three");
}

std::uint32_t crc32(const Bytes& bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}
void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = std::uint8_t(value); bytes[offset + 1] = std::uint8_t(value >> 8);
}
void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) bytes[offset + n] = std::uint8_t(value >> (n * 8));
}
void refreshChecksum(Bytes& bytes) { put32(bytes, bytes.size() - 4, crc32(bytes, bytes.size() - 4)); }

void persistenceAndValidation() {
    using namespace brick;
    const Bytes crcFixture{'1','2','3','4','5','6','7','8','9'};
    check(crc32(crcFixture, crcFixture.size()) == 0xcbf43926u, "Binary mutation fixtures use standard IEEE CRC32");
    auto bank = edit::defaultEnvelopePresets();
    const auto source = instrumentFixture();
    bank.volume[2] = edit::captureEnvelopePreset(source, false);
    auto& high = bank.volume[2];
    high.fadeout = 65535; high.vibratoType = 3; high.vibratoSweep = 255; high.vibratoDepth = 15; high.vibratoRate = 63;
    high.envelope.points[2].x = 65535; high.envelope.points[11] = {65535,65000};
    bank.panning[4] = edit::captureEnvelopePreset(source, true);
    auto& inactive = bank.panning[1]; inactive.custom = true;
    inactive.envelope.flags = 1; inactive.envelope.sustain = 255; inactive.envelope.loopStart = 254; inactive.envelope.loopEnd = 253;
    inactive.envelope.points[10] = {54321,65535};
    inactive.fadeout = 0xcafe; inactive.vibratoType = 1; inactive.vibratoSweep = 87; inactive.vibratoDepth = 9; inactive.vibratoRate = 31;
    const auto bytes = edit::encodeEnvelopePresets(bank);
    const std::array<std::uint8_t,16> header{{'B','R','X','E','N','V',0x1a,0,1,0,6,0,0xd0,2,0,0}};
    check(bytes.size() == 740 && std::equal(header.begin(), header.end(), bytes.begin()), "Preset files have the fixed version-one little-endian header and 720-byte payload");
    const auto loaded = edit::decodeEnvelopePresets(bytes);
    check(sameBank(loaded, bank) && edit::encodeEnvelopePresets(loaded) == bytes, "All twelve preset payloads and custom flags survive an exact bank round trip");
    check(loaded.volume[2].envelope.points[2].x == 65535 && loaded.volume[2].envelope.points[11].y == 65000 &&
          loaded.panning[1].envelope.points[10].y == 65535 && loaded.panning[1].envelope.sustain == 255 &&
          loaded.panning[1].envelope.loopStart == 254 && loaded.panning[1].envelope.loopEnd == 253,
          "High active ticks, raw inactive points and disabled markers remain lossless");
    const auto highBytes = [&]{auto instrument = instrumentFixture(); edit::applyEnvelopePreset(instrument, false, high); return io::encodeXi(instrument);}();
    check(io::encodeXi(io::decodeXi(highBytes)) == highBytes, "Maximum valid ticks and vibrato values also survive preset application and XI export");
    constexpr std::size_t first = 16, last = 16 + 11 * 60;
    auto corrupt = bytes; corrupt[first + 14] = 63;
    reject([&]{edit::decodeEnvelopePresets(corrupt);}, "A payload change without a new checksum is rejected");
    corrupt = bytes; corrupt.back() ^= 1;
    reject([&]{edit::decodeEnvelopePresets(corrupt);}, "A damaged stored checksum is rejected");
    corrupt = bytes; corrupt.pop_back();
    reject([&]{edit::decodeEnvelopePresets(corrupt);}, "Truncated preset files are rejected");
    corrupt = bytes; corrupt.push_back(0);
    reject([&]{edit::decodeEnvelopePresets(corrupt);}, "Trailing bytes cannot be silently accepted as another format");
    reject([&]{edit::decodeEnvelopePresets({});}, "An empty preset file is rejected");
    const auto rejectBytes = [&](Bytes bad, const char* message) {
        refreshChecksum(bad);
        reject([&]{edit::decodeEnvelopePresets(bad);}, message);
    };
    corrupt = bytes; corrupt[0] = 'X'; rejectBytes(corrupt, "Unknown preset magic is rejected even with a valid checksum");
    corrupt = bytes; put16(corrupt, 8, 2); rejectBytes(corrupt, "Unsupported file versions are rejected even with a valid checksum");
    corrupt = bytes; corrupt[10] = 5; rejectBytes(corrupt, "A changed bank slot count is rejected");
    corrupt = bytes; corrupt[11] = 1; rejectBytes(corrupt, "Reserved header bits must stay zero");
    corrupt = bytes; put32(corrupt, 12, 719); rejectBytes(corrupt, "The payload length field must match the fixed layout");
    corrupt = bytes; corrupt[first] = 2; rejectBytes(corrupt, "Custom flags other than zero and one are rejected");
    corrupt = bytes; corrupt[first + 1] = 13; rejectBytes(corrupt, "Decoded envelope counts cannot exceed twelve");
    corrupt = bytes; corrupt[first + 5] = 8; rejectBytes(corrupt, "Decoded envelope flags cannot contain unknown bits");
    corrupt = bytes; corrupt[first + 1] = 0; corrupt[first + 5] = 1; rejectBytes(corrupt, "Decoded enabled envelopes must have points");
    corrupt = bytes; put16(corrupt, first + 14, 65); rejectBytes(corrupt, "Decoded active point values cannot exceed sixty-four");
    corrupt = bytes; put16(corrupt, first + 12, 10); put16(corrupt, first + 16, 9);
    rejectBytes(corrupt, "Decoded active point ticks must be nondecreasing");
    corrupt = bytes; corrupt[first + 2] = corrupt[first + 1]; rejectBytes(corrupt, "Decoded enabled sustain markers must refer to an existing point");
    corrupt = bytes; corrupt[first + 5] |= 4; corrupt[first + 3] = 1; corrupt[first + 4] = 0;
    rejectBytes(corrupt, "Decoded enabled loop markers cannot be reversed");
    corrupt = bytes; corrupt[first + 5] |= 4; corrupt[first + 4] = corrupt[first + 1];
    rejectBytes(corrupt, "Decoded enabled loop endpoints must refer to existing points");
    corrupt = bytes; corrupt[first + 8] = 4; rejectBytes(corrupt, "Decoded vibrato waveform must be zero through three");
    corrupt = bytes; corrupt[first + 10] = 16; rejectBytes(corrupt, "Decoded vibrato depth cannot exceed fifteen");
    corrupt = bytes; corrupt[first + 11] = 64; rejectBytes(corrupt, "Decoded vibrato rate cannot exceed sixty-three");
    corrupt = bytes; corrupt[last + 8] = 4; rejectBytes(corrupt, "Stored panning records validate their volume-only payload fields too");

    auto invalidBank = bank; invalidBank.volume[5].envelope.count = 13;
    reject([&]{edit::encodeEnvelopePresets(invalidBank);}, "Encoding validates every volume preset before publishing bytes");
    invalidBank = bank; invalidBank.panning[5].vibratoRate = 64;
    reject([&]{edit::encodeEnvelopePresets(invalidBank);}, "Encoding also validates unused volume metadata in panning records");
    check(edit::encodeEnvelopePresets(bank) == bytes, "Rejected encoding does not modify the original bank");
    corrupt = bytes; corrupt[last] = 2; refreshChecksum(corrupt);
    auto retained = bank;
    for (int attempt = 0; attempt < 3; ++attempt) {
        reject([&]{retained = edit::decodeEnvelopePresets(corrupt);}, "An invalid final record rejects the entire decoded bank");
        check(sameBank(retained, bank) && edit::encodeEnvelopePresets(retained) == bytes, "Failed decoding exposes no partially replaced preset slots");
        check(sameBank(edit::decodeEnvelopePresets(bytes), bank), "Valid decoding remains reliable after repeated malformed files");
    }
}
}

int main() {
    try {
        defaultsAndCapture(); applyAndRollback(); persistenceAndValidation();
        std::cout << "Envelope presets passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
