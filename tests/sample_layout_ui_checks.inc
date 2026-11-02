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
                cutChordUsed=modalBackHeld=channelModifier=selectPending=selectUsed=false;channelChord=0;
                menuParents.clear();sampleRowFocus.fill(-1);
                undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;++documentRevision;
                normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);
            };
            auto focusSampleLayout=[&](int target){
                // These coordinates describe the visible Sample form, including
                // both endpoint pairs on one row and its two lower controls.
                const std::array<int,15> rows{{1,1,2,2,4,3,3,3,3,0,0,0,-1,-1,4}};
                const std::array<int,15> columns{{0,1,0,1,0,0,1,2,3,0,1,2,-1,-1,1}};
                require(target>=0&&target<15&&rows[std::size_t(target)]>=0,"Sample focus target exists");
                for(int step=0;step<5;++step) {
                    require(sampleField>=0&&sampleField<15&&rows[std::size_t(sampleField)]>=0,
                            "Sample navigation never selects removed PLAY or SET LOOP fields");
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
            tap(Button::Right);tap(Button::Right);require(sampleField==11,"Draw is the last top-row action");
            tap(Button::Down);require(sampleField==1,"Draw enters the right metadata column on first use");
            tap(Button::Left);require(sampleField==0,"Sample metadata left/right selects Volume or Panning");
            tap(Button::Up);require(sampleField==11,"Returning to the header restores Draw");
            tap(Button::Down);require(sampleField==0,"Returning to metadata restores its last field");
            focusSampleLayout(5);tap(Button::Left);require(sampleField==5,"Loop START clamps at the left endpoint edge");
            tap(Button::Right);require(sampleField==6,"Loop END follows Loop START on the same row");
            tap(Button::Right);require(sampleField==7,"Right crosses from loop END to selection START");
            tap(Button::Right);tap(Button::Right);require(sampleField==8,"Selection END clamps at the right endpoint edge");
            focusSampleLayout(14);tap(Button::Right);tap(Button::Down);require(sampleField==14,"PROCESS clamps at the bottom-right edge");
            tap(Button::Left);tap(Button::Left);require(sampleField==4,"Loop type is the only other bottom-row control");
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
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            std::vector<std::int16_t> expectedCut(selectionSource.begin(),selectionSource.begin()+200);
            expectedCut.insert(expectedCut.end(),selectionSource.begin()+600,selectionSource.end());
            require(currentSample()->pcm==expectedCut&&undoStack.size()==1&&!hasSelection(),
                    "A+B still cuts selected PCM while directions use ordinary field focus");
            undo();require(io::encodeXm(doc)==selectionDocument,"One Undo restores the sample after range cut");

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
