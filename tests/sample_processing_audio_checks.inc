// These checks run the processed XI through MilkyPlay, measuring delivered
// stereo PCM with toneFrequency() instead of reusing the pitch detector.
void processedPitchWithin(double actual, double expected, double maximumCents, const char* message) {
    const double cents = 1200.0 * std::log2(actual / expected);
    if (!std::isfinite(cents) || std::abs(cents) >= maximumCents)
        throw std::runtime_error(std::string(message) + ": " + std::to_string(actual) + " Hz / " + std::to_string(cents) + " cents");
}

void sampleProcessingPlayback() {
    constexpr int rate = 48000;
    constexpr double sourcePitch = 440.0, middleC = 261.6255653005986;
    std::vector<std::int16_t> stereo(std::size_t(rate) * 2);
    for (int frame = 0; frame < rate; ++frame) {
        const auto value = static_cast<std::int16_t>(std::lround(12000.0 * std::sin(6.283185307179586 * sourcePitch * frame / rate)));
        stereo[std::size_t(frame) * 2] = stereo[std::size_t(frame) * 2 + 1] = value;
    }
    // Real WAV import supplies XM's relative-note/finetune representation of
    // a 48 kHz sample clock. One second contains exactly 440 complete cycles.
    auto source = brick::io::decodeWav(brick::io::encodeWav16(stereo, rate), true);
    source.name = "440 HZ AT 48 KHZ";source.loopType = 1;source.loopLength = std::uint32_t(source.pcm.size());
    const auto estimate = brick::edit::detectSamplePitch(source, {0, source.pcm.size()});
    require(estimate.reliable, "Imported 48 kHz sine must have a reliable pitch estimate before retuning");
    auto corrected = source;
    corrected.relativeNote = static_cast<std::int8_t>(estimate.relativeNote);
    corrected.finetune = static_cast<std::int8_t>(estimate.finetune);
    brick::Instrument tuned;tuned.samples.push_back(corrected);
    tuned = brick::io::decodeXi(brick::io::encodeXi(tuned));

    // Chop the original 440 Hz sample, independently of the detector and its
    // suggestion. Eight unlooped slices should all retain its original speed
    // when played from their different consecutive mapped keys.
    const auto chop = brick::edit::chopSample(source, {0, source.pcm.size()}, {8, false, 49});
    auto sliced = brick::edit::placeChop(brick::Instrument{}, chop).instrument;
    sliced = brick::io::decodeXi(brick::io::encodeXi(sliced));
    for (const bool linear : {true, false}) {
        auto document = fixture();document.linear = linear;
        document.instruments = {tuned};document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
        brick::Playback player({rate, brick::Interpolation::Linear});
        player.load(document);player.playSong();
        processedPitchWithin(toneFrequency(read(player, rate / 4), rate), middleC, 10.0,
            linear ? "Detected tuning must play musical C-4 in linear XM" : "Detected tuning must play musical C-4 in Amiga XM");
        player.stop();player.previewNote(1, 49);
        processedPitchWithin(toneFrequency(read(player, rate / 4), rate), middleC, 10.0,
            "Instrument audition must use the detected tuning after XI round trip");
        player.stop();document.instruments = {sliced};player.load(document);
        double firstPitch = 0.0;
        for (int slice = 0; slice < 8; ++slice) {
            player.previewNote(1, 49 + slice);
            const double measured = toneFrequency(read(player, rate / 10), rate);
            processedPitchWithin(measured, sourcePitch, 10.0,
                linear ? "Mapped chop keys must retain 440 Hz in linear XM" : "Mapped chop keys must retain 440 Hz in Amiga XM");
            if (slice == 0) firstPitch = measured;
            else processedPitchWithin(measured, firstPitch, 2.0,
                "Consecutive chop trigger keys must not transpose their slices");
            player.stopPreview();
        }
    }
}
