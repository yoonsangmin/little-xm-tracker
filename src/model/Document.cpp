#include "model/Document.hpp"
#include <iterator>
#include <utility>

namespace brick {

Document makeDemoDocument() {
    Document document;
    document.name = "Brick first song";
    document.orders = {0};
    Pattern pattern;
    pattern.rows = 32;
    pattern.cells.resize(pattern.rows * document.channels);

    Instrument instrument;
    instrument.name = "Pocket PCM";
    Sample sample;
    sample.name = "Baked triangle";
    // Authored, fixed PCM waveform. The application has no synthesis engine.
    constexpr std::int16_t waveform[] = {
        0, 2048, 4096, 6144, 8192, 10240, 12288, 14336,
        16384, 14336, 12288, 10240, 8192, 6144, 4096, 2048,
        0, -2048, -4096, -6144, -8192, -10240, -12288, -14336,
        -16384, -14336, -12288, -10240, -8192, -6144, -4096, -2048
    };
    sample.pcm.assign(std::begin(waveform), std::end(waveform));
    sample.loopType = 1;
    sample.loopLength = static_cast<std::uint32_t>(sample.pcm.size());
    instrument.samples.push_back(sample);
    instrument.volumeEnvelope.count = 3;
    instrument.volumeEnvelope.points[0] = {0, 64};
    instrument.volumeEnvelope.points[1] = {12, 48};
    instrument.volumeEnvelope.points[2] = {24, 0};
    instrument.volumeEnvelope.flags = 1;
    instrument.fadeout = 512;
    document.instruments.push_back(instrument);
    constexpr std::uint8_t notes[] = {49, 53, 56, 60, 58, 56, 53, 51};
    for (std::size_t i = 0; i < std::size(notes); ++i) {
        auto& cell = pattern.at(i * 4, 0, document.channels);
        cell = {notes[i], 1, 0x40, 0, 0};
    }
    document.patterns.push_back(std::move(pattern));
    return document;
}

} // namespace brick
