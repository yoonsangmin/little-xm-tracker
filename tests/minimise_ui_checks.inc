        {
            const auto minimiseOriginalDoc=doc;
            const auto minimiseOriginalCellClipboard=clipboard;
            const auto minimiseOriginalPcmClipboard=pcmClipboard;
            const auto minimiseOriginalSampleClipboard=sampleClipboard;
            const auto minimiseOriginalInstrumentClipboard=instrumentClipboard;
            const bool minimiseOriginalDirty=dirty;
            auto resetMinimiseFixture=[&]{
                finishGesture();playback->stop();doc=makeDemoDocument();
                auto makePcm=[](std::size_t size,bool sixteenBit){
                    Sample result;result.sixteenBit=sixteenBit;
                    for(std::size_t n=0;n<size;++n)result.pcm.push_back(std::int16_t((int(n)-9)*(sixteenBit?173:256)));
                    return result;
                };
                auto& first=doc.instruments[0];first.samples.clear();
                auto a=makePcm(20,true);a.name="SIXTEEN LOOP";a.loopType=1;a.loopStart=4;a.loopLength=6;
                a.relativeNote=3;a.finetune=-16;a.panning=47;
                auto b=makePcm(17,false);b.name="EIGHT LOOP";b.loopType=2;b.loopStart=3;b.loopLength=5;
                auto c=makePcm(13,true);c.name="LOOP DISABLED";c.loopType=0;c.loopStart=2;c.loopLength=5;
                auto d=makePcm(14,true);d.name="NO LOOP TAIL";d.loopType=1;d.loopStart=2;d.loopLength=12;
                first.samples={a,b,c,d};first.noteToSample[60]=1;first.noteToSample[72]=2;first.extension[4]=19;
                Instrument unused;unused.name="UNUSED INSTRUMENT";
                auto e=makePcm(30,true);e.name="UNUSED TAIL";e.loopType=2;e.loopStart=5;e.loopLength=10;
                unused.samples={e,Sample{}};doc.instruments.push_back(unused);
                page=Page::Sample;mode=Mode::Normal;instrument=1;sample=0;sampleField=14;solo=-1;
                order=row=selectedPattern=channel=field=0;clearSelection();
                selectionStart=14;selectionEnd=18;waveformZoom=true;
                held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
                selectPending=selectUsed=channelModifier=false;channelChord=0;
                undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;++documentRevision;
                normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);
                clipboard=Cell{60,2,0x40,4,0x47};pcmClipboard=edit::PcmClip{{256,-512,768},false};
                sampleClipboard=c;instrumentClipboard=unused;
            };
            auto sampleClipboardBytes=[](const Sample& source){Instrument holder;holder.samples={source};return io::encodeXi(holder);};
            auto checkMinimiseClipboards=[&](const std::vector<std::uint8_t>& savedSample,const std::vector<std::uint8_t>& savedInstrument){
                require(clipboard&&same(*clipboard,Cell{60,2,0x40,4,0x47})&&pcmClipboard&&
                        pcmClipboard->pcm==std::vector<std::int16_t>({256,-512,768})&&!pcmClipboard->sixteenBit&&
                        sampleClipboard&&sampleClipboardBytes(*sampleClipboard)==savedSample&&
                        instrumentClipboard&&io::encodeXi(*instrumentClipboard)==savedInstrument,
                        "Minimise preserves cell, PCM, sample-slot and instrument clipboards");
            };
            auto openSampleMinimise=[&]{
                sampleField=14;tap(Button::A);
                require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS","Sample PROCESS opens while retaining its PCM selection");
                const auto trim=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="TRIM SAMPLE TO SELECTION";});
                const auto target=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="MINIMISE SAMPLE";});
                require(trim!=actions.end()&&target!=actions.end()&&std::distance(trim,target)==1,
                        "Minimise Sample is available directly beside selection Trim");
                const int index=int(std::distance(actions.begin(),target));
                while(menuIndex<index)tap(Button::Down);
                while(menuIndex>index)tap(Button::Up);
                tap(Button::A);
            };
            auto pullMinimise=[](Playback& engine,std::size_t frames){
                std::vector<std::int16_t> pcm(frames*2);engine.readFrames(pcm.data(),frames);return pcm;
            };

            resetMinimiseFixture();
            const auto sampleBefore=io::encodeXm(doc);const auto sampleRevision=documentRevision,sampleRecovery=recoverySerial;
            const auto savedSampleClipboard=sampleClipboardBytes(*sampleClipboard);
            const auto savedInstrumentClipboard=io::encodeXi(*instrumentClipboard);
            Playback minimiseReference(playback->audioSettings());minimiseReference.load(doc);
            playback->previewSample(1,0);minimiseReference.previewSample(1,0);
            require(pullMinimise(*playback,257)==pullMinimise(minimiseReference,257),"Minimise cancellation starts with matching live sample preview PCM");
            openSampleMinimise();
            require(mode==Mode::Confirm&&confirmTitle=="MINIMISE SAMPLE?"&&confirmDetail=="1 SAMPLES / 20 PCM BYTES AFTER LOOP END",
                    "sample confirmation reports the affected sample and saved sixteen-bit XM PCM bytes");
            noticeUntil=0;draw();screenshot(home/"minimise-sample-confirm.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            require(mode==Mode::Normal&&io::encodeXm(doc)==sampleBefore&&!dirty&&undoStack.empty()&&
                    documentRevision==sampleRevision&&recoverySerial==sampleRecovery&&selectionStart==14&&selectionEnd==18&&waveformZoom,
                    "canceling Minimise retains PCM, selection, zoom and document history");
            pullMinimise(*playback,512);
            const auto cancelledPreviewPcm=pullMinimise(*playback,2048);
            require(!playback->previewPlaying()&&std::none_of(cancelledPreviewPcm.begin(),cancelledPreviewPcm.end(),
                    [](std::int16_t value){return value!=0;}),
                    "opening Minimise stops the editor preview and canceling does not restart it");
            checkMinimiseClipboards(savedSampleClipboard,savedInstrumentClipboard);
            tap(Button::Start);
            const auto resumedPreview=playback->samplePreviewStatus();
            require(resumedPreview.active&&resumedPreview.instrument==1&&resumedPreview.sample==0&&
                    resumedPreview.frame>=14&&resumedPreview.frame<18,
                    "Start after canceling Minimise explicitly auditions the preserved sample selection");
            playback->stopPreview();
            auto expectedSample=doc;expectedSample.instruments[0].samples[0].pcm.resize(10);
            openSampleMinimise();tap(Button::A);
            require(mode==Mode::Normal&&undoStack.size()==1&&dirty&&io::encodeXm(doc)==io::encodeXm(expectedSample),
                    "Minimise Sample cuts only the PCM suffix after the loop, ignoring the selected tail range");
            require(selectionStart==10&&selectionEnd==10&&!waveformZoom,
                    "a removed selection clamps to the new sample end and leaves invalid waveform zoom");
            checkMinimiseClipboards(savedSampleClipboard,savedInstrumentClipboard);
            undo();require(io::encodeXm(doc)==sampleBefore,"one Undo restores the exact sample PCM and all XM metadata");
            undo(true);require(io::encodeXm(doc)==io::encodeXm(expectedSample),"Redo restores the minimised sample without renumbering slots or maps");
            const auto noTailUndo=undoStack.size(),noTailRedo=redoStack.size();
            const auto noTailRevision=documentRevision,noTailRecovery=recoverySerial;
            playback->playPattern(0);pullMinimise(*playback,257);const auto noTailClock=playback->status().elapsedSeconds;
            openSampleMinimise();
            require(mode==Mode::Normal&&undoStack.size()==noTailUndo&&redoStack.size()==noTailRedo&&
                    documentRevision==noTailRevision&&recoverySerial==noTailRecovery&&io::encodeXm(doc)==io::encodeXm(expectedSample)&&
                    playback->status().playing&&playback->status().elapsedSeconds==noTailClock,
                    "a sample with no remaining loop tail is a no-op and does not stop continuous playback");

            resetMinimiseFixture();
            const auto instrumentBefore=io::encodeXm(doc);auto expectedInstrument=doc;
            expectedInstrument.instruments[0].samples[0].pcm.resize(10);expectedInstrument.instruments[0].samples[1].pcm.resize(8);
            playback->playPattern(0);pullMinimise(*playback,257);const auto instrumentClock=playback->status().elapsedSeconds;
            menu("MINIMISE INSTRUMENT");
            require(mode==Mode::Confirm&&confirmTitle=="MINIMISE INSTRUMENT?"&&confirmDetail=="2 SAMPLES / 29 PCM BYTES AFTER LOOP END",
                    "instrument Minimise totals eight-bit and sixteen-bit stored PCM bytes separately");
            tap(Button::B);
            require(io::encodeXm(doc)==instrumentBefore&&undoStack.empty()&&!dirty&&playback->status().playing&&
                    playback->status().elapsedSeconds==instrumentClock,"canceling instrument Minimise preserves the song and continuous playback");
            menu("MINIMISE INSTRUMENT");tap(Button::A);
            require(io::encodeXm(doc)==io::encodeXm(expectedInstrument)&&undoStack.size()==1,
                    "instrument Minimise cuts both active loop tails but preserves disabled loops, complete loops and other instruments");
            checkMinimiseClipboards(savedSampleClipboard,savedInstrumentClipboard);
            undo();require(io::encodeXm(doc)==instrumentBefore,"one Undo recovers every sample changed in the instrument batch");
            undo(true);require(io::encodeXm(doc)==io::encodeXm(expectedInstrument),"instrument batch Redo restores all loop-tail cuts together");

            resetMinimiseFixture();const auto allBefore=io::encodeXm(doc);auto expectedAll=doc;
            expectedAll.instruments[0].samples[0].pcm.resize(10);expectedAll.instruments[0].samples[1].pcm.resize(8);
            expectedAll.instruments[1].samples[0].pcm.resize(15);
            menu("MINIMISE ALL INSTRUMENTS");
            require(mode==Mode::Confirm&&confirmTitle=="MINIMISE ALL INSTRUMENTS?"&&confirmDetail=="3 SAMPLES / 59 PCM BYTES AFTER LOOP END",
                    "Song Project Minimise includes loop tails in unused instruments without removing their slots");
            tap(Button::B);require(io::encodeXm(doc)==allBefore&&undoStack.empty()&&!dirty,"canceling the whole-song batch changes no instrument");
            menu("MINIMISE ALL INSTRUMENTS");tap(Button::A);
            require(io::encodeXm(doc)==io::encodeXm(expectedAll)&&undoStack.size()==1,
                    "all-instrument Minimise preserves IDs, key maps, envelopes, loop metadata and every uncut PCM prefix");
            checkMinimiseClipboards(savedSampleClipboard,savedInstrumentClipboard);
            undo();require(io::encodeXm(doc)==allBefore,"the complete all-instrument batch restores with one Undo");
            undo(true);require(io::encodeXm(doc)==io::encodeXm(expectedAll),"whole-song Redo reproduces the exact minimised XM");
            const auto allNoopUndo=undoStack.size(),allNoopRedo=redoStack.size();const auto allNoopRevision=documentRevision,allNoopRecovery=recoverySerial;
            playback->playPattern(0);const auto allNoopClock=playback->status().elapsedSeconds;
            menu("MINIMISE ALL INSTRUMENTS");
            require(mode==Mode::Normal&&io::encodeXm(doc)==io::encodeXm(expectedAll)&&undoStack.size()==allNoopUndo&&
                    redoStack.size()==allNoopRedo&&documentRevision==allNoopRevision&&recoverySerial==allNoopRecovery&&
                    playback->status().playing&&playback->status().elapsedSeconds==allNoopClock,
                    "an already minimised song creates no confirmation or new undo step and leaves playback running");

            resetMinimiseFixture();sample=2;selectionStart=selectionEnd=0;waveformZoom=false;normalize();
            const auto disabledLoopBefore=io::encodeXm(doc);openSampleMinimise();
            require(mode==Mode::Normal&&io::encodeXm(doc)==disabledLoopBefore&&undoStack.empty()&&!dirty,
                    "disabled loop markers cannot make Minimise trim a one-shot sample");
            sample=15;normalize();openSampleMinimise();
            require(mode==Mode::Normal&&doc.instruments[0].samples.size()==4&&io::encodeXm(doc)==disabledLoopBefore&&undoStack.empty(),
                    "Minimise on an absent sample slot is a no-op without lazy allocation");
            instrument=128;sample=0;normalize();menu("MINIMISE INSTRUMENT");
            require(mode==Mode::Normal&&doc.instruments.size()==2&&io::encodeXm(doc)==disabledLoopBefore&&undoStack.empty(),
                    "Minimise on an absent instrument is a no-op without creating headers");

            resetMinimiseFixture();const auto staleMinimiseDoc=io::encodeXm(doc);openSampleMinimise();
            ++documentRevision;bool staleMinimiseRejected=false;
            try{tap(Button::A);}catch(const std::exception&){staleMinimiseRejected=true;}
            require(staleMinimiseRejected&&io::encodeXm(doc)==staleMinimiseDoc&&undoStack.empty()&&!dirty,
                    "a stale Minimise confirmation cannot mutate a changed document revision");

            playback->stop();doc=minimiseOriginalDoc;clipboard=minimiseOriginalCellClipboard;pcmClipboard=minimiseOriginalPcmClipboard;
            sampleClipboard=minimiseOriginalSampleClipboard;instrumentClipboard=minimiseOriginalInstrumentClipboard;
            page=Page::Sample;mode=Mode::Normal;instrument=1;sample=0;sampleField=14;order=row=selectedPattern=channel=field=0;solo=-1;
            clearSelection();selectionStart=selectionEnd=0;waveformZoom=false;held.fill(false);
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=minimiseOriginalDirty;++documentRevision;normalize();playback->load(doc);noticeUntil=0;
        }
