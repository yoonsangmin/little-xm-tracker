        {
            auto resetAnalysisFixture=[&]{
                playback->stop();clearAnalysisDrafts();doc=makeDemoDocument();
                auto& source=doc.instruments[0].samples[0];source.name="A4 SOURCE";
                source.pcm.resize(8192);source.relativeNote=source.finetune=0;source.sixteenBit=true;
                for(std::size_t n=0;n<source.pcm.size();++n)
                    source.pcm[n]=std::int16_t(std::lround(15000*std::sin(6.283185307179586*440.0*double(n)/8363.0)));
                source.loopType=1;source.loopStart=0;source.loopLength=std::uint32_t(source.pcm.size());
                page=Page::Sample;mode=Mode::Normal;instrument=1;sample=0;sampleField=14;
                selectedPattern=order=row=channel=field=0;selectionStart=selectionEnd=0;solo=-1;
                clearSelection();held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
                selectPending=selectUsed=channelModifier=false;channelChord=0;pcmSelecting=false;
                undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;++documentRevision;
                normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);followPlayback=false;
                chopCount=8;
            };
            auto finishAnalysisWorker=[&]{
                const auto deadline=SDL_GetTicks()+10000;
                while(worker.valid()&&SDL_GetTicks()<deadline){pollTools();SDL_Delay(1);}
                require(!worker.valid(),"sample analysis completes within its bounded worker budget");
            };
            auto focusChop=[&](int target){
                // Navigate the visible rows through real button events. Field
                // IDs are deliberately not ordered by their screen positions.
                const std::array<int,10> rows{{0,0,1,2,3,-1,5,3,4,4}};
                const std::array<int,10> columns{{0,1,0,0,0,-1,0,1,0,1}};
                require(mode==Mode::ChopSettings&&target>=0&&target<10&&target!=5,"Chop focus target exists");
                for(int step=0;step<6;++step) {
                    require(chopField>=0&&chopField<10&&chopField!=5,"Chop navigation remains on a visible field");
                    const int current=rows[std::size_t(chopField)],desired=rows[std::size_t(target)];
                    if(current==desired)break;
                    tap(current<desired?Button::Down:Button::Up);
                }
                require(rows[std::size_t(chopField)]==rows[std::size_t(target)],"Chop up/down reaches the requested row");
                if(chopField!=target)tap(columns[std::size_t(target)]?Button::Right:Button::Left);
                require(chopField==target,"Chop left/right reaches the requested field within its row");
            };
            auto previewChop=[&]{focusChop(3);tap(Button::A);require(mode==Mode::Busy,"Chop builds preview off the UI thread");finishAnalysisWorker();
                require(mode==Mode::ChopSettings&&chopDraft.has_value(),"Chop worker returns a reviewable draft");};

            resetAnalysisFixture();selectionStart=256;selectionEnd=7936;
            const auto pitchBefore=io::encodeXm(doc);const auto pitchPcm=currentSample()->pcm;
            menu("DETECT PITCH");require(mode==Mode::Busy,"Detect Pitch is reachable from Advanced PCM with selection");
            finishAnalysisWorker();
            require(mode==Mode::PitchResult&&pitchEstimate.reliable&&pitchEstimate.note==58,
                    "selected 440 Hz PCM is detected as FT2 A-4");
            require(analysisRange.first==256&&analysisRange.last==7936&&io::encodeXm(doc)==pitchBefore&&undoStack.empty()&&!dirty,
                    "pitch analysis does not edit sample, history, selection or dirty state");
            tap(Button::Left);tap(Button::Right);tap(Button::Up);
            require(pitchField==0,"vertical Pitch buttons ignore Left/Right and clamp at the top");
            tap(Button::Down);tap(Button::Right);tap(Button::Left);tap(Button::Down);
            require(pitchField==1,"Pitch bottom button neither moves sideways nor wraps downward");
            tap(Button::Up);
            noticeUntil=0;draw();screenshot(home/"sample-pitch-result.bmp");SDL_RenderPresent(renderer);
            tap(Button::A);std::vector<std::int16_t> listenPcm(2048*2);playback->readFrames(listenPcm.data(),2048);
            require(std::any_of(listenPcm.begin(),listenPcm.end(),[](std::int16_t n){return n!=0;}),"Pitch result can audition source PCM");
            tap(Button::Down);tap(Button::A);
            require(mode==Mode::Normal&&undoStack.size()==1&&currentSample()->pcm==pitchPcm&&currentSample()->relativeNote==-9,
                    "Apply Pitch retunes A4 to C4 without rewriting PCM and creates one undo step");
            undo();require(io::encodeXm(doc)==pitchBefore,"Undo restores exact pre-tuning XM");
            menu("DETECT PITCH");tap(Button::B);finishAnalysisWorker();
            require(mode==Mode::Normal&&!analysisSource&&io::encodeXm(doc)==pitchBefore,"Cancelled analysis cannot open or apply a stale result");

            resetAnalysisFixture();currentSample()->pcm.assign(8192,0);playback->load(doc);
            const auto silentBefore=io::encodeXm(doc);
            menu("DETECT PITCH");finishAnalysisWorker();tap(Button::Down);tap(Button::A);
            require(mode==Mode::PitchResult&&!pitchEstimate.reliable&&undoStack.empty()&&io::encodeXm(doc)==silentBefore,
                    "Unreliable pitch disables tuning and preserves the instrument");tap(Button::B);

            resetAnalysisFixture();doc.instruments.resize(3);doc.instruments[1].name="KEEP EMPTY";
            // A referenced blank instrument is not free for a new Chop result.
            doc.patterns[0].at(1,0,doc.channels).instrument=3;playback->load(doc);
            selectionStart=128;selectionEnd=8064;const auto chopBefore=io::encodeXm(doc);
            const auto originalInstrument=io::encodeXi(doc.instruments[0]);
            menu("CHOP SAMPLE");
            require(mode==Mode::ChopSettings&&!chopSnap&&!chopCurrent&&chopNewInstrument==4,
                    "New Chop starts exact and skips authored or referenced empty instruments");
            tap(Button::Left);tap(Button::Up);require(chopField==0,"Chop top-left field clamps without wrapping");
            tap(Button::Right);tap(Button::Right);tap(Button::Up);
            require(chopField==1,"Chop right selects the other field on the same row and clamps at the edge");
            tap(Button::Down);tap(Button::Left);tap(Button::Right);
            require(chopField==2,"Chop single-field row ignores horizontal navigation");
            tap(Button::Up);require(chopField==1,"Returning to the Chop top row remembers its last column");
            previewChop();
            require(chopDraft->boundaries.front()==128&&chopDraft->boundaries.back()==8064&&chopSliceCount()==8&&
                    io::encodeXm(doc)==chopBefore&&undoStack.empty(),"Preview honors selection without changing the song");
            noticeUntil=0;draw();screenshot(home/"sample-chop-preview.bmp");SDL_RenderPresent(renderer);
            focusChop(6);tap(Button::Right);tap(Button::Down);
            require(chopField==6,"Chop Apply clamps at the right and bottom edges");
            tap(Button::Left);tap(Button::Left);tap(Button::Down);
            require(chopField==6,"Chop single Apply button ignores horizontal navigation and never reaches the removed Listen button");
            tap(Button::Up);require(chopField==8,"Chop up enters the preceding button row");
            tap(Button::Right);tap(Button::Down);require(chopField==6,"Chop down enters the single Apply button");
            tap(Button::Right);tap(Button::Up);require(chopField==9,"Chop add/remove row restores its last selected column");
            focusChop(4);tap(Button::Start);std::fill(listenPcm.begin(),listenPcm.end(),0);playback->readFrames(listenPcm.data(),2048);
            require(std::any_of(listenPcm.begin(),listenPcm.end(),[](std::int16_t n){return n!=0;})&&!playback->status().playing,
                    "Start in Chop auditions the selected slice independently of song transport");
            tap(Button::A);require(playback->samplePreviewStatus().active,"Releasing A does not stop a Start-triggered slice preview");
            tap(Button::Start);require(!playback->samplePreviewStatus().active,"A second Start stops the selected slice preview");
            tap(Button::Start);for(int n=0;n<6;++n)playback->readFrames(listenPcm.data(),2048);
            require(!playback->samplePreviewStatus().active,"A Chop slice ends naturally without looping");
            tap(Button::Start);playback->readFrames(listenPcm.data(),128);
            require(playback->samplePreviewStatus().active,"One Start retriggers a naturally finished Chop slice");
            playback->playSong(0,0);tap(Button::Start);
            require(!playback->status().playing&&!playback->samplePreviewStatus().active,
                    "Start in Chop stops active song transport before starting a new audition");
            focusChop(6);tap(Button::A);
            require(mode==Mode::Normal&&instrument==4&&sample==0&&doc.instruments.size()==4&&undoStack.size()==1,
                    "New Chop creates its chosen empty instrument with one undo step");
            require(io::encodeXi(doc.instruments[0])==originalInstrument&&currentInstrument()->samples.size()==8&&
                    currentInstrument()->volumeEnvelope.flags==0,"New Chop preserves source and starts with fresh instrument settings");
            for(int n=0;n<8;++n)require(currentInstrument()->noteToSample[std::size_t(48+n)]==n&&
                    currentInstrument()->samples[std::size_t(n)].relativeNote==-n&&!currentInstrument()->samples[std::size_t(n)].loopType,
                    "Slice key mapping compensates note pitch and disables source loop");
            const auto choppedXi=io::encodeXi(*currentInstrument());
            require(io::decodeXi(choppedXi).samples.size()==8,"All slices are exported inside one XI");
            undo();require(instrument==1&&sample==0&&selectionStart==128&&selectionEnd==8064&&io::encodeXm(doc)==chopBefore,
                    "Undo new Chop restores both source cursor and entire document");
            undo(true);require(instrument==4&&io::encodeXi(*currentInstrument())==choppedXi,"Redo restores new multisample instrument");

            resetAnalysisFixture();const auto fillBefore=io::encodeXm(doc);const auto sourcePcm=currentSample()->pcm;
            menu("CHOP SAMPLE");focusChop(2);chord({Button::Right});
            require(chopCurrent&&chopNewInstrument==2,"Current destination is selectable even when a new instrument is free");
            previewChop();focusChop(6);tap(Button::A);
            require(mode==Mode::Normal&&instrument==1&&sample==1&&doc.instruments.size()==1&&currentInstrument()->samples.size()==9&&
                    currentInstrument()->samples[0].pcm==sourcePcm&&currentInstrument()->volumeEnvelope.flags==1,
                    "Current Chop fills vacant slots without replacing source PCM or instrument settings");
            undo();require(io::encodeXm(doc)==fillBefore,"Current fill is undone atomically");

            resetAnalysisFixture();
            auto existing=currentInstrument()->samples[0];existing.pcm.resize(16);existing.loopLength=16;
            currentInstrument()->samples.resize(16,existing);playback->load(doc);
            const auto replaceBefore=io::encodeXm(doc);
            menu("CHOP SAMPLE");focusChop(2);chord({Button::Right});previewChop();focusChop(6);tap(Button::A);
            require(mode==Mode::Confirm&&io::encodeXm(doc)==replaceBefore&&undoStack.empty(),"Insufficient slots require whole-instrument replacement confirmation");
            tap(Button::B);require(mode==Mode::ChopSettings&&chopDraft&&io::encodeXm(doc)==replaceBefore,"Cancel replacement returns to intact preview");
            tap(Button::A);require(mode==Mode::Confirm,"Replacement can be requested again after cancellation");tap(Button::A);
            require(mode==Mode::Normal&&currentInstrument()->samples.size()==8&&!currentInstrument()->volumeEnvelope.flags&&undoStack.size()==1,
                    "Confirmed replacement commits only generated slices and a fresh keymap/settings");
            undo();require(io::encodeXm(doc)==replaceBefore,"One undo recovers all overwritten samples and metadata");

            resetAnalysisFixture();doc.instruments.resize(128);
            for(std::size_t n=1;n<128;++n)doc.instruments[n].name="OCCUPIED";
            playback->load(doc);const auto fullBefore=io::encodeXm(doc);
            menu("CHOP SAMPLE");require(chopNewInstrument==0,"Full instrument bank exposes unavailable New destination");
            previewChop();focusChop(6);tap(Button::A);
            require(mode==Mode::ChopSettings&&undoStack.empty()&&io::encodeXm(doc)==fullBefore,
                    "No available new instrument fails without overwriting a slot");
            focusChop(2);chord({Button::Right});focusChop(6);tap(Button::A);
            require(mode==Mode::Normal&&doc.instruments.size()==128&&currentInstrument()->samples.size()==9,
                    "Current destination still works when all instrument positions are occupied");
            resetAnalysisFixture();menu("CHOP SAMPLE");previewChop();focusChop(1);chord({Button::Right});
            require(chopSnap&&!chopDraft,"Changing boundary mode invalidates old preview");previewChop();
            require(chopSliceCount()==8,"Optional attack snap keeps requested slice count");tap(Button::B);
            require(!analysisSource&&!chopDraft&&undoStack.empty(),"Leaving Chop discards preview without an edit");

            resetAnalysisFixture();selectionStart=256;selectionEnd=7936;
            const auto manualBefore=io::encodeXm(doc),manualSource=io::encodeXi(doc.instruments[0]);
            const auto manualPcm=currentSample()->pcm;
            const auto manualSerial=recoverySerial,manualRevision=documentRevision;
            menu("CHOP SAMPLE");
            focusChop(7);chord({Button::Right});focusChop(8);tap(Button::A);focusChop(9);tap(Button::A);
            require(!chopDraft&&io::encodeXm(doc)==manualBefore&&undoStack.empty(),
                    "Manual controls require a built preview without editing the source");
            focusChop(0);chord({Button::Left,Button::Left,Button::Left,Button::Left});previewChop();
            require(chopDraft->boundaries==std::vector<std::size_t>({256,2176,4096,6016,7936}),
                    "Four-slice draft covers the full selected source range");
            focusChop(7);chord({Button::Right});
            require(chopDraft->boundaries[1]==2177,"A+Right moves the selected slice's right cut by one frame");
            chord({Button::Up});require(chopDraft->boundaries[1]==2253,"A+Up moves a cut by one percent of the selected range");
            chord({Button::Down});require(chopDraft->boundaries[1]==2177,"A+Down reverses the coarse cut move");
            focusChop(8);tap(Button::A);
            require(chopSliceCount()==5&&chopCount==5&&chopDraft->boundaries[1]==1216&&chopDraft->boundaries[2]==2177,
                    "Add Cut splits the selected slice at its midpoint without shifting existing cuts");
            focusChop(9);tap(Button::A);
            require(chopSliceCount()==4&&chopDraft->boundaries[1]==2177,"Remove Cut merges with the next slice without deleting PCM");
            focusChop(8);tap(Button::A);focusChop(4);chord({Button::Right});
            require(chopSlice==1,"Slice control selects the second manual slice");
            tap(Button::Start);playback->readFrames(listenPcm.data(),128);
            const auto firstManualCursor=playback->samplePreviewStatus();
            require(firstManualCursor.active&&firstManualCursor.instrument==1&&firstManualCursor.sample==0&&
                    firstManualCursor.frame>=1216&&firstManualCursor.frame<2177&&!playback->status().playing,
                    "Manual slice audition reports its source identity and cursor within the selected cuts");
            playback->readFrames(listenPcm.data(),512);
            const auto nextManualCursor=playback->samplePreviewStatus();
            require(nextManualCursor.active&&nextManualCursor.frame>firstManualCursor.frame&&nextManualCursor.frame<2177,
                    "Manual slice playback cursor advances within the audition range");
            noticeUntil=0;draw();screenshot(home/"sample-chop-manual.bmp");SDL_RenderPresent(renderer);
            focusChop(7);chord({Button::Left});
            require(!playback->samplePreviewStatus().active,"Moving a cut stops the old slice preview");
            require(chopDraft->manual&&chopDraft->snappedBoundaries==0&&io::encodeXm(doc)==manualBefore&&
                    undoStack.empty()&&redoStack.empty()&&!dirty&&recoverySerial==manualSerial&&documentRevision==manualRevision,
                    "Manual cuts only change the draft, never the document, history or recovery revision");
            const auto manualCuts=chopDraft->boundaries;
            focusChop(6);tap(Button::A);
            require(mode==Mode::Normal&&instrument==2&&sample==0&&undoStack.size()==1&&
                    currentInstrument()->samples.size()==manualCuts.size()-1&&io::encodeXi(doc.instruments[0])==manualSource,
                    "Apply builds the manually planned slices in one edit while preserving the source instrument");
            std::vector<std::int16_t> joinedManualPcm;
            for(std::size_t n=0;n+1<manualCuts.size();++n) {
                const auto& piece=currentInstrument()->samples[n].pcm;
                require(piece==std::vector<std::int16_t>(manualPcm.begin()+std::ptrdiff_t(manualCuts[n]),
                                                       manualPcm.begin()+std::ptrdiff_t(manualCuts[n+1])),
                        "Each applied manual slice contains its exact contiguous source frames");
                joinedManualPcm.insert(joinedManualPcm.end(),piece.begin(),piece.end());
            }
            require(joinedManualPcm==std::vector<std::int16_t>(manualPcm.begin()+256,manualPcm.begin()+7936),
                    "Joining manual slices reproduces the selection without gaps, overlaps or reprocessing");
            undo();require(io::encodeXm(doc)==manualBefore&&instrument==1&&sample==0&&selectionStart==256&&selectionEnd==7936,
                    "One Undo restores the entire pre-manual-Chop song and source selection");

            resetAnalysisFixture();selectionStart=100;selectionEnd=300;
            const auto boundedBefore=io::encodeXm(doc);
            menu("CHOP SAMPLE");focusChop(0);
            chord({Button::Left,Button::Left,Button::Left,Button::Left,Button::Left,Button::Left});previewChop();
            focusChop(7);chord({Button::Up});
            require(chopDraft->boundaries[1]==202,"Coarse movement scales to the selected range rather than full sample length");
            for(int n=0;n<128;++n)chord({Button::Down});
            require(chopDraft->boundaries==std::vector<std::size_t>({100,101,300}),
                    "Moving a cut left stops one frame after its previous neighbor");
            focusChop(8);tap(Button::A);
            require(chopSliceCount()==2&&chopDraft->boundaries[1]==101,"A one-frame slice cannot be split into empty slices");
            focusChop(7);for(int n=0;n<128;++n)chord({Button::Up});
            require(chopDraft->boundaries==std::vector<std::size_t>({100,299,300}),
                    "Moving a cut right stops one frame before its next neighbor");
            focusChop(4);chord({Button::Right});focusChop(7);chord({Button::Left,Button::Up});
            focusChop(9);tap(Button::A);
            require(chopDraft->boundaries==std::vector<std::size_t>({100,299,300}),
                    "The final endpoint cannot move or be removed");
            focusChop(4);chord({Button::Left});focusChop(9);tap(Button::A);tap(Button::A);
            require(chopSliceCount()==1&&chopCount==1&&chopSlice==0&&chopDraft->boundaries==std::vector<std::size_t>({100,300}),
                    "Removing the last interior cut leaves one complete slice and preserves both endpoints");
            focusChop(0);chord({Button::Left});
            require(chopCount==1&&chopDraft,"Automatic slice count clamps at one without discarding an unchanged manual draft");
            for(int n=0;n<20;++n)chord({Button::Right});
            require(chopCount==16&&!chopDraft,"Automatic slice count clamps at sixteen and invalidates changed cut plans");
            previewChop();const auto maximumCuts=chopDraft->boundaries;
            focusChop(8);tap(Button::A);
            require(chopSliceCount()==16&&chopDraft->boundaries==maximumCuts,"Add Cut refuses a seventeenth slice without changing the draft");
            focusChop(7);chord({Button::Right});tap(Button::B);
            require(mode==Mode::Normal&&!analysisSource&&!chopDraft&&io::encodeXm(doc)==boundedBefore&&undoStack.empty()&&!dirty,
                    "Cancel discards all manual changes without modifying or creating any instrument");
            resetAnalysisFixture();noticeUntil=0;
        }
