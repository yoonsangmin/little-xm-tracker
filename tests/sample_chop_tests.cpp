#include "model/SampleChop.hpp"
#include "io/Codec.hpp"
#include "text/Utf8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class Fn> void reject(Fn fn, const char* message) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
brick::Sample sequence(std::size_t frames) {
    brick::Sample sample;
    for (std::size_t n = 0; n < frames; ++n) sample.pcm.push_back(static_cast<std::int16_t>(int(n) * 37 - 1000));
    return sample;
}
bool sameSample(const brick::Sample& a, const brick::Sample& b) {
    return a.name == b.name && a.pcm == b.pcm && a.sixteenBit == b.sixteenBit && a.volume == b.volume &&
        a.panning == b.panning && a.loopType == b.loopType && a.finetune == b.finetune &&
        a.relativeNote == b.relativeNote && a.loopStart == b.loopStart && a.loopLength == b.loopLength && a.reserved == b.reserved;
}
std::vector<std::uint8_t> sampleXi(const brick::Sample& sample) {
    brick::Instrument instrument; instrument.samples.push_back(sample);
    return brick::io::encodeXi(instrument);
}
bool sameEnvelope(const brick::Envelope& a, const brick::Envelope& b) {
    if (a.count != b.count || a.sustain != b.sustain || a.loopStart != b.loopStart || a.loopEnd != b.loopEnd || a.flags != b.flags) return false;
    for (std::size_t n = 0; n < a.points.size(); ++n)
        if (a.points[n].x != b.points[n].x || a.points[n].y != b.points[n].y) return false;
    return true;
}
void conserved(const brick::Sample& source, brick::edit::FrameRange range, const brick::edit::ChopResult& result) {
    check(result.boundaries.size() == result.slices.size() + 1 && result.boundaries.front() == range.first && result.boundaries.back() == range.last,
          "Chop boundaries include the exact selected endpoints");
    std::vector<std::int16_t> joined;
    for (std::size_t n = 0; n < result.slices.size(); ++n) {
        check(result.boundaries[n] < result.boundaries[n + 1], "Every chop slice is nonempty and strictly ordered");
        const std::vector<std::int16_t> expected(source.pcm.begin() + result.boundaries[n], source.pcm.begin() + result.boundaries[n + 1]);
        check(result.slices[n].pcm == expected, "Each slice contains its exact contiguous source PCM span");
        joined.insert(joined.end(), result.slices[n].pcm.begin(), result.slices[n].pcm.end());
    }
    check(joined == std::vector<std::int16_t>(source.pcm.begin() + range.first, source.pcm.begin() + range.last),
          "Joining chop slices conserves every selected frame without duplication or loss");
}

void equalDivisionAndTuning() {
    using namespace brick;
    auto source = sequence(35);
    source.name = "ORIGINAL"; source.volume = 37; source.panning = 205;
    source.relativeNote = 12; source.finetune = -43;
    source.loopType = 2; source.loopStart = 4; source.loopLength = 20; source.reserved = 7;
    const auto before = source;
    const auto sourceBytes = sampleXi(source);
    const auto chopped = edit::chopSample(source, {3,24}, {4,false,37});
    check(chopped.boundaries == std::vector<std::size_t>({3,8,13,18,24}), "Uneven selections use exact proportional equal divisions");
    check(chopped.firstNote == 37 && chopped.snappedBoundaries == 0, "Equal chop retains the chosen trigger-note start and reports no snaps");
    conserved(source, {3,24}, chopped);
    for (std::size_t n = 0; n < chopped.slices.size(); ++n) {
        const auto& slice = chopped.slices[n];
        const int trigger = chopped.firstNote + static_cast<int>(n);
        check(slice.sixteenBit == source.sixteenBit && slice.volume == source.volume && slice.panning == source.panning && slice.finetune == source.finetune,
              "Chop preserves PCM depth, volume, panning and finetune");
        check(int(slice.relativeNote) + trigger - 49 == source.relativeNote, "Each consecutive trigger key plays its slice at the source C-4 speed");
        check(slice.loopType == 0 && slice.loopStart == 0 && slice.loopLength == 0, "Chop clears loop mode and markers from each slice");
    }
    check(sameSample(source, before) && sampleXi(source) == sourceBytes, "Successful chop leaves source PCM, metadata and exact XI bytes unchanged");
    const auto one = edit::chopSample(source, {7,12}, {1,false,49});
    check(one.boundaries == std::vector<std::size_t>({7,12}) && one.slices[0].relativeNote == source.relativeNote,
          "A single C-4 slice preserves its selected PCM and original relative note");
    conserved(source, {7,12}, one);

    Sample eight; eight.name = "8 BIT"; eight.sixteenBit = false;
    eight.pcm = {-32768,-16384,-256,0,256,8192,16384,32512};
    const auto eightBefore = eight;
    const auto eightChop = edit::chopSample(eight, {0,8}, {3,false,49});
    conserved(eight, {0,8}, eightChop);
    for (const auto& slice : eightChop.slices)
        check(!slice.sixteenBit && std::all_of(slice.pcm.begin(), slice.pcm.end(), [](std::int16_t value){ return value % 256 == 0; }),
              "Eight-bit chop keeps canonical eight-bit PCM without conversion");
    check(sameSample(eight, eightBefore), "Eight-bit chop keeps the original sample unchanged");

    const auto sixteen = edit::chopSample(source, {5,21}, {16,false,81});
    check(sixteen.slices.size() == 16 && sixteen.firstNote + 15 == 96, "Sixteen one-frame slices can map through the final XM note");
    conserved(source, {5,21}, sixteen);

    auto named = source;named.name = "가나다라마바사";
    const auto namedChop = edit::chopSample(named, {0,32}, {2,false,49});
    check(namedChop.slices[0].name == "가나다라마바 01" && namedChop.slices[1].name == "가나다라마바 02" &&
          text::isValidUtf8(namedChop.slices[0].name) && namedChop.slices[0].name.size() <= 22,
          "Slice name suffixes preserve complete UTF-8 characters within the XM name limit");
    check(named.name == "가나다라마바사", "Naming slices does not truncate or rename the source sample");
}

void invalidRequests() {
    using namespace brick;
    auto source = sequence(32); const auto before = source; const auto sourceBytes = sampleXi(source);
    reject([&]{edit::chopSample(source, {0,32}, {0,false,49});}, "Zero slices must be rejected");
    reject([&]{edit::chopSample(source, {0,32}, {-1,false,49});}, "Negative slice counts must be rejected");
    reject([&]{edit::chopSample(source, {0,32}, {17,false,49});}, "Chop cannot exceed sixteen sample slots");
    reject([&]{edit::chopSample(source, {4,7}, {4,false,49});}, "A selection shorter than its slice count must be rejected");
    reject([&]{edit::chopSample(source, {5,5}, {1,false,49});}, "An empty selection must be rejected");
    reject([&]{edit::chopSample(source, {9,4}, {1,false,49});}, "A reversed selection must be rejected");
    reject([&]{edit::chopSample(source, {0,33}, {1,false,49});}, "A selection beyond the source PCM must be rejected");
    reject([&]{edit::chopSample(source, {33,34}, {1,false,49});}, "An out-of-range selection start must be rejected");
    reject([&]{edit::chopSample(source, {0,std::numeric_limits<std::size_t>::max()}, {1,false,49});}, "Extreme range bounds cannot overflow partition arithmetic");
    reject([&]{edit::chopSample(source, {0,32}, {1,false,0});}, "XM trigger note zero is not a playable note");
    reject([&]{edit::chopSample(source, {0,32}, {1,false,97});}, "Key-off is not a valid slice trigger");
    reject([&]{edit::chopSample(source, {0,32}, {4,false,94});}, "Consecutive trigger notes cannot extend beyond B-7");
    reject([&]{edit::chopSample(Sample{}, {0,0}, {1,false,49});}, "An empty source sample cannot be chopped");
    check(sameSample(source, before) && sampleXi(source) == sourceBytes, "Rejected requests preserve the original sample and its exact XI bytes");
    auto invalidSample = source; invalidSample.volume = 65;
    reject([&]{edit::chopSample(invalidSample, {0,32}, {1,false,49});}, "Chop cannot propagate a sample volume outside the XM range");
    invalidSample = source; invalidSample.sixteenBit = false;
    reject([&]{edit::chopSample(invalidSample, {0,32}, {1,false,49});}, "Invalid eight-bit PCM is rejected instead of silently requantized");
    source.relativeNote = -128; const auto lowBefore = source;
    reject([&]{edit::chopSample(source, {0,32}, {2,false,49});}, "Retuning the second slice cannot underflow relative note");
    check(sameSample(source, lowBefore), "Tuning underflow rejects the whole chop without source changes");
    source.relativeNote = 127; const auto highBefore = source;
    reject([&]{edit::chopSample(source, {0,32}, {1,false,48});}, "Retuning a low trigger cannot overflow relative note");
    check(sameSample(source, highBefore), "Tuning overflow rejects the whole chop without source changes");
}

void manualBoundariesAndXi() {
    using namespace brick;
    for(const bool sixteenBit:{false,true}) {
        auto source=sequence(35);source.name="MANUAL CHOP";
        source.sixteenBit=sixteenBit;source.volume=37;source.panning=205;
        source.relativeNote=12;source.finetune=-43;source.reserved=7;
        source.loopType=2;source.loopStart=4;source.loopLength=20;
        if(!sixteenBit)
            for(std::size_t n=0;n<source.pcm.size();++n)source.pcm[n]=static_cast<std::int16_t>((int(n)-17)*256);
        const auto before=source;const auto sourceBytes=sampleXi(source);
        const edit::FrameRange range{3,30};
        const auto automatic=edit::chopSample(source,range,{4,false,37});
        const auto unchanged=edit::chopSampleAtBoundaries(source,range,automatic.boundaries,37);
        for(std::size_t n=0;n<automatic.slices.size();++n)
            check(sameSample(automatic.slices[n],unchanged.slices[n]),"Applying an unchanged boundary draft retains automatic slice metadata and PCM");

        auto boundaries=automatic.boundaries;
        boundaries[1]=11;
        const auto moved=edit::chopSampleAtBoundaries(source,range,boundaries,37);
        check(moved.boundaries==std::vector<std::size_t>({3,11,16,23,30}),"Moving a manual cut uses its exact frame without snapping");
        boundaries.insert(boundaries.begin()+3,20);
        const auto added=edit::chopSampleAtBoundaries(source,range,boundaries,37);
        check(added.slices.size()==5&&added.boundaries[3]==20,"Adding a boundary splits its containing slice");
        boundaries.erase(boundaries.begin()+2);
        const auto merged=edit::chopSampleAtBoundaries(source,range,boundaries,37);
        check(merged.slices.size()==4&&merged.slices[1].pcm==std::vector<std::int16_t>(source.pcm.begin()+11,source.pcm.begin()+20),
              "Deleting a boundary merges the adjacent PCM spans without removing either side");
        for(const auto* result:{&moved,&added,&merged}) {
            conserved(source,range,*result);
            check(result->firstNote==37&&result->snappedBoundaries==0,"Manual boundaries keep the trigger start and claim no automatic snaps");
            for(std::size_t n=0;n<result->slices.size();++n) {
                const auto& slice=result->slices[n];
                check(slice.sixteenBit==source.sixteenBit&&slice.volume==source.volume&&slice.panning==source.panning&&
                      slice.finetune==source.finetune&&slice.reserved==source.reserved,
                      "Manual slicing preserves depth, volume, panning, finetune and reserved metadata");
                check(int(slice.relativeNote)+result->firstNote+int(n)-49==source.relativeNote,
                      "Added and merged slices compensate their newly consecutive trigger notes");
                check(slice.loopType==0&&slice.loopStart==0&&slice.loopLength==0,"Manual slices clear inherited loop markers");
            }
        }
        const auto single=edit::chopSampleAtBoundaries(source,range,{3,30});
        check(single.firstNote==49&&single.slices.size()==1&&single.slices[0].relativeNote==source.relativeNote,
              "Two endpoints alone create one non-resampled slice at the default C-4 trigger");
        conserved(source,range,single);

        Instrument target;target.name="KEEP";target.samples.push_back(sequence(3));
        const auto targetBytes=io::encodeXi(target);
        const auto placed=edit::placeChop(target,added);
        check(placed.slots==std::vector<int>({1,2,3,4,5}),"Manual slices use the existing vacancy-based placement path");
        const auto placedBytes=io::encodeXi(placed.instrument);
        const auto reopened=io::decodeXi(placedBytes);
        check(io::encodeXi(reopened)==placedBytes,"Manual slice placement preserves exact XI data through save and reload");
        for(std::size_t n=0;n<added.slices.size();++n)
            check(reopened.noteToSample[36+n]==std::uint8_t(placed.slots[n])&&
                  sameSample(reopened.samples[std::size_t(placed.slots[n])],added.slices[n]),
                  "Reopened XI keeps manual slice PCM, metadata and trigger mappings");
        check(io::encodeXi(target)==targetBytes&&sameSample(source,before)&&sampleXi(source)==sourceBytes,
              "Moving, adding, merging and placing cuts never mutate the source sample or target instrument");
    }
}

void invalidManualBoundaries() {
    using namespace brick;
    auto source=sequence(32);const auto before=source;const auto sourceBytes=sampleXi(source);
    for(const auto& boundaries:std::vector<std::vector<std::size_t>>{
            {},{0},{1,16,32},{0,16,31},{0,16,16,32},{0,20,12,32},
            {0,33,32},{0,std::numeric_limits<std::size_t>::max(),32}})
        reject([&]{edit::chopSampleAtBoundaries(source,{0,32},boundaries);},
               "Incomplete, duplicate, reversed or out-of-range manual boundaries must be rejected");
    std::vector<std::size_t> sixteen;
    for(std::size_t n=0;n<=16;++n)sixteen.push_back(n*2);
    const auto maximum=edit::chopSampleAtBoundaries(source,{0,32},sixteen,81);
    check(maximum.slices.size()==16&&maximum.firstNote==81,"Sixteen manual slices can occupy the final sixteen XM notes");
    conserved(source,{0,32},maximum);
    auto seventeen=sixteen;seventeen.insert(seventeen.begin()+1,1);
    reject([&]{edit::chopSampleAtBoundaries(source,{0,32},seventeen);},"A seventeenth manual slice exceeds XI capacity");
    for(const edit::FrameRange range:std::array<edit::FrameRange,4>{{{5,5},{9,4},{0,33},{0,std::numeric_limits<std::size_t>::max()}}})
        reject([&]{edit::chopSampleAtBoundaries(source,range,{range.first,range.last});},"Invalid selected ranges cannot be hidden by matching endpoints");
    for(const int note:{0,97,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
        reject([&]{edit::chopSampleAtBoundaries(source,{0,32},{0,16,32},note);},"Manual slices require a valid playable first trigger note");
    reject([&]{edit::chopSampleAtBoundaries(source,{0,32},{0,8,16,24,32},94);},"Manual trigger mappings cannot cross B-7");
    reject([&]{edit::chopSampleAtBoundaries(Sample{},{0,0},{0,0});},"An empty source cannot form a manual slice");
    auto invalid=source;invalid.volume=65;
    reject([&]{edit::chopSampleAtBoundaries(invalid,{0,32},{0,16,32});},"Manual slicing rejects non-XM volume");
    invalid=source;invalid.sixteenBit=false;
    reject([&]{edit::chopSampleAtBoundaries(invalid,{0,32},{0,16,32});},"Manual slicing never silently quantizes invalid eight-bit PCM");
    invalid=source;invalid.relativeNote=-128;
    reject([&]{edit::chopSampleAtBoundaries(invalid,{0,32},{0,16,32});},"Added trigger tuning cannot underflow relative note");
    invalid=source;invalid.relativeNote=127;
    reject([&]{edit::chopSampleAtBoundaries(invalid,{0,32},{0,32},48);},"Manual trigger tuning cannot overflow relative note");
    check(sameSample(source,before)&&sampleXi(source)==sourceBytes,"Rejected manual boundary edits preserve the source and its exact XI bytes");
}

void onsetSnapping() {
    using namespace brick;
    Sample attacks; attacks.pcm.resize(1600);
    const std::array<std::size_t,3> onsets = {410,790,1215};
    for (const auto onset : onsets)
        for (std::size_t n = 0; n < 80; ++n) attacks.pcm[onset + n] = n % 2 ? -16000 : 16000;
    const auto before = attacks;
    const auto sourceBytes = sampleXi(attacks);
    const auto equal = edit::chopSample(attacks, {0,1600}, {4,false,49});
    check(equal.boundaries == std::vector<std::size_t>({0,400,800,1200,1600}) && equal.snappedBoundaries == 0,
          "Disabled onset snapping keeps equal divisions even beside strong attacks");
    const auto snapped = edit::chopSample(attacks, {0,1600}, {4,true,49});
    check(snapped.snappedBoundaries == 3, "Clear attacks close to each grid boundary are detected");
    for (std::size_t n = 0; n < onsets.size(); ++n)
        check(std::abs(static_cast<long long>(snapped.boundaries[n + 1]) - static_cast<long long>(onsets[n])) <= 4,
              "Snapping follows each attack start rather than a later waveform amplitude peak");
    conserved(attacks, {0,1600}, snapped);
    const auto selected = edit::chopSample(attacks, {100,1500}, {4,true,49});
    conserved(attacks, {100,1500}, selected);
    check(sameSample(attacks, before) && sampleXi(attacks) == sourceBytes, "Onset analysis and slicing leave their input and exact XI bytes unchanged");

    Sample steady; steady.pcm.assign(1600,12000);
    auto noOnset = edit::chopSample(steady, {0,1600}, {4,true,49});
    check(noOnset.boundaries == equal.boundaries && noOnset.snappedBoundaries == 0, "A constant level contains no attack and keeps the exact grid");
    for (std::size_t n = 0; n < steady.pcm.size(); ++n)
        steady.pcm[n] = static_cast<std::int16_t>(std::lround(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * double(n) / 8363.0)));
    noOnset = edit::chopSample(steady, {0,1600}, {4,true,49});
    check(noOnset.boundaries == equal.boundaries && noOnset.snappedBoundaries == 0, "Stationary tone peaks are not mistaken for transient onsets");
    steady.pcm.assign(1600,0);
    noOnset = edit::chopSample(steady, {0,1600}, {4,true,49});
    check(noOnset.boundaries == equal.boundaries && noOnset.snappedBoundaries == 0, "Silence keeps equal grid boundaries");

    Sample distant; distant.pcm.resize(1600);
    for (std::size_t onset : {200u,600u,1000u,1400u})
        for (std::size_t n = 0; n < 80; ++n) distant.pcm[onset + n] = n % 2 ? -16000 : 16000;
    const auto bounded = edit::chopSample(distant, {0,1600}, {4,true,49});
    check(bounded.boundaries == equal.boundaries && bounded.snappedBoundaries == 0, "Onsets outside a quarter-cell neighbourhood cannot pull the grid");
    distant.pcm.assign(12000,0);
    for (std::size_t n = 0; n < 80; ++n) distant.pcm[6400 + n] = n % 2 ? -16000 : 16000;
    const auto timeBounded = edit::chopSample(distant, {0,12000}, {2,true,49});
    check(timeBounded.boundaries == std::vector<std::size_t>({0,6000,12000}) && timeBounded.snappedBoundaries == 0,
          "The millisecond snap limit also bounds very large cells");
}

void placementAndXi() {
    using namespace brick;
    auto source = sequence(16); source.name = "CHOP SOURCE";
    const auto chop = edit::chopSample(source, {0,16}, {4,false,49});
    Instrument target; target.name = "KEEP INSTRUMENT"; target.samples.resize(5);
    for (std::size_t slot : {0u,2u,4u}) {
        target.samples[slot] = sequence(slot + 2); target.samples[slot].name = "KEEP " + std::to_string(slot);
    }
    target.samples[1].name = "EMPTY METADATA"; target.samples[3].volume = 12;
    target.noteToSample.fill(2);
    target.volumeEnvelope.count = 2; target.volumeEnvelope.points[0] = {0,64}; target.volumeEnvelope.points[1] = {12,0};
    target.volumeEnvelope.flags = 7; target.volumeEnvelope.loopEnd = 1;
    target.panningEnvelope.count = 2; target.panningEnvelope.points[0] = {0,32}; target.panningEnvelope.points[1] = {15,40};
    target.panningEnvelope.flags = 1;
    target.vibratoType = 2; target.vibratoSweep = 20; target.vibratoDepth = 6; target.vibratoRate = 14;
    target.fadeout = 1234; target.extension[0] = 1; target.extension[5] = 77;
    const auto targetBytes = io::encodeXi(target);
    check(edit::emptySampleSlots(target) == 13, "Empty PCM slots and absent slots both count toward the sixteen-slot capacity");
    const auto placed = edit::placeChop(target, chop);
    check(!placed.replaced && placed.slots == std::vector<int>({1,3,5,6}) && placed.instrument.samples.size() == 7,
          "Current-instrument chop fills existing holes before extending into omitted slots");
    check(placed.instrument.name == target.name && sameEnvelope(placed.instrument.volumeEnvelope, target.volumeEnvelope) &&
          sameEnvelope(placed.instrument.panningEnvelope, target.panningEnvelope) && placed.instrument.vibratoType == target.vibratoType &&
          placed.instrument.vibratoSweep == target.vibratoSweep && placed.instrument.vibratoDepth == target.vibratoDepth &&
          placed.instrument.vibratoRate == target.vibratoRate && placed.instrument.fadeout == target.fadeout && placed.instrument.extension == target.extension,
          "Filling holes preserves instrument name, envelopes, vibrato, fadeout and extension metadata");
    for (std::size_t slot : {0u,2u,4u})
        check(sameSample(placed.instrument.samples[slot], target.samples[slot]), "Existing nonempty sample slots remain intact");
    for (std::size_t n = 0; n < chop.slices.size(); ++n)
        check(sameSample(placed.instrument.samples[std::size_t(placed.slots[n])], chop.slices[n]), "Each destination slot receives its complete corresponding slice");
    for (std::size_t note = 0; note < 96; ++note) {
        const auto expected = note >= 48 && note < 52 ? std::uint8_t(placed.slots[note - 48]) : target.noteToSample[note];
        check(placed.instrument.noteToSample[note] == expected, "Only the consecutive slice-trigger key mappings are replaced");
    }
    check(io::encodeXi(target) == targetBytes, "Placement does not mutate its source instrument");
    const auto placedBytes = io::encodeXi(placed.instrument);
    check(io::encodeXi(io::decodeXi(placedBytes)) == placedBytes, "Filled-hole placement retains sliced PCM, tuning, maps and metadata through XI round trip");

    Instrument crowded = target; crowded.samples.resize(16);
    for (auto& sample : crowded.samples) if (sample.pcm.empty()) sample = sequence(2);
    crowded.samples[15].pcm.clear();
    const auto crowdedBytes = io::encodeXi(crowded);
    check(edit::emptySampleSlots(crowded) == 1, "A nearly full instrument reports its actual remaining capacity");
    reject([&]{edit::placeChop(crowded, chop);}, "Insufficient free slots require explicit whole-instrument replacement");
    check(io::encodeXi(crowded) == crowdedBytes, "Failed placement is transactional and keeps all original slots and metadata");
    const auto replacement = edit::placeChop(crowded, chop, true);
    check(replacement.replaced && replacement.slots == std::vector<int>({0,1,2,3}) && replacement.instrument.samples.size() == 4,
          "Explicit replacement starts a fresh instrument with consecutive slice slots");
    const Instrument defaults;
    check(replacement.instrument.name == defaults.name && sameEnvelope(replacement.instrument.volumeEnvelope, defaults.volumeEnvelope) &&
          sameEnvelope(replacement.instrument.panningEnvelope, defaults.panningEnvelope) && replacement.instrument.vibratoType == 0 &&
          replacement.instrument.vibratoSweep == 0 && replacement.instrument.vibratoDepth == 0 && replacement.instrument.vibratoRate == 0 &&
          replacement.instrument.fadeout == 0 && replacement.instrument.extension == defaults.extension,
          "Whole-instrument replacement resets authored instrument settings to fresh defaults");
    for (std::size_t note = 0; note < 96; ++note)
        check(replacement.instrument.noteToSample[note] == std::uint8_t(note >= 48 && note < 52 ? note - 48 : 0),
              "Replacement maps trigger notes and leaves all other keys at default slot zero");
    check(io::encodeXi(crowded) == crowdedBytes, "Successful explicit replacement still leaves its input instrument untouched");
    const auto replacementBytes = io::encodeXi(replacement.instrument);
    check(io::encodeXi(io::decodeXi(replacementBytes)) == replacementBytes, "Replacement slices survive XI round trip");

    const auto fresh = edit::placeChop(Instrument{}, chop);
    check(!fresh.replaced && fresh.slots == std::vector<int>({0,1,2,3}) && fresh.instrument.samples.size() == 4,
          "New-instrument placement uses the same non-destructive API with fresh defaults");
    const auto sixteen = edit::chopSample(source, {0,16}, {16,false,49});
    const auto full = edit::placeChop(Instrument{}, sixteen);
    check(full.instrument.samples.size() == 16 && full.slots.back() == 15 && edit::emptySampleSlots(full.instrument) == 0,
          "All sixteen slice slots can be filled without exceeding XI capacity");
    const auto fullBytes = io::encodeXi(full.instrument);
    check(io::encodeXi(io::decodeXi(fullBytes)) == fullBytes, "A sixteen-slice instrument survives XI round trip");
    auto malformed = chop; malformed.boundaries[1] = malformed.boundaries[0];
    reject([&]{edit::placeChop(target, malformed);}, "Malformed empty slice boundaries are rejected before placement");
    malformed = chop; malformed.slices[0].pcm.pop_back();
    reject([&]{edit::placeChop(target, malformed);}, "Slice PCM must match its declared source span before placement");
    malformed = chop; malformed.firstNote = 96;
    reject([&]{edit::placeChop(target, malformed);}, "Placement validates the complete trigger-note range");
    check(io::encodeXi(target) == targetBytes, "Malformed chop results cannot change the destination instrument");
}
}

int main() {
    try {
        equalDivisionAndTuning(); invalidRequests(); manualBoundariesAndXi(); invalidManualBoundaries(); onsetSnapping(); placementAndXi();
        std::cout << "Sample chop passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
