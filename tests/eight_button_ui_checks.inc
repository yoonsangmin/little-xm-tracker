        {
            // Deferred SDL-event regressions: chord release must never become
            // an extra edit, copy, back action or menu-open action.
            auto resetEightButtons=[&] {
                playback->stop();playback->stopPreview();held.fill(false);
                aUsed=bUsed=true;cutChordUsed=false;modalBackHeld=false;selectPending=selectUsed=false;
                channelModifier=false;channelChord=0;solo=-1;playback->setSolo(-1);playback->setMuteMask(0);
                doc=makeDemoDocument();edit::resizeChannels(doc,32);
                doc.patterns.push_back(doc.patterns[0]);
                doc.instruments.push_back(doc.instruments[0]);
                doc.instruments[0].samples.push_back(doc.instruments[0].samples[0]);
                page=Page::Pattern;mode=Mode::Normal;
                selectedPattern=order=row=channel=field=0;instrument=1;sample=0;
                songAppendOrder=false;songOrderColumn=0;
                patternViewChannels=8;patternViewRows=32;followPlayback=false;clearSelection();
                selectionStart=selectionEnd=0;waveformZoom=false;dirty=false;
                copyMask={};pasteMask={};copiedMask={};
                clipboard.reset();blockClipboard.reset();orderClipboard.reset();orderBlockClipboard.clear();
                undoStack.clear();redoStack.clear();undoBytes=0;
                normalize();playback->load(doc);
            };

            // Special notes are reachable only through single-cell entry;
            // block transpose must retain its existing all-or-nothing bounds.
            for(const auto lowerKey:{Button::Left,Button::Down})for(const int sourceInstrument:{0,2}) {
                resetEightButtons();
                const Cell lowest{1,std::uint8_t(sourceInstrument),0x39,4,0x47};
                doc.patterns[0].at(0,0,doc.channels)=lowest;playback->load(doc);
                auto off=lowest;off.note=97;
                chord({lowerKey});
                require(same(cell(),off)&&undoStack.size()==1&&row==0&&field==0,
                        "Lowering C-0 enters OFF without adding an instrument or changing volume/FX/cursor");
                chord({lowerKey});
                require(same(cell(),off)&&undoStack.size()==1,"Repeated lowering stays OFF without extra undo entries");
                undo();require(same(cell(),lowest),"Undo restores C-0 and every original column");
                undo(true);
                chord({lowerKey==Button::Left?Button::Right:Button::Up});
                require(same(cell(),lowest),"Raising OFF returns to C-0 for both fine and octave edits");
            }
            resetEightButtons();
            doc.patterns[0].at(0,0,doc.channels)={2,1,0x39,4,0x47};playback->load(doc);
            chord({Button::Down});require(cell().note==1,"C#0 octave-down clamps to C-0 before entering OFF");
            chord({Button::Down});require(cell().note==97,"Another downward input at C-0 enters OFF");
            const auto offRoundTrip=io::decodeXm(io::encodeXm(doc));
            require(same(offRoundTrip.patterns[0].at(0,0,doc.channels),cell()),"OFF entry remains a standard XM key-off after save/load");
            undo();require(cell().note==1,"Undo OFF preserves the preceding octave-down edit");
            undo();require(cell().note==2,"A separate Undo restores the original C#0");

            resetEightButtons();
            doc.patterns[0].at(0,0,doc.channels)={1,1,0x39,4,0x47};playback->load(doc);
            std::array<std::int16_t,2048> notePreview{};
            playback->previewNote(1,49);playback->readFrames(notePreview.data(),notePreview.size()/2);
            require(std::any_of(notePreview.begin(),notePreview.end(),[](auto value){return value!=0;}),"Preview fixture is audible before OFF entry");
            chord({Button::Left});playback->readFrames(notePreview.data(),notePreview.size()/2);
            require(std::all_of(notePreview.begin(),notePreview.end(),[](auto value){return value==0;}),"Entering OFF stops standalone audition without substituting a pitched note");
            tap(Button::A);playback->readFrames(notePreview.data(),notePreview.size()/2);
            require(cell().note==97&&std::all_of(notePreview.begin(),notePreview.end(),[](auto value){return value==0;}),
                    "A on OFF also stays silent and preserves the key-off event");
            menu("PATTERN TOOLS");
            require(std::none_of(actions.begin(),actions.end(),[](const Action& action){return action.label=="INSERT NOTE OFF";}),
                    "Pattern Actions no longer duplicate direct OFF entry");
            tap(Button::B);tap(Button::B);

            for(const auto lowerKey:{Button::Left,Button::Down}) {
                resetEightButtons();
                doc.patterns[0].at(0,0,doc.channels)={1,0,0x39,4,0x47};
                doc.patterns[0].at(1,0,doc.channels)={49,1,0x40,2,0x10};playback->load(doc);
                selectChord(Button::B);tap(Button::Down);
                const auto beforeLower=io::encodeXm(doc);
                bool rejectedLower=false;buttonInput(Button::A,true);
                try{buttonInput(lowerKey,true);}catch(const std::exception&){rejectedLower=true;}
                buttonInput(lowerKey,false);buttonInput(Button::A,false);
                require(rejectedLower&&hasSelection()&&undoStack.empty()&&!dirty&&io::encodeXm(doc)==beforeLower,
                        "Lowering a selected range containing C-0 rejects the whole transpose without creating OFF");
            }

            for(bool reversePress:{false,true})for(bool reverseRelease:{false,true}) {
                resetEightButtons();
                const Cell original{61,1,0x37,4,0x47};
                doc.patterns[0].at(0,0,doc.channels)=original;playback->load(doc);
                const Button first=reversePress?Button::B:Button::A;
                const Button second=reversePress?Button::A:Button::B;
                buttonInput(first,true);buttonInput(second,true);
                buttonInput(reverseRelease?second:first,false);buttonInput(reverseRelease?first:second,false);
                require(cell().note==0&&cell().instrument==original.instrument&&cell().volume==original.volume&&
                        cell().effect==original.effect&&cell().param==original.param,
                        "A+B cuts only the current field for either press and release order");
                require(mode==Mode::Normal&&clipboard&&same(*clipboard,original)&&undoStack.size()==1,
                        "A+B releases do not reinsert a note, overwrite the clipboard or create another edit");
                undo();require(same(cell(),original),"one Undo restores an A+B field cut");
            }

            resetEightButtons();
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x40,4,0x47};
            doc.patterns[0].at(0,1,doc.channels)={53,1,0x32,0x0a,1};
            doc.patterns[0].at(1,0,doc.channels)={56,1,0x30,3,0x20};
            doc.patterns[0].at(1,1,doc.channels)={61,1,0x28,0,0x37};
            const auto sourceBlock=edit::copyBlock(pattern(),doc.channels,{0,2,0,2});
            playback->load(doc);
            selectChord(Button::B);tap(Button::Down);
            require(blockSelected&&selectedBlock().lastRow==2&&selectedBlock().lastChannel==1&&
                    selectedBlock().firstField==0&&selectedBlock().lastField==1,
                    "vertical selection stays within the note column");
            chord({Button::Right});
            for(int r=0;r<2;++r)for(int c=0;c<2;++c) {
                const auto& old=sourceBlock.cells[std::size_t(r*2+c)];
                const auto& transposed=pattern().at(r,c,doc.channels);
                require(transposed.note==old.note+(c==0?1:0)&&transposed.instrument==old.instrument&&
                        transposed.volume==old.volume&&transposed.effect==old.effect&&transposed.param==old.param,
                        "A+Right changes selected notes without touching unselected fields or channels");
            }
            undo();
            require(hasSelection()&&same(pattern().at(0,0,doc.channels),sourceBlock.cells[0]),
                    "one Undo restores a selected-block transpose without cancelling the selection");
            for(int n=0;n<9;++n)tap(Button::Right);
            require(channel==1&&field==4&&selectedBlock().lastChannel==2&&selectedBlock().lastField==5,
                    "horizontal selection expands one field at a time across channels");
            chord({Button::Right});
            for(int r=0;r<2;++r)for(int c=0;c<2;++c) {
                const auto& old=sourceBlock.cells[std::size_t(r*2+c)];
                const auto& adjustedCell=pattern().at(r,c,doc.channels);
                require(adjustedCell.note==old.note&&adjustedCell.instrument==old.instrument&&
                        adjustedCell.volume==old.volume&&adjustedCell.effect==old.effect&&adjustedCell.param==old.param+1,
                        "mixed selection edits only the orange cursor's FX parameter field");
            }
            undo();
            chord({Button::Up});
            for(int r=0;r<2;++r)for(int c=0;c<2;++c) {
                const auto& old=sourceBlock.cells[std::size_t(r*2+c)];
                const auto& adjustedCell=pattern().at(r,c,doc.channels);
                require(adjustedCell.note==old.note&&adjustedCell.instrument==old.instrument&&
                        adjustedCell.volume==old.volume&&adjustedCell.effect==old.effect&&adjustedCell.param==old.param+16,
                        "coarse adjustment changes only selected FX parameters when that is the cursor field");
            }
            undo();
            buttonInput(Button::B,true);buttonInput(Button::A,true);buttonInput(Button::B,false);buttonInput(Button::A,false);
            bool cleared=true;
            for(int r=0;r<2;++r)for(int c=0;c<2;++c)
                cleared=cleared&&same(pattern().at(r,c,doc.channels),Cell{});
            require(cleared&&!blockSelected&&blockClipboard&&blockClipboard->rows==2&&blockClipboard->channels==2,
                    "A+B cuts the selected rectangle once and keeps its original clipboard");
            row=8;channel=4;
            buttonInput(Button::Select,true);buttonInput(Button::A,true);buttonInput(Button::Select,false);buttonInput(Button::A,false);
            bool restored=true;
            for(int r=0;r<2;++r)for(int c=0;c<2;++c)
                restored=restored&&same(pattern().at(8+r,4+c,doc.channels),sourceBlock.cells[std::size_t(r*2+c)]);
            require(restored&&mode==Mode::Normal&&!blockSelected,
                    "Select+A pastes all fields and releasing Select first does not trigger an A tap or menu");

            resetEightButtons();
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x30,4,0x47};
            doc.patterns[0].at(1,0,doc.channels)={97,2,0x50,0,0};
            const Cell pasteDestination{61,2,0x22,0x0c,0x20};
            doc.patterns[0].at(8,2,doc.channels)=pasteDestination;
            doc.patterns[0].at(9,2,doc.channels)=pasteDestination;
            playback->load(doc);field=2;
            selectChord(Button::B);tap(Button::Down);chord({Button::Right});
            require(pattern().at(0,0,doc.channels).volume==0x31&&pattern().at(1,0,doc.channels).volume==0x50&&
                    pattern().at(0,0,doc.channels).note==49&&pattern().at(1,0,doc.channels).note==97,
                    "volume-only batch edit preserves notes and clamps ordinary volume at 64");
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::B,false);buttonInput(Button::A,false);
            require(pattern().at(0,0,doc.channels).volume==0&&pattern().at(0,0,doc.channels).effect==4&&
                    pattern().at(0,0,doc.channels).note==49&&blockClipboard&&
                    blockClipboard->fieldMasks==std::vector<std::uint8_t>{4},
                    "partial block cut remembers the selected volume field and preserves surrounding data");
            row=8;channel=2;field=0;selectChord(Button::A);
            Cell expected=pasteDestination;expected.volume=0x31;
            require(same(pattern().at(8,2,doc.channels),expected),
                    "pasting a volume-only block changes volume at the destination without clearing other fields");
            expected.volume=0x50;
            require(same(pattern().at(9,2,doc.channels),expected),"partial block paste retains all selected rows");

            resetEightButtons();
            selectPattern(255);field=4;
            const auto beforeEmptyEdit=io::encodeXm(doc);
            selectChord(Button::B);tap(Button::Down);chord({Button::Right});
            require(doc.patterns.size()==2&&undoStack.empty()&&!dirty&&io::encodeXm(doc)==beforeEmptyEdit,
                    "adjusting selected empty FX fields does not create a lazy pattern or dirty the song");

            resetEightButtons();
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x30,4,0x47};
            doc.patterns[0].at(1,0,doc.channels)={96,1,0x40,0x0c,0x20};
            playback->load(doc);field=4;
            selectChord(Button::B);tap(Button::Down);
            for(int n=0;n<4;++n)tap(Button::Left);
            const auto beforeRejectedEdit=io::encodeXm(doc);
            bool rejectedMixedEdit=false;buttonInput(Button::A,true);
            try{buttonInput(Button::Right,true);}catch(const std::exception&){rejectedMixedEdit=true;}
            buttonInput(Button::Right,false);buttonInput(Button::A,false);
            require(rejectedMixedEdit&&hasSelection()&&undoStack.empty()&&!dirty&&io::encodeXm(doc)==beforeRejectedEdit,
                    "reversed mixed selection rejects an out-of-range note without changing other fields");

            resetEightButtons();clipboard=Cell{75,1,0x30,4,0x32};
            doc.orders={0,1};playback->load(doc);
            const auto beforeNavigation=io::encodeXm(doc);
            selectChord(Button::B);optionChord(Button::Right);
            require(channel==8&&!blockSelected&&clipboard&&clipboard->note==75,
                    "B+Right jumps the visible channel page and its release cannot copy the selection");
            optionChord(Button::Down);
            require(order==1&&selectedPattern==1&&clipboard&&clipboard->note==75&&io::encodeXm(doc)==beforeNavigation,
                    "B+Down follows the next order without changing song references or clipboard");
            changePage(Page::Instrument);optionChord(Button::Down);
            require(instrument==2&&sample==0,"B+Down moves between instruments");
            optionChord(Button::Up);changePage(Page::Sample);optionChord(Button::Down);
            require(instrument==1&&sample==1,"Sample B+Down moves between sample slots");
            optionChord(Button::Right);
            require(instrument==2&&sample==0,"Sample B+Right moves to the next instrument and its first sample");

            resetEightButtons();buttonInput(Button::Select,true);
            require(mode==Mode::Normal,"Select down waits for a chord or release instead of opening Actions");
            buttonInput(Button::Select,false);
            require(mode==Mode::Menu&&menuParents.empty(),"Select released without another button opens the contextual Actions root");
            tap(Button::B);
            buttonInput(Button::Select,true);buttonInput(Button::Right,true);buttonInput(Button::Select,false);
            require(page==Page::Instrument&&mode==Mode::Normal&&!held[std::size_t(Button::Right)],
                    "releasing Select after navigation consumes its direction without opening Actions");
            buttonInput(Button::Right,false);
            for(Page target:{Page::Song,Page::Pattern,Page::Instrument,Page::Sample}) {
                changePage(target);tap(Button::Select);
                require(mode==Mode::Menu&&menuParents.empty(),"every screen opens one contextual Actions root");
                require(actions.size()>=2&&actions[0].label=="UNDO"&&actions[1].label=="REDO"&&
                        std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.label=="HISTORY";}),
                        "Undo and Redo are the first root actions without a History submenu");
                tap(Button::B);
                require(mode==Mode::Normal&&page==target,"B closes contextual Actions directly to its original screen");
            }

            for(Page target:{Page::Pattern,Page::Song,Page::Sample}) {
                resetEightButtons();changePage(target);songField=0;
                clipboard=Cell{75,1,0x30,4,0x32};orderBlockClipboard={1,0};
                pcmClipboard=edit::copyPcm(*currentSample(),{0,4});
                const auto previousPcm=pcmClipboard->pcm;
                const auto previousDocument=io::encodeXm(doc);
                selectChord(Button::B);require(hasSelection(),"selection begins on each editable range screen");
                tap(Button::Select);
                require(!hasSelection()&&mode==Mode::Normal&&clipboard->note==75&&
                        orderBlockClipboard==std::vector<std::uint8_t>({1,0})&&pcmClipboard->pcm==previousPcm&&
                        io::encodeXm(doc)==previousDocument,
                        "Select tap cancels only the selection and preserves every clipboard and the document");
                tap(Button::Select);require(mode==Mode::Menu,"the next Select tap opens Actions after cancellation");
                tap(Button::B);
            }
            resetEightButtons();changePage(Page::Sample);
            selectionStart=4;selectionEnd=8;pcmClipboard.reset();
            tap(Button::B);
            require(pcmClipboard&&pcmClipboard->pcm.size()==4&&!hasSelection(),
                    "B copies a manually selected sample range and removes its selection highlight");
            selectionStart=4;selectionEnd=8;sampleField=8;
            tap(Button::Down);require(sampleField==14,"the Process button is directly below the selection endpoints");
            tap(Button::A);
            require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS"&&menuParents.empty()&&
                    selectionStart==4&&selectionEnd==8&&actions[0].label=="UNDO"&&actions[1].label=="REDO",
                    "Process opens directly with Undo/Redo and preserves the manual sample range");
            tap(Button::B);require(mode==Mode::Normal&&hasSelection(),"B from Process returns to the selected sample");
            selectChord(Button::B);tap(Button::A);
            require(mode==Mode::Normal&&sampleField==8&&selectionStart==4&&selectionEnd==8,
                    "A on a selected PCM endpoint stays on that field without opening a menu");
            tap(Button::Down);tap(Button::A);
            require(mode==Mode::Menu&&menuTitle=="SAMPLE PROCESS"&&!pcmSelecting&&selectionStart==4&&selectionEnd==8,
                    "A on Process opens the retained PCM range without copying or clearing it");
            tap(Button::B);tap(Button::Select);require(!hasSelection()&&mode==Mode::Normal,"Select cancels the retained range after closing Process");

            resetEightButtons();clipboard=Cell{75,1,0x30,4,0x32};
            const auto beforeSelectionCycle=io::encodeXm(doc);
            selectChord(Button::B);
            selectChord(Button::Up);selectChord(Button::Down);
            require(mode==Mode::Normal&&blockSelected&&blockSelectStage==1,
                    "unused Select+Up/Down neither open Actions nor cancel the selected block");
            selectChord(Button::B);selectChord(Button::B);
            require(blockSelected&&selectedBlock().firstRow==0&&selectedBlock().lastRow==pattern().rows&&
                    selectedBlock().firstChannel==0&&selectedBlock().lastChannel==doc.channels,
                    "repeated Select+B expands a field through its column to the whole pattern");
            selectChord(Button::B);
            require(!blockSelected&&mode==Mode::Normal&&!blockClipboard&&clipboard&&clipboard->note==75&&
                    io::encodeXm(doc)==beforeSelectionCycle,
                    "the fourth Select+B cancels without copying, changing music or opening Actions");

            resetEightButtons();changePage(Page::Song);songField=0;
            doc.orders={0,1,0};order=1;playback->load(doc);
            selectChord(Button::B);selectChord(Button::B);
            require(orderBlockSelected&&orderBlockStart==0&&order==2,
                    "Song Select+B expands the current order range to all orders");
            selectChord(Button::B);
            require(!orderBlockSelected&&orderBlockClipboard.empty()&&mode==Mode::Normal,
                    "Song selection cycle cancels without replacing the order clipboard");
            order=0;selectChord(Button::B);tap(Button::Down);
            tap(Button::Right);tap(Button::Right);tap(Button::B);
            require(!orderBlockSelected&&orderBlockClipboard==std::vector<std::uint8_t>({0,1}),
                    "B still copies the selected orders after the cursor moves into Song settings");
            songField=0;songOrderColumn=0;order=0;
            selectChord(Button::B);tap(Button::Down);tap(Button::Right);tap(Button::Right);
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::A,false);buttonInput(Button::B,false);
            require(!orderBlockSelected&&doc.orders==std::vector<std::uint8_t>({0})&&
                    orderBlockClipboard==std::vector<std::uint8_t>({0,1}),
                    "A+B still cuts the selected orders after moving into Song settings");

            resetEightButtons();changePage(Page::Sample);pcmClipboard.reset();
            selectionStart=4;selectionEnd=8;
            selectChord(Button::B);selectChord(Button::B);
            require(pcmSelecting&&selectionStart==0&&selectionEnd==int(currentSample()->pcm.size()),
                    "Sample Select+B expands its current PCM range to the whole sample");
            selectChord(Button::B);
            require(!pcmSelecting&&!pcmClipboard&&mode==Mode::Normal,
                    "Sample selection cycle cancels without copying PCM or opening Actions");

            resetEightButtons();const auto beforeIgnored=io::encodeXm(doc);
            for(Uint8 unused:{Uint8(SDL_CONTROLLER_BUTTON_X),Uint8(SDL_CONTROLLER_BUTTON_Y),
                              Uint8(SDL_CONTROLLER_BUTTON_LEFTSHOULDER),Uint8(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)}) {
                SDL_Event input{};input.type=SDL_CONTROLLERBUTTONDOWN;input.cbutton.button=unused;event(input);
                input.type=SDL_CONTROLLERBUTTONUP;event(input);
            }
            require(mode==Mode::Normal&&page==Page::Pattern&&channel==0&&row==0&&field==0&&
                    io::encodeXm(doc)==beforeIgnored&&!clipboard&&!blockClipboard,
                    "unused X/Y/shoulder buttons have no editor side effects");

            // A cancel chord must never act on the screen behind a modal,
            // including when B closes the modal before the A-down event.
            for(bool numberModal:{false,true})for(bool reversePress:{false,true})for(bool reverseRelease:{false,true}) {
                resetEightButtons();
                const auto beforeCancelChord=io::encodeXm(doc);
                if(numberModal)openNumber("CANCEL REGRESSION",1,0,8,[](int){});
                else tap(Button::Select);
                const Button first=reversePress?Button::B:Button::A;
                const Button second=reversePress?Button::A:Button::B;
                buttonInput(first,true);buttonInput(second,true);
                buttonInput(reverseRelease?second:first,false);buttonInput(reverseRelease?first:second,false);
                require(mode==Mode::Normal&&io::encodeXm(doc)==beforeCancelChord&&undoStack.empty()&&!clipboard,
                        "canceling a menu or numeric modal with A held never taps or cuts the underlying pattern");
            }

            resetEightButtons();changePage(Page::Song);songField=0;
            doc.orders={0,1,0,1};order=3;playback->load(doc);
            selectChord(Button::B);
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::A,false);buttonInput(Button::B,false);
            require(doc.orders==std::vector<std::uint8_t>({0,1,0})&&order==2&&!orderBlockSelected&&
                    orderBlockStart<int(doc.orders.size())&&orderBlockClipboard==std::vector<std::uint8_t>{1},
                    "A+B removes the selected last order, clamps its anchor and retains the cut reference after release");
            selectChord(Button::B);tap(Button::B);
            require(mode==Mode::Normal&&orderBlockClipboard==std::vector<std::uint8_t>{0},
                    "copy after an order deletion reads only the remaining valid selection");

            resetEightButtons();

            // Following moves the row, not the chosen channel/field. Reattach
            // from live playback state after navigation without restarting PCM.
            doc.orders={0,0,1};playback->load(doc);
            playback->playSong(1);playingPattern=false;followPlayback=true;
            std::vector<std::int16_t> followNavigationPcm(12000*2);
            playback->readFrames(followNavigationPcm.data(),12000);
            draw();field=2;channel=1;
            const auto beforeFollowNavigation=io::encodeXm(doc);
            tap(Button::Right);tap(Button::Left);
            require(followPlayback&&field==2&&channel==1,
                    "Horizontal field navigation keeps an active playback follow");
            field=4;tap(Button::Right);
            require(followPlayback&&field==0&&channel==2,
                    "Crossing a channel boundary keeps an active playback follow");
            optionChord(Button::Right);
            require(followPlayback&&channel==10&&field==0,
                    "B+Right preserves active follow while moving the visible channel page");
            tap(Button::Down);const auto detachedRow=row;
            require(!followPlayback,"Manual row navigation detaches follow");
            optionChord(Button::Left);tap(Button::Right);
            require(!followPlayback&&row==detachedRow&&channel==2&&field==1,
                    "Horizontal moves and releasing their B modifier never reattach a detached view");
            // Do not draw between advancing audio and B: the transport cache is
            // deliberately stale, and the repeated pattern must retain Order 1.
            playback->readFrames(followNavigationPcm.data(),12000);
            const auto beforeFollowResume=playback->status();
            tap(Button::B);
            require(followPlayback&&order==1&&selectedPattern==0&&row==beforeFollowResume.row&&
                    channel==2&&field==1&&playback->status().elapsedSeconds==beforeFollowResume.elapsedSeconds&&
                    !playingPattern&&io::encodeXm(doc)==beforeFollowNavigation&&undoStack.empty(),
                    "B reattaches to fresh transport and the repeated Order without restarting audio or editing music");
            chord({Button::Right});
            require(!followPlayback&&playback->status().playing,"A+direction editing detaches follow but preserves transport");
            for(const auto secondary:{Button::Select,Button::Start}) {
                optionChord(secondary);
                require(!followPlayback&&playback->status().playing,
                        "Mute/solo chord releases cannot become a B follow-resume tap");
            }
            playback->setSolo(-1);playback->setMuteMask(0);solo=-1;
            changePage(Page::Instrument);tap(Button::B);
            require(!followPlayback&&playback->status().playing,
                    "B follow-resume is scoped to Pattern rather than instrument navigation");
            resetEightButtons();

            doc.orders={0,0,1};order=1;selectedPattern=0;playback->load(doc);
            tap(Button::Start);playback->readFrames(followNavigationPcm.data(),12000);draw();
            const auto beforePatternResume=playback->status();
            optionChord(Button::Down);
            require(order==2&&selectedPattern==1&&!followPlayback&&playingPattern,
                    "A looping pattern can be left to browse the next Order without changing playback scope");
            tap(Button::B);
            require(order==1&&selectedPattern==0&&followPlayback&&playingPattern&&
                    playback->status().elapsedSeconds==beforePatternResume.elapsedSeconds,
                    "B restores the exact originating Order occurrence of a pattern loop without restarting audio");
            optionChord(Button::Up);
            require(order==0&&selectedPattern==0&&!followPlayback,
                    "After pattern-loop follow resumes, previous Order navigation uses the restored occurrence");
            resetEightButtons();

            row=8;selectChord(Button::B);tap(Button::Down);tap(Button::Down);
            const auto selectedBeforePlay=selectedBlock();
            tap(Button::Start);
            require(playback->status().playing&&playback->status().loopFirstRow==8&&
                    playback->status().loopEndRow==11&&row==10&&!followPlayback&&hasSelection()&&
                    selectedBlock().firstRow==selectedBeforePlay.firstRow&&
                    selectedBlock().lastRow==selectedBeforePlay.lastRow,
                    "Start snapshots selected rows without moving the cursor or selection endpoints");
            chord({Button::Right});
            require(playback->status().playing&&playback->status().loopFirstRow==8&&
                    playback->status().loopEndRow==11,
                    "Selected-field edits preserve the separate playback loop");
            tap(Button::Select);tap(Button::Down);
            require(!hasSelection()&&mode==Mode::Normal&&row==11&&playback->status().loopEndRow==11,
                    "Selection cancellation and cursor movement leave loop playback running");
            selectChord(Button::Right);
            require(page==Page::Instrument&&playback->status().loopFirstRow==8,
                    "Changing screens does not replace a playing selection loop");
            tap(Button::Start);require(!playback->status().playing&&playback->status().loopFirstRow<0,
                    "Start stops and clears the selection loop from another screen");
            changePage(Page::Pattern);doc.orders={0,1};playback->load(doc);order=1;selectedPattern=1;
            row=6;selectChord(Button::B);tap(Button::Down);
            selectChord(Button::Start);
            require(playback->status().playing&&!playingPattern&&playback->status().order==0&&
                    playback->status().row==0&&playback->status().loopFirstRow<0&&playback->status().loopEndRow<0&&
                    mode==Mode::Normal&&!followPlayback&&hasSelection()&&selectedPattern==1&&row==7&&
                    selectedBlock().firstRow==6&&selectedBlock().lastRow==8,
                    "Select+Start plays the whole song while retaining a selection in another pattern");
            tap(Button::Start);tap(Button::Select);tap(Button::Start);
            require(playback->status().row==0&&playback->status().loopFirstRow<0&&followPlayback,
                    "Starting without a selection restores ordinary pattern playback and follow");
            resetEightButtons();

            row=8;selectChord(Button::B);tap(Button::Down);tap(Button::Start);
            playback->readFrames(followNavigationPcm.data(),12000);
            const auto selectedLoopStatus=playback->status();
            tap(Button::B);
            require(!hasSelection()&&!followPlayback&&blockClipboard&&blockClipboard->rows==2&&
                    playback->status().loopFirstRow==8&&playback->status().loopEndRow==10&&
                    playback->status().elapsedSeconds==selectedLoopStatus.elapsedSeconds,
                    "First B copies and clears the selection without reattaching or replacing its playback loop");
            tap(Button::B);
            require(followPlayback&&row==selectedLoopStatus.row&&playback->status().loopFirstRow==8&&
                    playback->status().loopEndRow==10&&playback->status().elapsedSeconds==selectedLoopStatus.elapsedSeconds,
                    "Second B follows the existing selected-row loop without changing its bounds or elapsed audio");
            resetEightButtons();

            for(bool releaseBFirst:{false,true})for(Button command:{Button::Select,Button::Start}) {
                resetEightButtons();clipboard=Cell{75,1,0x30,4,0x32};
                selectChord(Button::B);tap(Button::Down);
                const auto originalSelection=selectedBlock();const auto originalSong=io::encodeXm(doc);
                tap(Button::Start); // Loop selection, then change listening without stopping it.
                buttonInput(Button::B,true);buttonInput(command,true);
                require(playback->status().playing&&mode==Mode::Normal&&hasSelection()&&
                        (command==Button::Select?playback->muteMask()==1:solo==0),
                        "B-first listening chords act immediately during playback without altering selection");
                buttonInput(releaseBFirst?Button::B:command,false);buttonInput(releaseBFirst?command:Button::B,false);
                require(channelChord==0&&mode==Mode::Normal&&hasSelection()&&clipboard->note==75&&
                        selectedBlock().firstRow==originalSelection.firstRow&&selectedBlock().lastRow==originalSelection.lastRow&&
                        io::encodeXm(doc)==originalSong&&undoStack.empty(),
                        "Either chord release order leaves clipboard, selection, XM and history untouched");
                tap(Button::Start);require(!playback->status().playing,"Unmodified Start still stops playback");
                tap(Button::Select);playback->setMuteMask(0);playback->setSolo(-1);solo=-1;
            }
            for(Page target:{Page::Pattern,Page::Song}) {
                resetEightButtons();changePage(target);clipboard=Cell{75,1,0x30,4,0x32};
                if(target==Page::Song)songField=12;
                else {selectChord(Button::B);tap(Button::Down);}
                const bool hadSelection=hasSelection();const auto beforeListening=io::encodeXm(doc);
                tap(Button::Start);buttonInput(Button::B,true);
                tap(Button::Select);
                require(playback->muteMask()==1&&channelChord==0&&held[std::size_t(Button::B)]&&channelModifier,
                        "Releasing Select rearms a held B after one mute tap");
                tap(Button::Select);
                require(playback->muteMask()==0,"Repeated Select taps toggle mute without releasing B");
                tap(Button::Start);
                require(solo==0&&playback->status().playing,"A held B can switch from mute taps to solo without stopping playback");
                tap(Button::Start);
                require(solo==-1&&playback->muteMask()==0,"Repeated Start taps toggle solo without releasing B");
                tap(Button::Start);tap(Button::Select);
                require(solo==-1&&playback->muteMask()==0xFFFFFFFFu,
                        "Switching directly from solo to mute under held B changes the current channel's effective state");
                tap(Button::Start);tap(Button::Start); // Return to all channels on.
                buttonInput(Button::Select,true);buttonDown(Button::Select,true);buttonDown(Button::Select,true);
                require(playback->muteMask()==1&&channelChord==1,"Holding or repeating Select toggles mute only once");
                buttonInput(Button::Select,false);tap(Button::Select);
                buttonInput(Button::Start,true);buttonDown(Button::Start,true);buttonDown(Button::Start,true);
                require(solo==0&&channelChord==2&&playback->status().playing,"Holding or repeating Start toggles solo only once");
                buttonInput(Button::Start,false);tap(Button::Start);buttonInput(Button::B,false);
                require(channelChord==0&&solo==-1&&playback->muteMask()==0&&mode==Mode::Normal&&
                        playback->status().playing&&hasSelection()==hadSelection&&clipboard->note==75&&
                        io::encodeXm(doc)==beforeListening&&undoStack.empty(),
                        "Repeated held-B listening taps preserve transport, selection, clipboard and XM in Pattern and Song");
            }
            for(bool releaseBFirst:{false,true})for(Button first:{Button::Select,Button::Start}) {
                resetEightButtons();clipboard=Cell{75,1,0x30,4,0x32};selectChord(Button::B);tap(Button::Down);
                const auto beforeOverlap=io::encodeXm(doc);const auto selectionBeforeOverlap=selectedBlock();
                tap(Button::Start);buttonInput(Button::B,true);buttonInput(first,true);
                const auto firstMask=playback->muteMask();const int firstSolo=solo;
                const Button second=first==Button::Select?Button::Start:Button::Select;
                buttonInput(second,true);buttonInput(first,false);
                require(channelChord!=0&&playback->muteMask()==firstMask&&solo==firstSolo&&playback->status().playing,
                        "An overlapping secondary key is consumed until its release instead of triggering another chord");
                buttonInput(releaseBFirst?Button::B:second,false);
                if(releaseBFirst) {
                    buttonDown(second==Button::Select?Button::Select:Button::Start,true);
                    require(channelChord!=0&&playback->status().playing,"B-first release keeps a remaining secondary key consumed");
                }
                buttonInput(releaseBFirst?second:Button::B,false);
                require(channelChord==0&&mode==Mode::Normal&&playback->status().playing&&hasSelection()&&
                        clipboard->note==75&&selectedBlock().firstRow==selectionBeforeOverlap.firstRow&&
                        selectedBlock().lastRow==selectionBeforeOverlap.lastRow&&io::encodeXm(doc)==beforeOverlap&&undoStack.empty(),
                        "Overlapping and reverse-order releases never copy, edit, select, open Actions or stop playback");
            }
            resetEightButtons();selectChord(Button::B);
            require(hasSelection()&&playback->muteMask()==0,"Select-first B still selects instead of muting");
            tap(Button::Select);tap(Button::Select); // Cancel selection, open Actions.
            buttonInput(Button::B,true);buttonInput(Button::Start,true);buttonInput(Button::Start,false);buttonInput(Button::B,false);
            require(solo==-1&&!playback->status().playing&&mode==Mode::Normal,
                    "B closing a modal cannot become a solo or play chord on the underlying screen");
            resetEightButtons();changePage(Page::Song);songField=7;order=0;
            tap(Button::Down);require(songField==12,"Song buttons lead down into the channel monitor");
            tap(Button::Right);tap(Button::Down);require(channel==9,"Monitor navigation follows its eight-column layout");
            const auto monitorOrders=doc.orders;
            optionChord(Button::Right);require(channel==10&&doc.orders==monitorOrders&&order==0,
                    "B navigation in the monitor moves channels without moving or editing orders");
            optionChord(Button::Select);require(playback->muteMask()==(1u<<10),"Song monitor shortcut targets its highlighted channel");
            optionChord(Button::Start);require(solo==10,"Monitor solo targets the highlighted channel");
            tap(Button::Up);tap(Button::Up);require(songField==7,"Up from the monitor top returns to its entry field");
            playback->setMuteMask(0);playback->setSolo(-1);solo=-1;
            for(Page target:{Page::Pattern,Page::Song,Page::Instrument,Page::Sample}) {
                changePage(target);menu("CONTROLS");require(mode==Mode::Help,"Every context retains one Controls entry");tap(Button::B);
            }
            resetEightButtons();field=2;cell().volume=0;playback->load(doc);lastCell.volume=0;
            tap(Button::A);require(cell().volume==0x50,"A inserts default maximum volume in an empty volume column");
            chord({Button::Up});require(cell().volume==0x60,"Up after maximum volume enters the first volume command");
            chord({Button::Right});require(cell().volume==0x61,"Horizontal command input edits x");
            chord({Button::Up});require(cell().volume==0x71,"Vertical command input changes type and preserves x");
            cell().volume=0x6F;playback->load(doc);chord({Button::Right});
            require(cell().volume==0x6F,"A command parameter cannot carry into a different command type");
            chord({Button::Down});require(cell().volume==0x50,"Down from the first command returns to absolute volume");
            resetEightButtons();

            // Waveform zeroing uses the same two-button chord without canceling.
            changePage(Page::Sample);sampleField=11;
            currentSample()->pcm[0]=8192;playback->load(doc);
            const auto beforeWave=io::encodeXm(doc);
            for(bool reversePress:{false,true})for(bool reverseRelease:{false,true}) {
                tap(Button::A);require(mode==Mode::WaveDraw&&drawingSample.has_value(),"Sample Draw is a direct A button");
                const Button first=reversePress?Button::B:Button::A;
                const Button second=reversePress?Button::A:Button::B;
                buttonInput(first,true);buttonInput(second,true);
                buttonInput(reverseRelease?second:first,false);buttonInput(reverseRelease?first:second,false);
                require(mode==Mode::WaveDraw&&drawingSample&&drawingSample->pcm[0]==0&&io::encodeXm(doc)==beforeWave,
                        "A+B zeros the draft in either key order without canceling or modifying the real sample");
                tap(Button::B);require(mode==Mode::Normal&&!drawingSample&&io::encodeXm(doc)==beforeWave,
                        "plain B cancels a zeroed draft without changing the source sample");
            }
            tap(Button::A);buttonInput(Button::Select,true);buttonInput(Button::Right,true);buttonInput(Button::Select,false);
            require(!held[std::size_t(Button::Right)],"releasing the wave-frame jump modifier consumes its held direction");
            buttonInput(Button::Right,false);tap(Button::B);
            clearSelection();held.fill(false);mode=Mode::Normal;patternViewChannels=4;saveEditorSettings();
        }
