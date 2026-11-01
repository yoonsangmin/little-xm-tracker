#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace brick {

struct Cell {
    std::uint8_t note = 0, instrument = 0, volume = 0, effect = 0, param = 0;
};

struct Pattern {
    std::uint16_t rows = 64;
    std::vector<Cell> cells;
    Cell& at(std::size_t row, std::size_t channel, std::size_t channels) {
        if (row >= rows || channel >= channels) throw std::out_of_range("Pattern cell");
        return cells.at(row * channels + channel);
    }
    const Cell& at(std::size_t row, std::size_t channel, std::size_t channels) const {
        if (row >= rows || channel >= channels) throw std::out_of_range("Pattern cell");
        return cells.at(row * channels + channel);
    }
};

struct EnvPoint { std::uint16_t x = 0, y = 0; };
struct Envelope {
    std::array<EnvPoint, 12> points{};
    std::uint8_t count = 0, sustain = 0, loopStart = 0, loopEnd = 0, flags = 0;
};

struct Sample {
    std::string name;
    // Canonical, signed full-scale PCM. Eight-bit samples are multiplied by 256.
    std::vector<std::int16_t> pcm;
    bool sixteenBit = true;
    std::uint8_t volume = 64, panning = 128, loopType = 0;
    std::int8_t finetune = 0, relativeNote = 0;
    // Always mono frames, never file bytes.
    std::uint32_t loopStart = 0, loopLength = 0;
    std::uint8_t reserved = 0;
};

struct Instrument {
    std::string name;
    std::array<std::uint8_t, 96> noteToSample{};
    Envelope volumeEnvelope, panningEnvelope;
    std::uint8_t vibratoType = 0, vibratoSweep = 0, vibratoDepth = 0, vibratoRate = 0;
    std::uint16_t fadeout = 0;
    // FT2's MIDI/mute extension is retained even though this app has no MIDI output.
    std::array<std::uint8_t, 22> extension{};
    std::vector<Sample> samples;
};

struct Document {
    std::string name;
    std::uint16_t channels = 8, restart = 0, speed = 6, bpm = 125;
    bool linear = true;
    std::vector<std::uint8_t> orders;
    std::vector<Pattern> patterns;
    std::vector<Instrument> instruments;
};

Document makeDemoDocument();

} // namespace brick
