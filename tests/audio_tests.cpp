#include "audio/Playback.hpp"
#include "io/Codec.hpp"
#include "model/Document.hpp"
#include "model/EditOperations.hpp"
#include "model/SampleAnalysis.hpp"
#include "model/SampleChop.hpp"
#include "model/SampleTools.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

brick::Document fixture() {
    brick::Document document;
    document.name = "Audio regression";
    document.channels = 2;
    document.orders = {0};
    brick::Pattern pattern;
    pattern.rows = 8;
    pattern.cells.resize(pattern.rows * document.channels);
    document.patterns.push_back(pattern);
    brick::Instrument instrument;
    instrument.name = "Audition";
    brick::Sample sine;
    sine.name = "Mapped";
    sine.loopType = 1;
    sine.loopLength = 96;
    for (int i = 0; i < 96; ++i)
        sine.pcm.push_back(static_cast<std::int16_t>(std::sin(i * 6.283185307179586 / 96.0) * 12000.0));
    instrument.samples.push_back(sine);
    brick::Sample unmapped;
    unmapped.name = "Unmapped";
    for (int i = 0; i < 256; ++i) unmapped.pcm.push_back(i % 32 < 16 ? 10000 : -10000);
    instrument.samples.push_back(unmapped);
    document.instruments.push_back(instrument);
    return document;
}

std::vector<std::int16_t> read(brick::Playback& playback, std::size_t frames = 4800) {
    std::vector<std::int16_t> result(frames * 2);
    playback.readFrames(result.data(), frames);
    return result;
}
bool audible(const std::vector<std::int16_t>& pcm) {
    return std::any_of(pcm.begin(), pcm.end(), [](auto value) { return value != 0; });
}

std::array<float, 2> stereoPeaks(const std::vector<std::int16_t>& pcm) {
    std::array<int, 2> peaks{};
    for (std::size_t i = 0; i < pcm.size(); ++i)
        peaks[i % 2] = std::max(peaks[i % 2], std::abs(static_cast<int>(pcm[i])));
    return {static_cast<float>(peaks[0]) / 32768.0f, static_cast<float>(peaks[1]) / 32768.0f};
}

bool emptyMeter(const brick::OutputMeter& meter) {
    return meter.peak == std::array<float, 2>{} && meter.clipped == std::array<bool, 2>{};
}

void previewActivity() {
    using namespace brick;
    auto document = fixture();
    document.patterns[0].at(0, 1, document.channels) = {49, 1, 0x50, 0, 0};
    Playback player, reference;
    require(!player.previewPlaying(), "An unloaded player has no active preview");
    player.load(document);reference.load(document);
    require(!player.previewPlaying(), "An idle document has no active preview");
    player.playSong();read(player, 2400);
    require(player.status().playing && !player.previewPlaying(), "Song voices must not count as preview playback");
    player.previewSample(1, 0);
    require(player.previewPlaying() && player.status().playing, "A raw preview is visible independently of a running song");
    player.stopPreview();
    require(!player.previewPlaying() && player.status().playing, "Stopping a preview must leave the song transport running");
    player.stop();

    player.load(document);reference.load(document);
    player.previewNote(1, 49);reference.previewNote(1, 49);
    require(player.previewPlaying() && !player.status().playing,
            "An instrument preview is active immediately without starting the song");
    for (const auto frames : {1u, 191u, 2400u}) {
        require(player.previewPlaying(), "Looped instrument audition remains active");
        require(read(player, frames) == read(reference, frames), "Preview activity queries cannot change PCM or timing");
    }
    player.stopPreview();
    require(!player.previewPlaying(), "Stopping instrument audition clears preview activity");

    player.previewRow(0, 0);
    require(player.previewPlaying(), "A row preview awaiting its first tick counts as active");
    read(player, 12000);
    require(player.previewPlaying(), "A row preview with only a higher-channel sustained voice remains active");
    player.stopPreview();
    require(!player.previewPlaying(), "Stopping row audition clears every preview channel");

    auto oneShot = document;
    oneShot.instruments[0].noteToSample.fill(1);
    player.load(oneShot);player.previewNote(1, 49);
    require(player.previewPlaying(), "A new one-shot instrument audition is active");
    read(player, 9600);
    require(!player.previewPlaying(), "A naturally finished instrument one-shot allows a fresh Start");
    player.previewNote(1, 49);
    require(player.previewPlaying(), "An ended instrument audition can be triggered again");
    player.stopPreview();player.previewSample(1, 1);
    require(player.previewPlaying(), "A raw one-shot preview is active before mixing");
    read(player, 4800);
    require(!player.previewPlaying(), "A naturally finished raw one-shot is no longer active");
    player.previewSample(oneShot.instruments[0].samples[1]);
    require(player.previewPlaying(), "Unsaved draft previews count as preview playback");
    read(player, 4800);
    require(!player.previewPlaying(), "A finished unsaved draft no longer counts as active");
    player.previewRow(0, 0);read(player, 20000);
    require(!player.previewPlaying(), "A completed row preview with no remaining voices becomes inactive");

    player.load(document);player.previewSample(1, 0);player.stop();
    require(!player.previewPlaying(), "Stop clears all preview activity");
    player.previewNote(1, 49);player.load(document);
    require(!player.previewPlaying(), "Document reload clears all preview activity");
    player.previewSample(1, 0);player.configure({44100, Interpolation::Linear});
    require(!player.previewPlaying(), "Changing output settings clears all preview activity");

    // Compatible updates preserve the sounding preview snapshot. PCM edits
    // apply to its next trigger, while structural updates replace the core.
    player.configure({});player.load(document);reference.load(document);
    player.previewSample(1, 0);reference.previewSample(1, 0);
    require(read(player, 1920) == read(reference, 1920), "Live preview edit fixtures begin identically");
    auto edited = document;edited.instruments[0].name = "Renamed preview";
    require(player.updateDocument(edited) == DocumentUpdateResult::Preserved && player.previewPlaying(),
            "Compatible instrument edits retain active preview playback");
    edited.instruments[0].samples[0].pcm.assign(96, 0);
    require(player.updateDocument(edited) == DocumentUpdateResult::Preserved && player.previewPlaying(),
            "Replacing PCM retains the currently sounding preview until retrigger");
    require(read(player, 1920) == read(reference, 1920), "A preserved raw voice still uses its original PCM");
    player.previewSample(1, 0);
    require(player.previewPlaying() && !audible(read(player, 1920)),
            "Retriggering applies new PCM; a silent loop is still an active preview");
    edit::resizeChannels(edited, 3);
    require(player.updateDocument(edited) == DocumentUpdateResult::IdleReloaded && !player.previewPlaying(),
            "Structural updates end previews without claiming a song restart");
}

void instrumentEnvelopeCompletion() {
    using namespace brick;
    for (const int rate : {44100, 48000}) {
        auto document = fixture();
        auto& env = document.instruments[0].volumeEnvelope;
        env.flags = 1; env.count = 2;
        env.points[0] = {0, 64}; env.points[1] = {4, 0};
        Playback player({rate, Interpolation::Linear});
        player.load(document);player.previewNote(1, 49);
        require(audible(read(player, rate / 25)) && player.previewPlaying(),
                "An instrument envelope must remain active while its decay is audible");
        read(player, rate / 4);
        require(!player.previewPlaying() && !audible(read(player, rate / 20)),
                "A looped sample preview finishes automatically after its volume envelope ends at zero");
        player.previewNote(1, 49);
        require(player.previewPlaying() && audible(read(player, rate / 25)),
                "A naturally completed envelope can restart on the next Start without an extra stop");

        // An initial rest and a long middle rest must both survive until the
        // later attack. A silence timeout would cut this envelope too early.
        env.count = 7;
        env.points[0] = {0, 0}; env.points[1] = {4, 0}; env.points[2] = {6, 64};
        env.points[3] = {8, 0}; env.points[4] = {20, 0}; env.points[5] = {24, 64};
        env.points[6] = {28, 0};
        player.load(document);player.previewNote(1, 49);
        require(!audible(read(player, rate / 20)) && player.previewPlaying(),
                "A silent attack does not end an instrument audition");
        require(audible(read(player, rate / 5)), "The first attack still sounds after its silent start");
        require(!audible(read(player, rate / 10)) && player.previewPlaying(),
                "A middle-envelope rest remains active");
        require(audible(read(player, rate * 3 / 20)) && player.previewPlaying(),
                "The envelope can become audible again after a long rest");
        read(player, rate / 5);
        require(!player.previewPlaying(), "The later final zero still completes the instrument audition");

        env.flags = 5; env.count = 2; env.loopStart = 0; env.loopEnd = 1;
        env.points[0] = {0, 64}; env.points[1] = {4, 0};
        player.load(document);player.previewNote(1, 49);read(player, rate);
        require(player.previewPlaying() && audible(read(player, rate / 5)),
                "An envelope loop ending at zero continues through later audible passes");

        env.flags = 3; env.count = 4; env.sustain = 1;
        env.points[0] = {0, 64}; env.points[1] = {2, 0};
        env.points[2] = {4, 64}; env.points[3] = {8, 0};
        player.load(document);player.previewNote(1, 49);read(player, rate / 2);
        require(player.previewPlaying() && !audible(read(player, rate / 10)),
                "A silent sustain holds its note rather than being mistaken for envelope completion");

        env.flags = 0;
        player.load(document);player.previewNote(1, 49);read(player, rate / 2);
        require(player.previewPlaying() && audible(read(player, rate / 10)),
                "A disabled volume envelope cannot end an instrument audition");

        env.flags = 1; env.count = 2;
        env.points[0] = {0, 64}; env.points[1] = {4, 0};
        document.instruments.push_back(document.instruments[0]);
        document.instruments[1].volumeEnvelope.flags = 0;
        document.patterns[0].at(0, 1, document.channels) = {49, 2, 0x50, 0, 0};
        player.load(document);player.playSong();player.previewNote(1, 49);read(player, rate / 4);
        require(!player.previewPlaying() && player.status().playing && audible(read(player, rate / 10)),
                "Completing an instrument envelope leaves independent song playback sounding");

        document.patterns[0].at(0, 0, document.channels) = {49, 1, 0x50, 0, 0};
        player.load(document);player.previewRow(0, 0);read(player, rate / 4);
        require(player.previewPlaying() && audible(read(player, rate / 10)),
                "A row audition retains other sustained voices after one instrument decays");

        auto raw = document.instruments[0].samples[0];raw.volume = 0;
        player.load(document);player.previewSample(raw);read(player, rate / 4);
        require(player.previewPlaying() && !audible(read(player, rate / 10)),
                "Silent raw waveform previews stay active for live Draw edits");
    }
}

void songMonitorEnvelopeCompletion() {
    using namespace brick;
    for (const int rate : {44100, 48000}) {
        auto document = fixture();
        auto& pattern = document.patterns[0];
        pattern.at(0, 0, document.channels) = {49, 1, 0x50, 0, 0};
        auto& env = document.instruments[0].volumeEnvelope;
        env.flags = 1; env.count = 2;
        env.points[0] = {0, 64}; env.points[1] = {4, 0};
        Playback player({rate, Interpolation::Linear}), reference({rate, Interpolation::Linear});
        auto start = [&] {
            player.load(document); reference.load(document);
            player.playSong(); reference.playSong();
        };
        auto pull = [&](std::size_t frames) {
            const auto pcm = read(player, frames);
            require(pcm == read(reference, frames), "Song monitor queries never change rendered PCM or its timing");
            return pcm;
        };
        auto voice = [&] { return player.monitor().channels[0]; };
        auto ended = [&] {
            const auto state = voice();
            return !state.active && state.note == 0 && state.peak == 0 &&
                std::all_of(state.waveform.begin(), state.waveform.end(), [](auto value) { return value == 0; });
        };
        start();
        require(audible(pull(rate / 25)) && voice().active && voice().note == 49,
                "Song monitor retains the note while its volume envelope is audibly decaying");
        pull(rate / 4);
        require(ended() && player.status().playing,
                "A terminal-zero envelope clears the monitor note and waveform without stopping the song");
        const auto before = player.status();
        for (int n = 0; n < 50; ++n) require(ended(), "A finished envelope stays absent across monitor reads");
        require(player.status().row == before.row && player.status().elapsedSeconds == before.elapsedSeconds &&
                !audible(pull(rate / 20)), "Reading a finished voice neither advances transport nor restores its sound");

        // Clearing a display must not destroy the live core voice or its note:
        // L00 can move the same envelope back to its attack before a new note.
        pattern.at(4, 0, document.channels) = {0, 0, 0, 0x15, 0};
        pattern.at(6, 0, document.channels) = {53, 1, 0x50, 0, 0};
        start();pull(rate / 4);require(ended(), "Envelope reaches silence before the later L00 command");
        // Read inside the revived attack, not exactly on its tick boundary:
        // output packets at 44.1 and 48 kHz can straddle that boundary differently.
        require(audible(pull(rate * 29 / 100)) && voice().active && voice().note == 49,
                "L00 revives a completed envelope with its original monitor note");
        pull(rate * 11 / 100);require(ended(), "The repositioned envelope can finish again");
        require(audible(pull(rate * 13 / 100)) && voice().active && voice().note == 53,
                "A later note replaces the completed monitor voice and starts a fresh envelope");
        pattern.at(4, 0, document.channels) = {};
        pattern.at(6, 0, document.channels) = {};

        env.count = 7;
        env.points[0] = {0, 0}; env.points[1] = {4, 0}; env.points[2] = {6, 64};
        env.points[3] = {8, 0}; env.points[4] = {20, 0}; env.points[5] = {24, 64}; env.points[6] = {28, 0};
        start();
        require(!audible(pull(rate / 20)) && voice().active && voice().note == 49,
                "An initial silent envelope section is not a completed monitor voice");
        require(audible(pull(rate / 5)), "The song envelope attacks after its initial silence");
        require(!audible(pull(rate / 10)) && voice().active,
                "An intermediate envelope rest retains the monitor voice for its later attack");
        require(audible(pull(rate * 3 / 20)) && voice().active,
                "The same monitored voice resumes after an intermediate envelope rest");
        pull(rate / 5);require(ended(), "Only the final zero clears the monitor after both attacks");

        env.flags = 5; env.count = 2; env.loopStart = 0; env.loopEnd = 1;
        env.points[0] = {0, 64}; env.points[1] = {4, 0};
        start();pull(rate / 2);
        require(voice().active && voice().note == 49 && audible(pull(rate / 5)),
                "An envelope loop ending at zero retains its monitor note and repeats audibly");

        env.flags = 3; env.count = 4; env.sustain = 1;
        env.points[0] = {0, 64}; env.points[1] = {2, 0}; env.points[2] = {4, 64}; env.points[3] = {8, 0};
        start();pull(rate / 2);
        require(voice().active && voice().note == 49 && !audible(pull(rate / 10)),
                "A silent sustain keeps its monitor identity until key-off releases it");
        pattern.at(2, 0, document.channels) = {97, 0, 0, 0, 0};
        start();pull(rate / 5);
        require(voice().active && audible(pull(rate * 13 / 100)),
                "Key-off can release a silent sustain into its later audible envelope points");
        pull(rate / 5);require(ended(), "A released sustain clears the monitor after its final zero");
        pattern.at(2, 0, document.channels) = {};

        env.flags = 1; env.count = 2; env.points[0] = {0, 64}; env.points[1] = {4, 24};
        start();pull(rate / 2);
        require(voice().active && audible(pull(rate / 10)), "A completed nonzero envelope remains audible and monitored");
        env.flags = 0; env.points[1] = {4, 0};
        start();pull(rate / 2);
        require(voice().active && audible(pull(rate / 10)), "A disabled envelope does not end the monitor voice");
    }
}

void instrumentPreviewPlayheads() {
    using namespace brick;
    const auto inactive = [](const InstrumentPreviewStatus& value) {
        return !value.active && !value.instrument && value.volumeTick == -1 && value.panningTick == -1;
    };
    const auto same = [](const InstrumentPreviewStatus& a, const InstrumentPreviewStatus& b) {
        return a.active == b.active && a.instrument == b.instrument &&
            a.volumeTick == b.volumeTick && a.panningTick == b.panningTick;
    };
    for (const int rate : {44100, 48000}) {
        auto document = fixture();
        document.instruments.reserve(2);
        auto& volume = document.instruments[0].volumeEnvelope;
        volume.flags = 3; volume.count = 3; volume.sustain = 1;
        volume.points[0] = {0, 64}; volume.points[1] = {3, 32}; volume.points[2] = {9, 48};
        auto& pan = document.instruments[0].panningEnvelope;
        pan.flags = 5; pan.count = 3; pan.loopStart = 1; pan.loopEnd = 2;
        pan.points[0] = {0, 32}; pan.points[1] = {2, 0}; pan.points[2] = {6, 64};
        document.instruments.push_back(document.instruments[0]);
        document.instruments[1].volumeEnvelope.flags = 0;
        document.instruments[1].panningEnvelope.flags = 0;
        document.patterns[0].at(0, 0, document.channels) = {49, 1, 0x50, 0, 0};
        Playback player({rate, Interpolation::Linear}), reference({rate, Interpolation::Linear});
        require(inactive(player.instrumentPreviewStatus()), "Unloaded instrument playheads are inactive");
        player.load(document); reference.load(document);
        require(inactive(player.instrumentPreviewStatus()), "Loaded idle voices have no instrument playhead");
        player.previewNote(1,49); reference.previewNote(1,49);
        auto state = player.instrumentPreviewStatus();
        require(state.active && state.instrument == 1 && state.volumeTick == 0 && state.panningTick == 0,
                "Fresh instrument playheads start at actual trigger tick zero");
        bool progressed = false, wrapped = false;
        int previousPan = 0;
        for (int packet = 0; packet < 160; ++packet) {
            const auto before = player.instrumentPreviewStatus();
            require(same(before, player.instrumentPreviewStatus()), "Reading envelope state does not advance audio clocks");
            require(read(player, std::size_t(rate / 250)) == read(reference, std::size_t(rate / 250)),
                    "Observing envelope playheads leaves exact PCM and timing unchanged");
            state = player.instrumentPreviewStatus();
            require(state.active && state.instrument == 1 && state.volumeTick >= 0 && state.volumeTick <= 3,
                    "Volume playhead follows the real sustain point without advancing beyond it");
            require(state.panningTick >= 0 && state.panningTick < 6,
                    "Panning playhead follows the actual loop without an early end-point jump");
            progressed |= state.volumeTick > 0;
            wrapped |= state.panningTick < previousPan;
            previousPan = state.panningTick;
        }
        require(progressed && wrapped && state.volumeTick == 3,
                "Independent volume sustain and panning loop clocks progress at both output rates");
        player.previewNote(1,61);
        state = player.instrumentPreviewStatus();
        require(state.active && state.volumeTick == 0 && state.panningTick == 0,
                "Retriggering a note resets both envelope positions immediately");
        player.previewNote(2,49); state = player.instrumentPreviewStatus();
        require(state.active && state.instrument == 2 && state.volumeTick == -1 && state.panningTick == -1,
                "Switching preview instruments changes identity and hides disabled envelopes");
        player.stopPreview(); require(inactive(player.instrumentPreviewStatus()), "Stop preview clears the envelope playhead immediately");
        player.previewNote(1,49); player.stop();
        require(inactive(player.instrumentPreviewStatus()), "Global stop clears instrument envelope playheads");
        player.previewNote(1,49); player.previewSample(1,0);
        require(inactive(player.instrumentPreviewStatus()), "Raw sample preview cannot inherit an instrument envelope playhead");
        player.stopPreview(); player.previewNote(1,49); player.previewSample(document.instruments[0].samples[0]);
        require(inactive(player.instrumentPreviewStatus()), "Unsaved raw drafts do not expose an instrument envelope playhead");
        player.stopPreview(); player.previewNote(1,49); player.previewRow(0,0); read(player,rate/5);
        require(inactive(player.instrumentPreviewStatus()), "Held row voices never masquerade as isolated instrument previews");
        player.stopPreview(); player.playSong(); read(player,rate/10);
        require(inactive(player.instrumentPreviewStatus()), "Song envelope voices do not expose isolated preview positions");
        player.previewNote(1,49); read(player,rate/10);
        require(player.instrumentPreviewStatus().active && player.status().playing,
                "An explicitly auditioned instrument keeps its own playhead during independent song playback");
        player.stop(); player.previewNote(1,49); player.load(document);
        require(inactive(player.instrumentPreviewStatus()), "Document reload removes stale instrument identity and clocks");

        // Endpoint handling is driven by the envelope state, not a timeout.
        volume.flags = 1; volume.count = 2; volume.points[1] = {4,48};
        pan.flags = 1; pan.count = 2; pan.points[1] = {7,16};
        player.load(document); player.previewNote(1,49); read(player,rate/2);
        state = player.instrumentPreviewStatus();
        require(state.active && state.volumeTick == 4 && state.panningTick == 7,
                "Completed nonzero envelopes hold their own final tick independently");
        volume.points[1].y = 0;
        player.load(document); player.previewNote(1,49); read(player,rate/2);
        require(inactive(player.instrumentPreviewStatus()), "Naturally completed zero-volume envelopes hide their playheads");
        volume.flags = pan.flags = 0; document.instruments[0].noteToSample.fill(1);
        player.load(document); player.previewNote(1,49); read(player,rate/2);
        require(inactive(player.instrumentPreviewStatus()), "A completed one-shot sample hides even disabled-envelope preview identity");
    }
}

void samplePreviewPlayhead() {
    using namespace brick;
    auto document = fixture();
    auto& source = document.instruments[0].samples[1];
    source.pcm.resize(4096);
    for (std::size_t n = 0; n < source.pcm.size(); ++n) source.pcm[n] = n % 32 < 16 ? 10000 : -10000;
    document.instruments[0].samples.push_back(Sample{});
    document.instruments.push_back(document.instruments[0]);
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    Playback player, reference;
    const auto inactive = [&] {
        const auto status = player.samplePreviewStatus();
        return !status.active && status.instrument == 0 && status.sample == -1 && status.frame == 0;
    };
    require(inactive(), "An unloaded sample playhead must be inactive with no stale source identity");
    player.load(document);reference.load(document);
    require(inactive(), "Loading a document must not activate its sample playhead");

    player.previewSample(2, 1, 49, 1000, 1800);
    reference.previewSample(2, 1, 49, 1000, 1800);
    auto position = player.samplePreviewStatus();
    require(position.active && position.instrument == 2 && position.sample == 1 && position.frame == 1000,
            "A range preview starts at its absolute source frame with the correct instrument and slot");
    require(read(player, 192) == read(reference, 192), "Inspecting the raw playhead must not change preview PCM");
    position = player.samplePreviewStatus();
    require(position.active && position.frame > 1000 && position.frame < 1800,
            "Audio mixing advances the playhead within the absolute selected source range");
    const auto stationary = player.samplePreviewStatus();
    require(stationary.frame == position.frame, "Reading a sample playhead does not advance audio");
    require(read(player, 257) == read(reference, 257), "Repeated playhead queries leave mixer timing unchanged");
    require(player.samplePreviewStatus().frame > position.frame,
            "A non-looping preview advances after more frames are mixed");
    read(player, 12000);
    require(inactive(), "A completed range preview clears its public playhead and identity");

    player.previewSample(1, 1);read(player, 192);
    player.previewSample(2, 1, 49, 1500, 2000);
    position = player.samplePreviewStatus();
    require(position.active && position.instrument == 2 && position.sample == 1 && position.frame == 1500,
            "Replacing an active preview must not expose the old position under the new source identity");
    bool rejected = false;
    try { player.previewSample(1, 1, 49, 2000, 1500); } catch (const std::exception&) { rejected = true; }
    position = player.samplePreviewStatus();
    require(rejected && position.active && position.instrument == 2 && position.frame == 1500,
            "A rejected preview range preserves the currently active source and position");
    player.stopPreview();
    require(inactive() && !audible(read(player, 192)), "Stop Preview hides the raw playhead and silences its voice");

    player.previewSample(1, 1);read(player, 192);player.previewNote(1, 49);
    require(inactive(), "Instrument audition immediately clears the preceding raw sample playhead");
    // Instrument volume/frequency are applied at the next tracker tick, which
    // may be later than the first 192-frame mixer packet. Raw PCM starts sooner.
    require(audible(read(player, 2400)) && inactive(),
            "Instrument audition sounds after its clock tick without claiming a raw sample playhead");
    player.stopPreview();player.previewSample(1, 1);player.previewRow(0, 0);
    require(inactive(), "Row audition must not inherit a previous raw sample playhead");
    player.stopPreview();
    player.previewSample(1, 1);player.previewSample(1, 2);
    require(inactive(), "Auditioning an empty committed slot clears the preceding raw preview");

    const auto draft = document.instruments[0].samples[1];
    player.previewSample(2, 1);player.previewSample(draft);
    position = player.samplePreviewStatus();
    require(position.active && position.instrument == 0 && position.sample == -1 && position.frame == 0,
            "An unsaved draft has its own active playhead without claiming a document slot");
    read(player, 192);
    require(player.samplePreviewStatus().frame > 0, "Draft preview playheads use actual mixer progress");
    player.previewSample(Sample{});
    require(inactive(), "An empty draft clears the previous raw playhead");

    player.previewSample(1, 1);player.stop();
    require(inactive(), "Stop clears raw preview identity");
    player.previewSample(1, 1);player.load(document);
    require(inactive(), "Document reload clears raw preview identity");
    player.previewSample(1, 1);player.configure({44100, Interpolation::Linear});
    require(inactive(), "Reconfiguring output clears raw preview identity");
    player.previewSample(1, 1);
    auto structural = document;edit::resizeChannels(structural, 3);
    player.updateDocument(structural);
    require(inactive(), "Replacing the playback core during an edit clears its old raw playhead");
    player.load(document);player.playSong();
    require(inactive(), "Starting a song must not activate the raw preview playhead");
    require(audible(read(player, 2400)) && inactive(), "Song voices are separate from the raw preview playhead");
    player.stop();player.configure({});

    for (const int loop : {1, 2, 3}) {
        auto looped = document;
        auto& sample = looped.instruments[0].samples[1];
        sample.loopType = std::uint8_t(loop);sample.loopStart = 64;sample.loopLength = 128;
        player.load(looped);player.previewSample(1, 1);
        std::size_t previous = 0;
        bool movedForward = false, movedBackward = false;
        for (int n = 0; n < 100; ++n) {
            read(player, 192);
            const auto current = player.samplePreviewStatus();
            require(current.active && current.instrument == 1 && current.sample == 1 && current.frame < 192,
                    "Forward and ping-pong loops retain a live playhead inside the source loop end");
            if (n >= 3) require(current.frame >= 64, "A looping playhead cannot return into the pre-loop attack");
            movedForward = movedForward || current.frame > previous;
            movedBackward = movedBackward || current.frame < previous;
            previous = current.frame;
        }
        require(movedForward && movedBackward,
                "Loop playheads must visibly wrap or reverse rather than increase as elapsed time");
    }
}

void draftSampleAudition() {
    using namespace brick;
    const auto original = fixture();
    const auto originalBytes = io::encodeXm(original);
    for (const auto interpolation : {Interpolation::Nearest, Interpolation::Linear,
         Interpolation::CubicLagrange, Interpolation::CubicSpline, Interpolation::Sinc, Interpolation::SincPrecise}) {
        for (const bool sixteenBit : {false, true}) for (const int loop : {0, 1, 2, 3}) {
            auto committed = original;
            auto& sample = committed.instruments[0].samples[0];
            sample.sixteenBit = sixteenBit;
            if (!sixteenBit) for (auto& value : sample.pcm) value = (value / 256) * 256;
            sample.loopType = loop;
            sample.loopStart = 7;
            sample.loopLength = 53;
            sample.relativeNote = -5;
            sample.finetune = 39;
            sample.volume = 43;
            sample.panning = 29;
            Playback draft({48000, interpolation}), saved({48000, interpolation});
            auto empty = original;empty.instruments.clear();
            draft.load(empty);saved.load(committed);
            draft.previewSample(sample, 54);saved.previewSample(1, 0, 54);
            const auto expected = read(saved, 6000);
            require(audible(expected) && read(draft, 6000) == expected,
                    "Draft PCM must match saved sample audition at every interpolation, depth and loop mode");
            require(!draft.status().playing && draft.status().elapsedSeconds == 0,
                    "Draft sample audition must not start transport or advance song time");
        }
    }

    Playback player, reference;
    player.load(original);reference.load(original);
    auto source = original.instruments[0].samples[0];
    player.previewSample(source);reference.previewSample(source);
    source.pcm.assign(2, 0);source.loopLength=2;
    require(read(player, 901) == read(reference, 901), "Draft audition owns PCM after the caller replaces its vector");
    {
        auto temporary = original.instruments[0].samples[0];
        player.previewSample(temporary);reference.previewSample(temporary);
    }
    require(read(player, 913) == read(reference, 913), "Temporary sample destruction cannot invalidate draft audition");
    source = original.instruments[0].samples[0];
    player.previewSample(source);reference.previewSample(source);
    read(player, 1);read(reference, 1);
    auto invalid = source;invalid.loopStart=std::uint32_t(invalid.pcm.size());
    bool rejected=false;
    try { player.previewSample(invalid); } catch (const std::exception&) { rejected=true; }
    require(rejected && read(player, 177) == read(reference, 177),
            "Invalid draft loop must leave the currently sounding preview intact");
    invalid=source;invalid.sixteenBit=false;invalid.pcm[0]=1;rejected=false;
    try { player.previewSample(invalid); } catch (const std::exception&) { rejected=true; }
    require(rejected && read(player, 91) == read(reference, 91),
            "Noncanonical eight-bit draft rejection must preserve the current preview");

    auto cleared=source;std::fill(cleared.pcm.begin(),cleared.pcm.end(),0);
    player.previewSample(cleared);
    require(!audible(read(player, 19)), "Cleared draft must immediately replace the old buffered tone with silence");
    player.previewSample(source);
    require(audible(read(player, 411)), "Editing after clear must restart the updated draft tone");
    player.stopPreview();
    require(!audible(read(player, 43)), "Stopping draft audition must silence the next pull immediately");
    player.previewSample(source);read(player, 1);player.previewSample(Sample{});
    require(!audible(read(player, 43)), "An empty draft must safely silence a previous raw preview");

    // Returning to a document-owned sample must release the draft only after
    // resetting its voice; the new output is still the ordinary sample path.
    player.previewSample(source);read(player, 777);player.previewSample(1, 1);
    reference.load(original);reference.previewSample(1, 1);
    require(read(player, 1800)==read(reference,1800), "Committed audition safely replaces a sounding draft snapshot");
    player.previewSample(source);player.stop();
    require(!audible(read(player,17)), "Stop releases draft voices and their pending PCM");
    player.previewSample(source);player.load(original);
    require(!audible(read(player,17)), "Loading a document releases all old draft audio");
    player.previewSample(source);player.configure({44100,Interpolation::CubicSpline});
    require(!audible(read(player,17)), "Reconfiguring output discards the old draft voice safely");
    player.previewSample(source);require(audible(read(player,1000)), "Draft audition restarts with the configured output rate");
    require(io::encodeXm(original)==originalBytes,
            "Preparing and playing draft snapshots must not edit any original document sample");

    // Full-length chip noise and tiny imported loops exercise both sides of
    // the upstream interpolation guards without requiring a saved slot.
    player.configure({48000,Interpolation::SincPrecise});
    for (const int length : {1,2,3,127,32767}) {
        Sample sample;sample.sixteenBit=false;sample.loopType=1;sample.loopLength=length;
        sample.pcm.assign(std::size_t(length),4096);
        if (length>=127) edit::fillNoisePreset(sample,{0,sample.pcm.size()},
            length==127?edit::NoisePreset::Short:edit::NoisePreset::Long);
        player.previewSample(sample);
        require(audible(read(player,8000)), "Tiny loops and full chip noise periods must both audition with sinc guards");
    }

    auto song=original;song.patterns[0].cells[0]={49,1,0x50,0,0};
    player.configure({});player.load(song);reference.load(song);player.playSong();reference.playSong();
    read(player,13);read(reference,13);
    const auto before=player.status();player.previewSample(source);
    require(player.status().elapsedSeconds==before.elapsedSeconds && player.status().row==before.row,
            "Starting a draft must not move a running song's transport");
    read(player,73);read(reference,73);player.previewSample(cleared);
    const auto quietSong=read(player,510), plainSong=read(reference,510);
    constexpr auto packetSamples=(Playback::sampleRate/250)*2;
    require(std::equal(quietSong.begin()+packetSamples,quietSong.end(),plainSong.begin()+packetSamples),
            "Refreshing a draft must preserve the buffered song timing after the current mixer packet");
    require(player.status().elapsedSeconds==reference.status().elapsedSeconds &&
            player.updateDocument(song)==DocumentUpdateResult::Preserved,
            "Draft audition must retain song timing and the document's live-update shape");
}

void outputMeter() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    document.instruments[0].samples[0].panning = 0;
    document.instruments[0].samples[1].panning = 255;
    brick::Playback metered, reference;
    require(emptyMeter(metered.takeOutputMeter()), "Unloaded output meter must be empty");
    metered.load(document);
    reference.load(document);
    metered.playSong();
    reference.playSong();
    require(emptyMeter(metered.takeOutputMeter()), "Starting transport without delivering PCM must not light the output meter");
    for (const auto frames : {1u, 13u, 64u, 111u, 3u, 5000u}) {
        const auto pcm = read(metered, frames);
        require(pcm == read(reference, frames), "Reading the output meter must not change output PCM or timing");
        const auto meter = metered.takeOutputMeter();
        require(meter.peak == stereoPeaks(pcm), "Output peaks must match only the consumed stereo frames, even inside a prepared block");
        require(!meter.clipped[0] && !meter.clipped[1], "A quiet song must not report clipping");
        require(emptyMeter(metered.takeOutputMeter()), "Taking a meter snapshot must consume peaks and clipping together");
    }
    metered.stop();
    reference.stop();
    for (int preview = 0; preview < 2; ++preview) {
        if (preview == 0) { metered.previewNote(1, 49); reference.previewNote(1, 49); }
        else { metered.previewSample(1, 1); reference.previewSample(1, 1); }
        const auto pcm = read(metered, 2400);
        require(pcm == read(reference, 2400), "Output metering must preserve instrument and raw sample auditions");
        const auto meter = metered.takeOutputMeter();
        // FT2 pan FF retains a little left signal; only pan 00 is fully one-sided.
        require(meter.peak == stereoPeaks(pcm) && meter.peak[preview] > 0 &&
                meter.peak[preview] > meter.peak[1 - preview] * 8,
                "Each preview must appear in its actual stereo output side while song transport is stopped");
        metered.stopPreview();
        reference.stopPreview();
    }

    metered.playSong();reference.playSong();
    metered.previewNote(1, 61);reference.previewNote(1, 61);
    metered.previewSample(1, 1);reference.previewSample(1, 1);
    auto combined = read(metered, 257);
    require(combined == read(reference, 257), "All three output sources must retain their original mixed PCM");
    const auto tail = read(metered, 2301);
    require(tail == read(reference, 2301), "Meter accumulation must not affect an overlapping song and previews");
    combined.insert(combined.end(), tail.begin(), tail.end());
    metered.stop();
    require(!audible(read(metered, 384)), "Stop must deliver silence after all mixed sources");
    const auto accumulated = metered.takeOutputMeter();
    require(accumulated.peak == stereoPeaks(combined) && accumulated.peak[0] > 0 && accumulated.peak[1] > 0,
            "Short transients from song and both previews must remain latched through later silence until consumed");
    require(emptyMeter(metered.takeOutputMeter()), "Consumed mixed output must leave an empty meter");

    metered.setMuteMask(3);metered.playSong();
    require(!audible(read(metered)) && emptyMeter(metered.takeOutputMeter()),
            "Muted output must produce zero peaks, independent of active channel scope state");
    metered.stop();metered.setMuteMask(0);
}

void outputMeterClipping() {
    for (int side = 0; side < 2; ++side) {
        auto document = fixture();
        brick::edit::resizeChannels(document, 32);
        auto& sample = document.instruments[0].samples[0];
        sample.panning = side == 0 ? 0 : 255;
        std::fill(sample.pcm.begin(), sample.pcm.end(), static_cast<std::int16_t>(side == 0 ? 32767 : -32768));
        // Eight aligned voices overload the main side while keeping FT2's
        // small pan-FF left leakage below full scale.
        for (int channel = 0; channel < 8; ++channel)
            document.patterns[0].at(0, channel, 32) = {49, 1, 0x50, 0, 0};
        brick::Playback player;
        player.load(document);player.playSong();
        const auto clippedPcm = read(player, 2400);
        require(std::find(clippedPcm.begin(), clippedPcm.end(), side == 0 ? 32767 : -32768) != clippedPcm.end(),
                "Clipping fixture must reach the signed 16-bit output boundary");
        player.stop();read(player, 384);
        const auto meter = player.takeOutputMeter();
        require(meter.peak == stereoPeaks(clippedPcm) && meter.clipped[side] && !meter.clipped[1 - side],
                "Positive and negative overloads must latch only on the clipped stereo side, even after later silence");
        require(emptyMeter(player.takeOutputMeter()), "Consuming a clipping latch must clear it without altering audio");
        read(player, 384);
        require(emptyMeter(player.takeOutputMeter()), "Silence must not regenerate a consumed overload indication");
    }
}

void defaultMixerHeadroom() {
    // Constant, hard-left PCM gives a stable reference after the attack ramp.
    // Upstream ChannelMixer at XM volume 40 produces +989/-990 for +/-1001,
    // -989 for -1000, and +23719/-23720 per voice for +/-24001. MilkyTracker's default
    // MasterMixer (volume 256, sampleShift 1) halves the sum before PCM16 output.
    // Two loud voices previously clipped in our adapter despite fitting that
    // standard output range. Odd values of both signs check shift rounding.
    for (const int source : {1001, -1000, -1001, 24001, -24001}) {
        const int voices = std::abs(source) > 20000 ? 2 : 1;
        const int expected = source == 1001 ? 494 : (source == -1001 || source == -1000) ? -495 :
                             source == 24001 ? 23719 : -23720;
        auto document = fixture();
        auto& sample = document.instruments[0].samples[0];
        sample.pcm.assign(128, static_cast<std::int16_t>(source));
        sample.loopStart = 0;sample.loopLength = 128;sample.panning = 0;
        for (int channel = 0; channel < voices; ++channel)
            document.patterns[0].at(0, channel, document.channels) = {49, 1, 0x50, 0, 0};
        brick::Playback player;player.load(document);player.playSong();
        const auto pcm = read(player, 4800);
        for (std::size_t frame = 2400; frame < 4800; ++frame)
            require(pcm[frame * 2] == expected && pcm[frame * 2 + 1] == 0,
                    "Default output must match MilkyTracker's headroom and signed rounding after summing voices");
        const auto meter = player.takeOutputMeter();
        require(meter.peak == stereoPeaks(pcm) && !meter.clipped[0] && !meter.clipped[1],
                "Mixes that fit the default output headroom must not light the clipping meter");
    }
}

void independentMute() {
    auto document=fixture();document.patterns[0].cells[0]={49,1,0x50,0,0};
    brick::Playback player;player.load(document);player.setMuted(0,true);player.playSong();
    require(!audible(read(player)),"Manual mute silences the only sounding channel");
    player.setSolo(0);player.playSong();
    require(player.muteMask()==2&&audible(read(player)),"Solo must enable its selected channel even when it was manually muted");
    player.setMuted(1,false);player.playSong();
    require(player.muteMask()==0&&!player.monitor().channels[1].muted&&audible(read(player)),
            "Manual mute edits must use the effective solo mask and remove solo exclusions");
    player.setMuted(0,true);player.setSolo(0);player.setSolo(-1);player.stop();
    require(player.muteMask()==0&&!player.monitor().channels[0].muted&&!player.monitor().channels[1].muted,
            "Leaving solo must clear every mute rather than restore an earlier manual mask");
    player.setSolo(0);player.setMuteMask(1);player.playSong();
    require(!audible(read(player))&&!player.monitor().channels[1].muted,
            "An explicit mute mask must replace solo exclusions");
    player.stop();
    player.setMuteMask(3);player.previewNote(1,49);
    require(audible(read(player)),"Instrument audition is independent of song mutes");player.stopPreview();
    auto smaller=document;brick::edit::resizeChannels(smaller,1);player.updateDocument(smaller);
    require(player.muteMask()==1,"Removed channels lose their manual mute bit");
    player.updateDocument(document);require(player.muteMask()==1,"Re-added channels do not inherit old mute bits");
    player.setMuteMask(0);player.playSong();require(audible(read(player)),"Clear mutes restores ordinary song playback");
    player.stop();player.setSolo(0);
    auto larger=document;brick::edit::resizeChannels(larger,4);player.updateDocument(larger);
    require(player.muteMask()==14,"Growing a document during Solo must keep every new channel excluded");
    player.setSolo(3);player.updateDocument(document);
    require(player.muteMask()==0&&!player.monitor().channels[0].muted&&!player.monitor().channels[1].muted,
            "Removing the selected Solo channel must leave the remaining channels unmuted");
}

void playbackAndAudition() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    brick::Playback playback;
    playback.load(document);
    require(!audible(read(playback)), "Idle player must be silent");
    playback.playSong();
    require(audible(read(playback)), "Song must produce PCM");
    require(playback.status().playing, "Song should still be playing");
    playback.stop();
    require(!audible(read(playback)), "Stop must clear song voices");
    playback.previewNote(1, 49);
    require(audible(read(playback)), "Instrument note audition must produce PCM");
    require(!playback.status().playing, "Audition must not start song transport");
    playback.stopPreview();
    read(playback, 192);
    require(!audible(read(playback)), "Stop preview must clear preview voices");
    playback.previewSample(1, 1);
    require(audible(read(playback)), "Unmapped instrument sample must be auditionable");
    playback.stopPreview();
    playback.setSolo(1);
    playback.playSong();
    require(!audible(read(playback)), "Solo of an empty channel must mute the sounding channel");
    playback.setSolo(-1);
    playback.playSong();
    require(audible(read(playback)), "Unsolo must restore the sounding channel");
    read(playback, 48000);
    require(!playback.status().playing, "Non-repeating song must finish");
    require(!audible(read(playback)), "Song end must stop looping sample voices");
}

void liveCellAndOneRow() {
    auto document = fixture();
    brick::Playback playback;
    playback.load(document);
    playback.playSong();
    require(!audible(read(playback, 4800)), "Initial empty row should be silent");
    require(playback.status().row == 0, "Fixture must be on the initial row before the live edit");
    playback.setCell(0, 1, 0, {49, 1, 0x50, 0, 0});
    require(playback.status().playing, "Cell edit must not reload or stop transport");
    require(audible(read(playback, 6000)), "Edited future row must play without module reload");
    require(playback.status().row >= 1, "Transport must advance after cell edit");
    playback.stop();
    playback.playPattern(0, 1, true);
    require(playback.status().rowAudition, "Active one-row playback must be distinguishable from continuous playback");
    require(audible(read(playback, 12000)), "One-row audition must play its note");
    require(playback.status().row == 1, "One-row audition must not advance to the next row");
    require(!playback.status().playing && !playback.status().rowAudition, "One-row transport and audition status must finish after the row ticks");
    require(!audible(read(playback)), "One-row audition must stop sustained loop voices after its row");
    playback.playPattern(0, 1, true);
    require(audible(read(playback, 2400)), "Row audition should sound before cancellation");
    playback.stopPreview();
    require(!playback.status().playing && !playback.status().rowAudition, "Stop preview must cancel an active row audition");
    require(!audible(read(playback)), "Cancelled row audition must be silent");

    document.patterns[0].at(1, 0, document.channels) = {49, 1, 0x50, 0, 0};
    playback.load(document);
    playback.playPattern(0, 1, true);
    require(audible(read(playback, 2400)), "Row preview must be active before the structural edit");
    document.patterns[0].rows = 9;
    document.patterns[0].cells.resize(9 * document.channels);
    require(playback.updateDocument(document) == brick::DocumentUpdateResult::Restarted &&
            playback.status().playing && playback.status().rowAudition,
            "Replacing an incompatible playback snapshot must preserve an active one-row limit");
    require(audible(read(playback, 12000)) && !playback.status().playing &&
            !playback.status().rowAudition && playback.status().row == 1 && !audible(read(playback)),
            "A structurally edited row audition must finish on the same row without a continuous loop");
    playback.playPattern(0, 1);
    require(playback.status().playing && !playback.status().rowAudition,
            "Ordinary pattern playback must not be reported as a row audition");
    playback.stop();

    document.patterns.clear();
    document.orders = {0};
    playback.load(document);
    playback.playPattern(0, 0, true);
    require(!audible(read(playback, 12000)), "Omitted order-referenced patterns must audition as silence");
    require(document.patterns.empty(), "Playback normalization must not modify the canonical document");
}

void independentPatterns() {
    auto document = fixture();
    document.patterns[0].at(0, 0, document.channels) = {49, 1, 0x50, 0, 0};
    brick::Pattern unused;
    unused.rows = 3;
    unused.cells.resize(unused.rows * document.channels);
    unused.at(0, 0, document.channels) = {61, 1, 0x38, 4, 0x34};
    document.patterns.push_back(unused);
    const auto originalBytes = brick::io::encodeXm(document);

    brick::Playback player, reference;
    player.load(document);
    player.playSong();
    require(audible(read(player, 2400)) && player.monitor().channels[0].note == 49,
            "Song playback must still use its order-assigned pattern");
    // Compare fresh cores so the upstream free-running fractional tick clock
    // left by the earlier Song audition does not shift the comparison window.
    player.load(document);
    player.playPattern(1);
    auto assigned = document;
    assigned.orders = {1};
    reference.load(assigned);
    reference.playSong();
    auto patternPcm = read(player, 2400);
    require(player.status().pattern == 1 && player.monitor().channels[0].note == 61,
            "Pattern audition must report the selected pattern instead of the order's pattern");
    const auto continuation = read(player, 9600);
    patternPcm.insert(patternPcm.end(), continuation.begin(), continuation.end());
    require(audible(patternPcm) && patternPcm == read(reference, 12000),
            "An unused pattern must produce the same PCM as placing it in the song");
    require(player.monitor().channels[0].note == 61,
            "A sustained unused-pattern voice must retain its note across empty rows");
    read(player, 12000);
    require(player.status().playing && player.status().pattern == 1 && player.status().row < 3,
            "An independently selected pattern must loop its own row count");
    require(brick::io::encodeXm(document) == originalBytes,
            "Independent playback must not assign the pattern or alter XM bytes");

    // LoaderXM already materializes missing patterns up to the highest order
    // reference. Exercise that public loader contract at the maximum XM ID.
    document.patterns.clear();
    document.orders = {255};
    const auto omittedBytes = brick::io::encodeXm(document);
    player.load(document);
    player.playPattern(255, 63);
    require(!audible(read(player, 6000)), "A high omitted pattern must play as silence");
    require(player.status().playing && player.status().pattern == 255 && player.status().row == 0,
            "Omitted patterns must retain the virtual 64-row looping boundary");
    player.stop();
    player.playPattern(42);
    require(!audible(read(player, 6000)) && player.status().pattern == 42,
            "A virtual unreferenced slot below the highest order reference must be selectable");
    player.stop();
    require(document.patterns.empty() && brick::io::encodeXm(document) == omittedBytes,
            "Loader and playback must preserve omitted-pattern serialization");
}

void unallocatedPatterns() {
    auto document = fixture();
    document.patterns[0].at(0, 0, document.channels) = {49, 1, 0x50, 0, 0};
    const auto originalBytes = brick::io::encodeXm(document);
    brick::Playback player;
    player.load(document);
    player.playPattern(255, 63);
    require(!audible(read(player, 6000)) && player.status().playing &&
            player.status().pattern == 255 && player.status().row == 0,
            "An unallocated ID above every stored and referenced pattern loops as 64 silent rows");
    require(brick::io::encodeXm(document) == originalBytes,
            "Auditioning a virtual pattern does not allocate canonical XM patterns");

    document.instruments[0].name = "Blank playback rename";
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Preserved &&
            player.status().pattern == 255 && !audible(read(player, 6000)),
            "A compatible document edit preserves a virtual pattern and its silent buffer");
    const auto elapsed = player.status().elapsedSeconds;
    document.bpm = 150;
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Restarted &&
            player.status().playing && player.status().pattern == 255 &&
            player.status().elapsedSeconds == elapsed && !audible(read(player, 6000)),
            "A structural reload recreates the same virtual pattern without reverting to Song pattern zero");

    document.patterns.resize(256);
    for (std::size_t index = 1; index < document.patterns.size(); ++index)
        document.patterns[index].cells.resize(64 * document.channels);
    document.patterns[255].at(0, 0, document.channels) = {61, 1, 0x50, 0, 0};
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Restarted &&
            player.status().pattern == 255,
            "Materializing the currently playing virtual pattern retains its selected ID");
    player.playPattern(255);
    require(audible(read(player, 6000)), "The materialized pattern replaces silent audition with its entered notes");

    document = fixture();
    player.load(document);
    player.playPattern(254, 10, true);
    require(!audible(read(player, 12000)) && !player.status().playing && player.status().row == 10,
            "A single virtual row audition is silent and stops after one row");
    for (const auto invalid : std::array<std::pair<int,int>, 3>{{{-1, 0}, {256, 0}, {255, 64}}}) {
        bool rejected = false;
        try { player.playPattern(invalid.first, invalid.second); }
        catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "Virtual playback validates the XM ID and default row bounds");
    }
}

void sustainedMonitorNotes() {
    auto document=fixture();
    auto& instrument=document.instruments[0];
    instrument.volumeEnvelope.count=2;
    instrument.volumeEnvelope.points[0]={0,64};
    instrument.volumeEnvelope.points[1]={40,0};
    instrument.volumeEnvelope.flags=1;
    instrument.fadeout=128;
    auto& pattern=document.patterns[0];
    pattern.at(0,0,document.channels)={49,1,0x50,0,0};
    pattern.at(2,0,document.channels)={97,0,0,0,0};
    pattern.at(3,0,document.channels)={53,1,0x50,0,0};
    brick::Pattern blank;blank.rows=8;blank.cells.resize(8*document.channels);
    document.patterns.push_back(blank);
    brick::Playback player,reference;
    player.load(document);reference.load(document);
    player.playSong();reference.playSong();
    for (const auto frames: {2400u,6000u,6000u}) {
        require(read(player,frames)==read(reference,frames),"Sustained-note monitor cache must not affect output PCM");
        const auto monitor=player.monitor();
        require(monitor.channels[0].active && monitor.channels[0].note==49,
                "The sounding note must survive empty rows and key-off envelope tails");
    }
    require(read(player,6000)==read(reference,6000),"A later note must remain identical with monitor capture active");
    require(player.monitor().channels[0].note==53,"A new note must replace the sustained monitor note");
    player.stop();
    require(!player.monitor().channels[0].active && player.monitor().channels[0].note==0,
            "Stop must clear monitor voice identity");
    player.playPattern(1);
    require(!audible(read(player,2400)) && player.monitor().channels[0].note==0,
            "Restarting a blank pattern must not resurrect an earlier note label");
    player.load(document);player.playPattern(0);read(player,2400);
    require(player.monitor().channels[0].note==49,"Loading and starting again must capture the new first note");
}

void cellConversionMatchesLoader() {
    const std::array<brick::Cell, 8> cells{{
        {49, 1, 0x50, 0x00, 0x37},
        {49, 1, 0xc4, 0x0c, 0x20},
        {49, 1, 0x50, 0x0e, 0xc2},
        {49, 1, 0xb4, 0x04, 0x47},
        {49, 1, 0xf4, 0x03, 0x08},
        {49, 1, 0x72, 0x10, 0x20},
        {49, 1, 0x50, 0x21, 0x13},
        {97, 1, 0x00, 0x00, 0x00}
    }};
    for (const auto& cell : cells) {
        auto original = fixture();
        auto edited = original;
        edited.patterns[0].cells[0] = cell;
        brick::Playback loaded, patched;
        loaded.load(edited);
        patched.load(original);
        patched.setCell(0, 0, 0, cell);
        loaded.playPattern(0);
        patched.playPattern(0);
        require(read(loaded, 12000) == read(patched, 12000),
                "Live XM cell conversion must match the upstream XM loader PCM");
    }
}

void elapsedPlaybackTime() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    brick::Playback player;
    player.load(document);
    auto atFrames = [&](std::uint64_t frames) {
        return std::abs(player.status().elapsedSeconds -
                        static_cast<double>(frames) / brick::Playback::sampleRate) < 1e-12;
    };
    read(player, 1000);
    player.previewNote(1, 49);read(player, 1000);player.stopPreview();
    player.previewSample(1, 1);read(player, 1000);player.stopPreview();
    require(atFrames(0), "Idle, instrument and raw sample audition must not advance the song clock");

    player.playSong();read(player, 7);
    require(atFrames(7), "Elapsed time counts delivered PCM, not a whole pre-rendered block");
    read(player, 193);
    require(atFrames(200), "Non-aligned pull sizes must preserve an exact audio-frame clock");
    player.setMuteMask(3);read(player, 1000);
    require(atFrames(1200), "Muted song transport still advances elapsed playback time");
    player.setCell(0, 1, 0, {53, 1, 0x40, 0, 0});
    document.patterns[0].at(1, 0, 2) = {53, 1, 0x40, 0, 0};
    document.instruments[0].name = "Clock preserved";
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Preserved && atFrames(1200),
            "Live cell and compatible document changes preserve elapsed time");
    document.patterns[0].rows = 9;document.patterns[0].cells.resize(18);
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Restarted && atFrames(1200),
            "Structural live-edit reconstruction preserves elapsed time");
    read(player, 97);
    require(atFrames(1297), "Rebuilt transport continues counting delivered frames");
    player.stop();read(player, 1000);
    player.previewNote(1, 49);read(player, 1000);player.stopPreview();
    require(atFrames(1297), "Stop freezes time and later previews cannot advance it");

    player.playPattern(0);
    require(atFrames(0), "An explicit Pattern start resets elapsed time");
    read(player, brick::Playback::sampleRate * 2);
    require(atFrames(brick::Playback::sampleRate * 2), "Pattern loops accumulate time across their row wrap");
    player.playSong();require(atFrames(0), "An explicit Song start resets elapsed time");
    read(player, brick::Playback::sampleRate * 2);
    const double finished = player.status().elapsedSeconds;
    require(!player.status().playing && finished > 0 && finished < 2.0,
            "Song completion stops the clock before unused output-buffer silence");
    read(player, 1000);
    require(player.status().elapsedSeconds == finished, "A naturally ended song leaves its final time visible");

    player.load(document);require(atFrames(0), "Loading another document resets elapsed time");
}

void liveDocumentUpdates() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    brick::Playback playback, reference;
    playback.load(document);
    reference.load(document);
    playback.playSong();
    reference.playSong();
    require(read(playback, 1920) == read(reference, 1920), "Update fixture players must begin identically");
    auto edited = document;
    edited.instruments[0].name = "Live metadata rename";
    require(playback.updateDocument(edited) == brick::DocumentUpdateResult::Preserved,
            "Instrument metadata updates must preserve the playback core");
    require(read(playback, 1920) == read(reference, 1920),
            "Metadata update must preserve tick timing and mixer phase");
    edited.instruments[0].samples[0].pcm.assign(96, 0);
    require(playback.updateDocument(edited) == brick::DocumentUpdateResult::Preserved,
            "PCM replacement within the same instrument layout must preserve transport");
    require(read(playback, 1920) == read(reference, 1920),
            "Already-playing voices must retain valid old PCM after replacement");
    playback.stop();
    playback.previewNote(1, 49);
    require(!audible(read(playback)), "New notes must use the replacement PCM");
    playback.stop();
    playback.playSong();
    edited.patterns[0].rows = 9;
    edited.patterns[0].cells.resize(18);
    require(playback.updateDocument(edited) == brick::DocumentUpdateResult::Restarted,
            "Structural edits must explicitly report transport reconstruction");
    require(playback.status().playing, "Structural reconstruction should continue from the current cursor");
}

void streamingWav() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    const auto path = std::filesystem::temp_directory_path() / "rowtrip-stream-regression.wav";
    const std::vector<std::uint8_t> previous{1, 2, 3, 4};
    brick::io::writeFileAtomic(path, previous);
    brick::RenderOptions options;
    options.maxSeconds = 10;
    options.tailSeconds = 0.1;
    const auto cancelled = brick::renderWav(document, path, options,
        [](const brick::RenderProgress& progress) { return progress.seconds < 0.25; });
    require(cancelled.cancelled, "WAV rendering must acknowledge cancellation");
    require(brick::io::readFile(path) == previous, "Cancelled WAV render must preserve the destination file");
    double lastProgress = -1;
    const auto single = brick::renderWav(document, path, options,
        [&](const brick::RenderProgress& progress) {
            require(progress.seconds >= lastProgress, "WAV progress must be monotonic");
            lastProgress = progress.seconds;
            return true;
        });
    require(!single.cancelled && !single.limitReached && single.seconds > 0.9,
            "A complete WAV render must include the song and configured tail");
    require(brick::io::decodeWav(brick::io::readFile(path), true).pcm.size() ==
                static_cast<std::size_t>(std::llround(single.seconds * brick::Playback::sampleRate)),
            "Streaming WAV header must describe the exact final frame count");
    options.repeats = 2;
    const auto repeated = brick::renderWav(document, path, options);
    require(repeated.seconds > single.seconds * 1.5, "WAV repeat count must render additional song passes");
    options.maxSeconds = 0.25;
    const auto limited = brick::renderWav(document, path, options);
    require(limited.limitReached && std::abs(limited.seconds - 0.25) < 0.00001,
            "WAV duration limit must be reported and honored precisely");
    std::filesystem::remove(path);
}

void orderRangeWav() {
    auto document = fixture();
    document.orders = {0, 1, 2, 3};
    document.patterns.resize(4);
    for (int index = 0; index < 4; ++index) {
        auto& pattern = document.patterns[std::size_t(index)];
        pattern.rows = 2;
        pattern.cells.assign(2 * document.channels, {});
        pattern.at(0, 0, document.channels) =
            {static_cast<std::uint8_t>(37 + index * 12), 1, 0x50, 0, 0};
    }
    // Earlier tempo and voices must not be warmed into an explicit range.
    document.patterns[0].at(0, 1, document.channels) = {49, 1, 0x50, 0x0F, 3};
    const auto original = brick::io::encodeXm(document);
    const auto path = std::filesystem::temp_directory_path() / "rowtrip-order-range-regression.wav";
    brick::RenderOptions options;
    options.maxSeconds = 10;
    options.tailSeconds = 0;
    options.firstOrder = 1;
    options.lastOrder = 1;
    auto pcmFromFile = [&]() {
        const auto bytes = brick::io::readFile(path);
        require(bytes.size() >= 44 && (bytes.size() - 44) % 4 == 0, "Rendered stereo WAV has complete frames");
        std::vector<std::int16_t> pcm((bytes.size() - 44) / 2);
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            const auto value = std::uint16_t(bytes[44 + i * 2]) | (std::uint16_t(bytes[45 + i * 2]) << 8);
            pcm[i] = static_cast<std::int16_t>(value < 32768 ? value : int(value) - 65536);
        }
        return pcm;
    };
    auto render = [&](const brick::Document& source) {
        const auto result = brick::renderWav(source, path, options);
        require(!result.cancelled && !result.limitReached, "An ordinary Order range must finish without hitting its limit");
        auto pcm = pcmFromFile();
        require(pcm.size() / 2 == static_cast<std::size_t>(std::llround(result.seconds * brick::Playback::sampleRate)),
                "Order-range result and WAV frame count agree");
        return pcm;
    };
    const auto single = render(document);
    require(audible(single) && single.size() / 2 > 11000 && single.size() / 2 < 13000,
            "An inclusive single-Order range contains both complete rows at the fresh song tempo");
    brick::Playback reference;
    reference.load(document);reference.playSong(1);
    require(single == read(reference, single.size() / 2),
            "An Order range starts at row zero with the same fresh PCM as ordinary cursor playback");

    options.lastOrder = 2;
    const auto pair = render(document);
    require(pair.size() > single.size() * 19 / 10 && pair.size() < single.size() * 21 / 10 &&
            std::equal(single.begin(), single.end(), pair.begin()),
            "The final selected Order is included and extends the same ongoing voices and effects");
    reference.load(document);reference.playSong(1);
    require(pair == read(reference, pair.size() / 2), "Multiple selected Orders match unmodified song playback PCM");
    options.repeats = 2;
    const auto repeated = render(document);
    require(repeated.size() == pair.size() * 2 && std::equal(pair.begin(), pair.end(), repeated.begin()) &&
            std::equal(pair.begin(), pair.end(), repeated.begin() + static_cast<std::ptrdiff_t>(pair.size())),
            "Each repeat renders exactly the selected span with a fresh playback state");
    options.repeats = 1;
    options.tailSeconds = 0.1;
    const auto tailed = render(document);
    require(tailed.size() == pair.size() + brick::Playback::sampleRate / 10 * 2 &&
            std::equal(pair.begin(), pair.end(), tailed.begin()),
            "The configured tail follows the complete range without adding a boundary-silence packet");
    auto changedOutside = document;
    changedOutside.patterns[0].at(0, 0, document.channels) = {96, 1, 0x10, 0x0F, 255};
    changedOutside.patterns[3].at(0, 0, document.channels) = {12, 1, 0x10, 0x10, 0};
    changedOutside.patterns[3].at(0, 1, document.channels) = {96, 1, 0x50, 0x0F, 32};
    require(render(changedOutside) == tailed,
            "Outside notes, tempo and global-volume effects cannot leak into the range or its tail");
    options.tailSeconds = 0;

    for (const auto target : {0, 3}) {
        auto jumped = document;
        jumped.patterns[1].at(0, 0, document.channels).effect = 0x0B;
        jumped.patterns[1].at(0, 0, document.channels).param = static_cast<std::uint8_t>(target);
        const auto jumpedPcm = render(jumped);
        require(jumpedPcm.size() > single.size() * 4 / 10 && jumpedPcm.size() < single.size() * 6 / 10,
                "A jump below or above the selected Orders stops after the complete triggering row");
        reference.load(jumped);reference.playSong(1);
        require(jumpedPcm == read(reference, jumpedPcm.size() / 2),
                "An out-of-range jump retains every tick of its originating row");
    }
    auto internalJump = document;
    internalJump.patterns[1].at(0, 0, document.channels).effect = 0x0B;
    internalJump.patterns[1].at(0, 0, document.channels).param = 3;
    internalJump.patterns[3].at(1, 0, document.channels) = {0, 0, 0, 0x0B, 0};
    options.lastOrder = 3;
    const auto internalPcm = render(internalJump);
    require(internalPcm.size() > single.size() * 14 / 10 && internalPcm.size() < single.size() * 16 / 10,
            "An in-range B03 keeps its original absolute Order and skips Order 02");
    reference.load(internalJump);reference.playSong(1);
    require(internalPcm == read(reference, internalPcm.size() / 2),
            "Absolute in-range jumps are rendered without rebasing the document");

    options.lastOrder = 1;
    auto looped = document;
    looped.patterns[1].at(0, 0, document.channels).effect = 0x0E;
    looped.patterns[1].at(0, 0, document.channels).param = 0x60;
    looped.patterns[1].at(1, 0, document.channels) = {0, 0, 0, 0x0E, 0x62};
    const auto loopPcm = render(looped);
    require(loopPcm.size() > single.size() * 28 / 10 && loopPcm.size() < single.size() * 32 / 10,
            "In-range E6 pattern loops keep their repeat count");
    options.maxSeconds = 0.1;
    const auto limited = brick::renderWav(looped, path, options);
    require(limited.limitReached && std::abs(limited.seconds - 0.1) < 0.00001 &&
            pcmFromFile().size() == brick::Playback::sampleRate / 10 * 2,
            "Duration limits bound looping range exports to the exact frame count");
    options.maxSeconds = 10;
    options.lastOrder = 3;
    const auto throughEnd = brick::renderWav(document, path, options);
    require(!throughEnd.limitReached && throughEnd.seconds > 0.7 && throughEnd.seconds < 0.8,
            "A range reaching the document end stops naturally instead of repeating the song");

    const std::vector<std::uint8_t> previous{4, 3, 2, 1};
    brick::io::writeFileAtomic(path, previous);
    const auto cancelled = brick::renderWav(document, path, options,
        [](const brick::RenderProgress& progress) { return progress.seconds < 0.25; });
    require(cancelled.cancelled && brick::io::readFile(path) == previous,
            "Cancelling an Order-range render preserves the previous destination");
    for (const auto bounds : {std::pair<int, int>{-1, 1}, {2, 1}, {1, 4}, {0, -2}}) {
        options.firstOrder = bounds.first;options.lastOrder = bounds.second;
        bool rejected = false, notified = false;
        try {
            brick::renderWav(document, path, options, [&](const brick::RenderProgress&) { notified = true;return true; });
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !notified && brick::io::readFile(path) == previous,
                "Invalid Order bounds are rejected before rendering and destination replacement");
    }
    options.firstOrder = 0;options.lastOrder = -1;
    const auto whole = render(document);
    options.firstOrder = 200;
    require(render(document) == whole, "The default -1 endpoint keeps whole-song behavior and ignores firstOrder");
    options.firstOrder = 0;options.lastOrder = 3;
    require(render(document) == whole,
            "An explicit range through the natural final Order retains the full-song ending PCM");
    options.repeats = 2;options.tailSeconds = 0.1;options.lastOrder = -1;
    const auto wholeRepeatedTail = render(document);
    options.lastOrder = 3;
    require(render(document) == wholeRepeatedTail,
            "Natural-end range repeats and tail match the existing whole-song rendering path");
    require(brick::io::encodeXm(document) == original, "Order-range exports never trim or rewrite the XM document");
    std::filesystem::remove(path);
}

bool monitorAudible(const brick::ChannelMonitor& channel) {
    return std::any_of(channel.waveform.begin(), channel.waveform.end(), [](auto value) { return value != 0; });
}

void channelMonitor() {
    brick::Playback empty;
    require(empty.monitor().channelCount == 0, "An unloaded monitor must contain no channels");
    auto document = fixture();
    document.channels = 32;
    document.patterns[0].cells.assign(document.patterns[0].rows * document.channels, {});
    for (std::size_t channel = 0; channel < 32; ++channel)
        if (channel != 1) document.patterns[0].at(0, channel, 32) =
            {static_cast<std::uint8_t>(49 + channel % 12), 1, 0x50, 0, 0};
    brick::Playback monitored, reference;
    monitored.load(document);
    reference.load(document);
    const auto idle = monitored.monitor();
    require(idle.channelCount == 32, "Monitor must include all 32 XM channels");
    for (const auto& channel : idle.channels)
        require(!channel.active && channel.peak == 0 && !monitorAudible(channel), "Idle monitor channels must be silent");
    monitored.playSong();
    reference.playSong();
    require(read(monitored, 1920) == read(reference, 1920), "Monitor fixture players must begin with identical PCM");
    const auto sounding = monitored.monitor();
    for (std::size_t channel = 0; channel < sounding.channels.size(); ++channel) {
        const auto& value = sounding.channels[channel];
        if (channel == 1) {
            require(!value.active && !monitorAudible(value) && value.peak == 0, "An empty channel must have no invented waveform");
        } else {
            require(value.active && !value.muted && value.instrument == 1 && value.note == static_cast<int>(49 + channel % 12),
                    "Monitor notes and instruments must come from the active player channels");
            require(monitorAudible(value) && value.peak > 0 && value.peak <= 1,
                    "An active mixer voice must produce a bounded real PCM scope");
        }
    }
    require(sounding.channels[0].waveform != sounding.channels[31].waveform,
            "Different channel pitches must yield their own waveform, not a shared master signal");
    const auto range = std::minmax_element(sounding.channels[0].waveform.begin(), sounding.channels[0].waveform.end());
    require(*range.first < 0 && *range.second > 0,
            "The linear-XM scope window must show both halves of the fixture's C4 waveform");
    const auto before = monitored.status();
    for (int repetition = 0; repetition < 100; ++repetition) (void)monitored.monitor();
    require(monitored.status().row == before.row, "Reading the monitor must not advance transport");
    require(read(monitored, 384) == read(reference, 384), "Reading channel scopes must not change the actual rendered PCM");

    monitored.setSolo(31);
    reference.setSolo(31);
    const auto solo = monitored.monitor();
    require(solo.channels[0].muted && solo.channels[0].active && !monitorAudible(solo.channels[0]) && solo.channels[0].peak == 0,
            "Muted channels must retain voice identity but show a silent scope");
    require(!solo.channels[31].muted && monitorAudible(solo.channels[31]), "Solo channel must remain visible and audible");
    require(read(monitored, 384) == read(reference, 384), "Muted monitor reads must not alter the mixer");
    monitored.setSolo(-1);
    reference.setSolo(-1);

    monitored.setCell(0, 0, 0, {60, 1, 0x50, 0, 0});
    require(monitored.monitor().channels[0].note == 49, "Editing the already-playing row must not force a note retrigger");
    const brick::Cell futureNote{53, 1, 0x50, 0, 0};
    monitored.setCell(0, 1, 0, futureNote);
    reference.setCell(0, 1, 0, futureNote);
    require(read(monitored, 4800) == read(reference, 4800), "Live edits and scope reads must preserve PCM until the edited row is reached");
    require(monitored.monitor().channels[0].note == 53, "The next edited row must appear in the real channel monitor");
    monitored.stop();
    const auto stopped = monitored.monitor();
    for (const auto& channel : stopped.channels)
        require(!channel.active && channel.note == 0 && !monitorAudible(channel) && channel.peak == 0,
                "Stopping playback must clear stale channel monitor output");
}

void highNoteMonitor() {
    for (const bool linear : {false, true}) {
        auto document = fixture();
        document.linear = linear;
        // PlayerSTD accepts note + relativeNote only through 119; 120 is an
        // invalid trigger, so this exercises the highest actually played note.
        document.instruments[0].samples[0].relativeNote = 23;
        document.instruments[0].samples[0].finetune = 127;
        document.instruments[0].vibratoDepth = 15;
        document.instruments[0].vibratoRate = 63;
        document.patterns[0].cells[0] = {96, 1, 0x50, 0x04, 0xff};
        brick::Playback monitored, reference;
        monitored.load(document);
        reference.load(document);
        monitored.playSong();
        reference.playSong();
        require(read(monitored, 1920) == read(reference, 1920), "High-note monitor fixtures must begin with identical PCM");
        for (int block = 0; block < 64; ++block) {
            const auto scope = monitored.monitor();
            require(scope.channelCount == 2 && scope.channels[0].active,
                    "The high-note scope must retain the actual active voice");
            require(std::isfinite(scope.channels[0].peak) && scope.channels[0].peak >= 0 && scope.channels[0].peak <= 1,
                    "High-note scope peaks must remain finite and bounded in both XM frequency modes");
            require(read(monitored, 192) == read(reference, 192),
                    "High-note scope scaling must not mutate output PCM or playback timing");
        }
    }
}

void liveTempoStatus() {
    auto document = fixture();
    document.patterns[0].at(0, 0, 2) = {49, 1, 0x50, 0x0f, 150};
    document.patterns[0].at(0, 1, 2) = {0, 0, 0, 0x0f, 3};
    document.patterns[0].at(1, 0, 2) = {0, 0, 0, 0x0f, 200};
    document.patterns[0].at(1, 1, 2) = {0, 0, 0, 0x0f, 8};
    brick::Playback playback;
    playback.load(document);
    require(playback.status().bpm == 125 && playback.status().speed == 6, "Idle status must report the loaded initial tempo/speed");
    playback.playSong();
    read(playback, 1920);
    require(playback.status().bpm == 150 && playback.status().speed == 3, "Live status must apply Fxx BPM and speed effects");
    read(playback, 4800);
    require(playback.status().bpm == 200 && playback.status().speed == 8, "Live status must follow later row tempo/speed changes");
    require(document.bpm == 125 && document.speed == 6, "Live tempo reporting must not modify document initial settings");
}

void selectedRowLoop() {
    auto document = fixture();
    document.patterns[0].at(0, 0, 2) = {84, 1, 0x50, 0x0f, 255};
    document.patterns[0].at(2, 0, 2) = {49, 1, 0x50, 0x0f, 3};
    document.patterns[0].at(2, 1, 2) = {56, 1, 0x40, 8, 255};
    document.patterns[0].at(3, 0, 2) = {0, 0, 0, 0x0a, 1};
    document.patterns[0].at(4, 0, 2) = {0, 0, 0, 0x0e, 0xe1};
    document.patterns[0].at(5, 0, 2) = {97, 0, 0, 0x0f, 255};
    auto referenceDocument = document;
    auto& referencePattern = referenceDocument.patterns[0];
    referencePattern.rows = 3;
    referencePattern.cells.assign(document.patterns[0].cells.begin() + 4,
                                  document.patterns[0].cells.begin() + 10);
    brick::Playback player, reference;
    player.load(document);reference.load(referenceDocument);
    player.playPattern(0, 2, false, 5);reference.playPattern(0);
    unsigned visited = 0;
    for (int i = 0; i < 900; ++i) {
        const auto frames = i % 3 == 0 ? 193u : 191u;
        require(read(player, frames) == read(reference, frames),
                "Selected rows must match a standalone pattern, including delay ticks, all channels and carried effect/voice state");
        const auto state = player.status();
        require(state.playing && state.row >= 2 && state.row < 5 &&
                state.loopFirstRow == 2 && state.loopEndRow == 5,
                "Selection loop must never advance into an outside row");
        visited |= 1u << state.row;
        if (i == 100) {
            const brick::Cell changed{53, 1, 0x40, 0x0a, 1};
            player.setCell(0, 3, 0, changed);reference.setCell(0, 1, 0, changed);
            document.patterns[0].at(3, 0, 2) = changed;
            referenceDocument.patterns[0].at(1, 0, 2) = changed;
        }
        if (i == 300) {
            document.patterns[0].at(2, 1, 2).note = 60;
            referenceDocument.patterns[0].at(0, 1, 2).note = 60;
            require(player.updateDocument(document) == brick::DocumentUpdateResult::Preserved &&
                    reference.updateDocument(referenceDocument) == brick::DocumentUpdateResult::Preserved,
                    "Block edits during looping must preserve the live playback state");
        }
    }
    require(visited == ((1u << 2) | (1u << 3) | (1u << 4)), "Every selected row must play");
    require(player.status().elapsedSeconds > 3.0, "Loop wrap must not reset elapsed listening time");
    for (const auto end : {0, 2, 9}) {
        bool rejected = false;
        try { player.playPattern(0, 2, false, end); } catch (const std::exception&) { rejected = true; }
        require(rejected && player.status().loopEndRow == 5, "Invalid range must preserve the running loop");
    }
    brick::edit::resizePattern(document.patterns[0], 2, 4, true);
    require(player.updateDocument(document) == brick::DocumentUpdateResult::Restarted &&
            player.status().loopFirstRow == 2 && player.status().loopEndRow == 4,
            "Shortening the live pattern clamps the stored loop to its surviving rows");
    player.playPattern(0, 3, false, 4);
    for (int i = 0; i < 100; ++i) {
        read(player, 192);
        require(player.status().playing && player.status().row == 3,
                "A one-row range at pattern end must repeat instead of entering row zero");
    }
    player.stop();
    require(player.status().loopFirstRow < 0, "Stop clears the session-only loop");
    player.playSong();require(player.status().loopFirstRow < 0, "Song playback must not inherit a row loop");
    player.playPattern(0, 2, false, 4);
    player.playPattern(0, 1);require(player.status().loopFirstRow < 0, "Ordinary cursor playback replaces a row loop");
    player.playPattern(0, 3, false, 4, 2);
    require(player.status().row == 3 && player.status().loopFirstRow == 2,
            "A cursor start inside a selection must not shorten the stored loop");
    bool visitedLoopBeginning = false;
    for(int i=0;i<100;++i) {
        read(player,192);
        const auto current = player.status();
        require(current.row>=2&&current.row<4,"Cursor-start selection stays inside both range boundaries");
        visitedLoopBeginning=visitedLoopBeginning||current.row==2;
    }
    require(visitedLoopBeginning,"Cursor-start playback wraps to the selection beginning, not the initial cursor");

    // Jump/break and an E6 loop whose marker is outside the selection cannot
    // escape the selected rows. The actual XM effect data remains intact.
    for (const auto command : {brick::Cell{0,0,0,0x0b,0}, brick::Cell{0,0,0,0x0d,0}, brick::Cell{0,0,0,0x0e,0x62}}) {
        auto jumping = fixture();jumping.speed = 1;
        jumping.patterns[0].at(3, 0, 2) = command;
        player.load(jumping);player.playPattern(0, 2, false, 4);
        for (int i = 0; i < 150; ++i) {
            read(player, 192);
            require(player.status().playing && player.status().row >= 2 && player.status().row < 4,
                    "Pattern flow effects must stay within the session loop");
        }
    }
}

void wavAndSdl() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    brick::Playback idleUi;
    idleUi.load(document);
    const auto path = std::filesystem::temp_directory_path() / "rowtrip-audio-regression.wav";
    brick::renderWav(document, path, 0.25);
    require(emptyMeter(idleUi.takeOutputMeter()), "Offline WAV rendering must not populate the live player's output meter");
    const auto bytes = brick::io::readFile(path);
    require(bytes.size() > 44, "WAV render must contain PCM payload");
    const auto decoded = brick::io::decodeWav(bytes, true);
    require(audible(decoded.pcm), "WAV render must contain audible PCM");
    require(decoded.pcm.size() == 12000, "WAV duration cap must be honored");
    std::filesystem::remove(path);

    // Exercise the actual SDL callback/device lifetime without physical audio hardware.
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    brick::Playback playback;
    playback.load(document);
    playback.open();
    playback.playSong();
    SDL_Delay(180);
    require(playback.status().row >= 1, "SDL callback must advance transport");
    const auto callbackMeter = playback.takeOutputMeter();
    require(callbackMeter.peak[0] > 0 && callbackMeter.peak[1] > 0 &&
            !callbackMeter.clipped[0] && !callbackMeter.clipped[1],
            "The real SDL callback path must publish the final stereo output meter");
    playback.setCell(0, 3, 0, {53, 1, 0x50, 0, 0});
    playback.load(document);
    require(!playback.status().playing, "Structural reload must safely return transport to idle");
    playback.previewSample(1, 1);
    SDL_Delay(30);
    playback.stop();
}
#include "audio_settings_checks.inc"
#include "sample_processing_audio_checks.inc"
#include "held_audition_audio_checks.inc"
} // namespace

int main() {
    try {
        playbackAndAudition();
        previewActivity();
        instrumentEnvelopeCompletion();
        songMonitorEnvelopeCompletion();
        instrumentPreviewPlayheads();
        samplePreviewPlayhead();
        draftSampleAudition();
        outputMeter();
        outputMeterClipping();
        defaultMixerHeadroom();
        independentMute();
        liveCellAndOneRow();
        heldRowAudition();
        heldRowInstrumentClock();
        heldRowOverridesAndMutes();
        heldRowDuringSong();
        independentPatterns();
        unallocatedPatterns();
        sustainedMonitorNotes();
        cellConversionMatchesLoader();
        elapsedPlaybackTime();
        liveDocumentUpdates();
        streamingWav();
        orderRangeWav();
        channelMonitor();
        highNoteMonitor();
        liveTempoStatus();
        selectedRowLoop();
        configuredMixers();
        configuredTempo();
        configureExistingPlayer();
        configuredWav();
        sampleProcessingPlayback();
        configureSdlDevice();
        wavAndSdl();
        std::cout << "Audio tests passed: PCM, auditions, output peaks/clipping, solo, live edits, row playback, conversion, streaming WAV, 32-channel monitor, live tempo, SDL callback.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio test failure: " << error.what() << '\n';
        return 1;
    }
}
