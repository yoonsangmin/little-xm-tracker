        {
            const auto previousSampleLayoutDocument=doc;
            const auto previousSampleLayoutClipboard=pcmClipboard;
            auto resetSampleLayout=[&]{
                playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
                doc=makeDemoDocument();auto& s=doc.instruments[0].samples[0];
                s.name="RANGE EDITOR";s.pcm.resize(1024);s.sixteenBit=true;
                for(std::size_t n=0;n<s.pcm.size();++n)s.pcm[n]=std::int16_t(int(n)*31-16000);
                s.loopType=1;s.loopStart=100;s.loopLength=300;
                page=Page::Sample;mode=Mode::Normal;instrument=1;sample=0;sampleField=9;
                order=row=selectedPattern=channel=field=0;solo=-1;keymapNote=49;
                clearSelection();selectionStart=selectionEnd=0;waveformZoom=false;followPlayback=false;
                held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
                directionPressedAt.fill(0);editPressedAt=0;
                cutChordUsed=modalBackHeld=channelModifier=selectPending=selectUsed=false;channelChord=0;
                menuParents.clear();sampleRowFocus.fill(-1);
                undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;++documentRevision;
                normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);
            };
            auto focusSampleLayout=[&](int target){
                // These coordinates describe the visible Sample form, including
                // both endpoint pairs on one row and its three lower controls.
                const std::array<int,15> rows{{1,1,2,2,4,3,3,3,3,0,0,0,4,-1,4}};
                const std::array<int,15> columns{{0,1,0,1,0,0,1,2,3,0,1,2,1,-1,2}};
                require(target>=0&&target<15&&rows[std::size_t(target)]>=0,"Sample focus target exists");
                for(int step=0;step<5;++step) {
                    require(sampleField>=0&&sampleField<15&&rows[std::size_t(sampleField)]>=0,
                            "Sample navigation selects only visible controls");
                    const int current=rows[std::size_t(sampleField)],desired=rows[std::size_t(target)];
                    if(current==desired)break;
                    tap(current<desired?Button::Down:Button::Up);
                }
                require(rows[std::size_t(sampleField)]==rows[std::size_t(target)],"Sample directions reach the requested form row");
                for(int step=0;step<4&&sampleField!=target;++step)
                    tap(columns[std::size_t(sampleField)]<columns[std::size_t(target)]?Button::Right:Button::Left);
                require(sampleField==target,"Sample directions select the requested item within its row");
            };

            resetSampleLayout();selectionStart=200;selectionEnd=600;
            const auto layoutBefore=io::encodeXm(doc);const auto layoutRevision=documentRevision,layoutRecovery=recoverySerial;
            tap(Button::Up);tap(Button::Left);require(sampleField==9,"Sample Name clamps at the top-left edge");
            tap(Button::Right);require(sampleField==10,"Name and Load share the Sample top row");
            tap(Button::Right);tap(Button::Right);require(sampleField==11,"New Wave is the last top-row action");
            tap(Button::Down);require(sampleField==1,"New Wave enters the right metadata column on first use");
            tap(Button::Left);require(sampleField==0,"Sample metadata left/right selects Volume or Panning");
            tap(Button::Up);require(sampleField==11,"Returning to the header restores New Wave");
            tap(Button::Down);require(sampleField==0,"Returning to metadata restores its last field");
            focusSampleLayout(5);tap(Button::Left);require(sampleField==5,"Loop START clamps at the left endpoint edge");
            tap(Button::Right);require(sampleField==6,"Loop END follows Loop START on the same row");
            tap(Button::Right);require(sampleField==7,"Right crosses from loop END to selection START");
            tap(Button::Right);tap(Button::Right);require(sampleField==8,"Selection END clamps at the right endpoint edge");
            focusSampleLayout(14);tap(Button::Right);tap(Button::Down);require(sampleField==14,"PROCESS clamps at the bottom-right edge");
            tap(Button::Left);require(sampleField==12,"DRAW sits immediately left of PROCESS in the selection box");
            tap(Button::Up);require(sampleField==8,"DRAW returns to the last selection endpoint");
            tap(Button::Down);require(sampleField==12,"Returning to selection tools remembers DRAW");
            tap(Button::Left);tap(Button::Left);require(sampleField==4,"Loop type clamps at the left of the bottom row");
            selectChord(Button::B);require(sampleField==8&&hasSelection(),"Select+B focuses selection END without entering a separate cursor mode");
            tap(Button::Left);require(sampleField==7,"Directions still move focus while PCM selection is active");
            tap(Button::Up);tap(Button::Down);tap(Button::A);
            require(mode==Mode::Normal&&selectionStart==200&&selectionEnd==600&&io::encodeXm(doc)==layoutBefore&&
                    undoStack.empty()&&!dirty&&documentRevision==layoutRevision&&recoverySerial==layoutRecovery,
                    "Navigation and plain A on range fields neither edit endpoints nor open PROCESS");
            focusSampleLayout(14);tap(Button::A);
            require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS"&&selectionStart==200&&selectionEnd==600,
                    "PROCESS explicitly opens tools without losing the selected range");
            tap(Button::B);require(mode==Mode::Normal&&selectionStart==200&&selectionEnd==600,"PROCESS Back retains both endpoints");

            focusSampleLayout(12);tap(Button::A);
            require(mode==Mode::WaveDraw&&!wavePresetOpen&&drawingSample&&waveDrawFirst==200&&waveDrawLast==600&&
                    drawingSample->pcm==currentSample()->pcm,
                    "Selection DRAW opens an existing-sample draft restricted to the selected range");
            tap(Button::B);
            focusSampleLayout(11);tap(Button::A);
            require(mode==Mode::WaveDraw&&wavePresetOpen&&!drawingSample,
                    "Top-row NEW WAVE opens creation settings instead of the selected PCM");
            tap(Button::B);
            require(mode==Mode::Normal&&sampleField==11&&selectionStart==200&&selectionEnd==600&&
                    io::encodeXm(doc)==layoutBefore&&undoStack.empty()&&!dirty&&documentRevision==layoutRevision&&recoverySerial==layoutRecovery,
                    "Canceling either Sample entry preserves PCM, selection, history and recovery state");

            focusSampleLayout(7);chord({Button::Right});
            require(selectionStart==201&&selectionEnd==600,"Selection START changes by one frame while preserving END");
            focusSampleLayout(8);chord({Button::Up});
            require(selectionStart==201&&selectionEnd==610,"Selection END coarse edit uses one percent of sample length");
            selectionStart=200;selectionEnd=204;focusSampleLayout(7);chord({Button::Up});
            require(selectionStart==203&&selectionEnd==204,"An active selection START cannot cross END or empty the range");
            focusSampleLayout(8);chord({Button::Down});
            require(selectionStart==203&&selectionEnd==204,"An active selection END clamps one frame after START");
            selectionStart=selectionEnd=0;clearSelection();focusSampleLayout(7);chord({Button::Up});
            require(selectionStart==10&&selectionEnd==11,"Editing START on an empty selection creates a one-frame range");
            selectionStart=selectionEnd=1024;focusSampleLayout(8);chord({Button::Left});
            require(selectionStart==1023&&selectionEnd==1024,"Editing END at the sample boundary creates a valid final-frame range");
            require(io::encodeXm(doc)==layoutBefore&&undoStack.empty()&&!dirty&&documentRevision==layoutRevision&&recoverySerial==layoutRecovery,
                    "Selection endpoint edits never modify XM data, history or recovery state");

            resetSampleLayout();const auto loopBefore=io::encodeXm(doc);
            focusSampleLayout(5);chord({Button::Right});
            require(currentSample()->loopStart==101&&currentSample()->loopLength==299,
                    "Loop START moves while preserving the exclusive END rather than the old loop length");
            focusSampleLayout(6);chord({Button::Left});
            require(currentSample()->loopStart==101&&currentSample()->loopLength==298,
                    "Loop END edits the exclusive end frame and keeps START fixed");
            require(undoStack.size()==2,"Separate loop endpoint gestures each create one undo entry");
            undo();undo();require(io::encodeXm(doc)==loopBefore,"Undo restores exact loop endpoints and PCM");
            currentSample()->loopLength=4;playback->load(doc);focusSampleLayout(5);chord({Button::Up});
            require(currentSample()->loopStart==103&&currentSample()->loopLength==1,
                    "An active loop START clamps below its existing END");
            focusSampleLayout(6);chord({Button::Down});
            require(currentSample()->loopStart==103&&currentSample()->loopLength==1,"Loop END cannot make an active loop empty");
            focusSampleLayout(4);chord({Button::Left});
            require(currentSample()->loopType==0&&currentSample()->loopStart==103&&currentSample()->loopLength==1,
                    "Turning looping Off preserves both loop markers");

            for(const auto& markers:std::array<std::pair<std::uint32_t,std::uint32_t>,2>{{{5000,20},{0xffffffffu,0xffffffffu}}}) {
                resetSampleLayout();auto& offSample=*currentSample();
                // XM stores byte offsets. Eight-bit PCM allows each marker to
                // reach uint32 max while their sum exceeds uint32, still valid
                // metadata when looping is Off.
                if(markers.first==0xffffffffu)edit::convertSampleDepth(offSample,false);
                offSample.loopType=0;offSample.loopStart=markers.first;offSample.loopLength=markers.second;
                playback->load(doc);const auto offMetadataBefore=io::encodeXm(doc);const auto offPcm=offSample.pcm;
                focusSampleLayout(5);tap(Button::Right);tap(Button::Left);focusSampleLayout(6);draw();
                require(io::encodeXm(doc)==offMetadataBefore&&undoStack.empty()&&!dirty&&
                        currentSample()->loopStart==markers.first&&currentSample()->loopLength==markers.second,
                        "Viewing or navigating preserves imported disabled loop markers outside PCM, including overflowing sums");
                for(int endpoint:{5,6})for(Button direction:{Button::Left,Button::Right}) {
                    focusSampleLayout(endpoint);chord({direction});
                    const auto& edited=*currentSample();
                    require(!edited.loopType&&edited.loopStart<edited.pcm.size()&&edited.loopLength>=1&&
                            std::uint64_t(edited.loopStart)+edited.loopLength<=edited.pcm.size()&&edited.pcm==offPcm&&undoStack.size()==1,
                            "Editing either disabled loop endpoint clamps oversized markers to nonempty PCM bounds without overflow");
                    undo();require(io::encodeXm(doc)==offMetadataBefore&&currentSample()->loopStart==markers.first&&
                            currentSample()->loopLength==markers.second,
                            "Undo restores the exact imported disabled loop metadata after a bounded endpoint edit");
                }
                focusSampleLayout(4);chord({Button::Right});
                const auto& enabled=*currentSample();
                require(enabled.loopType==1&&enabled.loopStart<enabled.pcm.size()&&enabled.loopLength>=1&&
                        std::uint64_t(enabled.loopStart)+enabled.loopLength<=enabled.pcm.size()&&enabled.pcm==offPcm&&undoStack.size()==1,
                        "Enabling an imported disabled loop normalizes oversized nonzero markers into a legal forward loop");
                undo();require(io::encodeXm(doc)==offMetadataBefore,"Undo enabling a loop restores its disabled out-of-range metadata");
            }

            resetSampleLayout();selectionStart=200;selectionEnd=600;
            const auto selectionSource=currentSample()->pcm;const auto selectionDocument=io::encodeXm(doc);
            selectChord(Button::B);focusSampleLayout(0);tap(Button::A);
            require(mode==Mode::Normal&&selectionStart==200&&selectionEnd==600,"A on metadata does not become PROCESS during Select+B selection");
            tap(Button::B);
            require(pcmClipboard&&pcmClipboard->pcm==std::vector<std::int16_t>(selectionSource.begin()+200,selectionSource.begin()+600)&&
                    !hasSelection()&&io::encodeXm(doc)==selectionDocument&&undoStack.empty(),
                    "B copies the selected range and ends selection without editing sample PCM");
            selectChord(Button::B);require(sampleField==8&&selectionStart==0&&selectionEnd==1024,"Select+B from no range selects the whole sample and focuses END");
            selectChord(Button::B);selectChord(Button::B);require(!hasSelection(),"Repeated Select+B still cycles through all then cancel");
            selectionStart=200;selectionEnd=600;selectChord(Button::B);tap(Button::Select);
            require(!hasSelection()&&mode==Mode::Normal&&io::encodeXm(doc)==selectionDocument,"Select alone cancels PCM selection without opening Actions");
            selectionStart=200;selectionEnd=600;selectChord(Button::B);
            const auto clipboardBeforeReset=pcmClipboard->pcm;
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(selectionStart==0&&selectionEnd==0&&!hasSelection()&&currentSample()->pcm==selectionSource&&
                    pcmClipboard->pcm==clipboardBeforeReset&&undoStack.empty()&&io::encodeXm(doc)==selectionDocument,
                    "A+B zeros selection END and clears START to keep a valid range, preserving PCM, clipboard and history");
            selectionStart=200;selectionEnd=600;focusSampleLayout(14);tap(Button::A);
            const auto cutAction=std::find_if(actions.begin(),actions.end(),[](const Action& action){return action.label=="CUT PCM";});
            require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS"&&cutAction!=actions.end(),
                    "PCM cutting is explicitly available in PROCESS rather than overloading numeric-field reset");
            const int cutActionIndex=int(std::distance(actions.begin(),cutAction));
            while(menuIndex<cutActionIndex)tap(Button::Down);
            while(menuIndex>cutActionIndex)tap(Button::Up);
            tap(Button::A);
            std::vector<std::int16_t> expectedCut(selectionSource.begin(),selectionSource.begin()+200);
            expectedCut.insert(expectedCut.end(),selectionSource.begin()+600,selectionSource.end());
            require(currentSample()->pcm==expectedCut&&undoStack.size()==1&&!hasSelection(),
                    "PROCESS Cut PCM deletes only the selected frames as one undoable edit");
            require(pcmClipboard&&pcmClipboard->pcm==clipboardBeforeReset,
                    "PROCESS Cut PCM copies the removed frames to the PCM clipboard");
            undo();require(io::encodeXm(doc)==selectionDocument,"One Undo restores the sample after range cut");

            // Removed PCM-paste chords must consume both keys even above an
            // actionable button, without falling through to A or Select taps.
            for(int context=0;context<3;++context) {
                resetSampleLayout();pcmClipboard=edit::PcmClip{{768,-512,256},false};
                if(context==1){selectionStart=200;selectionEnd=204;selectChord(Button::B);}
                if(context==2){sample=15;normalize();}
                focusSampleLayout(context==0?9:context==1?14:10);
                const auto beforeChord=io::encodeXm(doc);
                const auto chordRevision=documentRevision,chordRecovery=recoverySerial;
                const int chordField=sampleField,chordStart=selectionStart,chordEnd=selectionEnd;
                const bool chordSelecting=pcmSelecting;
                const Button first=context==1?Button::A:Button::Select;
                const Button second=context==1?Button::Select:Button::A;
                buttonInput(first,true);buttonInput(second,true);
                buttonInput(context==0?Button::Select:Button::A,false);
                buttonInput(context==0?Button::A:Button::Select,false);
                require(mode==Mode::Normal&&sampleField==chordField&&selectionStart==chordStart&&selectionEnd==chordEnd&&
                        pcmSelecting==chordSelecting&&io::encodeXm(doc)==beforeChord&&undoStack.empty()&&!dirty&&
                        documentRevision==chordRevision&&recoverySerial==chordRecovery&&
                        pcmClipboard&&pcmClipboard->pcm==std::vector<std::int16_t>({768,-512,256}),
                        "SAMPLE Select+A in either key order preserves data and selection without opening Name, Process, Actions or Load");
                if(context==2)require(!currentSample(),"Removed PCM-paste shortcut never creates an empty sample slot");
            }
            resetSampleLayout();selectionStart=200;selectionEnd=204;
            pcmClipboard=edit::PcmClip{{768,-512,256},false};
            const auto beforeProcessPaste=io::encodeXm(doc);const auto pasteSource=currentSample()->pcm;
            focusSampleLayout(14);tap(Button::A);
            const auto pasteAction=std::find_if(actions.begin(),actions.end(),[](const Action& action){return action.label=="PASTE PCM";});
            require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS"&&pasteAction!=actions.end(),
                    "Sample PROCESS explicitly exposes Paste PCM alongside range tools");
            const int pasteActionIndex=int(std::distance(actions.begin(),pasteAction));
            while(menuIndex<pasteActionIndex)tap(Button::Down);
            while(menuIndex>pasteActionIndex)tap(Button::Up);
            tap(Button::A);
            std::vector<std::int16_t> expectedPaste(pasteSource.begin(),pasteSource.begin()+200);
            expectedPaste.insert(expectedPaste.end(),{768,-512,256});
            expectedPaste.insert(expectedPaste.end(),pasteSource.begin()+204,pasteSource.end());
            require(mode==Mode::Normal&&currentSample()->pcm==expectedPaste&&undoStack.size()==1&&undoStack.back().label=="PASTE"&&
                    currentSample()->sixteenBit&&pcmClipboard->pcm==std::vector<std::int16_t>({768,-512,256}),
                    "PROCESS Paste replaces only the selected PCM range, preserving destination precision and the clipboard");
            undo();require(io::encodeXm(doc)==beforeProcessPaste,"One Undo restores PCM and loop metadata after PROCESS Paste");

            for(int resetField=0;resetField<=6;++resetField) {
                resetSampleLayout();selectionStart=200;selectionEnd=600;
                auto& resetSource=*currentSample();resetSource.volume=23;resetSource.panning=37;
                resetSource.finetune=29;resetSource.relativeNote=5;playback->load(doc);
                pcmClipboard=edit::copyPcm(resetSource,selectedFrames());const auto savedClip=pcmClipboard->pcm;
                const auto beforeReset=io::encodeXm(doc);auto expectedReset=doc;
                auto& expectedSample=expectedReset.instruments[0].samples[0];
                switch(resetField) {
                    case 0:expectedSample.volume=0;break;
                    case 1:expectedSample.panning=0;break;
                    case 2:expectedSample.finetune=0;break;
                    case 3:expectedSample.relativeNote=0;break;
                    case 4:expectedSample.loopType=0;break;
                    case 5:expectedSample.loopStart=0;expectedSample.loopLength=400;break;
                    case 6:expectedSample.loopStart=0;expectedSample.loopLength=0;expectedSample.loopType=0;break;
                }
                focusSampleLayout(resetField);
                const Button first=resetField%2?Button::B:Button::A,second=resetField%2?Button::A:Button::B;
                buttonInput(first,true);buttonInput(second,true);buttonDown(second,true);
                buttonInput(first,false);buttonInput(second,false);
                require(mode==Mode::Normal&&sampleField==resetField&&io::encodeXm(doc)==io::encodeXm(expectedReset)&&
                        undoStack.size()==1&&pcmClipboard->pcm==savedClip&&selectionStart==200&&selectionEnd==600,
                        "Either A+B press/release order zeros the focused value once with valid loop markers, preserving PCM, clipboard and selection");
                undo();require(io::encodeXm(doc)==beforeReset,"One Undo restores the sample parameter or loop endpoint reset exactly");
            }
            for(int endpoint:{7,8}) {
                resetSampleLayout();selectionStart=200;selectionEnd=600;focusSampleLayout(endpoint);
                pcmClipboard=edit::copyPcm(*currentSample(),selectedFrames());const auto savedClip=pcmClipboard->pcm;
                const auto beforeReset=io::encodeXm(doc);
                const auto beforeRevision=documentRevision,beforeRecovery=recoverySerial;
                buttonInput(Button::B,true);tap(Button::A);buttonInput(Button::B,false);
                require(selectionStart==0&&selectionEnd==(endpoint==8?0:600)&&
                        io::encodeXm(doc)==beforeReset&&undoStack.empty()&&!dirty&&
                        documentRevision==beforeRevision&&recoverySerial==beforeRecovery&&pcmClipboard->pcm==savedClip,
                        "Selection endpoint reset zeros its value and keeps a valid range without changing XM, recovery, Undo or clipboard");
                if(endpoint==8) {
                    chord({Button::Right});
                    require(selectionStart==0&&selectionEnd==1&&io::encodeXm(doc)==beforeReset&&undoStack.empty()&&!dirty,
                            "Selection END can be edited again from a cleared range without changing the sample");
                }
            }
            resetSampleLayout();sampleField=0;currentSample()->volume=0;playback->load(doc);
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(!dirty&&undoStack.empty(),"Resetting an already-zero sample value adds no Undo entry");
            currentSample()->loopType=0;currentSample()->loopStart=0;currentSample()->loopLength=0;
            playback->load(doc);focusSampleLayout(5);const auto disabledMarkers=io::encodeXm(doc);
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(io::encodeXm(doc)==disabledMarkers&&!dirty&&undoStack.empty(),
                    "Resetting an already-zero disabled loop START preserves its zero-length markers without a new edit");
            focusSampleLayout(6);chord({Button::Right});
            require(currentSample()->loopStart==0&&currentSample()->loopLength==1&&currentSample()->loopType==0&&undoStack.size()==1,
                    "A cleared loop END can be edited into a valid one-frame range while looping remains Off");
            undo();require(io::encodeXm(doc)==disabledMarkers,"Undo returns edited loop END to its cleared markers");
            dirty=false;redoStack.clear();
            const auto beforeEmptyReset=io::encodeXm(doc);
            for(int owner:{1,128}) {
                instrument=owner;sample=15;selectionStart=selectionEnd=0;normalize();
                for(int resetField=0;resetField<=8;++resetField) {
                    focusSampleLayout(resetField);buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
                    require(!currentSample()&&!dirty&&undoStack.empty()&&io::encodeXm(doc)==beforeEmptyReset,
                            "Reset on an implicit sample or instrument never creates a slot or edits another sample");
                }
            }

            resetSampleLayout();auto& longSample=*currentSample();longSample.pcm.resize(65536);
            longSample.loopStart=8000;longSample.loopLength=40000;selectionStart=8000;selectionEnd=48000;
            playback->load(doc);focusSampleLayout(7);
            const auto longBefore=io::encodeXm(doc);
            const auto longRevision=documentRevision,longRecovery=recoverySerial;
            auto heldRangeRepeat=[&](Button direction,Uint32 duration) {
                const auto now=SDL_GetTicks();editPressedAt=now-duration;directionPressedAt[std::size_t(direction)]=now-duration;
                buttonDown(direction,true);
            };
            buttonInput(Button::A,true);buttonInput(Button::Right,true);
            require(selectionStart==8001,"A short horizontal endpoint press still moves one frame in a long sample");
            heldRangeRepeat(Button::Right,800);require(selectionStart==8017,"Holding a horizontal range edit accelerates after 600 ms");
            heldRangeRepeat(Button::Right,1800);require(selectionStart==8081,"Longer held range editing reaches the faster 1400 ms step");
            buttonInput(Button::Right,false);buttonInput(Button::A,false);
            focusSampleLayout(8);selectionEnd=30000;
            buttonInput(Button::A,true);buttonInput(Button::Up,true);
            require(selectionEnd==30655,"A short vertical range press preserves the one-percent coarse step");
            heldRangeRepeat(Button::Up,800);require(selectionEnd==33275,"Held vertical range editing accelerates to four coarse steps");
            heldRangeRepeat(Button::Up,1800);require(selectionEnd==43755,"Long-held vertical range editing accelerates to sixteen coarse steps");
            for(int n=0;n<8;++n)heldRangeRepeat(Button::Up,1800);
            require(selectionEnd==65536&&selectionStart==8081,"Accelerated selection editing clamps at PCM bounds without moving the other endpoint");
            buttonInput(Button::Up,false);buttonInput(Button::A,false);
            focusSampleLayout(7);selectionStart=8000;selectionEnd=48000;
            buttonInput(Button::A,true);editPressedAt=SDL_GetTicks()-2000;buttonInput(Button::Right,true);
            require(selectionStart==8001,"A fresh direction press starts fine even when A has already been held for a long time");
            buttonInput(Button::Right,false);buttonInput(Button::A,false);
            buttonInput(Button::Right,true);directionPressedAt[std::size_t(Button::Right)]=SDL_GetTicks()-2000;
            buttonInput(Button::A,true);buttonDown(Button::Right,true);
            require(sampleField==8&&selectionEnd==48001,"A fresh A press starts fine even when the navigation direction was already held");
            buttonInput(Button::Right,false);buttonInput(Button::A,false);
            require(io::encodeXm(doc)==longBefore&&undoStack.empty()&&!dirty&&documentRevision==longRevision&&recoverySerial==longRecovery,
                    "All accelerated selection editing remains outside the XM document and history");

            focusSampleLayout(5);buttonInput(Button::A,true);buttonInput(Button::Right,true);
            heldRangeRepeat(Button::Right,800);heldRangeRepeat(Button::Right,1800);
            buttonInput(Button::Right,false);buttonInput(Button::A,false);
            require(currentSample()->loopStart==8081&&currentSample()->loopStart+currentSample()->loopLength==48000&&undoStack.size()==1,
                    "Accelerated loop START editing preserves END and commits all held repeats as one gesture");
            undo();require(io::encodeXm(doc)==longBefore,"Undo restores PCM and both loop endpoints after accelerated movement");
            focusSampleLayout(6);buttonInput(Button::A,true);buttonInput(Button::Down,true);
            for(int n=0;n<8;++n)heldRangeRepeat(Button::Down,1800);
            buttonInput(Button::Down,false);buttonInput(Button::A,false);
            require(currentSample()->loopStart==8000&&currentSample()->loopLength==1&&undoStack.size()==1,
                    "Accelerated loop END movement cannot cross START or create an empty loop");
            undo();require(io::encodeXm(doc)==longBefore,"Accelerated loop END has a single complete Undo");

            resetSampleLayout();selectionStart=128;selectionEnd=512;
            const auto selectionLoopBefore=io::encodeXm(doc);
            focusSampleLayout(14);tap(Button::A);
            const auto loopAction=std::find_if(actions.begin(),actions.end(),[](const Action& action){return action.label=="LOOP FROM SELECTION";});
            require(loopAction!=actions.end(),"PROCESS contains Loop From Selection in place of the removed top-level Set Loop button");
            const int loopActionIndex=int(std::distance(actions.begin(),loopAction));
            while(menuIndex<loopActionIndex)tap(Button::Down);
            while(menuIndex>loopActionIndex)tap(Button::Up);
            tap(Button::A);
            require(mode==Mode::Normal&&currentSample()->loopType==1&&currentSample()->loopStart==128&&currentSample()->loopLength==384&&undoStack.size()==1,
                    "Loop From Selection makes one forward loop edit with exclusive endpoints");
            undo();require(io::encodeXm(doc)==selectionLoopBefore,"Undo restores the prior loop after Loop From Selection");
            selectionStart=128;selectionEnd=512;sampleField=8;noticeUntil=0;draw();screenshot(home/"sample-layout-selection.bmp");SDL_RenderPresent(renderer);
            sampleField=6;noticeUntil=0;draw();screenshot(home/"sample-layout-loop.bmp");SDL_RenderPresent(renderer);
            selectionStart=selectionEnd=0;
            const auto whole=selectedFrames();require(whole.first==0&&whole.last==currentSample()->pcm.size(),"No selection continues to mean the whole sample for processing");

            playback->stop();playback->stopPreview();doc=previousSampleLayoutDocument;pcmClipboard=previousSampleLayoutClipboard;
            page=Page::Pattern;mode=Mode::Normal;instrument=1;sample=0;sampleField=0;order=row=selectedPattern=channel=field=0;solo=-1;
            clearSelection();selectionStart=selectionEnd=0;waveformZoom=false;held.fill(false);menuParents.clear();sampleRowFocus.fill(-1);
            aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;cutChordUsed=modalBackHeld=channelModifier=selectPending=selectUsed=false;channelChord=0;
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;++documentRevision;normalize();playback->load(doc);noticeUntil=0;
        }
