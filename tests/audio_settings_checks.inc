// Included in audio_tests.cpp so these checks share its real XM/PCM fixtures.
double toneFrequency(const std::vector<std::int16_t>& pcm, int rate) {
    std::vector<double> crossings;
    // Ignore the start ramp and measure interpolated positive crossings on L.
    for (std::size_t frame = static_cast<std::size_t>(rate / 25); frame + 1 < pcm.size() / 2; ++frame) {
        const int a = pcm[frame * 2], b = pcm[(frame + 1) * 2];
        if (a <= 0 && b > 0)
            crossings.push_back(static_cast<double>(frame) - static_cast<double>(a) / (b - a));
    }
    require(crossings.size() >= 5, "Configured mixer must retain a periodic audible tone");
    return (crossings.size() - 1) * rate / (crossings.back() - crossings.front());
}

void configuredMixers() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    const auto original = brick::io::encodeXm(document);
    constexpr std::array<brick::Interpolation, 6> methods{{
        brick::Interpolation::Nearest, brick::Interpolation::Linear,
        brick::Interpolation::CubicLagrange, brick::Interpolation::CubicSpline,
        brick::Interpolation::Sinc, brick::Interpolation::SincPrecise
    }};
    for (const int rate : {44100, 48000}) {
        std::vector<std::int16_t> nearest;
        for (const auto method : methods) {
            brick::Playback player({rate, method});
            player.load(document);player.playSong();
            const auto pcm = read(player, static_cast<std::size_t>(rate / 4));
            require(std::abs(toneFrequency(pcm, rate) - 8363.0 / 96.0) < 1.0,
                    "All output rates and interpolation modes must preserve the sample's C-4 pitch");
            require(std::abs(player.status().elapsedSeconds - 0.25) < 1e-12,
                    "Elapsed playback time must use the active output rate");
            require(player.takeOutputMeter().peak == stereoPeaks(pcm),
                    "Configured mixer output meters must match the delivered stereo PCM");
            const auto monitored = player.monitor().channels[0];
            require(monitored.active && monitored.note == 49 && monitored.peak > 0 && monitorAudible(monitored),
                    "Configured playback must retain a real active channel waveform and note identity");
            if (method == brick::Interpolation::Nearest) nearest = pcm;
            if (method == brick::Interpolation::Linear)
                require(nearest != pcm, "Interpolation selection must change actual mixed PCM");
            player.stop();player.previewNote(1, 61);
            require(std::abs(toneFrequency(read(player, rate / 5), rate) - 8363.0 / 48.0) < 2.0,
                    "Instrument audition must use the selected output rate and preserve octave pitch");
            player.stopPreview();player.previewSample(1, 0);
            require(std::abs(toneFrequency(read(player, rate / 5), rate) - 8363.0 / 96.0) < 1.0,
                    "Raw PCM audition must use the selected output rate without retuning the sample");
            player.stop();player.playPattern(0, 0, true);
            require(audible(read(player, rate / 4)) && !player.status().playing &&
                    !audible(read(player, rate / 20)),
                    "One-row preview must still stop its looping voices at either output rate");
        }
    }
    require(brick::io::encodeXm(document) == original, "Mixer settings must not modify stored sample tuning or XM data");
}

struct TimedRow { double seconds = -1.0;int bpm = 0, speed = 0; };
double configuredSongDuration(const brick::Document& document, int rate,
                              std::array<TimedRow, 200>* timeline = nullptr) {
    brick::Playback player({rate, brick::Interpolation::Linear});
    player.load(document);player.playSong();
    std::array<std::int16_t, 384> pcm{};
    const std::size_t packet = static_cast<std::size_t>(rate / 250);
    std::size_t frames = 0;
    while (player.status().playing && frames < static_cast<std::size_t>(rate) * 90) {
        player.readFrames(pcm.data(), packet);
        frames += packet;
        const auto state = player.status();
        if (timeline && state.playing && state.row >= 0 && state.row < 200 &&
            (*timeline)[static_cast<std::size_t>(state.row)].seconds < 0)
            (*timeline)[static_cast<std::size_t>(state.row)] =
                {static_cast<double>(frames) / rate, state.bpm, state.speed};
    }
    require(!player.status().playing, "Finite tempo fixture must finish without a duration limit");
    const double duration = static_cast<double>(frames) / rate;
    require(std::abs(player.status().elapsedSeconds - duration) <= 0.0041,
            "Finished song clock may exclude only its final halted packet");
    return duration;
}

void configuredTempo() {
    auto document = fixture();
    auto& pattern = document.patterns[0];
    pattern.rows = 200;pattern.cells.assign(pattern.rows * document.channels, {});
    // Long enough to expose native 44.1 kHz's 176-frame packet rounding drift.
    const double constant48 = configuredSongDuration(document, 48000);
    const double constant44 = configuredSongDuration(document, 44100);
    require(constant48 > 23.9 && constant48 < 24.1 && std::abs(constant48 - constant44) < 0.0081,
            "44.1 and 48 kHz must keep the same long-term tempo despite different mixer packet lengths");
    auto effect = [&](int row, int channel, int command, int value) {
        pattern.at(row, channel, document.channels) =
            {0, 0, 0, static_cast<std::uint8_t>(command), static_cast<std::uint8_t>(value)};
    };
    effect(0, 0, 0x0f, 100);effect(32, 0, 0x0f, 200);
    effect(64, 0, 0x0f, 40);effect(96, 0, 0x0f, 255);
    // Multiple Fxx commands on one row, including a later return to the old BPM.
    effect(128, 0, 0x0f, 100);effect(128, 1, 0x0f, 255);
    effect(160, 0, 0x0f, 3);effect(176, 0, 0x0f, 6);effect(176, 1, 0x0f, 80);
    effect(24, 0, 0x0e, 0xe2);effect(112, 0, 0x0e, 0xe1);
    std::array<TimedRow, 200> timeline48{}, timeline44{};
    const double changing48 = configuredSongDuration(document, 48000, &timeline48);
    const double changing44 = configuredSongDuration(document, 44100, &timeline44);
    const double nominal = 2.5 / document.bpm + 34 * 6 * 2.5 / 100 + 32 * 6 * 2.5 / 200 + 32 * 6 * 2.5 / 40 +
                           65 * 6 * 2.5 / 255 + 16 * 3 * 2.5 / 255 + 24 * 6 * 2.5 / 80;
    // A BPM change carries fractional tick phase from the old tempo into the
    // new one. Their different packet grids can therefore accumulate boundary
    // offsets; a whole changing-tempo song cannot use a single-packet bound.
    // Nominal includes the initial 125 BPM tick. For packet length h <= 4 ms,
    // a change contributes at most h*abs(1-oldBpm/newBpm), plus one end packet.
    // Measure stable segments separately below to detect persistent drift.
    const double boundaryAllowance = 0.004 * (1 + std::abs(1 - 125.0 / 100) + std::abs(1 - 100.0 / 200) +
        std::abs(1 - 200.0 / 40) + std::abs(1 - 40.0 / 255) + std::abs(1 - 255.0 / 80)) + 0.0001;
    require(std::abs(changing48 - nominal) < boundaryAllowance && std::abs(changing44 - nominal) < boundaryAllowance,
            "Tempo and pattern-delay commands must retain the fixture's analytic song length");
    struct Span { int first, last, bpm, speed, extraRows; };
    const std::array<Span, 7> spans{{
        {2, 30, 100, 6, 2}, {34, 62, 200, 6, 0}, {66, 94, 40, 6, 0},
        {98, 126, 255, 6, 1}, {130, 158, 255, 6, 0}, {162, 174, 255, 3, 0},
        {178, 198, 80, 6, 0}
    }};
    for (const auto& span : spans) {
        const auto first = static_cast<std::size_t>(span.first), last = static_cast<std::size_t>(span.last);
        const double expected = (span.last - span.first + span.extraRows) * span.speed * 2.5 / span.bpm;
        for (const auto* timeline : {&timeline48, &timeline44}) {
            const auto& a = (*timeline)[first];const auto& b = (*timeline)[last];
            require(a.seconds >= 0 && b.seconds > a.seconds && a.bpm == span.bpm && b.bpm == span.bpm &&
                    a.speed == span.speed && b.speed == span.speed,
                    "Each stable span must apply the expected Fxx tempo and speed, including repeated Fxx");
            require(std::abs((b.seconds - a.seconds) - expected) < 0.0041,
                    "Stable tempo spans and EEx delays must match their analytic duration within one output packet");
        }
        require(std::abs((timeline48[last].seconds - timeline48[first].seconds) -
                         (timeline44[last].seconds - timeline44[first].seconds)) < 0.0081,
                "44.1 and 48 kHz must retain identical tempo between Fxx boundaries");
    }
}

void configureExistingPlayer() {
    auto document = fixture();
    document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    brick::Playback player;
    player.load(document);player.setMuted(1, true);player.setSolo(1);
    player.playSong();player.previewNote(1, 61);player.previewSample(1, 0);
    read(player, 17);
    player.configure({44100, brick::Interpolation::CubicSpline});
    require(player.audioSettings().sampleRate == 44100 &&
            player.audioSettings().interpolation == brick::Interpolation::CubicSpline && !player.isOpen(),
            "Headless settings changes must not unexpectedly open an audio device");
    require(!player.status().playing && !audible(read(player, 1000)),
            "Applying audio settings must stop song and both preview paths, including buffered PCM");
    require(player.muteMask() == 1 && player.monitor().channels[0].muted && !player.monitor().channels[1].muted,
            "Audio reconfiguration must preserve the effective solo mask with its selected channel audible");
    player.setSolo(-1);player.playSong();
    require(audible(read(player, 1000)), "Reconfiguration must retain the loaded song and samples");
    for (const auto settings : {brick::AudioSettings{32000, brick::Interpolation::Linear},
                               brick::AudioSettings{44100, static_cast<brick::Interpolation>(99)}}) {
        const auto before = player.status();
        bool rejected = false;
        try { player.configure(settings); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && player.audioSettings().sampleRate == 44100 &&
                player.audioSettings().interpolation == brick::Interpolation::CubicSpline &&
                player.status().playing && player.status().elapsedSeconds == before.elapsedSeconds,
                "Unsupported settings must be rejected without stopping or changing the active configuration");
    }
    player.configure({48000, brick::Interpolation::Linear});
    player.playSong();
    brick::Playback reference;reference.load(document);reference.setMuted(1, true);reference.playSong();
    require(read(player, 4800) == read(reference, 4800),
            "Returning to the default settings must reproduce a fresh default player's PCM");
}

void configuredWav() {
    auto document = fixture();
    document.orders = {0, 1, 2};document.patterns.resize(3);
    for (int index = 0; index < 3; ++index) {
        auto& pattern = document.patterns[static_cast<std::size_t>(index)];
        pattern.rows = 2;pattern.cells.assign(2 * document.channels, {});
        pattern.cells[0] = {static_cast<std::uint8_t>(37 + index * 12), 1, 0x50, 0, 0};
    }
    const auto path = std::filesystem::temp_directory_path() / "rowtrip-configured-rate.wav";
    for (const int rate : {44100, 48000}) {
        brick::RenderOptions options;
        options.audioSettings = {rate, brick::Interpolation::CubicSpline};
        options.firstOrder = options.lastOrder = 1;options.maxSeconds = 10;options.tailSeconds = 0;
        auto render = [&]() {
            const auto result = brick::renderWav(document, path, options);
            const auto bytes = brick::io::readFile(path);
            auto word = [&](std::size_t offset) {
                return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
                       (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
            };
            require(bytes.size() >= 44 && word(24) == static_cast<std::uint32_t>(rate) &&
                    word(28) == static_cast<std::uint32_t>(rate * 4) && word(40) == bytes.size() - 44,
                    "WAV header must use the selected sample rate, stereo byte rate and exact PCM size");
            require(std::abs(result.seconds - static_cast<double>(bytes.size() - 44) / (rate * 4.0)) < 1e-12,
                    "WAV result duration must use the active output rate");
            std::vector<std::int16_t> pcm((bytes.size() - 44) / 2);
            for (std::size_t i = 0; i < pcm.size(); ++i) {
                const auto value = std::uint16_t(bytes[44 + i * 2]) | (std::uint16_t(bytes[45 + i * 2]) << 8);
                pcm[i] = static_cast<std::int16_t>(value < 32768 ? value : int(value) - 65536);
            }
            return std::pair<brick::RenderResult, std::vector<std::int16_t>>(result, std::move(pcm));
        };
        const auto single = render();
        // Fresh playback waits for its first tick before the two six-tick
        // rows: (1 + 2*6) * 2.5/125 = 260 ms, quantized to one mixer packet.
        const double expectedRangeSeconds = (1 + 2 * document.speed) * 2.5 / document.bpm;
        require(!single.first.limitReached && std::abs(single.first.seconds - expectedRangeSeconds) < 0.0041,
                "Configured range render must contain the two selected rows at their original tempo");
        brick::Playback player(options.audioSettings);player.load(document);player.playSong(1);
        require(single.second == read(player, single.second.size() / 2),
                "WAV interpolation and rate must match the same configured live playback path");
        options.repeats = 2;
        const auto repeated = render();
        require(repeated.second.size() == single.second.size() * 2 &&
                std::equal(single.second.begin(), single.second.end(), repeated.second.begin()) &&
                std::equal(single.second.begin(), single.second.end(), repeated.second.begin() +
                           static_cast<std::ptrdiff_t>(single.second.size())),
                "Configured range repeats must reset timer phase and reproduce exactly the same PCM");
        options.repeats = 1;options.tailSeconds = 0.1;
        const auto tailed = render();
        require(tailed.second.size() == single.second.size() + static_cast<std::size_t>(rate / 10) * 2 &&
                std::equal(single.second.begin(), single.second.end(), tailed.second.begin()),
                "Range tail length must use the selected sample rate and preserve the entire range");
        options.maxSeconds = 0.1;
        const auto limited = render();
        require(limited.first.limitReached && limited.second.size() == static_cast<std::size_t>(rate / 10) * 2,
                "WAV duration caps must be exact at both sample rates");
        const auto previous = brick::io::readFile(path);
        options.audioSettings.sampleRate = 22050;
        bool rejected = false, notified = false;
        try {
            brick::renderWav(document, path, options, [&](const brick::RenderProgress&) { notified = true;return true; });
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !notified && brick::io::readFile(path) == previous,
                "Unsupported render settings must preserve an existing destination without beginning a render");
    }
    std::filesystem::remove(path);
}

void configureSdlDevice() {
    auto document = fixture();document.patterns[0].cells[0] = {49, 1, 0x50, 0, 0};
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    brick::Playback player;player.load(document);player.open();
    for (const int rate : {44100, 48000}) {
        player.playSong();
        player.configure({rate, brick::Interpolation::Linear});
        require(player.isOpen() && !player.status().playing && player.audioSettings().sampleRate == rate,
                "Applying settings must reopen the SDL device at the requested rate and leave transport stopped");
        player.playSong();SDL_Delay(150);
        require(player.status().playing && player.status().elapsedSeconds > 0.05 &&
                player.takeOutputMeter().peak[0] > 0,
                "The reopened SDL callback must deliver audible song PCM and advance the configured clock");
        player.stop();
    }
}
