// Held previews are deliberately independent of the song/legacy one-row
// transport. These tests cover audible continuation, rather than UI flags.
void heldRowAudition() {
    for (const int rate : {44100, 48000}) {
        auto document = fixture();
        auto& pattern = document.patterns[0];
        pattern.at(0, 0, 2) = {49, 1, 0, 0x0f, 200};
        pattern.at(0, 1, 2) = {0, 0, 0, 0x10, 0};
        pattern.at(2, 0, 2) = {49, 1, 0x50, 0x0a, 0x04};
        pattern.at(3, 0, 2) = {97, 0, 0, 0, 0};
        brick::Playback held({rate}), oneRow({rate});
        held.load(document);oneRow.load(document);
        held.previewRow(0, 2);oneRow.playPattern(0, 2, true);
        const auto first = read(held, rate / 12);
        require(audible(first) && first == read(oneRow, rate / 12),
                "Held row must start at the selected row with the same note/effects as a fresh one-row audition");
        require(!held.status().playing && !held.status().rowAudition && held.status().row == 0 &&
                held.status().bpm == 125 && held.status().elapsedSeconds == 0,
                "Held row audition must neither preroll earlier tempo/global-volume effects nor alter song status/time");
        read(held, rate * 2);
        require(audible(read(held, rate / 10)),
                "Loop voices must survive the row; the next row's OFF and repeated volume slides must not run");
        held.stopPreview();
        require(!audible(read(held, rate / 5)), "Releasing a held row must immediately silence its looping voices");
        held.previewRow(0, 2);read(held, 1);held.stopPreview();
        require(!audible(read(held, rate)), "Stopping before the first tick must not let a preview row retrigger later");
        held.previewRow(0, 2);
        require(read(held, rate / 12) == first, "A repeated row audition must restart its voice and tick phase deterministically");
        bool rejected = false;
        try { held.previewRow(0, 1000); } catch (const std::out_of_range&) { rejected = true; }
        require(rejected && audible(read(held, rate / 10)), "Invalid row preview input must leave the current preview running");
        held.stop();
        require(!audible(read(held, rate / 5)), "Transport stop must also cancel a held row preview");

        auto stopped = fixture();stopped.patterns[0].cells[0] = {49, 1, 0x50, 0x0f, 0};
        held.load(stopped);held.previewRow(0, 0);
        require(audible(read(held, rate / 10)), "F00 row must play before its explicit end-of-row stop");
        read(held, rate / 5);
        require(!audible(read(held, rate / 5)), "F00 must stop held loop voices rather than freeze them indefinitely");

        // Redirecting effects must not reread the sounding row through the
        // pattern jump/loop path while the preview changes to instrument-only.
        for (const brick::Cell command : {brick::Cell{0, 0, 0, 0x0b, 0},
                                          brick::Cell{0, 0, 0, 0x0d, 0},
                                          brick::Cell{0, 0, 0, 0x0e, 0x63}}) {
            auto once = fixture();
            auto& sample = once.instruments[0].samples[0];
            sample.loopType = 0;sample.loopLength = 0;
            once.patterns[0].at(2, 0, 2) = {49, 1, 0x50, 0, 0};
            once.patterns[0].at(2, 1, 2) = command;
            held.load(once);held.previewRow(0, 2);
            require(audible(read(held, rate / 10)), "Redirecting row must initially trigger its one-shot sample");
            read(held, rate / 4);
            require(!audible(read(held, rate / 2)), "Bxx/Dxx/E6x must not repeat a held row's one-shot sample");
        }
    }
}

void heldRowInstrumentClock() {
    for (const int rate : {44100, 48000}) {
        auto document = fixture();
        auto& instrument = document.instruments[0];
        instrument.volumeEnvelope.count = 2;
        instrument.volumeEnvelope.flags = 1;
        instrument.volumeEnvelope.points[0] = {0, 64};
        instrument.volumeEnvelope.points[1] = {24, 0};
        // Tremolo owns upstream's DVS flag during the row. Its completion
        // must not freeze the instrument's later volume-envelope updates.
        document.patterns[0].cells[0] = {49, 1, 0x50, 7, 0x31};
        brick::Playback player({rate});player.load(document);player.previewRow(0, 0);
        require(audible(read(player, rate / 5)), "A held instrument envelope must sound beyond one row");
        const auto middle = stereoPeaks(read(player, rate / 10));
        read(player, rate / 2);
        require(middle[0] > 0 && !audible(read(player, rate / 5)),
                "Volume envelopes must continue to zero after the row's tremolo ends");

        instrument.volumeEnvelope.points[1] = {100, 64};
        instrument.fadeout = 4096;
        document.patterns[0].cells[0] = {49, 1, 0x50, 0x14, 3};
        player.load(document);player.previewRow(0, 0);
        require(audible(read(player, rate / 8)), "An in-row key-off must initially retain its instrument fadeout");
        read(player, rate);
        require(!audible(read(player, rate / 5)), "A row's key-off fadeout must keep progressing after row effects end");

        instrument.fadeout = 0;
        instrument.volumeEnvelope = {};
        instrument.panningEnvelope.count = 2;
        instrument.panningEnvelope.flags = 1;
        instrument.panningEnvelope.points[0] = {0, 0};
        instrument.panningEnvelope.points[1] = {20, 64};
        document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
        player.load(document);player.previewRow(0, 0);
        const auto early = stereoPeaks(read(player, rate / 10));
        read(player, rate / 2);
        const auto late = stereoPeaks(read(player, rate / 10));
        require(early[0] > early[1] * 2 && late[1] > late[0] * 8,
                "Panning envelopes must continue moving after the selected row ends");

        // Compare with normal playback followed by empty rows. This verifies
        // auto-vibrato phase (including the final row tick) without guessing
        // frequencies from short mixed waveforms.
        instrument.panningEnvelope = {};
        instrument.vibratoType = 0;instrument.vibratoDepth = 10;
        instrument.vibratoRate = 13;instrument.vibratoSweep = 20;
        document.patterns[0].cells[0] = {49, 1, 0x50, 4, 0x31};
        player.load(document);player.previewRow(0, 0);
        brick::Playback normal({rate});normal.load(document);normal.playPattern(0);
        require(read(player, rate * 3 / 4) == read(normal, rate * 3 / 4),
                "Held-row instrument auto-vibrato and envelopes must follow normal empty-row continuation without freezing or losing a tick");
    }
}

void heldRowOverridesAndMutes() {
    auto document = fixture();
    document.patterns[0].at(2, 1, 2) = {61, 1, 0, 8, 255};
    const brick::Cell candidate{49, 1, 0x40, 8, 0};
    auto entered = document;entered.patterns[0].at(2, 0, 2) = candidate;
    const auto original = brick::io::encodeXm(document);
    brick::Playback preview, reference;
    preview.load(document);reference.load(entered);
    preview.previewRow(0, 2, 0, &candidate);reference.previewRow(0, 2);
    require(read(preview, 12000) == read(reference, 12000),
            "A prospective cell override must sound exactly like the same cell entered into the row");
    require(brick::io::encodeXm(document) == original, "Prospective row audition must not mutate the canonical document");
    preview.stopPreview();reference.stopPreview();reference.updateDocument(document);
    preview.playPattern(0, 2);reference.playPattern(0, 2);
    require(read(preview, 4800) == read(reference, 4800),
            "A preview override must not leak into later song/pattern playback");
    preview.stop();

    // Edit the underlying source during the overridden row's delayed-note
    // ticks. Restoring a cached old cell would lose this live edit.
    const brick::Cell delayed{49, 1, 0x40, 0x0e, 0xd3};
    const brick::Cell liveEdit{73, 1, 0x50, 8, 0};
    preview.load(document);reference.load(document);
    preview.previewRow(0, 2, 0, &delayed);read(preview, 1920);read(reference, 1920);
    preview.setCell(0, 2, 0, liveEdit);reference.setCell(0, 2, 0, liveEdit);
    read(preview, 9600);read(reference, 9600);preview.stopPreview();reference.stopPreview();
    entered = document;entered.patterns[0].at(2, 0, 2) = liveEdit;
    reference.playPattern(0, 2);preview.playPattern(0, 2);
    require(read(preview, 4800) == read(reference, 4800),
            "Live source edits during an overridden row must survive each preview mix and its eventual release");

    preview.load(entered);preview.setMuteMask(3);preview.previewRow(0, 2);
    require(!audible(read(preview, 12000)), "Whole-row audition must honor an all-muted effective mask");
    preview.setSolo(0);read(preview, 384);
    auto peaks = stereoPeaks(read(preview, 4800));
    require(preview.muteMask() == 2 && peaks[0] > 0 && peaks[1] == 0,
            "Solo must enable a previously muted row-preview channel even after the row ends");
    preview.setMuted(1, false);read(preview, 384);peaks = stereoPeaks(read(preview, 4800));
    require(preview.muteMask() == 0 && peaks[0] > 0 && peaks[1] > 0,
            "Editing the solo mask must enable the other held voice without retriggering the row");
    preview.setMuteMask(3);preview.stopPreview();preview.previewNote(1, 49);
    require(audible(read(preview, 4800)), "Note-only preview after an all-muted row must remain independent of channel mutes");
    preview.stopPreview();require(!audible(read(preview, 4800)), "Note-only release must silence a looping instrument");
}

void heldRowDuringSong() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x40, 8, 0};
    document.patterns[0].at(3, 1, 2) = {61, 1, 0x40, 8, 255};
    brick::Playback player, reference;
    player.load(document);reference.load(document);player.playPattern(0);reference.playPattern(0);
    require(read(player, 1920) == read(reference, 1920), "Song reference must match before preview");
    const auto before = player.status();
    player.previewRow(0, 3);
    require(player.status().playing && player.status().row == before.row &&
            player.status().elapsedSeconds == before.elapsedSeconds,
            "Starting a held row must preserve running song position and elapsed time");
    require(read(player, 19200) != read(reference, 19200), "Independent row preview must mix audibly over the running song");
    const auto active = player.status(), plain = reference.status();
    require(active.playing && active.row == plain.row && active.order == plain.order &&
            active.elapsedSeconds == plain.elapsedSeconds && active.bpm == plain.bpm,
            "Row preview must not change the real song's row, tempo, or delivered time");
    player.stopPreview();
    require(read(player, 1920) == read(reference, 1920) && player.status().playing,
            "Releasing a preview at a packet boundary must leave real song playback bit-identical and running");
}
