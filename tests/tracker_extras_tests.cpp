#include "model/TrackerExtras.hpp"
#include "io/Codec.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Fn> void rejected(Fn fn, const char* message) {
    bool threw = false;
    try { fn(); } catch (const std::exception&) { threw = true; }
    check(threw, message);
}
bool same(const brick::Cell& a, const brick::Cell& b) {
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume &&
           a.effect == b.effect && a.param == b.param;
}
bool same(const brick::Pattern& a, const brick::Pattern& b) {
    if (a.rows != b.rows || a.cells.size() != b.cells.size()) return false;
    for (std::size_t n = 0; n < a.cells.size(); ++n)
        if (!same(a.cells[n], b.cells[n])) return false;
    return true;
}
bool same(const brick::Sample& a, const brick::Sample& b) {
    return a.name == b.name && a.pcm == b.pcm && a.sixteenBit == b.sixteenBit &&
           a.volume == b.volume && a.panning == b.panning && a.loopType == b.loopType &&
           a.finetune == b.finetune && a.relativeNote == b.relativeNote &&
           a.loopStart == b.loopStart && a.loopLength == b.loopLength && a.reserved == b.reserved;
}
brick::Pattern pattern(int rows, int channels) {
    brick::Pattern result;
    result.rows = static_cast<std::uint16_t>(rows);
    result.cells.resize(std::size_t(rows) * std::size_t(channels));
    return result;
}

void maskedPaste() {
    using namespace brick;
    const Cell original{60, 2, 0x30, 4, 0x47};
    edit::PasteMask mask;
    mask.skipEmpty = true;
    check(same(edit::mergeCell(original, {}, mask), original), "Transparent blank cell preserves all destination data");
    auto result = edit::mergeCell(original, {0, 0, 0, 0, 0x37}, mask);
    check(result.effect == 0 && result.param == 0x37, "Arpeggio's zero command is not mistaken for an empty FX field");
    result = edit::mergeCell(original, {0, 0, 0, 4, 0}, mask);
    check(result.effect == 4 && result.param == 0, "Effect-memory zero parameter is copied with a nonzero command");
    result = edit::mergeCell(original, {97, 0, 0x10, 0, 0}, mask);
    check(result.note == 97 && result.instrument == 2 && result.volume == 0x10,
          "Key Off and zero-volume command are meaningful while instrument zero is empty");
    mask = {false, false, false, true, false, true};
    result = edit::mergeCell(original, {49, 3, 0x40, 0, 0x37}, mask);
    check(result.effect == 0 && result.param == 0x47 && result.note == 60 && result.instrument == 2,
          "Command-only mask preserves destination parameter and unrelated fields");
    mask = {false, false, false, false, true, true};
    result = edit::mergeCell(original, {0, 0, 0, 4, 0}, mask);
    check(result.effect == 4 && result.param == 0, "Parameter-only transparent paste retains original FX context");
    mask.skipEmpty = false;
    check(edit::mergeCell(original, {}, mask).param == 0, "Opaque paste clears selected fields");

    auto destination = pattern(4, 2);
    destination.cells.assign(8, original);
    const auto before = destination;
    edit::PatternBlock clip{2, 2, {{49, 1, 0, 0, 0}, {0, 0, 0, 4, 0}, {97, 0, 0x10, 0, 0}, {0, 0, 0, 0, 0x37}}, {}};
    rejected([&] { edit::pasteMaskedBlock(destination, 2, 3, 0, clip, {}); }, "Overflowing masked paste is rejected");
    check(same(destination, before), "Rejected masked paste cannot partially overwrite destination");
    edit::pasteMaskedBlock(destination, 2, 1, 0, clip, {false, false, false, true, true, true});
    check(destination.at(1, 1, 2).param == 0 && destination.at(2, 1, 2).effect == 0,
          "Block FX transparency uses paired emptiness");
    for (const auto& cell : destination.cells)
        check(cell.note == original.note && cell.instrument == original.instrument && cell.volume == original.volume,
              "FX-only block paste keeps notes, instruments and volume");
}

void partialPatternBlocks() {
    using namespace brick;
    const Cell sourceCell{49, 2, 0x30, 4, 0x47};
    const Cell destinationCell{60, 7, 0x50, 0x0F, 0xAA};
    auto source = pattern(3, 4);
    source.cells.assign(12, sourceCell);
    source.at(1, 1, 4).param = 0; // 400 must retain its meaning in a parameter-only paste.
    const edit::BlockRange range{0, 2, 1, 4, 3, 2};
    check(edit::selectedFields(range, 0) == 0 && edit::selectedFields(range, 4) == 0,
          "Channels outside the selection have no selected fields");
    check(edit::selectedFields(range, 1) == 24 && edit::selectedFields(range, 2) == 31 &&
          edit::selectedFields(range, 3) == 3,
          "Cross-channel selection has partial endpoints and a complete middle channel");
    const auto clip = edit::copyBlock(source, 4, range);
    check(clip.rows == 2 && clip.channels == 3 && clip.fieldMasks == std::vector<std::uint8_t>({24, 31, 3}),
          "Partial clipboard keeps its selected columns per channel");
    check(edit::copyBlock(source, 4, {0, 2, 0, 4}).fieldMasks.empty(),
          "Legacy whole-channel copy keeps the implicit all-fields mask");
    auto destination = pattern(4, 4);
    destination.cells.assign(16, destinationCell);
    const auto before = destination;
    edit::pasteBlock(destination, 4, 1, 0, clip);
    check(same(destination.at(1, 0, 4), {60, 7, 0x50, 4, 0x47}) &&
          same(destination.at(1, 1, 4), sourceCell) &&
          same(destination.at(1, 2, 4), {49, 2, 0x50, 0x0F, 0xAA}),
          "Partial paste changes only copied columns after channel relocation");
    check(same(destination.at(0, 0, 4), destinationCell) &&
          same(destination.at(3, 1, 4), destinationCell) && same(destination.at(1, 3, 4), destinationCell),
          "Partial paste preserves rows and channels outside the destination block");

    destination = before;
    edit::pasteMaskedBlock(destination, 4, 1, 0, clip, {false, false, true, false, true, true});
    check(same(destination.at(1, 0, 4), {60, 7, 0x50, 0x0F, 0x47}) &&
          same(destination.at(1, 1, 4), {60, 7, 0x30, 0x0F, 0x47}) &&
          same(destination.at(1, 2, 4), destinationCell),
          "Clipboard field masks and user paste masks intersect");
    check(destination.at(2, 0, 4).param == 0 && destination.at(2, 0, 4).effect == 0x0F,
          "A selected zero parameter in 400 survives transparent paste without replacing the command");
    const auto parameterClip = edit::copyBlock(source, 4, {1, 2, 1, 2, 4, 5});
    destination = before;
    edit::pasteMaskedBlock(destination, 4, 0, 0, parameterClip, {true, true, true, true, true, true});
    check(parameterClip.fieldMasks == std::vector<std::uint8_t>({16}) &&
          same(destination.at(0, 0, 4), {60, 7, 0x50, 0x0F, 0}),
          "A parameter-only clipboard retains unselected command context for transparent zero parameters");

    auto cleared = source;
    edit::clearBlock(cleared, 4, range);
    check(same(cleared.at(0, 1, 4), {49, 2, 0x30, 0, 0}) &&
          same(cleared.at(0, 2, 4), {}) && same(cleared.at(0, 3, 4), {0, 0, 0x30, 4, 0x47}) &&
          same(cleared.at(2, 2, 4), sourceCell),
          "Partial clear preserves unselected fields and rows");
    auto transposed = source;
    transposed.at(0, 1, 4).note = 96;
    edit::transposeBlock(transposed, 4, range, 1);
    check(transposed.at(0, 1, 4).note == 96 && transposed.at(0, 2, 4).note == 50 &&
          transposed.at(0, 3, 4).note == 50 && transposed.at(2, 2, 4).note == 49,
          "Transpose ignores excluded notes including notes that would otherwise overflow");

    auto badClip = clip;
    badClip.fieldMasks.pop_back();
    destination = before;
    rejected([&] { edit::pasteBlock(destination, 4, 1, 0, badClip); }, "Mismatched clipboard masks are rejected");
    rejected([&] { edit::pasteMaskedBlock(destination, 4, 1, 0, badClip, {}); }, "Masked paste also validates mask count");
    badClip = clip;
    badClip.fieldMasks.back() = 0x80;
    rejected([&] { edit::pasteBlock(destination, 4, 1, 0, badClip); }, "Unknown clipboard field bits are rejected");
    rejected([&] { edit::pasteMaskedBlock(destination, 4, 1, 0, badClip, {}); }, "Masked paste rejects unknown field bits");
    check(same(destination, before), "Every invalid clipboard mask is rejected before any destination write");
    rejected([&] { edit::clearBlock(destination, 4, {0, 2, 1, 2, 3, 2}); }, "An inverted single-channel field range is rejected");
    check(same(destination, before), "Invalid selection leaves all fields intact");
}

void selectedFieldAdjustments() {
    using namespace brick;
    const Cell initial{49, 2, 0x30, 4, 0x47};
    const edit::BlockRange range{0, 2, 0, 3, 1, 4};
    for (std::size_t field = 0; field < 5; ++field) {
        auto value = pattern(2, 3);value.cells.assign(6, initial);
        edit::adjustBlock(value, 3, range, 1, true, field, initial.volume);
        for (std::size_t row = 0; row < 2; ++row) for (std::size_t channel = 0; channel < 3; ++channel) {
            auto expected = initial;
            if (edit::selectedFields(range, channel) & (1u << field)) {
                if (field == 0) expected.note = 61;
                if (field == 1) expected.instrument = 18;
                if (field == 2) expected.volume = 0x40;
                if (field == 3) expected.effect = 5;
                if (field == 4) expected.param = 0x57;
            }
            check(same(value.at(row, channel, 3), expected),
                  "A mixed block adjusts only the cursor field, intersected with selected columns");
        }
        edit::adjustBlock(value, 3, range, -1, true, field, initial.volume);
        for (const auto& cell : value.cells) check(same(cell, initial), "Inverse adjustment restores only the targeted field");
    }
    auto value = pattern(9, 1);
    const std::uint8_t volumes[]{0, 0x0F, 0x10, 0x50, 0x51, 0x5F, 0x60, 0x6F, 0xFF};
    for (std::size_t row = 0; row < 9; ++row) value.at(row, 0, 1).volume = volumes[row];
    auto commands = value;
    edit::adjustBlock(value, 1, {0, 9, 0, 1}, 1, true, 2, 0x30);
    const std::uint8_t absolute[]{0, 0x0F, 0x20, 0x50, 0x51, 0x5F, 0x60, 0x6F, 0xFF};
    for (std::size_t row = 0; row < 9; ++row)
        check(value.at(row, 0, 1).volume == absolute[row], "A volume cursor changes only absolute volumes and clamps at 64");
    edit::adjustBlock(commands, 1, {0, 9, 0, 1}, 1, false, 2, 0xB4);
    check(commands.cells[6].volume == 0x61 && commands.cells[7].volume == 0x6F && commands.cells[8].volume == 0xFF,
          "A volume command cursor changes only x without carrying into the command type");
    edit::adjustBlock(commands, 1, {0, 9, 0, 1}, 1, true, 2, 0xB4);
    check(commands.cells[6].volume == 0x71 && commands.cells[7].volume == 0x7F && commands.cells[8].volume == 0xFF,
          "Vertical command adjustment preserves x while clamping command types");
    for (std::size_t row = 0; row < 6; ++row)
        check(commands.cells[row].volume == volumes[row], "Volume commands do not change absolute, empty or reserved values");
    value = pattern(4, 1);
    value.cells = {{0, 0, 0, 0, 0}, {97, 0, 0, 0, 0x0F}, {49, 128, 0x50, 35, 255}, {0, 0, 0, 1, 0}};
    const auto before = value;
    edit::adjustBlock(value, 1, {0, 4, 0, 1}, 1, false, 0);
    check(same(value.cells[0], before.cells[0]) && same(value.cells[1], before.cells[1]) &&
          value.cells[2].note == 50 && value.cells[2].effect == 35 && same(value.cells[3], before.cells[3]),
          "Note cursor preserves OFF, blanks and all other selected fields");
    edit::adjustBlock(value, 1, {0, 4, 0, 1}, 1, true, 4);
    check(value.cells[0].param == 0 && value.cells[1].effect == 0 && value.cells[1].param == 0x1F &&
          value.cells[2].param == 255 && value.cells[3].effect == 1 && value.cells[3].param == 16,
          "FX parameter editing distinguishes 000 from memory effects and leaves effect types intact");
    value = pattern(3, 2);value.cells.assign(6, initial);value.at(2, 1, 2).note = 96;
    const auto overflow = value;
    rejected([&] { edit::adjustBlock(value, 2, {0, 3, 0, 2}, 1, false, 0); }, "Note overflow rejects the whole operation");
    check(same(value, overflow), "All targeted notes are validated before writing");
    edit::adjustBlock(value, 2, {0, 3, 0, 2}, 1, false, 2, 0x30);
    check(value.at(2, 1, 2).note == 96 && value.at(2, 1, 2).volume == 0x31,
          "An unedited note cannot reject a volume adjustment");
    rejected([&] { edit::adjustBlock(value, 2, {0, 3, 0, 2}, 0, false, 0); }, "Invalid adjustment direction is rejected");
    rejected([&] { edit::adjustBlock(value, 2, {0, 3, 0, 2}, 1, false, 5); }, "Invalid cursor field is rejected");
}

void patternTimingEdits() {
    using namespace brick;
    auto value = pattern(3, 2);
    value.at(0, 0, 2) = {49, 1, 0x30, 0x0B, 2};
    value.at(1, 1, 2) = {0, 1, 0x40, 0x0D, 0x12};
    value.at(2, 0, 2) = {97, 0, 0, 4, 0};
    const auto original = value;
    edit::expandPattern(value, 2);
    check(value.rows == 6, "Expansion doubles row count");
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t c = 0; c < 2; ++c) {
            check(same(value.at(r * 2, c, 2), original.at(r, c, 2)), "Expansion preserves every raw source byte including jumps");
            check(same(value.at(r * 2 + 1, c, 2), {}), "Expansion inserts a blank row");
        }
    edit::shrinkPattern(value, 2);
    check(same(value, original), "Shrinking a freshly expanded pattern restores the original");
    rejected([&] { edit::shrinkPattern(value, 2); }, "Shrinking nonempty odd rows requires explicit discard");
    check(same(value, original), "Rejected shrink preserves pattern transactionally");
    edit::shrinkPattern(value, 2, true);
    check(value.rows == 2 && same(value.at(1, 0, 2), original.at(2, 0, 2)), "Odd length keeps the final even row");

    value = pattern(129, 1);
    value.at(128, 0, 1) = {65, 1, 0x40, 0x0D, 0x32};
    const auto tooLong = value;
    rejected([&] { edit::expandPattern(value, 1); }, "Expansion beyond 256 cannot silently drop occupied source rows");
    check(same(value, tooLong), "Rejected expansion preserves the entire original");
    edit::expandPattern(value, 1, true);
    check(value.rows == 256 && value.cells.size() == 256, "Explicitly truncated expansion stays within the XM limit");
    value = pattern(1, 1);
    value.cells[0] = {49, 1, 0, 0, 0};
    edit::shrinkPattern(value, 1);
    check(value.rows == 1 && value.cells[0].note == 49, "One-row pattern never shrinks to zero");
}

void wholePatternReplacement() {
    using namespace brick;
    Document document;
    document.channels = 2;
    document.orders = {1, 0, 1};
    document.patterns = {pattern(3, 2), pattern(9, 2)};
    document.patterns[0].at(0, 0, 2) = {49, 1, 0x50, 4, 0x47};
    document.patterns[0].at(2, 1, 2) = {97, 0, 0x10, 0x0B, 2};
    document.patterns[1].cells.assign(18, {60, 2, 0x30, 0x0F, 125});
    const auto original = document;
    const auto originalBytes = io::encodeXm(document);
    const auto source = document.patterns[0];
    edit::replacePattern(document, 1, document.patterns[0], 2);
    check(same(document.patterns[1], source) && document.patterns[1].rows == 3,
          "Whole pattern paste replaces length and every field, including empty cells");
    check(document.orders == original.orders && same(document.patterns[0], source),
          "Replacing a shared pattern keeps Order references and neighboring source intact");
    check(document.patterns[1].cells.data() != document.patterns[0].cells.data(),
          "Whole pattern paste owns independent cell storage");
    const auto replacedBytes = io::encodeXm(document);
    check(io::encodeXm(io::decodeXm(replacedBytes)) == replacedBytes,
          "Whole pattern replacement round-trips through XM");
    auto delta = edit::DocumentDelta::between(original, document);
    delta.undo(document);
    check(io::encodeXm(document) == originalBytes, "Undo restores overwritten pattern length and cells");
    delta.redo(document);
    check(io::encodeXm(document) == replacedBytes, "Redo restores complete replacement without changing Orders");
    edit::replacePattern(document, 1, document.patterns[1], 2);
    check(io::encodeXm(document) == replacedBytes, "Replacing a pattern with its own aliased data is safe");
    auto expanded = original;
    edit::replacePattern(expanded, 0, expanded.patterns[1], 2);
    check(same(expanded.patterns[0], original.patterns[1]) && expanded.orders == original.orders,
          "Whole pattern paste also grows a shorter destination to the source length");

    const auto beforeGrowth = document;
    edit::replacePattern(document, 255, document.patterns[0], 2);
    check(document.patterns.size() == 256 && same(document.patterns[255], source) &&
          document.orders == original.orders, "Whole paste can materialize pattern FF from an aliased source");
    bool emptyGaps = true;
    for (std::size_t index = beforeGrowth.patterns.size(); index < 255; ++index)
        emptyGaps = emptyGaps && same(document.patterns[index], pattern(64, 2));
    check(emptyGaps, "Intervening pattern numbers remain valid empty 64-row slots");
    delta = edit::DocumentDelta::between(beforeGrowth, document);
    const auto grownBytes = io::encodeXm(document);
    delta.undo(document);
    check(io::encodeXm(document) == replacedBytes && document.patterns.size() == 2,
          "Undo removes pattern slots and gaps introduced by whole paste");
    delta.redo(document);
    check(io::encodeXm(document) == grownBytes, "Redo restores the distant pattern and empty gaps");

    document = original;
    for (const int destination : {-1, 256})
        rejected([&] { edit::replacePattern(document, destination, source, 2); },
                 "Whole pattern paste rejects destination IDs outside 00..FF");
    for (const int channels : {-1, 0, 1, 33})
        rejected([&] { edit::replacePattern(document, 255, pattern(3, channels > 0 ? channels : 1), channels); },
                 "Whole pattern paste rejects invalid or mismatched channel counts before creating slots");
    auto malformed = source;
    malformed.cells.pop_back();
    rejected([&] { edit::replacePattern(document, 255, malformed, 2); },
             "Malformed source storage is rejected before creating slots");
    for (const int rows : {0, 257})
        rejected([&] { edit::replacePattern(document, 255, pattern(rows, 2), 2); },
                 "Whole pattern paste rejects row lengths outside 1..256");
    check(io::encodeXm(document) == originalBytes && document.patterns.size() == 2,
          "All rejected replacements preserve original contents and slot count");
}

void wholeSlots() {
    using namespace brick;
    auto document = makeDemoDocument();
    document.instruments.resize(2);
    auto& source = document.instruments[0];
    source.name = "Multisample source";
    source.samples.push_back(source.samples.at(0));
    source.samples[1].name = "Second slot";
    source.noteToSample.fill(1);
    source.fadeout = 2048;
    source.extension[0] = 0x7f;
    const auto sourceXi = io::encodeXi(source);
    const auto orders = document.orders;
    const auto patterns = document.patterns;
    edit::copyInstrument(document, 1, 2);
    check(io::encodeXi(document.instruments[1]) == sourceXi, "Whole instrument copy includes all metadata and sample PCM");
    document.instruments[1].samples[1].pcm[0] ^= 0x100;
    check(io::encodeXi(document.instruments[0]) == sourceXi, "Copied PCM storage is independent");
    edit::replaceInstrument(document, 3, document.instruments[0]);
    check(document.instruments.size() == 3 && io::encodeXi(document.instruments[2]) == sourceXi,
          "Appending from an aliased instrument snapshots before vector growth");
    const auto secondXi = io::encodeXi(document.instruments[1]);
    edit::swapInstruments(document, 1, 2);
    check(io::encodeXi(document.instruments[0]) == secondXi && io::encodeXi(document.instruments[1]) == sourceXi,
          "Instrument swap exchanges complete slot contents");
    check(document.orders == orders, "Slot edits do not rewrite song order references");
    for (std::size_t p = 0; p < patterns.size(); ++p)
        check(same(document.patterns[p], patterns[p]), "Slot edits do not rewrite pattern instrument numbers");

    const auto sourceSample = document.instruments[1].samples[0];
    const auto targetMap = document.instruments[2].noteToSample;
    edit::copySample(document, 2, 0, 3, 2);
    check(document.instruments[2].samples.size() == 3 && document.instruments[2].samples[2].pcm == sourceSample.pcm,
          "Whole sample copy may append the next sample slot");
    check(document.instruments[2].noteToSample == targetMap, "Appending a sample leaves existing key mappings intact");
    document.instruments[2].samples[2].name = "Independent copy";
    document.instruments[2].samples[2].pcm[0] ^= 0x100;
    check(document.instruments[1].samples[0].pcm == sourceSample.pcm, "Sample copy does not share PCM with its source");
    edit::swapSamples(document, 2, 0, 3, 2);
    check(document.instruments[1].samples[0].name == "Independent copy" &&
          document.instruments[2].samples[2].pcm == sourceSample.pcm,
          "Cross-instrument sample swap carries complete sample metadata and PCM");
    check(document.instruments[2].noteToSample == targetMap, "Sample swap changes slot content rather than remapping keys");
    const auto encoded = io::encodeXm(document);
    check(io::encodeXm(io::decodeXm(encoded)) == encoded, "Edited slots round-trip through XM");
    rejected([&] { edit::copyInstrument(document, 1, 129); }, "Instrument copy cannot exceed 128 slots");
    rejected([&] { edit::copySample(document, 1, 0, 2, 16); }, "Sample copy cannot exceed sixteen slots");
    rejected([&] { edit::swapSamples(document, 1, 0, 2, 15); }, "Sample swap rejects a missing slot");
    check(io::encodeXm(document) == encoded, "Invalid slot operations preserve document bytes");

    edit::DocumentDelta delta = edit::DocumentDelta::between(makeDemoDocument(), document);
    auto recovered = document;
    delta.undo(recovered);
    check(io::encodeXm(recovered) == io::encodeXm(makeDemoDocument()), "Existing history restores complete pre-copy slot state");
    delta.redo(recovered);
    check(io::encodeXm(recovered) == encoded, "History restores copied and swapped PCM and metadata");
}

void emptySlotReplacement() {
    using namespace brick;
    auto document = makeDemoDocument();
    const auto original = io::encodeXm(document);
    const auto source = document.instruments[0];
    const auto sourceXi = io::encodeXi(source);
    edit::replaceInstrument(document, 128, document.instruments[0]);
    check(document.instruments.size() == 128 && io::encodeXi(document.instruments[127]) == sourceXi,
          "An aliased instrument may be pasted directly into the highest empty ID");
    check(document.instruments[126].name.empty() && document.instruments[126].samples.empty(),
          "Intervening instrument IDs remain empty");
    edit::replaceSample(document, 127, 15, document.instruments[0].samples[0]);
    check(document.instruments[126].samples.size() == 16 &&
          document.instruments[126].samples[15].pcm == source.samples[0].pcm &&
          document.instruments[126].samples[14].pcm.empty(),
          "An existing empty instrument accepts the highest sample ID with empty gaps");

    document = makeDemoDocument();
    edit::replaceSample(document, 128, 15, document.instruments[0].samples[0]);
    check(document.instruments.size() == 128 && document.instruments[127].samples.size() == 16 &&
          document.instruments[127].samples[15].pcm == source.samples[0].pcm,
          "Sample paste can create both instrument and sample ID from an aliased source");
    check(document.instruments[126].samples.empty() && document.instruments[127].samples[14].pcm.empty(),
          "Creating a distant sample keeps all intervening slots empty");
    const auto encoded = io::encodeXm(document);
    check(io::encodeXm(io::decodeXm(encoded)) == encoded, "Sparse slot edits round-trip through XM");
    auto delta = edit::DocumentDelta::between(makeDemoDocument(), document);
    delta.undo(document);
    check(io::encodeXm(document) == original, "Undo removes empty gap slots introduced by sample paste");
    delta.redo(document);
    check(io::encodeXm(document) == encoded, "Redo restores a distant sample and its empty gaps");

    document = makeDemoDocument();
    auto invalidSample = source.samples[0];
    invalidSample.volume = 65;
    rejected([&] { edit::replaceSample(document, 128, 15, invalidSample); },
             "Invalid PCM metadata is rejected before creating gap slots");
    auto invalidInstrument = source;
    invalidInstrument.noteToSample[0] = 16;
    rejected([&] { edit::replaceInstrument(document, 128, invalidInstrument); },
             "Invalid instrument mapping is rejected before creating gap slots");
    rejected([&] { edit::copyInstrument(document, 128, 2); }, "Empty source instruments remain invalid");
    rejected([&] { edit::copySample(document, 1, 15, 128, 15); }, "Empty source samples remain invalid");
    rejected([&] { edit::swapInstruments(document, 1, 128); }, "Swap does not create missing instrument slots");
    check(io::encodeXm(document) == original, "All rejected sparse replacements preserve the original document");
}

void clearSlotContents() {
    using namespace brick;
    Document document;
    document.channels = 2;
    document.orders = {1, 0, 1, 2};
    document.patterns = {pattern(3, 2), pattern(7, 2), pattern(5, 2)};
    for (auto& value : document.patterns)
        value.cells.assign(value.cells.size(), {49, 2, 0x50, 4, 0x47});
    document.patterns[0].at(1, 1, 2) = {97, 3, 0x10, 0x0B, 2};
    auto rich = makeDemoDocument().instruments[0];
    const auto sourceSample = rich.samples[0];
    rich.samples = {sourceSample, sourceSample, sourceSample};
    rich.samples[0].name = "First sample";
    rich.samples[1].name = "Middle sample";
    rich.samples[2].name = "Last sample";
    auto& middle = rich.samples[1];
    middle.sixteenBit = false;
    middle.volume = 13; middle.panning = 23; middle.finetune = -21; middle.relativeNote = 12;
    middle.loopType = 2; middle.loopStart = 1; middle.loopLength = 4; middle.reserved = 0x7f;
    rich.noteToSample.fill(1);
    rich.noteToSample.back() = 2;
    rich.panningEnvelope = rich.volumeEnvelope;
    rich.vibratoType = 2; rich.vibratoSweep = 17; rich.vibratoDepth = 9; rich.vibratoRate = 31;
    rich.extension[0] = 0x7f;
    document.instruments = {rich, rich, rich};
    document.instruments[1].name = "Middle instrument";
    document.instruments[2].name = "Last instrument";
    const auto before = document;
    const auto original = io::encodeXm(before);

    edit::clearSampleSlot(document, 1, 1);
    const auto& owner = document.instruments[0];
    check(owner.samples.size() == 3 && same(owner.samples[1], Sample{}),
          "Sample clear resets PCM and every sample setting without removing the middle slot");
    check(owner.noteToSample == before.instruments[0].noteToSample &&
          same(owner.samples[0], before.instruments[0].samples[0]) &&
          same(owner.samples[2], before.instruments[0].samples[2]),
          "Sample clear keeps keys pointing at the same numbered slots and preserves neighboring samples");
    auto expectedOwner = before.instruments[0];
    expectedOwner.samples[1] = Sample{};
    const auto ownerXi = io::encodeXi(owner);
    check(ownerXi == io::encodeXi(expectedOwner), "Sample clear preserves all owning instrument settings");
    check(io::encodeXi(io::decodeXi(ownerXi)) == ownerXi,
          "A cleared sample remains a valid empty XI slot with its original key mappings");

    edit::clearInstrumentSlot(document, 2);
    check(document.instruments.size() == 3 && document.instruments[1].samples.empty() &&
          io::encodeXi(document.instruments[1]) == io::encodeXi(Instrument{}),
          "Instrument clear resets name, key map, envelopes, vibrato, extension and all samples");
    check(io::encodeXi(document.instruments[0]) == ownerXi &&
          io::encodeXi(document.instruments[2]) == io::encodeXi(before.instruments[2]),
          "Clearing a middle instrument does not shift or change neighboring instruments");
    for (std::size_t p = 0; p < document.patterns.size(); ++p)
        check(same(document.patterns[p], before.patterns[p]),
              "Instrument and sample clear preserve every pattern instrument reference");

    edit::clearPatternSlot(document, 1);
    check(document.patterns.size() == 3 && document.patterns[1].rows == 7 &&
          document.patterns[1].cells.size() == 14, "Pattern clear preserves its slot and nonstandard row length");
    for (const auto& cell : document.patterns[1].cells)
        check(same(cell, Cell{}), "Pattern clear resets all five fields on every channel and row");
    check(document.orders == before.orders && same(document.patterns[0], before.patterns[0]) &&
          same(document.patterns[2], before.patterns[2]),
          "Clearing a shared pattern keeps repeated Order references and neighboring patterns intact");
    const auto encoded = io::encodeXm(document);
    const auto reloaded = io::decodeXm(encoded);
    check(io::encodeXm(reloaded) == encoded && reloaded.patterns[1].rows == 7 &&
          reloaded.instruments.size() == 3 && reloaded.instruments[0].samples.size() == 3,
          "XM round-trip retains cleared pattern length and empty numbered instrument and sample slots");

    auto delta = edit::DocumentDelta::between(before, document);
    check(!delta.empty(), "Clearing occupied slots creates an undoable document change");
    delta.undo(document);
    check(io::encodeXm(document) == original, "Undo restores cleared pattern cells, instrument metadata and PCM");
    delta.redo(document);
    check(io::encodeXm(document) == encoded, "Redo restores empty slots without renumbering references");
    const auto alreadyCleared = document;
    edit::clearPatternSlot(document, 1);
    edit::clearInstrumentSlot(document, 2);
    edit::clearSampleSlot(document, 1, 1);
    check(edit::DocumentDelta::between(alreadyCleared, document).empty(),
          "Clearing an already empty slot produces no further document change");

    auto tail = before;
    edit::clearPatternSlot(tail, 2);
    edit::clearInstrumentSlot(tail, 3);
    edit::clearSampleSlot(tail, 1, 2);
    check(tail.patterns.size() == 3 && tail.patterns[2].rows == 5 && tail.instruments.size() == 3 &&
          tail.instruments[0].samples.size() == 3 && tail.instruments[0].noteToSample.back() == 2,
          "Clearing final slots never trims vectors or changes a key mapped to the last sample");
}

void clearSlotValidation() {
    using namespace brick;
    auto document = makeDemoDocument();
    const auto before = document;
    const auto original = io::encodeXm(document);
    edit::clearPatternSlot(document, 255);
    edit::clearInstrumentSlot(document, 128);
    edit::clearSampleSlot(document, 1, 15);
    edit::clearSampleSlot(document, 128, 15);
    check(edit::DocumentDelta::between(before, document).empty() && io::encodeXm(document) == original,
          "Valid missing slots including maximum IDs are no-ops without allocating gaps");
    for (const int number : {-1, 256})
        rejected([&] { edit::clearPatternSlot(document, number); }, "Pattern clear rejects IDs outside 00..FF");
    for (const int number : {-1, 0, 129}) {
        rejected([&] { edit::clearInstrumentSlot(document, number); }, "Instrument clear rejects IDs outside 01..80");
        rejected([&] { edit::clearSampleSlot(document, number, 0); }, "Sample clear validates its owning instrument number");
    }
    for (const int number : {-1, 16}) {
        rejected([&] { edit::clearSampleSlot(document, 1, number); }, "Sample clear rejects IDs outside 00..0F");
        rejected([&] { edit::clearSampleSlot(document, 128, number); },
                 "A missing instrument does not hide an invalid sample number");
    }
    check(io::encodeXm(document) == original, "All invalid clear requests leave the document intact");
    document.patterns[0].cells.pop_back();
    const auto malformed = document.patterns[0];
    rejected([&] { edit::clearPatternSlot(document, 0); }, "Pattern clear rejects malformed storage before editing");
    check(same(document.patterns[0], malformed), "Rejected pattern clear preserves malformed storage without partial writes");
}
} // namespace

int main() {
    try {
        maskedPaste();
        partialPatternBlocks();
        selectedFieldAdjustments();
        patternTimingEdits();
        wholePatternReplacement();
        wholeSlots();
        emptySlotReplacement();
        clearSlotContents();
        clearSlotValidation();
        std::cout << "Tracker extras passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Tracker extras failure: " << error.what() << '\n';
        return 1;
    }
}
