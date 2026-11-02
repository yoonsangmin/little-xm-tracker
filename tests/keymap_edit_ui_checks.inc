        {
            playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
            held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            doc=makeDemoDocument();
            const auto originalSample=doc.instruments[0].samples[0];
            doc.instruments[0].samples.assign(4,originalSample);
            doc.instruments[0].noteToSample.fill(0);
            doc.instruments[0].noteToSample[48]=1;
            doc.instruments[0].noteToSample[49]=2;
            doc.instruments[0].noteToSample[50]=3;
            Instrument recipient=doc.instruments[0];recipient.name="DESTINATION";
            recipient.samples.resize(1);recipient.samples[0].name="DESTINATION PCM";
            recipient.noteToSample.fill(0);doc.instruments.push_back(recipient);
            instrumentClipboard=recipient;keymapClipboard.clear();
            page=Page::Instrument;mode=Mode::Normal;instrumentMapView=true;instrumentField=14;
            instrument=1;sample=0;keymapNote=49;selectedPattern=order=row=channel=field=0;
            selectionStart=selectionEnd=0;songAppendOrder=false;
            waveformZoom=followPlayback=dirty=false;clearSelection();
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            const auto keymapDocument=io::encodeXm(doc);

            optionChord(Button::Down);
            require(keymapNote==73&&instrument==1&&instrumentMapView&&instrumentField==14,
                    "Key map B+Down pages 24 notes without switching instruments or leaving the map");
            optionChord(Button::Down);optionChord(Button::Down);
            require(keymapNote==96&&instrument==1,"Key map paging clamps at the last note");
            keymapNote=1;optionChord(Button::Up);
            require(keymapNote==1&&instrumentField==14,"Key map paging clamps at the first note");
            tap(Button::B);
            require(!instrumentMapView&&instrumentField==0&&keymapClipboard.empty()&&mode==Mode::Normal,
                    "B with no key map selection returns to instrument settings without copying or opening Actions");
            instrumentField=12;tap(Button::A);keymapNote=50;
            require(instrumentMapView&&io::encodeXm(doc)==keymapDocument&&!dirty&&undoStack.empty(),
                    "Key map navigation and returning to settings leave XM and history untouched");

            selectChord(Button::B);
            require(hasSelection()&&keymapRange()==std::pair<int,int>{49,50},
                    "Select-first B begins a one-note key map selection");
            selectChord(Button::B);
            require(hasSelection()&&keymapRange()==std::pair<int,int>{48,72},
                    "The next Select+B selects the current 24-note page");
            selectChord(Button::B);
            require(hasSelection()&&keymapRange()==std::pair<int,int>{0,96},
                    "The next Select+B selects all 96 key map notes");
            selectChord(Button::B);
            require(!hasSelection()&&instrumentMapView&&mode==Mode::Normal&&keymapClipboard.empty(),
                    "The final Select+B cancels without copying, closing the map or opening Actions");

            keymapNote=49;selectChord(Button::B);tap(Button::Down);tap(Button::Down);
            optionChord(Button::Down);
            require(hasSelection()&&keymapRange()==std::pair<int,int>{48,75}&&instrument==1,
                    "B+Down extends an existing key map range across a page while keeping its anchor");
            optionChord(Button::Up);
            require(hasSelection()&&keymapRange()==std::pair<int,int>{48,51},
                    "B+Up restores the previous range endpoint without clearing selection");
            tap(Button::Left);tap(Button::Right);
            require(instrumentField==14&&hasSelection(),
                    "Horizontal navigation cannot accidentally leave an active key map range");
            noticeUntil=0;draw();
            int mapWidth=0,mapHeight=0;SDL_GetRendererOutputSize(renderer,&mapWidth,&mapHeight);
            if(mapWidth==W&&mapHeight==H) {
                const auto selectedColor=(std::uint32_t(selectionColor.r)<<16)|(std::uint32_t(selectionColor.g)<<8)|selectionColor.b;
                const auto normalColor=(std::uint32_t(bg.r)<<16)|(std::uint32_t(bg.g)<<8)|bg.b;
                for(int x:{363,477,478,513,514,1000}) {
                    const auto expected=x>=478&&x<514?selectedColor:normalColor;
                    const SDL_Rect selectedGap{x,142+23-3,1,1};std::uint32_t pixel=0;
                    require(SDL_RenderReadPixels(renderer,&selectedGap,SDL_PIXELFORMAT_ARGB8888,&pixel,sizeof(pixel))==0&&
                            (pixel&0x00ffffffu)==expected,
                            "Key map selection colors only the sample-number input, leaving note and name columns unchanged");
                }
            }
            screenshot(home/"keymap-selection.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            require(!hasSelection()&&instrumentMapView&&keymapClipboard==std::vector<std::uint8_t>({1,2,3})&&
                    instrumentClipboard&&instrumentClipboard->name=="DESTINATION",
                    "B copies the selected mapping numbers without closing the map or replacing the whole-instrument clipboard");
            require(io::encodeXm(doc)==keymapDocument&&!dirty&&undoStack.empty(),
                    "Range selection, paging and copy do not change the song");

            keymapNote=81;selectChord(Button::A);
            require(doc.instruments[0].noteToSample[80]==1&&doc.instruments[0].noteToSample[81]==2&&
                    doc.instruments[0].noteToSample[82]==3&&doc.instruments[0].noteToSample[79]==0&&
                    doc.instruments[0].noteToSample[83]==0&&undoStack.size()==1,
                    "Select+A pastes only the copied mapping range at the cursor as one undoable edit");
            const auto pastedKeymap=io::encodeXm(doc);
            undo();require(io::encodeXm(doc)==keymapDocument,"Undo restores all overwritten mapping entries");
            undo(true);require(io::encodeXm(doc)==pastedKeymap,"Redo restores the complete mapping paste");
            undo();keymapNote=95;
            bool mapOverflow=false;
            try {selectChord(Button::A);}catch(const std::runtime_error& error) {
                mapOverflow=std::string(error.what()).find("96 NOTES")!=std::string::npos;
                buttonInput(Button::A,false);buttonInput(Button::Select,false);
            }
            require(mapOverflow&&io::encodeXm(doc)==keymapDocument&&undoStack.empty()&&
                    keymapClipboard==std::vector<std::uint8_t>({1,2,3}),
                    "A paste beyond note 96 is rejected atomically and keeps the mapping clipboard");

            keymapNote=50;selectChord(Button::B);tap(Button::Down);tap(Button::Select);
            require(!hasSelection()&&instrumentMapView&&mode==Mode::Normal&&
                    keymapClipboard==std::vector<std::uint8_t>({1,2,3}),
                    "Select alone cancels a key map selection without copying it or opening Actions");
            keymapNote=50;
            buttonInput(Button::A,true);buttonInput(Button::B,true);
            buttonInput(Button::A,false);buttonInput(Button::B,false);
            require(instrumentMapView&&doc.instruments[0].noteToSample[49]==0&&
                    keymapClipboard==std::vector<std::uint8_t>({2})&&undoStack.size()==1,
                    "A-first cut resets one mapping to 00 and consumes both releases without returning to settings");
            undo();require(io::encodeXm(doc)==keymapDocument,"Undo restores the single-note mapping cut");
            buttonInput(Button::B,true);buttonInput(Button::A,true);
            buttonInput(Button::B,false);buttonInput(Button::A,false);
            require(instrumentMapView&&doc.instruments[0].noteToSample[49]==0&&undoStack.size()==1,
                    "B-first cut and the opposite release order perform one edit without leaving the key map");
            undo();keymapNote=49;selectChord(Button::B);tap(Button::Down);tap(Button::Down);
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            auto cutExpected=doc;cutExpected.instruments[0].noteToSample[48]=1;
            cutExpected.instruments[0].noteToSample[49]=2;cutExpected.instruments[0].noteToSample[50]=3;
            require(!hasSelection()&&instrumentMapView&&doc.instruments[0].noteToSample[48]==0&&
                    doc.instruments[0].noteToSample[49]==0&&doc.instruments[0].noteToSample[50]==0&&
                    keymapClipboard==std::vector<std::uint8_t>({1,2,3})&&io::encodeXm(cutExpected)==keymapDocument,
                    "Cut clears the selected mappings to 00 without deleting sample PCM, metadata or other instrument data");
            undo();require(io::encodeXm(doc)==keymapDocument,"Undo restores the entire selected mapping cut");

            keymapNote=49;selectChord(Button::B);tap(Button::Down);tap(Button::Down);
            chord({Button::Right,Button::Right});
            require(hasSelection()&&doc.instruments[0].noteToSample[48]==3&&
                    doc.instruments[0].noteToSample[49]==4&&doc.instruments[0].noteToSample[50]==5&&
                    doc.instruments[0].noteToSample[47]==0&&doc.instruments[0].noteToSample[51]==0&&
                    undoStack.size()==1&&doc.instruments[0].samples.size()==6,
                    "A-held range editing changes every selected mapping, creates required slots and commits one gesture");
            undo();require(io::encodeXm(doc)==keymapDocument,"Undo removes slots created by a key map gesture as well as restoring assignments");
            buttonInput(Button::A,true);for(int n=0;n<20;++n)tap(Button::Up);buttonInput(Button::A,false);
            require(doc.instruments[0].noteToSample[48]==15&&doc.instruments[0].noteToSample[49]==15&&
                    doc.instruments[0].noteToSample[50]==15&&doc.instruments[0].samples.size()==16&&undoStack.size()==1,
                    "Repeated A+Up clamps selected sample numbers to 0F without exceeding sixteen slots");
            undo();require(io::encodeXm(doc)==keymapDocument,"Undo restores the complete clamped range gesture");
            tap(Button::Select);tap(Button::B);optionChord(Button::Down);
            require(instrument==2&&!instrumentMapView,"B+Down in settings still changes instrument number");
            instrumentField=12;tap(Button::A);keymapNote=73;selectChord(Button::A);
            require(doc.instruments[1].noteToSample[72]==1&&doc.instruments[1].noteToSample[73]==2&&
                    doc.instruments[1].noteToSample[74]==3&&doc.instruments[1].samples.size()==4&&
                    doc.instruments[1].samples[0].name=="DESTINATION PCM"&&
                    doc.instruments[1].samples[0].pcm==originalSample.pcm&&
                    doc.instruments[1].samples[1].pcm.empty()&&doc.instruments[1].samples[2].pcm.empty()&&
                    doc.instruments[1].samples[3].pcm.empty(),
                    "Pasting mappings into another instrument creates empty destination slots without copying source samples");
            undo();require(io::encodeXm(doc)==keymapDocument,"Cross-instrument paste Undo restores the destination sample count and mapping");

            instrument=128;keymapNote=1;clearSelection();dirty=false;
            chord({Button::Left});selectChord(Button::B);tap(Button::B);selectChord(Button::A);
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(!currentInstrument()&&!dirty&&undoStack.empty()&&io::encodeXm(doc)==keymapDocument,
                    "Copy, paste, cut and decrement of an implicit all-zero map never create an unused instrument");
            chord({Button::Right});
            require(currentInstrument()&&currentInstrument()->noteToSample[0]==1&&currentInstrument()->samples.size()==2,
                    "An actual mapping edit creates an implicit instrument and only its required sample slots");
            undo();require(!currentInstrument()&&io::encodeXm(doc)==keymapDocument,
                    "Undo removes an instrument created solely by its first key map edit");

            playback->stop();playback->stopPreview();held.fill(false);clearSelection();keymapClipboard.clear();
            instrument=1;sample=0;keymapNote=49;instrumentMapView=false;instrumentField=0;
            instrumentTopFocus=8;instrumentEnvelopeFocus=10;page=Page::Pattern;mode=Mode::Normal;
            aUsed=bUsed=true;selectPending=selectUsed=false;cutChordUsed=modalBackHeld=channelModifier=false;channelChord=0;
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;noticeUntil=0;
            normalize();playback->load(doc);
        }
