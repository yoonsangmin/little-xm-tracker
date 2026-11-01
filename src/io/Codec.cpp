#include "io/Codec.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

namespace brick::io {
namespace {
constexpr std::size_t maxFileBytes = 128u * 1024u * 1024u;
constexpr std::size_t maxPcmFrames = 32u * 1024u * 1024u;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<std::uint8_t>& data) : Reader(data.data(), data.size()) {}
    std::size_t left() const { return size_ - position_; }
    std::uint8_t u8() { need(1); return data_[position_++]; }
    std::uint16_t u16() { const auto a = u8(); return a | (std::uint16_t(u8()) << 8); }
    std::uint32_t u32() { const auto a = u16(); return a | (std::uint32_t(u16()) << 16); }
    Reader take(std::size_t size) {
        need(size);
        Reader result(data_ + position_, size);
        position_ += size;
        return result;
    }
    std::string text(std::size_t size) {
        need(size);
        std::string result(reinterpret_cast<const char*>(data_ + position_), size);
        position_ += size;
        return result;
    }
    std::string name(std::size_t size) {
        auto result = text(size);
        while (!result.empty() && (result.back() == '\0' || result.back() == ' ')) result.pop_back();
        return result;
    }
    void skip(std::size_t size) { need(size); position_ += size; }
    void zeroPadding(const char* label) {
        while (left()) require(u8() == 0, std::string(label) + ": unknown extension data is unsupported");
    }
private:
    void need(std::size_t size) const { require(size <= left(), "Truncated file or invalid declared length"); }
    const std::uint8_t* data_;
    std::size_t size_, position_ = 0;
};

class Writer {
public:
    std::vector<std::uint8_t> data;
    void u8(std::uint8_t value) { data.push_back(value); }
    void u16(std::uint16_t value) { u8(static_cast<std::uint8_t>(value)); u8(static_cast<std::uint8_t>(value >> 8)); }
    void u32(std::uint32_t value) { u16(static_cast<std::uint16_t>(value)); u16(static_cast<std::uint16_t>(value >> 16)); }
    void raw(const char* value, std::size_t size) { data.insert(data.end(), value, value + size); }
    void raw(const std::vector<std::uint8_t>& value) { data.insert(data.end(), value.begin(), value.end()); }
    void name(const std::string& value, std::size_t size) {
        require(value.size() <= size, "Name exceeds XM/XI byte limit (" + std::to_string(size) + ")");
        raw(value.data(), value.size());
        data.insert(data.end(), size - value.size(), 0);
    }
};

std::int8_t signed8(std::uint8_t value) {
    return static_cast<std::int8_t>(value < 128 ? int(value) : int(value) - 256);
}
std::int16_t signed16(std::uint16_t value) {
    return static_cast<std::int16_t>(value < 32768 ? int(value) : int(value) - 65536);
}

void validateEnvelope(const Envelope& envelope) {
    require(envelope.count <= 12, "XM envelope exceeds 12 points");
    require((envelope.flags & ~7) == 0, "Unsupported XM envelope flags");
    if (envelope.flags & 1) require(envelope.count > 0, "Enabled envelope has no points");
    if (envelope.flags & 2) require(envelope.sustain < envelope.count, "Invalid envelope sustain point");
    if (envelope.flags & 4) {
        require(envelope.loopStart <= envelope.loopEnd && envelope.loopEnd < envelope.count,
                "Invalid envelope loop points");
    }
    for (std::size_t i = 0; i < envelope.count; ++i) {
        require(envelope.points[i].y <= 64, "Envelope value exceeds 64");
        if (i) require(envelope.points[i].x >= envelope.points[i - 1].x, "Envelope ticks are not ordered");
    }
}

void validateSample(const Sample& sample) {
    require(sample.pcm.size() <= maxPcmFrames, "Sample exceeds the 32M-frame memory limit");
    require(sample.volume <= 64, "Sample volume exceeds 64");
    require(sample.loopType <= 3, "Invalid sample loop type");
    if (sample.loopType) {
        require(sample.loopLength > 0 && sample.loopStart < sample.pcm.size() &&
                std::uint64_t(sample.loopStart) + sample.loopLength <= sample.pcm.size(),
                "Sample loop is outside PCM data");
    }
    if (!sample.sixteenBit) {
        require(sample.reserved != 0xAD, "ModPlug ADPCM sample extension is unsupported");
        for (const auto value : sample.pcm)
            require(value % 256 == 0, "Eight-bit sample contains higher precision PCM; convert to 16-bit explicitly");
    }
}

void validateInstrument(const Instrument& instrument) {
    require(instrument.samples.size() <= 16, "XM/XI instruments with more than 16 samples are unsupported");
    validateEnvelope(instrument.volumeEnvelope);
    validateEnvelope(instrument.panningEnvelope);
    require(instrument.vibratoType <= 3, "Unsupported instrument vibrato waveform");
    for (const auto index : instrument.noteToSample)
        require(index < std::max<std::size_t>(1, instrument.samples.size()), "Instrument key map references missing sample");
    for (const auto& sample : instrument.samples) validateSample(sample);
}

void readInstrumentBody(Reader& reader, Instrument& instrument, std::size_t extensionBytes = 22) {
    for (auto& value : instrument.noteToSample) value = reader.u8();
    for (auto* envelope : {&instrument.volumeEnvelope, &instrument.panningEnvelope})
        for (auto& point : envelope->points) { point.x = reader.u16(); point.y = reader.u16(); }
    instrument.volumeEnvelope.count = reader.u8();
    instrument.panningEnvelope.count = reader.u8();
    instrument.volumeEnvelope.sustain = reader.u8();
    instrument.volumeEnvelope.loopStart = reader.u8();
    instrument.volumeEnvelope.loopEnd = reader.u8();
    instrument.panningEnvelope.sustain = reader.u8();
    instrument.panningEnvelope.loopStart = reader.u8();
    instrument.panningEnvelope.loopEnd = reader.u8();
    instrument.volumeEnvelope.flags = reader.u8();
    instrument.panningEnvelope.flags = reader.u8();
    instrument.vibratoType = reader.u8();
    instrument.vibratoSweep = reader.u8();
    instrument.vibratoDepth = reader.u8();
    instrument.vibratoRate = reader.u8();
    instrument.fadeout = reader.u16();
    require(extensionBytes <= instrument.extension.size(), "Invalid instrument extension size");
    for (std::size_t i = 0; i < extensionBytes; ++i) instrument.extension[i] = reader.u8();
}

void writeInstrumentBody(Writer& writer, const Instrument& instrument) {
    for (auto value : instrument.noteToSample) writer.u8(value);
    for (const auto* envelope : {&instrument.volumeEnvelope, &instrument.panningEnvelope})
        for (const auto& point : envelope->points) { writer.u16(point.x); writer.u16(point.y); }
    writer.u8(instrument.volumeEnvelope.count);
    writer.u8(instrument.panningEnvelope.count);
    writer.u8(instrument.volumeEnvelope.sustain);
    writer.u8(instrument.volumeEnvelope.loopStart);
    writer.u8(instrument.volumeEnvelope.loopEnd);
    writer.u8(instrument.panningEnvelope.sustain);
    writer.u8(instrument.panningEnvelope.loopStart);
    writer.u8(instrument.panningEnvelope.loopEnd);
    writer.u8(instrument.volumeEnvelope.flags);
    writer.u8(instrument.panningEnvelope.flags);
    writer.u8(instrument.vibratoType);
    writer.u8(instrument.vibratoSweep);
    writer.u8(instrument.vibratoDepth);
    writer.u8(instrument.vibratoRate);
    writer.u16(instrument.fadeout);
    for (auto value : instrument.extension) writer.u8(value);
}

struct SampleHeader { Sample sample; std::uint32_t byteLength = 0; };
SampleHeader readSampleHeader(Reader& reader) {
    SampleHeader header;
    header.byteLength = reader.u32();
    const auto loopStartBytes = reader.u32(), loopLengthBytes = reader.u32();
    auto& sample = header.sample;
    sample.volume = reader.u8();
    sample.finetune = signed8(reader.u8());
    const auto flags = reader.u8();
    require((flags & ~0x13) == 0, "Stereo/extended XM samples are unsupported; import stereo WAV with explicit downmix");
    sample.sixteenBit = (flags & 16) != 0;
    sample.loopType = flags & 3;
    sample.panning = reader.u8();
    sample.relativeNote = signed8(reader.u8());
    sample.reserved = reader.u8();
    require(sample.sixteenBit || sample.reserved != 0xAD, "ModPlug ADPCM samples are unsupported");
    sample.name = reader.name(22);
    const std::uint32_t width = sample.sixteenBit ? 2 : 1;
    require(header.byteLength % width == 0 && loopStartBytes % width == 0 && loopLengthBytes % width == 0,
            "Sixteen-bit sample lengths and loop positions must be even byte counts");
    sample.loopStart = loopStartBytes / width;
    sample.loopLength = loopLengthBytes / width;
    require(header.byteLength / width <= maxPcmFrames, "Sample exceeds the 32M-frame memory limit");
    return header;
}

std::vector<SampleHeader> readSampleHeaders(Reader& reader, std::size_t count, std::size_t headerSize) {
    require(count <= 16, "XM/XI instruments with more than 16 samples are unsupported");
    require(headerSize >= 40, "Invalid sample header size");
    std::vector<SampleHeader> headers;
    headers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto headerReader = reader.take(headerSize);
        headers.push_back(readSampleHeader(headerReader));
        headerReader.zeroPadding("Sample header");
    }
    return headers;
}

void readSampleData(Reader& reader, Instrument& instrument, std::vector<SampleHeader>& headers,
                    std::size_t& totalFrames) {
    for (auto& header : headers) {
        auto packed = reader.take(header.byteLength);
        auto& sample = header.sample;
        const auto frames = header.byteLength / (sample.sixteenBit ? 2u : 1u);
        require(frames <= maxPcmFrames - totalFrames, "Combined samples exceed the 32M-frame memory limit");
        totalFrames += frames;
        sample.pcm.resize(frames);
        std::uint16_t accumulator = 0;
        for (auto& value : sample.pcm) {
            if (sample.sixteenBit) {
                accumulator = static_cast<std::uint16_t>(accumulator + packed.u16());
                value = signed16(accumulator);
            } else {
                accumulator = static_cast<std::uint8_t>(accumulator + packed.u8());
                value = static_cast<std::int16_t>(int(signed8(static_cast<std::uint8_t>(accumulator))) * 256);
            }
        }
        validateSample(sample);
        instrument.samples.push_back(std::move(sample));
    }
}

void readSamples(Reader& reader, Instrument& instrument, std::size_t count,
                 std::size_t headerSize, std::size_t& totalFrames) {
    auto headers = readSampleHeaders(reader, count, headerSize);
    readSampleData(reader, instrument, headers, totalFrames);
}

struct PendingInstrument { Instrument instrument; std::vector<SampleHeader> samples; };
PendingInstrument readXmInstrumentHeader(Reader& reader) {
    const auto size = reader.u32();
    require(size >= 29, "XM instrument header is too small");
    auto header = reader.take(size - 4);
    PendingInstrument result;
    result.instrument.name = header.name(22);
    require(header.u8() == 0, "Unsupported XM instrument type");
    const auto sampleCount = header.u16();
    require(sampleCount <= 16, "XM instruments with more than 16 samples are unsupported");
    if (sampleCount) {
        require(size >= 241, "XM instrument header lacks envelope/sample metadata");
        const auto sampleHeaderSize = header.u32();
        readInstrumentBody(header, result.instrument, std::min<std::size_t>(size - 241, 22));
        header.zeroPadding("Instrument header");
        result.samples = readSampleHeaders(reader, sampleCount, sampleHeaderSize);
    } else {
        // FT2 writes a 33-byte empty header, including sampleHeaderSize=40.
        if (header.left() >= 4) {
            const auto sampleHeaderSize = header.u32();
            require(sampleHeaderSize == 0 || sampleHeaderSize == 40, "Unsupported empty instrument header");
        }
        // Some MilkyTracker XMs keep a full standard header after the last
        // sample is removed. Its old key map/envelopes are inactive without
        // samples; LoaderXM likewise ignores them. Only skip the known body,
        // leaving unknown nonzero extensions subject to the usual rejection.
        if (size >= 241) header.skip(std::min<std::size_t>(size - 33, 230));
        header.zeroPadding("Empty instrument header");
    }
    return result;
}

void writeSampleHeaders(Writer& writer, const Instrument& instrument) {
    for (const auto& sample : instrument.samples) {
        const std::uint32_t width = sample.sixteenBit ? 2 : 1;
        require(std::uint64_t(sample.loopStart) * width <= std::numeric_limits<std::uint32_t>::max() &&
                std::uint64_t(sample.loopLength) * width <= std::numeric_limits<std::uint32_t>::max(),
                "Sample loop byte count overflows XM field");
        writer.u32(static_cast<std::uint32_t>(sample.pcm.size() * width));
        writer.u32(sample.loopStart * width);
        writer.u32(sample.loopLength * width);
        writer.u8(sample.volume);
        writer.u8(static_cast<std::uint8_t>(sample.finetune));
        writer.u8(static_cast<std::uint8_t>(sample.loopType | (sample.sixteenBit ? 16 : 0)));
        writer.u8(sample.panning);
        writer.u8(static_cast<std::uint8_t>(sample.relativeNote));
        writer.u8(sample.reserved);
        writer.name(sample.name, 22);
    }
}

void writeSampleData(Writer& writer, const Instrument& instrument) {
    for (const auto& sample : instrument.samples) {
        std::uint16_t previous = 0;
        for (const auto value : sample.pcm) {
            const auto current = static_cast<std::uint16_t>(sample.sixteenBit ? value : value / 256);
            const auto difference = static_cast<std::uint16_t>(current - previous);
            if (sample.sixteenBit) writer.u16(difference);
            else writer.u8(static_cast<std::uint8_t>(difference));
            previous = current;
        }
    }
}

void checkFileSize(std::size_t size) { require(size <= maxFileBytes, "File exceeds the 128 MiB input limit"); }

} // namespace

Document decodeXm(const std::vector<std::uint8_t>& bytes) {
    checkFileSize(bytes.size());
    Reader reader(bytes);
    require(reader.text(17) == "Extended Module: ", "Not an XM module");
    Document document;
    document.name = reader.name(20);
    require(reader.u8() == 0x1A, "Invalid XM marker");
    reader.skip(20); // Creator name is not song data.
    const auto version = reader.u16();
    require(version >= 0x0102 && version <= 0x0104, "Only XM 1.02, 1.03 and 1.04 are supported");
    const auto headerSize = reader.u32();
    require(headerSize >= 276, "XM header is too small");
    auto header = reader.take(headerSize - 4);
    const auto orderCount = header.u16();
    document.restart = header.u16();
    document.channels = header.u16();
    const auto patternCount = header.u16(), instrumentCount = header.u16();
    const auto flags = header.u16();
    require((flags & ~1) == 0, "Unsupported XM frequency flags");
    document.linear = (flags & 1) != 0;
    document.speed = header.u16();
    document.bpm = header.u16();
    require(orderCount >= 1 && orderCount <= 256, "XM must contain 1 to 256 orders");
    require(document.restart < orderCount, "XM restart order is outside order list");
    require(document.channels >= 1 && document.channels <= 32, "Only 1 to 32 XM channels are supported");
    require(patternCount <= 256 && instrumentCount <= 128, "XM exceeds 256 patterns or 128 instruments");
    require(document.speed >= 1 && document.speed <= 31 && document.bpm >= 32 && document.bpm <= 255,
            "XM initial speed/BPM is outside supported FT2 range");
    for (std::size_t i = 0; i < 256; ++i) {
        const auto value = header.u8();
        if (i < orderCount) document.orders.push_back(value);
    }
    header.zeroPadding("XM header");
    // XM 1.02/1.03 layout: ALL instrument/sample headers, then patterns,
    // then ALL sample payloads. XM 1.04: patterns, then each instrument and PCM.
    // Verified against upstream LoaderXM.cpp at e3ceb051 (137..318, 320..335,
    // 466..470, 716..747) and FT2-clone loadXM at 4f0f30aa (106..140).
    std::vector<PendingInstrument> pending;
    if (version < 0x0104)
        for (std::size_t i = 0; i < instrumentCount; ++i) pending.push_back(readXmInstrumentHeader(reader));
    document.patterns.reserve(patternCount);
    for (std::size_t i = 0; i < patternCount; ++i) {
        const auto size = reader.u32();
        require(size >= (version == 0x0102 ? 8u : 9u), "XM pattern header is too small");
        auto patternHeader = reader.take(size - 4);
        require(patternHeader.u8() == 0, "Unsupported XM pattern packing type");
        Pattern pattern;
        pattern.rows = version == 0x0102 ? static_cast<std::uint16_t>(patternHeader.u8() + 1) : patternHeader.u16();
        const auto packedSize = patternHeader.u16();
        require(pattern.rows >= 1 && pattern.rows <= 256, "XM patterns must contain 1 to 256 rows");
        patternHeader.zeroPadding("Pattern header");
        auto packed = reader.take(packedSize);
        pattern.cells.resize(std::size_t(pattern.rows) * document.channels);
        if (packedSize) {
            for (auto& cell : pattern.cells) {
                auto mask = packed.u8();
                if (mask & 128) {
                    require((mask & 0x60) == 0, "Unsupported XM packed-cell flags");
                    if (mask & 1) cell.note = packed.u8();
                    if (mask & 2) cell.instrument = packed.u8();
                    if (mask & 4) cell.volume = packed.u8();
                    if (mask & 8) cell.effect = packed.u8();
                    if (mask & 16) cell.param = packed.u8();
                } else {
                    cell.note = mask;
                    cell.instrument = packed.u8();
                    cell.volume = packed.u8();
                    cell.effect = packed.u8();
                    cell.param = packed.u8();
                }
            }
            require(packed.left() == 0, "XM packed pattern has unexpected trailing bytes");
        }
        document.patterns.push_back(std::move(pattern));
    }
    std::size_t totalFrames = 0;
    for (std::size_t i = 0; i < instrumentCount; ++i) {
        auto loaded = version < 0x0104 ? std::move(pending[i]) : readXmInstrumentHeader(reader);
        readSampleData(reader, loaded.instrument, loaded.samples, totalFrames);
        validateInstrument(loaded.instrument);
        document.instruments.push_back(std::move(loaded.instrument));
    }
    require(reader.left() == 0, "XM has trailing extension data unsupported by this version");
    return document;
}

std::vector<std::uint8_t> encodeXm(const Document& document) {
    require(document.channels >= 1 && document.channels <= 32, "Only 1 to 32 XM channels are supported");
    require(!document.orders.empty() && document.orders.size() <= 256, "XM must contain 1 to 256 orders");
    require(document.restart < document.orders.size(), "XM restart order is outside order list");
    require(document.patterns.size() <= 256 && document.instruments.size() <= 128,
            "XM exceeds 256 patterns or 128 instruments");
    require(document.speed >= 1 && document.speed <= 31 && document.bpm >= 32 && document.bpm <= 255,
            "XM initial speed/BPM is outside supported FT2 range");
    std::size_t totalFrames = 0;
    for (const auto& instrument : document.instruments) {
        validateInstrument(instrument);
        for (const auto& sample : instrument.samples) {
            require(sample.pcm.size() <= maxPcmFrames - totalFrames, "Combined samples exceed the 32M-frame memory limit");
            totalFrames += sample.pcm.size();
        }
        if (instrument.samples.empty()) {
            Writer body;
            writeInstrumentBody(body, instrument);
            require(std::all_of(body.data.begin(), body.data.end(), [](auto v) { return v == 0; }),
                    "Empty XM instrument has metadata: add an empty sample slot to retain it");
        }
    }
    Writer writer;
    writer.raw("Extended Module: ", 17);
    writer.name(document.name, 20);
    writer.u8(0x1A);
    writer.name("Little XM Tracker", 20);
    writer.u16(0x0104);
    writer.u32(276);
    writer.u16(static_cast<std::uint16_t>(document.orders.size()));
    writer.u16(document.restart);
    writer.u16(document.channels);
    writer.u16(static_cast<std::uint16_t>(document.patterns.size()));
    writer.u16(static_cast<std::uint16_t>(document.instruments.size()));
    writer.u16(document.linear ? 1 : 0);
    writer.u16(document.speed);
    writer.u16(document.bpm);
    for (std::size_t i = 0; i < 256; ++i) writer.u8(i < document.orders.size() ? document.orders[i] : 0);
    for (const auto& pattern : document.patterns) {
        require(pattern.rows >= 1 && pattern.rows <= 256, "XM patterns must contain 1 to 256 rows");
        require(pattern.cells.size() == std::size_t(pattern.rows) * document.channels, "Pattern dimensions do not match cell storage");
        Writer packed;
        bool empty = true;
        for (const auto& cell : pattern.cells) {
            const std::uint8_t fields[] = {cell.note, cell.instrument, cell.volume, cell.effect, cell.param};
            std::uint8_t mask = 128;
            for (std::size_t i = 0; i < 5; ++i) if (fields[i]) mask = static_cast<std::uint8_t>(mask | (1u << i));
            if (mask != 128) empty = false;
            if ((mask & 15) == 15 && cell.note < 128) {
                for (auto value : fields) packed.u8(value);
            } else {
                packed.u8(mask);
                for (std::size_t i = 0; i < 5; ++i) if (mask & (1u << i)) packed.u8(fields[i]);
            }
        }
        if (empty) packed.data.clear();
        require(packed.data.size() <= 65535, "Packed XM pattern exceeds 65535 bytes");
        writer.u32(9); writer.u8(0); writer.u16(pattern.rows);
        writer.u16(static_cast<std::uint16_t>(packed.data.size()));
        writer.raw(packed.data);
    }
    for (const auto& instrument : document.instruments) {
        writer.u32(instrument.samples.empty() ? 29 : 263);
        writer.name(instrument.name, 22);
        writer.u8(0);
        writer.u16(static_cast<std::uint16_t>(instrument.samples.size()));
        if (!instrument.samples.empty()) {
            writer.u32(40);
            writeInstrumentBody(writer, instrument);
            writeSampleHeaders(writer, instrument);
            writeSampleData(writer, instrument);
        }
    }
    checkFileSize(writer.data.size());
    return std::move(writer.data);
}

Instrument decodeXi(const std::vector<std::uint8_t>& bytes) {
    checkFileSize(bytes.size());
    Reader reader(bytes);
    require(reader.text(21) == "Extended Instrument: ", "Not an XI instrument");
    Instrument instrument;
    instrument.name = reader.name(22);
    require(reader.u8() == 0x1A, "Invalid XI marker");
    reader.skip(20);
    require(reader.u16() == 0x0102, "Only XI 1.02 is supported in this version");
    readInstrumentBody(reader, instrument);
    const auto count = reader.u16();
    std::size_t frames = 0;
    readSamples(reader, instrument, count, 40, frames);
    validateInstrument(instrument);
    require(reader.left() == 0, "XI has unsupported trailing extension data");
    return instrument;
}

std::vector<std::uint8_t> encodeXi(const Instrument& instrument) {
    validateInstrument(instrument);
    std::size_t total = 0;
    for (const auto& sample : instrument.samples) {
        require(sample.pcm.size() <= maxPcmFrames - total, "Combined samples exceed the 32M-frame memory limit");
        total += sample.pcm.size();
    }
    Writer writer;
    writer.raw("Extended Instrument: ", 21);
    writer.name(instrument.name, 22); writer.u8(0x1A);
    writer.name("Little XM Tracker", 20); writer.u16(0x0102);
    writeInstrumentBody(writer, instrument);
    writer.u16(static_cast<std::uint16_t>(instrument.samples.size()));
    writeSampleHeaders(writer, instrument);
    writeSampleData(writer, instrument);
    checkFileSize(writer.data.size());
    return std::move(writer.data);
}

Sample decodeWav(const std::vector<std::uint8_t>& bytes, bool downmixStereo) {
    checkFileSize(bytes.size());
    Reader source(bytes);
    require(source.text(4) == "RIFF", "Only little-endian RIFF WAV is supported");
    const auto riffSize = source.u32();
    require(riffSize >= 4, "Invalid WAV RIFF size");
    auto riff = source.take(riffSize);
    require(source.left() == 0, "WAV has bytes outside the declared RIFF container");
    require(riff.text(4) == "WAVE", "Not a WAV file");
    bool hasFormat = false, hasData = false;
    std::uint16_t channels = 0, bits = 0, blockAlign = 0;
    std::uint32_t rate = 0;
    Reader data(nullptr, 0);
    while (riff.left()) {
        const auto id = riff.text(4);
        const auto size = riff.u32();
        auto chunk = riff.take(size);
        if (id == "fmt ") {
            require(!hasFormat, "WAV has multiple format chunks");
            require(chunk.u16() == 1, "Only integer PCM WAV is supported (8/16-bit mono/stereo)");
            channels = chunk.u16(); rate = chunk.u32();
            const auto byteRate = chunk.u32();
            blockAlign = chunk.u16(); bits = chunk.u16();
            require(channels == 1 || channels == 2, "Only mono/stereo WAV is supported");
            require(bits == 8 || bits == 16, "Only 8-bit and 16-bit PCM WAV is supported");
            require(rate >= 1000 && rate <= 384000, "Unsupported WAV sample rate");
            require(blockAlign == channels * (bits / 8) && byteRate == std::uint64_t(rate) * blockAlign,
                    "Inconsistent WAV PCM alignment/rate");
            if (chunk.left()) {
                require(chunk.u16() == 0, "Unsupported extended PCM WAV format fields");
                chunk.zeroPadding("WAV format");
            }
            hasFormat = true;
        } else if (id == "data") {
            require(!hasData, "Multiple WAV data chunks are unsupported");
            data = chunk; hasData = true;
        }
        if (size & 1) riff.skip(1);
    }
    require(hasFormat && hasData, "WAV is missing format or PCM data");
    require(channels != 2 || downmixStereo, "Stereo WAV requires explicit downmix to mono");
    require(data.left() % blockAlign == 0, "WAV PCM ends in a partial frame");
    const auto frames = data.left() / blockAlign;
    require(frames <= maxPcmFrames, "WAV exceeds the 32M-frame memory limit");
    Sample sample;
    sample.name = "Imported WAV";
    sample.sixteenBit = bits == 16 || channels == 2;
    sample.pcm.reserve(frames);
    const auto readValue = [&]() -> std::int32_t {
        if (bits == 16) return signed16(data.u16());
        return (int(data.u8()) - 128) * 256;
    };
    for (std::size_t i = 0; i < frames; ++i) {
        std::int32_t value = readValue();
        if (channels == 2) value = (value + readValue()) / 2;
        sample.pcm.push_back(static_cast<std::int16_t>(value));
    }
    // Preserve the WAV's intended root pitch in XM's relative-note/finetune units.
    const auto tuning = static_cast<int>(std::lround(1536.0 * std::log2(double(rate) / 8363.0)));
    const auto note = static_cast<int>(std::floor(double(tuning + 64) / 128.0));
    require(note >= -128 && note <= 127, "WAV sample rate cannot be represented by XM tuning");
    sample.relativeNote = static_cast<std::int8_t>(note);
    sample.finetune = static_cast<std::int8_t>(tuning - note * 128);
    return sample;
}

std::vector<std::uint8_t> encodeWavMono(const Sample& sample) {
    return encodeWavMono(sample, 0, sample.pcm.size());
}

std::vector<std::uint8_t> encodeWavMono(const Sample& sample, std::size_t first, std::size_t last) {
    require(first <= last && last <= sample.pcm.size(), "WAV export range is outside the sample");
    validateSample(sample);
    const double tunedRate = 8363.0 * std::exp2((sample.relativeNote + sample.finetune / 128.0) / 12.0);
    require(std::isfinite(tunedRate) && tunedRate >= 1000.0 && tunedRate <= 384000.0,
            "Sample tuning is outside the supported WAV export rate (1000..384000 Hz)");
    const auto rate = static_cast<std::uint32_t>(std::lround(tunedRate));
    const auto length = static_cast<std::uint32_t>((last - first) * 2);
    Writer writer;
    writer.raw("RIFF", 4); writer.u32(36 + length); writer.raw("WAVEfmt ", 8);
    writer.u32(16); writer.u16(1); writer.u16(1); writer.u32(rate);
    writer.u32(rate * 2); writer.u16(2); writer.u16(16);
    writer.raw("data", 4); writer.u32(length);
    for (auto frame = first; frame < last; ++frame)
        writer.u16(static_cast<std::uint16_t>(sample.pcm[frame]));
    return std::move(writer.data);
}

std::vector<std::uint8_t> encodeWav16(const std::vector<std::int16_t>& pcm, std::uint32_t rate) {
    require(pcm.size() % 2 == 0, "Stereo WAV requires interleaved pairs");
    require(rate >= 1000 && rate <= 384000, "Unsupported WAV sample rate");
    require(pcm.size() <= (std::numeric_limits<std::uint32_t>::max() - 36u) / 2u, "WAV exceeds RIFF size limit");
    const auto length = static_cast<std::uint32_t>(pcm.size() * 2);
    Writer writer;
    writer.raw("RIFF", 4); writer.u32(36 + length); writer.raw("WAVEfmt ", 8);
    writer.u32(16); writer.u16(1); writer.u16(2); writer.u32(rate);
    writer.u32(rate * 4); writer.u16(4); writer.u16(16);
    writer.raw("data", 4); writer.u32(length);
    for (auto value : pcm) writer.u16(static_cast<std::uint16_t>(value));
    return std::move(writer.data);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(file.is_open(), "Cannot open file: " + path.u8string());
    const auto length = static_cast<std::streamoff>(file.tellg());
    require(length >= 0, "Cannot read file length: " + path.u8string());
    require(static_cast<std::uint64_t>(length) <= maxFileBytes, "File exceeds the 128 MiB input limit");
    checkFileSize(static_cast<std::size_t>(length));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(file), "Cannot read complete file: " + path.u8string());
    return bytes;
}

void writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    require(!path.filename().empty(), "Save filename is empty");
    static std::atomic<unsigned long> serial{0};
    auto temporary = path;
    temporary += ".tmp." + std::to_string(getpid()) + "." + std::to_string(++serial);
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
    directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    directoryFlags |= O_CLOEXEC;
#endif
    const int directory = ::open(parent.c_str(), directoryFlags);
    require(directory >= 0, "Cannot open save directory: " + parent.u8string());
    const auto synchronize = [](int descriptor) {
        int result;
        do { result = ::fsync(descriptor); } while (result < 0 && errno == EINTR);
        return result;
    };
    int file = -1;
    bool closed = false;
    bool replaced = false;
    try {
        file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        require(file >= 0, "Cannot create temporary save file: " + temporary.u8string());
        std::size_t position = 0;
        while (position < bytes.size()) {
            const auto written = ::write(file, bytes.data() + position, bytes.size() - position);
            if (written < 0 && errno == EINTR) continue;
            require(written > 0, "Cannot write save data");
            position += static_cast<std::size_t>(written);
        }
        require(synchronize(file) == 0, "Cannot flush save data");
        const auto closeResult = ::close(file); closed = true;
        require(closeResult == 0, "Cannot close save file");
        require(::rename(temporary.c_str(), path.c_str()) == 0,
                "Cannot replace destination file; existing file was preserved");
        replaced = true;
        require(synchronize(directory) == 0,
                "File replaced; directory sync failed. Storage durability is uncertain");
    } catch (...) {
        if (file >= 0 && !closed) ::close(file);
        if (file >= 0 && !replaced) ::unlink(temporary.c_str());
        ::close(directory);
        throw;
    }
    ::close(directory);
}

} // namespace brick::io
