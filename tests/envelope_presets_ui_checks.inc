        {
            const auto presetsOriginalHome=home;
            const auto presetsOriginalDocument=doc;
            const auto presetsOriginalBank=envelopePresets;
            const bool presetsOriginalDirty=dirty;
            // Keep test persistence isolated from the user's ordinary bank.
            home=presetsOriginalHome/"envelope-preset-ui";
            std::filesystem::create_directories(home);
            const auto presetPath=home/"envelope-presets.dat";
            const auto builtInPresets=edit::defaultEnvelopePresets();
            envelopePresets=builtInPresets;
            io::writeFileAtomic(presetPath,edit::encodeEnvelopePresets(envelopePresets));
            finishGesture();playback->stop();doc=makeDemoDocument();
            auto& presetSource=doc.instruments[0];
            presetSource.volumeEnvelope={};presetSource.volumeEnvelope.count=3;presetSource.volumeEnvelope.flags=7;
            presetSource.volumeEnvelope.points[0]={0,64};presetSource.volumeEnvelope.points[1]={8,35};presetSource.volumeEnvelope.points[2]={24,0};
            presetSource.volumeEnvelope.sustain=1;presetSource.volumeEnvelope.loopEnd=2;
            presetSource.panningEnvelope={};presetSource.panningEnvelope.count=3;presetSource.panningEnvelope.flags=1;
            presetSource.panningEnvelope.points[0]={0,12};presetSource.panningEnvelope.points[1]={6,54};presetSource.panningEnvelope.points[2]={20,32};
            presetSource.fadeout=1234;presetSource.vibratoType=2;presetSource.vibratoSweep=15;presetSource.vibratoDepth=8;presetSource.vibratoRate=19;
            presetSource.extension[3]=17;
            const auto savedVolumeInstrument=presetSource;
            page=Page::Instrument;mode=Mode::Normal;instrument=1;sample=0;solo=-1;
            selectedPattern=order=row=channel=field=0;envelopePresetSlot=envelopePresetField=0;
            held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
            selectPending=selectUsed=channelModifier=false;channelChord=0;clearSelection();
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;normalize();playback->load(doc);
            playback->setMuteMask(0);playback->setSolo(-1);
            const auto untouchedPresetSong=io::encodeXm(doc);
            const auto untouchedPresetRevision=documentRevision,untouchedPresetRecovery=recoverySerial;
            auto noPresetSongEdit=[&]{
                require(io::encodeXm(doc)==untouchedPresetSong&&!dirty&&undoStack.empty()&&redoStack.empty()&&undoBytes==0&&
                        documentRevision==untouchedPresetRevision&&recoverySerial==untouchedPresetRecovery,
                        "preset browsing and saving do not edit the song, undo history or recovery state");
            };
            auto enterEnvelopePresets=[&](bool pan){
                menu(pan?"PANNING ENVELOPE":"VOLUME ENVELOPE");
                require(mode==Mode::Envelope&&envelopePan==pan,"instrument envelope opens in the requested bank");
                while(envelopeField<9)tap(Button::Down);
                while(envelopeField<11)tap(Button::Right);
                tap(Button::A);
                require(mode==Mode::EnvelopePresets&&envelopePresetField==envelopePresetSlot,
                        "the envelope PRESET button opens its six-card preset panel");
            };
            auto focusPresetCard=[&](int target){
                if(envelopePresetField>=6)tap(Button::Up);
                if(envelopePresetField/3<target/3)tap(Button::Down);
                else if(envelopePresetField/3>target/3)tap(Button::Up);
                while(envelopePresetField%3<target%3)tap(Button::Right);
                while(envelopePresetField%3>target%3)tap(Button::Left);
                require(envelopePresetField==target&&envelopePresetSlot==target,"D-pad reaches the requested preset card");
            };
            auto focusPresetAction=[&](int target){
                if(envelopePresetField<6)tap(Button::A);
                require(mode==Mode::EnvelopePresets&&envelopePresetField>=6,
                        "A chooses a card without applying it and enters the action row");
                while(envelopePresetField<target)tap(Button::Right);
                while(envelopePresetField>target)tap(Button::Left);
            };

            menu("VOLUME ENVELOPE");
            tap(Button::Up);require(envelopeField==0,"envelope top does not wrap to its buttons");
            for(int n=0;n<9;++n)tap(Button::Down);
            require(envelopeField==9,"the last envelope field enters the button row at Insert initially");
            tap(Button::Left);require(envelopeField==9,"envelope button row clamps its left edge");
            tap(Button::Right);tap(Button::Right);tap(Button::Right);
            require(envelopeField==11,"envelope button row clamps its right edge");
            tap(Button::Up);require(envelopeField==8,"Up from Preset goes directly to Loop End, not Delete");
            tap(Button::Down);require(envelopeField==11,"returning to the envelope button row restores Preset");
            tap(Button::Down);require(envelopeField==11,"Down at envelope buttons never wraps or moves sideways");
            tap(Button::Left);tap(Button::Up);tap(Button::Down);
            require(envelopeField==10,"Delete is also remembered independently of the vertical field");
            noPresetSongEdit();tap(Button::B);

            enterEnvelopePresets(false);
            tap(Button::Up);tap(Button::Left);
            require(envelopePresetField==0,"preset grid top and left edges do not wrap");
            tap(Button::Right);require(envelopePresetSlot==1&&envelopePresetField==1,"Right selects the next preset card");
            tap(Button::Down);require(envelopePresetSlot==4&&envelopePresetField==4,"Down moves to the matching card in the second row");
            tap(Button::Left);tap(Button::Up);
            require(envelopePresetSlot==1&&envelopePresetField==1,"returning to a preset row restores its own last column");
            tap(Button::Right);tap(Button::Down);
            require(envelopePresetField==3,"the second preset row also retains its last column");
            tap(Button::Down);tap(Button::Right);
            require(envelopePresetField==7&&envelopePresetSlot==3,"preset buttons have an independent horizontal focus");
            tap(Button::Up);require(envelopePresetField==3,"Up leaves preset buttons for the selected card immediately");
            tap(Button::Down);require(envelopePresetField==7,"returning to preset buttons restores Save Current");
            tap(Button::Down);require(envelopePresetField==7,"preset buttons never wrap downward into cards");
            tap(Button::Right);tap(Button::Right);require(envelopePresetField==8,"preset button right edge is clamped");
            focusPresetCard(0);
            noPresetSongEdit();tap(Button::B);
            require(mode==Mode::Envelope&&envelopeField==11,"B returns from presets to its envelope without applying a slot");
            noPresetSongEdit();tap(Button::A);

            // Choosing a top-row card must retain it while entering buttons;
            // reaching Save cannot silently target a lower-row preset instead.
            focusPresetAction(7);
            require(envelopePresetField==7&&envelopePresetSlot==0,"top-row A reaches Save Current while preserving the chosen slot");
            tap(Button::A);noPresetSongEdit();
            auto expectedPresetBank=builtInPresets;
            expectedPresetBank.volume[0]=edit::captureEnvelopePreset(savedVolumeInstrument,false);
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank)&&
                    io::readFile(presetPath)==edit::encodeEnvelopePresets(expectedPresetBank),
                    "Save Current persists only the selected volume slot, including fadeout and auto-vibrato");
            envelopePresets=builtInPresets;loadEnvelopePresetBank();
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank),
                    "saved custom presets survive bank reload");
            tap(Button::Up);require(envelopePresetField==0,"buttons return directly to the chosen top-row card");
            focusPresetCard(3);focusPresetAction(7);
            require(envelopePresetField==7&&envelopePresetSlot==3,"choosing a bottom-row card restores Save without changing its slot");
            tap(Button::A);expectedPresetBank.volume[3]=edit::captureEnvelopePreset(savedVolumeInstrument,false);
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank),
                    "saving a bottom-row slot does not overwrite its top-row counterpart");
            noPresetSongEdit();noticeUntil=0;draw();screenshot(presetsOriginalHome/"envelope-presets-volume.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);tap(Button::B);

            // Applying a volume preset changes only its documented instrument
            // properties, leaving opposite envelope, key map and PCM intact.
            auto& changedVolume=doc.instruments[0];
            changedVolume.volumeEnvelope.points[1].y=9;changedVolume.fadeout=456;
            changedVolume.vibratoType=0;changedVolume.vibratoSweep=3;changedVolume.vibratoDepth=2;changedVolume.vibratoRate=7;
            changedVolume.panningEnvelope.points[1].y=7;changedVolume.samples[0].panning=77;
            playback->load(doc);const auto beforeVolumeLoad=io::encodeXm(doc);
            auto expectedVolume=changedVolume;
            expectedVolume.volumeEnvelope=savedVolumeInstrument.volumeEnvelope;expectedVolume.fadeout=savedVolumeInstrument.fadeout;
            expectedVolume.vibratoType=savedVolumeInstrument.vibratoType;expectedVolume.vibratoSweep=savedVolumeInstrument.vibratoSweep;
            expectedVolume.vibratoDepth=savedVolumeInstrument.vibratoDepth;expectedVolume.vibratoRate=savedVolumeInstrument.vibratoRate;
            enterEnvelopePresets(false);focusPresetCard(0);focusPresetAction(6);tap(Button::A);
            require(mode==Mode::Envelope&&envelopeField==11&&undoStack.size()==1&&dirty&&
                    io::encodeXi(*currentInstrument())==io::encodeXi(expectedVolume),
                    "choosing a card then Load applies volume settings in one undo step and preserves unrelated data");
            const auto afterVolumeLoad=io::encodeXm(doc);
            undo();require(io::encodeXm(doc)==beforeVolumeLoad,"Undo restores the complete pre-preset volume instrument");
            undo(true);require(io::encodeXm(doc)==afterVolumeLoad,"Redo restores the exact loaded volume preset");
            require(io::readFile(presetPath)==edit::encodeEnvelopePresets(expectedPresetBank),"loading and undoing presets do not rewrite the bank file");

            doc.instruments[0].panningEnvelope.count=2;playback->load(doc);envelopePoint=2;
            tap(Button::A);selectChord(Button::Right);
            require(mode==Mode::EnvelopePresets&&envelopePan&&envelopePresetSlot==0&&envelopePresetField==0&&envelopePoint==0,
                    "Select+Right switches to the independent panning preset bank and resets focus");
            tap(Button::B);
            require(mode==Mode::Envelope&&envelopePan&&currentEnvelope().count==2&&envelopePoint==0,
                    "returning after a bank switch cannot retain a volume point index beyond the panning envelope");
            tap(Button::A);
            tap(Button::Right);noticeUntil=0;draw();screenshot(presetsOriginalHome/"envelope-presets-pan.bmp");SDL_RenderPresent(renderer);
            const auto savedPanEnvelope=currentInstrument()->panningEnvelope;
            const auto panSaveSong=io::encodeXm(doc);const auto panSaveUndo=undoStack.size(),panSaveRedo=redoStack.size();
            const auto panSaveRevision=documentRevision,panSaveRecovery=recoverySerial;
            const bool panSaveDirty=dirty;
            focusPresetAction(7);require(envelopePresetField==7&&envelopePresetSlot==1,"the second top card reaches Save without changing its target");tap(Button::A);
            expectedPresetBank.panning[1]=edit::captureEnvelopePreset(*currentInstrument(),true);
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank)&&
                    io::encodeXm(doc)==panSaveSong&&undoStack.size()==panSaveUndo&&redoStack.size()==panSaveRedo&&
                    documentRevision==panSaveRevision&&recoverySerial==panSaveRecovery&&dirty==panSaveDirty,
                    "saving panning changes only that preset slot, without touching the volume bank or song history");
            envelopePresets=builtInPresets;loadEnvelopePresetBank();
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank),
                    "both independent preset banks survive persistence together");
            tap(Button::B);tap(Button::B);
            auto& changedPan=doc.instruments[0];changedPan.panningEnvelope.points[0].y=55;
            changedPan.volumeEnvelope.points[1].y=17;changedPan.fadeout=901;changedPan.vibratoRate=31;
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;playback->load(doc);
            const auto beforePanLoad=io::encodeXm(doc);auto expectedPan=changedPan;expectedPan.panningEnvelope=savedPanEnvelope;
            enterEnvelopePresets(true);focusPresetAction(6);tap(Button::A);
            require(mode==Mode::Envelope&&undoStack.size()==1&&io::encodeXi(*currentInstrument())==io::encodeXi(expectedPan),
                    "the Load button applies only panning and preserves volume, fadeout, vibrato, samples and key map");
            const auto afterPanLoad=io::encodeXm(doc);
            undo();require(io::encodeXm(doc)==beforePanLoad,"panning preset load is undoable");
            undo(true);require(io::encodeXm(doc)==afterPanLoad,"panning preset redo restores the exact result");

            tap(Button::A);focusPresetAction(8);
            require(envelopePresetField==8&&envelopePresetSlot==1,"Reset Slot retains the selected custom panning slot");
            const auto resetSong=io::encodeXm(doc);const auto resetUndo=undoStack.size(),resetRedo=redoStack.size();
            const auto resetRevision=documentRevision,resetRecovery=recoverySerial;const bool resetDirty=dirty;
            const auto beforeResetBank=edit::encodeEnvelopePresets(envelopePresets);
            tap(Button::A);require(mode==Mode::Confirm,"Reset Slot requires confirmation before replacing saved data");
            tap(Button::B);
            require(mode==Mode::EnvelopePresets&&edit::encodeEnvelopePresets(envelopePresets)==beforeResetBank&&
                    io::readFile(presetPath)==beforeResetBank,"canceling Reset Slot preserves the custom bank and returns to its panel");
            tap(Button::A);tap(Button::A);expectedPresetBank.panning[1]=builtInPresets.panning[1];
            require(mode==Mode::EnvelopePresets&&edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(expectedPresetBank)&&
                    io::readFile(presetPath)==edit::encodeEnvelopePresets(expectedPresetBank),
                    "confirmed reset persists just the selected bank slot's built-in preset");
            require(io::encodeXm(doc)==resetSong&&undoStack.size()==resetUndo&&redoStack.size()==resetRedo&&
                    documentRevision==resetRevision&&recoverySerial==resetRecovery&&dirty==resetDirty,
                    "Reset Slot does not reset the active envelope or create a song undo step");

            // Failed disk writes must not replace the in-memory bank. A regular
            // file used as the parent is a deterministic failure on both OSes.
            const auto presetWorkingHome=home;const auto blockedPresetHome=home/"not-a-directory";
            const std::vector<std::uint8_t> parentFile{7,8,9};io::writeFileAtomic(blockedPresetHome,parentFile);
            selectChord(Button::Left);require(!envelopePan&&envelopePresetSlot==0,"Select+Left returns to volume presets");
            const auto beforeFailedBank=edit::encodeEnvelopePresets(envelopePresets);
            bool presetSaveFailed=false,presetResetFailed=false;
            home=blockedPresetHome;
            try{saveEnvelopePresetSlot();}catch(const std::exception&){presetSaveFailed=true;}
            try{resetEnvelopePresetSlot();}catch(const std::exception&){presetResetFailed=true;}
            home=presetWorkingHome;
            require(presetSaveFailed&&presetResetFailed&&edit::encodeEnvelopePresets(envelopePresets)==beforeFailedBank&&
                    io::readFile(presetPath)==beforeFailedBank&&io::readFile(blockedPresetHome)==parentFile&&io::encodeXm(doc)==resetSong,
                    "failed preset saves and resets retain bank memory, existing files and the active instrument");

            const std::vector<std::uint8_t> corruptPresetFile{0,1,2,3,4};io::writeFileAtomic(presetPath,corruptPresetFile);
            loadEnvelopePresetBank();
            require(edit::encodeEnvelopePresets(envelopePresets)==edit::encodeEnvelopePresets(builtInPresets)&&
                    io::readFile(presetPath)==corruptPresetFile&&errorMessage&&message.find("PRESETS UNREADABLE")!=std::string::npos,
                    "an unreadable bank falls back to built-ins, warns, and preserves the bad file for recovery");
            require(io::encodeXm(doc)==resetSong,"a corrupt editor bank cannot modify the current song");

            tap(Button::B);tap(Button::B);instrument=128;normalize();const auto beforeLazyPresets=io::encodeXm(doc);
            const auto lazyPresetUndo=undoStack.size();enterEnvelopePresets(false);tap(Button::Right);tap(Button::B);tap(Button::B);
            require(doc.instruments.size()==1&&io::encodeXm(doc)==beforeLazyPresets&&undoStack.size()==lazyPresetUndo,
                    "viewing and canceling presets on an uncreated instrument does not allocate it");
            bool noInstrumentSave=false;
            try{saveEnvelopePresetSlot();}catch(const std::exception&){noInstrumentSave=true;}
            require(noInstrumentSave&&doc.instruments.size()==1&&io::readFile(presetPath)==corruptPresetFile,
                    "saving an absent instrument fails without creating it or replacing the bank file");

            home=presetsOriginalHome;envelopePresets=presetsOriginalBank;doc=presetsOriginalDocument;
            page=Page::Instrument;mode=Mode::Normal;instrument=1;sample=0;envelopePan=false;envelopeField=envelopePoint=0;
            envelopePresetSlot=envelopePresetField=0;undoStack.clear();redoStack.clear();undoBytes=0;dirty=presetsOriginalDirty;
            clearSelection();held.fill(false);++documentRevision;normalize();playback->load(doc);noticeUntil=0;
        }
