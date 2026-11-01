        {
            // Independent PCM fixtures exercise the browser/options/commit path.
            auto wavFixture=[](int bits,int channels,const std::vector<int>& values) {
                std::vector<std::uint8_t> bytes;
                auto word=[&](unsigned value){bytes.push_back(std::uint8_t(value));bytes.push_back(std::uint8_t(value>>8));};
                auto dword=[&](unsigned value){for(int n=0;n<4;++n)bytes.push_back(std::uint8_t(value>>(n*8)));};
                auto tag=[&](const char* value){for(int n=0;n<4;++n)bytes.push_back(std::uint8_t(value[n]));};
                const unsigned size=unsigned(values.size())*unsigned(bits/8);
                tag("RIFF");dword(36+size+(size&1));tag("WAVE");tag("fmt ");dword(16);
                word(1);word(unsigned(channels));dword(8363);dword(unsigned(8363*channels*(bits/8)));
                word(unsigned(channels*(bits/8)));word(unsigned(bits));tag("data");dword(size);
                for(const int value:values)if(bits==8)bytes.push_back(std::uint8_t(value));else word(unsigned(value));
                if(size&1)bytes.push_back(0);
                return bytes;
            };
            const auto importFolder=browserDefaultFolder(2);
            const auto stereoPath=importFolder/"import-options-stereo16.WAV";
            const auto stereo8Path=importFolder/"import-options-stereo8.wav";
            const auto monoPath=importFolder/"import-options-mono8.wav";
            const auto brokenPath=importFolder/"import-options-broken.wav";
            io::writeFileAtomic(stereoPath,wavFixture(16,2,{-32768,0,32512,-16384,8192,16384,-8192,-16384}));
            io::writeFileAtomic(stereo8Path,wavFixture(8,2,{0,128,255,129,160,192,96,64}));
            io::writeFileAtomic(monoPath,wavFixture(8,1,{0,128,255}));
            io::writeFileAtomic(brokenPath,{'R','I','F','F',0,0,0,0,'W','A','V','E'});
            playback->stop();doc=makeDemoDocument();doc.instruments.push_back(doc.instruments[0]);
            doc.instruments[1].name="Import target";doc.instruments[1].samples.push_back(doc.instruments[1].samples[0]);
            page=Page::Sample;mode=Mode::Normal;selectedPattern=order=row=channel=field=0;instrument=2;sample=1;
            sampleField=0;followPlayback=false;blockSelected=false;dirty=false;
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            browserDirectory=importFolder;
            auto chooseAudio=[&](const std::filesystem::path& path) {
                mode=Mode::Normal;menu("IMPORT AUDIO");
                require(mode==Mode::Browser&&fileAction==FileAction::ImportAudio,"Import Audio opens the format browser");
                const auto entry=std::find_if(entries.begin(),entries.end(),[&](const Entry& value){return value.path==path;});
                require(entry!=entries.end(),"audio browser accepts the PCM fixture extension");
                const int target=int(std::distance(entries.begin(),entry));
                while(browserIndex<target)tap(Button::Down);
                while(browserIndex>target)tap(Button::Up);
                tap(Button::A);
                require(mode==Mode::ImportOptions&&pendingAudio.has_value(),"selecting audio stages options before changing the sample");
            };
            auto applyImportedAudio=[&]{while(importField<2)tap(Button::Down);tap(Button::A);};
            const auto importBefore=io::encodeXm(doc);const auto importSerial=recoverySerial;
            chooseAudio(stereoPath);const int rememberedEntry=browserIndex;
            require(importStereo==io::StereoMode::Blend&&importBits==16,"imports start with explicit Blend and sixteen-bit options");
            require(io::encodeXm(doc)==importBefore&&!dirty&&recoverySerial==importSerial&&undoStack.empty(),
                    "opening import options does not modify the song or recovery state");
            noticeUntil=0;draw();screenshot(home/"audio-import-options.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            require(mode==Mode::Browser&&!pendingAudio&&browserIndex==rememberedEntry&&io::encodeXm(doc)==importBefore,
                    "B cancels pending import and preserves the browser selection and music");
            mode=Mode::Normal;chooseAudio(stereoPath);tap(Button::A);
            require(mode==Mode::ImportOptions&&pendingAudio&&io::encodeXm(doc)==importBefore,"A on an import value does not change it or import the file");tap(Button::B);
            mode=Mode::Normal;chooseAudio(stereoPath);chord({Button::Right});
            require(importStereo==io::StereoMode::Left,"A+Right on channel mode selects Left only");
            const auto targetBefore=doc;applyImportedAudio();
            require(page==Page::Sample&&mode==Mode::Normal&&!pendingAudio&&instrument==2&&sample==1,
                    "Apply returns to the captured sample slot without an extra input action");
            require(currentSample()->sixteenBit&&currentSample()->pcm==std::vector<std::int16_t>({-32768,32512,8192,-8192}),
                    "Left import preserves the selected channel's sixteen-bit PCM");
            auto targetExpected=targetBefore;targetExpected.instruments[1].samples[1]=*currentSample();
            require(io::encodeXm(doc)==io::encodeXm(targetExpected),"import replaces only the selected sample and preserves other slots, keymap and envelopes");
            menu("UNDO");require(io::encodeXm(doc)==importBefore,"Undo restores the complete pre-import song");
            chooseAudio(stereoPath);chord({Button::Right,Button::Right});
            require(importStereo==io::StereoMode::Right,"channel options include Right only");applyImportedAudio();
            require(currentSample()->pcm==std::vector<std::int16_t>({0,-16384,16384,-16384}),"Right import does not mix in the left channel");
            chooseAudio(stereoPath);applyImportedAudio();
            require(currentSample()->pcm==std::vector<std::int16_t>({-16384,8064,12288,-12288}),"Blend averages both channels before sixteen-bit conversion");
            chooseAudio(stereo8Path);tap(Button::Down);chord({Button::Left});
            require(importField==1&&importBits==8,"bit depth changes with A+direction");applyImportedAudio();
            require(!currentSample()->sixteenBit&&currentSample()->pcm==std::vector<std::int16_t>({-16384,16384,12288,-12288}),
                    "stereo eight-bit import applies Blend and the requested eight-bit storage");
            chooseAudio(monoPath);chord({Button::Right});
            require(importStereo==io::StereoMode::Blend,"mono source does not expose meaningless left/right choices");
            tap(Button::Down);chord({Button::Left});applyImportedAudio();
            require(!currentSample()->sixteenBit&&currentSample()->pcm==std::vector<std::int16_t>({-32768,0,32512}),
                    "mono eight-bit import preserves unsigned WAV endpoints as signed XM PCM");
            const auto beforeError=io::encodeXm(doc);const auto historyBeforeError=undoStack.size();
            mode=Mode::Browser;bool badAudioRejected=false;
            try{openAudioImport(brokenPath);}catch(const std::exception&){badAudioRejected=true;}
            require(badAudioRejected&&mode!=Mode::ImportOptions&&mode!=Mode::Confirm&&!pendingAudio&&
                    io::encodeXm(doc)==beforeError&&undoStack.size()==historyBeforeError,
                    "malformed WAV reports an error without a false stereo prompt or sample replacement");
            mode=Mode::Normal;

            // The two cleanup actions are independent document transactions.
            playback->stop();doc=makeDemoDocument();doc.patterns.resize(4,doc.patterns[0]);
            doc.instruments.resize(4,doc.instruments[0]);doc.orders={2,0,2};doc.restart=2;
            for(auto& patternValue:doc.patterns)for(auto& value:patternValue.cells)value={};
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x40,0,0};
            doc.patterns[1].at(0,0,doc.channels)={55,2,0x35,0x0a,1};
            doc.patterns[2].at(0,0,doc.channels)={61,3,0x30,4,0x47};
            doc.patterns[2].at(1,0,doc.channels)={0,3,0,0,0};
            doc.patterns[3].at(0,0,doc.channels)={67,2,0x3f,0x0b,2};
            page=Page::Song;mode=Mode::Normal;songField=0;order=row=channel=0;field=4;selectedPattern=2;instrument=3;sample=0;
            dirty=false;followPlayback=false;undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            lastCell=cell();clipboard=cell();orderClipboard=2;blockClipboard=edit::copyBlock(pattern(),doc.channels,{0,1,0,1});
            patternSlotClipboard=pattern();patternSlotClipboardChannels=doc.channels;
            blockSelected=true;blockPattern=2;blockRow=blockChannel=0;
            const auto cleanupBefore=io::encodeXm(doc);const auto cleanupSerial=recoverySerial;
            menu("CLEAN UP INSTRUMENTS");
            require(mode==Mode::Confirm&&confirmDetail.find("1 UNUSED INSTRUMENTS")!=std::string::npos,
                    "instrument cleanup counts references in unarranged patterns as used");
            tap(Button::A);
            require(doc.patterns.size()==4&&doc.instruments.size()==3&&doc.orders==std::vector<std::uint8_t>({2,0,2})&&
                    doc.patterns[1].at(0,0,doc.channels).instrument==2&&doc.patterns[3].at(0,0,doc.channels).instrument==2&&
                    selectedPattern==2&&instrument==3&&undoStack.size()==1,
                    "instrument-only cleanup keeps unarranged patterns and their instruments in one transaction");
            menu("UNDO");require(io::encodeXm(doc)==cleanupBefore&&selectedPattern==2&&instrument==3,
                    "Undo restores an independent instrument cleanup");
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;
            lastCell=cell();clipboard=cell();orderClipboard=2;blockClipboard=edit::copyBlock(pattern(),doc.channels,{0,1,0,1});
            patternSlotClipboard=pattern();patternSlotClipboardChannels=doc.channels;
            blockSelected=true;blockPattern=2;blockRow=blockChannel=0;
            const auto patternCleanupSerial=recoverySerial;
            playback->playPattern(2,0);playingPattern=true;
            menu("CLEAN UP PATTERNS");
            require(mode==Mode::Confirm&&io::encodeXm(doc)==cleanupBefore&&playback->status().playing,
                    "pattern cleanup presents a confirmation without changing music or stopping transport");
            noticeUntil=0;draw();screenshot(home/"cleanup-patterns-confirm.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            require(io::encodeXm(doc)==cleanupBefore&&!dirty&&recoverySerial==patternCleanupSerial&&clipboard&&orderClipboard&&blockClipboard&&patternSlotClipboard,
                    "cancelling pattern cleanup preserves music, edit state and clipboards");
            menu("CLEAN UP PATTERNS");tap(Button::A);
            require(doc.patterns.size()==2&&doc.instruments.size()==4&&doc.orders==std::vector<std::uint8_t>({1,0,1})&&doc.restart==2&&
                    doc.patterns[1].at(0,0,doc.channels).instrument==3&&selectedPattern==1&&instrument==3&&undoStack.size()==1,
                    "pattern cleanup removes unused patterns while keeping every instrument and its ID");
            require(lastCell.instrument==3&&!playback->status().playing&&!clipboard&&!orderClipboard&&!blockClipboard&&!patternSlotClipboard&&!blockSelected,
                    "accepted pattern cleanup maps the editor target, stops transport and clears stale references");
            const auto patternsCleaned=io::encodeXm(doc);const auto instrumentCleanupSerial=recoverySerial;
            menu("CLEAN UP INSTRUMENTS");
            require(mode==Mode::Confirm&&confirmDetail.find("2 UNUSED INSTRUMENTS")!=std::string::npos&&
                    io::encodeXm(doc)==patternsCleaned,"instrument cleanup confirms only the now-unused instruments");
            noticeUntil=0;draw();screenshot(home/"cleanup-instruments-confirm.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            require(io::encodeXm(doc)==patternsCleaned&&undoStack.size()==1&&recoverySerial==instrumentCleanupSerial,
                    "cancelling instrument cleanup does not change the song or its undo history");
            menu("CLEAN UP INSTRUMENTS");tap(Button::A);
            require(doc.patterns.size()==2&&doc.instruments.size()==2&&doc.orders==std::vector<std::uint8_t>({1,0,1})&&doc.restart==2,
                    "the separate instrument cleanup preserves pattern IDs, sequence and restart");
            require(doc.patterns[1].at(0,0,doc.channels).instrument==2&&doc.patterns[1].at(1,0,doc.channels).instrument==2&&
                    doc.patterns[1].at(0,0,doc.channels).volume==0x30&&doc.patterns[1].at(0,0,doc.channels).effect==4&&
                    doc.patterns[1].at(0,0,doc.channels).param==0x47,
                    "instrument cleanup rewrites instrument-only references and retains volume and effect data");
            require(selectedPattern==1&&instrument==2&&lastCell.instrument==2&&!playback->status().playing&&!clipboard&&!orderClipboard&&!blockClipboard&&!patternSlotClipboard&&!blockSelected,
                    "accepted instrument cleanup maps only the instrument target and clears stale reference clipboards");
            require(undoStack.size()==2&&recoverySerial>cleanupSerial,"each cleanup adds exactly one undoable document change");
            const auto cleanupAfter=io::encodeXm(doc);
            clipboard=doc.patterns[1].at(0,0,doc.channels);orderClipboard=1;blockClipboard=edit::copyBlock(doc.patterns[1],doc.channels,{0,1,0,1});
            patternSlotClipboard=pattern();patternSlotClipboardChannels=doc.channels;
            menu("UNDO");
            require(io::encodeXm(doc)==patternsCleaned&&selectedPattern==1&&instrument==3&&!clipboard&&!orderClipboard&&!blockClipboard&&!patternSlotClipboard,
                    "instrument cleanup Undo restores its logical target and clears copied compact IDs without undoing pattern cleanup");
            menu("UNDO");require(io::encodeXm(doc)==cleanupBefore&&selectedPattern==2&&instrument==3,
                    "the next Undo independently restores the removed patterns and selected pattern ID");
            menu("REDO");require(io::encodeXm(doc)==patternsCleaned&&selectedPattern==1&&instrument==3,
                    "the first Redo restores only pattern cleanup");
            menu("REDO");require(io::encodeXm(doc)==cleanupAfter&&selectedPattern==1&&instrument==2,
                    "the next Redo restores instrument cleanup and its editor target");
            const auto compactHistory=undoStack.size();menu("CLEAN UP PATTERNS");
            require(mode==Mode::Normal&&undoStack.size()==compactHistory&&io::encodeXm(doc)==cleanupAfter,
                    "already-clean patterns do not create a new edit");
            menu("CLEAN UP INSTRUMENTS");
            require(mode==Mode::Normal&&undoStack.size()==compactHistory&&io::encodeXm(doc)==cleanupAfter,
                    "already-clean instruments do not create a new edit");
            page=Page::Song;songField=0;noticeUntil=0;draw();screenshot(home/"cleanup-unused-song.bmp");SDL_RenderPresent(renderer);
        }
