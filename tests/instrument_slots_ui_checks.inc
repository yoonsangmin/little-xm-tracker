        {
            playback->stop();playback->stopPreview();held.fill(false);
            aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            doc=makeDemoDocument();doc.orders={0,0};doc.patterns.push_back(doc.patterns[0]);
            doc.instruments[0].samples.push_back(doc.instruments[0].samples[0]);
            doc.instruments[0].samples[1].name="SECOND SAMPLE";
            doc.instruments[0].noteToSample[48]=1;
            doc.instruments.push_back(doc.instruments[0]);doc.instruments[1].name="SECOND INSTRUMENT";
            page=Page::Instrument;mode=Mode::Normal;instrumentMapView=false;instrumentField=12;
            instrument=1;sample=0;keymapNote=49;order=row=channel=field=selectedPattern=0;
            selectionStart=selectionEnd=0;songAppendOrder=false;waveformZoom=followPlayback=dirty=false;
            clearSelection();gestureBefore.reset();undoStack.clear();redoStack.clear();undoBytes=0;
            normalize();playback->load(doc);
            const auto originalSlots=io::encodeXm(doc);
            tap(Button::A);draw();
            require(instrumentMapView&&instrumentField==14&&io::encodeXm(doc)==originalSlots,
                    "KEY MAP opens the note table without changing instrument data");
            tap(Button::Down);chord({Button::Right});
            require(keymapNote==50&&doc.instruments[0].noteToSample[49]==1&&sample==0&&undoStack.size()==1,
                    "Mapping a note changes only its assignment, not the selected sample");
            undo();require(io::encodeXm(doc)==originalSlots,"Undo restores a key map edit");
            tap(Button::Left);tap(Button::Down);
            require(instrumentField==13&&sample==1&&io::encodeXm(doc)==originalSlots,
                    "Selecting an owned sample never changes any note mapping");
            tap(Button::A);
            require(page==Page::Sample&&sample==1&&io::encodeXm(doc)==originalSlots,
                    "A on the sample list opens SAMPLE without changing PCM");
            changePage(Page::Instrument);instrumentField=12;tap(Button::A);
            require(!instrumentMapView&&instrumentField==0,"SETTINGS returns from the note table");
            menu("COPY INSTRUMENT");
            require(instrumentClipboard&&instrumentClipboard->noteToSample==doc.instruments[0].noteToSample&&
                    instrumentClipboard->samples[1].name=="SECOND SAMPLE","Instrument copy still includes every mapping and sample");
            tap(Button::B); // Cancel only destination selection, preserving the copied instrument.

            instrumentField=13;
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(mode==Mode::Confirm&&io::encodeXm(doc)==originalSlots,"Sample-list A+B asks before clearing a slot");
            tap(Button::B);require(mode==Mode::Normal&&io::encodeXm(doc)==originalSlots,"Cancel sample deletion leaves all data intact");
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);tap(Button::A);
            require(mode==Mode::Normal&&doc.instruments[0].samples.size()==2&&doc.instruments[0].samples[1].pcm.empty()&&
                    doc.instruments[0].noteToSample[48]==1&&!doc.instruments[1].samples[1].pcm.empty(),
                    "Confirmed deletion clears only this sample and preserves numbering and key maps");
            undo();require(io::encodeXm(doc)==originalSlots,"Undo restores deleted sample metadata and PCM");

            menu("INSTRUMENT LIST");draw();
            require(mode==Mode::SlotList&&slotKind==SlotKind::Instrument&&!slotPaste&&slotIndex==0,"Instrument Actions opens the numbered slot list");
            chord({Button::Down});tap(Button::A);tap(Button::A);
            require(page==Page::Instrument&&instrument==17&&!currentInstrument()&&io::encodeXm(doc)==originalSlots,
                    "A+Down pages through empty slots; Choose then Open keeps empty slots implicit");
            menu("INSTRUMENT LIST");chord({Button::Up});tap(Button::A);tap(Button::Right);tap(Button::A);
            require(mode==Mode::Confirm&&io::encodeXm(doc)==originalSlots,"List Delete asks before clearing instrument contents");
            tap(Button::B);require(mode==Mode::SlotList&&slotIndex==0&&slotFocus==1,"Cancel returns to the same list row and Delete button");
            tap(Button::A);tap(Button::A);
            require(mode==Mode::SlotList&&doc.instruments.size()==2&&doc.instruments[0].samples.empty()&&
                    doc.instruments[1].name=="SECOND INSTRUMENT"&&doc.patterns[0].at(0,0,doc.channels).instrument==1,
                    "List clear keeps later instruments and pattern references in place");
            tap(Button::B);tap(Button::B);tap(Button::B);menu("UNDO");
            require(io::encodeXm(doc)==originalSlots,"Actions Undo restores an instrument deleted from the list");

            changePage(Page::Pattern);menu("PATTERN LIST");draw();
            buttonInput(Button::A,true);for(int n=0;n<16;++n)tap(Button::Down);buttonInput(Button::A,false);tap(Button::A);tap(Button::A);
            require(selectedPattern==255&&doc.orders==std::vector<std::uint8_t>({0,0})&&io::encodeXm(doc)==originalSlots,
                    "Opening implicit pattern FF neither creates data nor inserts an Order");
            menu("PATTERN LIST");buttonInput(Button::A,true);for(int n=0;n<16;++n)tap(Button::Up);buttonInput(Button::A,false);
            tap(Button::A);tap(Button::Right);tap(Button::A);tap(Button::A);
            require(mode==Mode::SlotList&&doc.patterns.size()==2&&doc.patterns[0].rows==32&&
                    doc.patterns[0].at(0,0,doc.channels).note==0&&doc.patterns[1].at(0,0,doc.channels).note==49&&
                    doc.orders==std::vector<std::uint8_t>({0,0}),"Pattern clear preserves its length, shared Orders and other patterns");
            tap(Button::B);tap(Button::B);tap(Button::B);menu("UNDO");
            require(io::encodeXm(doc)==originalSlots,"Undo restores all shared pattern contents");
            selectPattern(0);instrument=1;sample=0;instrumentMapView=false;instrumentField=0;
            clearSelection();mode=Mode::Normal;held.fill(false);
        }
