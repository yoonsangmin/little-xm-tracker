        {
            // Navigation reads implicit slots; only successful edits may grow XM.
            playback->stop();doc=makeDemoDocument();doc.orders={0,255};page=Page::Pattern;mode=Mode::Normal;
            selectedPattern=order=row=channel=field=0;instrument=1;sample=0;songAppendOrder=false;songOrderColumn=0;
            held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
            clearSelection();gestureBefore.reset();followPlayback=false;dirty=false;
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            const auto untouched=io::encodeXm(doc);const auto untouchedRecovery=recoverySerial;
            const auto storedPatternCount=doc.patterns.size();
            for(int n=0;n<260;++n)optionChord(Button::Down);
            require(order==1&&selectedPattern==255&&pattern().rows==64&&cell().note==0,
                    "B+Down reaches the order's implicit pattern FF and clamps at the final order");
            normalize();draw();
            require(selectedPattern==255&&!dirty&&undoStack.empty()&&recoverySerial==untouchedRecovery&&io::encodeXm(doc)==untouched,
                    "browsing, normalizing and drawing an uncreated pattern never edits XM");
            tap(Button::Start);require(playback->status().playing&&playback->status().pattern==255,"Start can loop an implicit empty pattern");
            draw();require(selectedPattern==255&&io::encodeXm(doc)==untouched,"following an empty pattern does not move back to a stored ID");
            tap(Button::Start);
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::A,false);buttonInput(Button::B,false);
            mutatePattern([&](Pattern& p){edit::transposeBlock(p,doc.channels,{0,1,0,1},1);});
            require(io::encodeXm(doc)==untouched&&!dirty,"cutting or transposing an empty slot does not create it");
            field=0;tap(Button::A);
            require(doc.patterns.size()==256&&doc.patterns[255].at(0,0,doc.channels).note!=0&&doc.orders.front()==0,
                    "the first note creates the referenced pattern without changing the arrangement");
            undo();require(doc.patterns.size()==storedPatternCount&&selectedPattern==255&&cell().note==0,
                    "undo removes newly materialized data while preserving the empty slot cursor");

            changePage(Page::Instrument);instrument=1;sample=0;
            for(int n=0;n<130;++n)optionChord(Button::Down);
            require(instrument==128&&!currentInstrument(),"empty instrument 80 is reachable without allocation");
            const auto beforeInstrument=io::encodeXm(doc);
            instrumentField=8;tap(Button::A);tap(Button::B);
            instrumentField=10;tap(Button::A);tap(Button::B);
            require(io::encodeXm(doc)==beforeInstrument&&!currentInstrument(),"canceling name and envelope editing keeps a slot uncreated");
            instrumentField=3;chord({Button::Left});
            require(io::encodeXm(doc)==beforeInstrument,"clamping an empty instrument default is not an edit");
            chord({Button::Right});
            require(currentInstrument()&&currentInstrument()->fadeout==16&&currentInstrument()->samples.size()==1,
                    "editing metadata materializes an instrument with a sample header so XM retains metadata");
            const auto instrumentRoundtrip=io::decodeXm(io::encodeXm(doc));
            require(instrumentRoundtrip.instruments[127].fadeout==16,"new instrument metadata survives XM round trip");
            undo();require(!currentInstrument()&&instrument==128,"undo retains the selected unused instrument number");

            changePage(Page::Sample);sample=0;
            for(int n=0;n<20;++n)optionChord(Button::Down);
            require(sample==15&&!currentSample(),"sample 0F is reachable inside an uncreated instrument");
            const auto beforeSample=io::encodeXm(doc);
            sampleField=11;tap(Button::A);tap(Button::B);
            require(!currentInstrument()&&io::encodeXm(doc)==beforeSample,"canceling an empty-slot Draw creates neither instrument nor sample");
            sampleField=0;chord({Button::Up});
            require(io::encodeXm(doc)==beforeSample,"clamping the default sample volume does not create a sample");
            chord({Button::Left});
            require(currentSample()&&currentSample()->volume==63&&currentInstrument()->samples.size()==16,
                    "sample metadata edit creates only the necessary slot span");
            undo();require(!currentSample()&&instrument==128&&sample==15,"undo preserves an implicit sample cursor");
            pcmClipboard=edit::PcmClip{{0,4096,-4096,0},true};selectChord(Button::A);
            require(currentSample()&&currentSample()->pcm==pcmClipboard->pcm,
                    "PCM paste works directly in an uncreated sample of an uncreated instrument");
            undo();sampleField=11;tap(Button::A);
            require(wavePresetOpen&&!currentSample(),"empty-slot DRAW opens creation settings without materializing a sample");
            tap(Button::A);tap(Button::Down);tap(Button::A); // BLANK -> FRAMES -> CREATE.
            require(drawingSample&&!currentSample(),"CREATE remains a local draft in an implicit sample slot");
            tap(Button::Down);tap(Button::Down);tap(Button::Right);tap(Button::A); // DRAW -> APPLY.
            require(currentSample()&&currentSample()->pcm.size()==32&&currentSample()->loopLength==32,
                    "Draw Apply creates the selected empty instrument and sample slot");
            playback->stop();page=Page::Pattern;selectedPattern=0;row=channel=field=0;instrument=1;sample=0;
            clearSelection();held.fill(false);normalize();mode=Mode::Normal;
        }
