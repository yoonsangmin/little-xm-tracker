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
                while(chopField<target)tap(Button::Down);
                while(chopField>target)tap(Button::Up);
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
            tap(Button::Left);tap(Button::Right);tap(Button::Up);
            require(chopField==0,"vertical Chop fields ignore Left/Right and clamp at the top");
            previewChop();
            require(chopDraft->boundaries.front()==128&&chopDraft->boundaries.back()==8064&&chopDraft->slices.size()==8&&
                    io::encodeXm(doc)==chopBefore&&undoStack.empty(),"Preview honors selection without changing the song");
            noticeUntil=0;draw();screenshot(home/"sample-chop-preview.bmp");SDL_RenderPresent(renderer);
            focusChop(6);tap(Button::Right);tap(Button::Left);tap(Button::Down);
            require(chopField==6,"Chop Apply ignores Left/Right and clamps at the bottom");
            focusChop(5);tap(Button::A);std::fill(listenPcm.begin(),listenPcm.end(),0);playback->readFrames(listenPcm.data(),2048);
            require(std::any_of(listenPcm.begin(),listenPcm.end(),[](std::int16_t n){return n!=0;}),"Chop preview auditions the selected slice");
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
            require(chopDraft->slices.size()==8,"Optional attack snap keeps requested slice count");tap(Button::B);
            require(!analysisSource&&!chopDraft&&undoStack.empty(),"Leaving Chop discards preview without an edit");
            resetAnalysisFixture();noticeUntil=0;
        }
