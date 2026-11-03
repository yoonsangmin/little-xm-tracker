#pragma once

#include "model/Document.hpp"
#include <memory>

namespace brick::edit {

// Every selection is half-open: first is included, last is excluded.
struct FrameRange { std::size_t first = 0, last = 0; };
FrameRange wholeSample(const Sample& sample);
enum class SampleTransform { Reverse, Normalize, FadeIn, FadeOut, Gain, Silence, Invert, RemoveDc };
void transformSample(Sample& sample, FrameRange range, SampleTransform operation, double gain = 1.0);
struct PcmClip {
    std::vector<std::int16_t> pcm;
    bool sixteenBit = true;
    std::int8_t relativeNote = 0, finetune = 0;
};
PcmClip copyPcm(const Sample& sample, FrameRange range);
// Replaces the selected range, or inserts at first when first == last.
// Aligns clipboard PCM to the destination tuning to retain pitch and duration
// at the same trigger note. Equal tuning copies PCM exactly. Returns the actual
// inserted frame count, which can differ from the clipboard's length.
// Pasting higher precision PCM promotes an 8-bit destination to 16-bit.
std::size_t pastePcm(Sample& sample, FrameRange replaced, const PcmClip& clip);
void deletePcm(Sample& sample, FrameRange range);
void trimPcm(Sample& sample, FrameRange range);
void convertSampleDepth(Sample& sample, bool sixteenBit);
void setSampleLoop(Sample& sample, std::uint8_t type, std::uint32_t first, std::uint32_t length);

// Envelope points remain ordered; all marker indices are adjusted on insert/delete.
void setEnvelopePoint(Envelope& envelope, std::size_t index, EnvPoint point);
void insertEnvelopePoint(Envelope& envelope, std::size_t index, EnvPoint point);
void deleteEnvelopePoint(Envelope& envelope, std::size_t index);
void setEnvelopeFlags(Envelope& envelope, std::uint8_t flags);
void setEnvelopeMarkers(Envelope& envelope, std::uint8_t sustain, std::uint8_t loopStart, std::uint8_t loopEnd);

struct BlockRange {
    std::size_t firstRow = 0, lastRow = 0, firstChannel = 0, lastChannel = 0;
    // Only the first/last channels are partial; intervening channels include all fields.
    std::size_t firstField = 0, lastField = 5;
};
// Bits 0..4 are note, instrument, volume, effect and parameter. Outside the
// selected channels (or for invalid field bounds), the result is zero.
std::uint8_t selectedFields(BlockRange range, std::size_t channel);
struct PatternBlock {
    std::size_t rows = 0, channels = 0;
    std::vector<Cell> cells;
    // One mask per channel; empty means every field. Cells retain FX context
    // for transparent pastes even when only one half of the FX pair is selected.
    std::vector<std::uint8_t> fieldMasks;
};
PatternBlock copyBlock(const Pattern& pattern, std::size_t channels, BlockRange range);
// Rejects a block that would run beyond the destination; never silently clips it.
void pasteBlock(Pattern& pattern, std::size_t channels, std::size_t row, std::size_t channel, const PatternBlock& block);
void clearBlock(Pattern& pattern, std::size_t channels, BlockRange range);
void transposeBlock(Pattern& pattern, std::size_t channels, BlockRange range, int semitones);
// Only the cursor field is edited within the selection. direction is -1/+1;
// coarse uses octaves for notes and 16 for numeric fields. cursorVolume chooses
// absolute-volume or command-only edits; commands use coarse for type, fine for x.
// The effect command always moves by one, including coarse adjustments.
// Empty/reserved values are preserved. An out-of-range note rejects the whole edit.
void adjustBlock(Pattern& pattern, std::size_t channels, BlockRange range, int direction, bool coarse,
                 std::size_t field, std::uint8_t cursorVolume = 0);
// Row insert/delete shifts selected channels only. Insertion refuses to drop a
// nonempty last row unless allowDiscard is explicitly true.
void insertRow(Pattern& pattern, std::size_t channels, std::size_t row,
               std::size_t firstChannel, std::size_t lastChannel, bool allowDiscard = false);
void deleteRow(Pattern& pattern, std::size_t channels, std::size_t row,
               std::size_t firstChannel, std::size_t lastChannel);
void resizePattern(Pattern& pattern, std::size_t channels, std::uint16_t rows, bool allowDiscard = false);
void resizeChannels(Document& document, std::uint16_t channels, bool allowDiscard = false);

// Compact history: metadata and changed patterns are stored separately from PCM;
// PCM retains only its changed contiguous span, not every sample in the song.
class DocumentDelta {
public:
    static DocumentDelta between(const Document& before, const Document& after);
    void undo(Document& document) const;
    void redo(Document& document) const;
    std::size_t bytes() const;
    bool empty() const;
private:
    struct Data;
    std::shared_ptr<const Data> data_;
};

} // namespace brick::edit
