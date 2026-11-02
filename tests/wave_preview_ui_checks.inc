        {
            const auto previewSavedDocument=doc;
            const auto previewSavedPage=page;
            const auto previewSavedCursor=std::array<int,10>{{instrument,sample,sampleField,selectionStart,selectionEnd,
                selectedPattern,order,row,channel,field}};
            const bool previewSavedDirty=dirty,previewSavedFollow=followPlayback,previewSavedZoom=waveformZoom;
            const auto previewSavedRevision=documentRevision,previewSavedRecovery=recoverySerial;
            const auto previewSavedUndoBytes=undoBytes;
            auto previewSavedUndo=std::move(undoStack),previewSavedRedo=std::move(redoStack);
            undoStack.clear();redoStack.clear();undoBytes=0;
            playback->stop();playback->stopPreview();held.fill(false);
            aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;selectPending=selectUsed=false;
            channelModifier=false;channelChord=0;cutChordUsed=modalBackHeld=false;
            doc=makeDemoDocument();doc.instruments[0]=Instrument{};
            page=Page::Sample;mode=Mode::Normal;instrument=1;sample=sampleField=0;
            selectionStart=selectionEnd=0;waveformZoom=false;followPlayback=false;dirty=false;
            normalize();playback->load(doc);
            const auto previewSource=io::encodeXm(doc);
            const auto previewInitialRevision=documentRevision,previewInitialRecovery=recoverySerial;
            auto previewPcm=[&](std::size_t frames=1024){
                std::vector<std::int16_t> pcm(frames*2);
                playback->readFrames(pcm.data(),frames);return pcm;
            };
            auto previewAudible=[&]{
                const auto pcm=previewPcm();
                return std::any_of(pcm.begin(),pcm.end(),[](std::int16_t value){return value!=0;});
            };
            auto previewPositiveFraction=[&]{
                previewPcm(256); // Ignore the short mixer ramp on a fresh trigger.
                const auto pcm=previewPcm(4096);std::size_t positive=0;
                for(std::size_t n=0;n<pcm.size();n+=2)if(pcm[n]>0)++positive;
                return double(positive)/double(pcm.size()/2);
            };
            auto previewDrawFocus=[&](int target){
                if(waveDrawFocus>=2&&target<2)tap(Button::Up);
                if(target<2) {
                    if(waveDrawFocus!=target)tap(target==0?Button::Up:Button::Down);
                } else {
                    while(waveDrawFocus<2)tap(Button::Down);
                    for(int n=0;n<3&&waveDrawFocus!=target;++n)
                        tap(target<waveDrawFocus?Button::Left:Button::Right);
                }
                require(waveDrawFocus==target,"audition workflow can navigate the DRAW button row");
            };
            auto previewCreateFocus=[&](int target){
                for(int step=0;step<32&&waveNewFocus!=target;++step) {
                    const auto rows=waveNewRows();
                    int fromRow=-1,toRow=-1,fromColumn=0,toColumn=0;
                    for(std::size_t r=0;r<rows.size();++r)
                        for(std::size_t c=0;c<rows[r].size();++c) {
                            if(rows[r][c]==waveNewFocus){fromRow=int(r);fromColumn=int(c);}
                            if(rows[r][c]==target){toRow=int(r);toColumn=int(c);}
                        }
                    require(fromRow>=0&&toRow>=0,"audition navigation uses visible creation fields");
                    tap(fromRow!=toRow?(fromRow<toRow?Button::Down:Button::Up):
                        (fromColumn<toColumn?Button::Right:Button::Left));
                }
                require(waveNewFocus==target,"audition workflow can reach the requested NEW WAVE field");
            };

            openWaveDraw();
            require(wavePresetOpen&&!drawingSample&&!currentSample()&&!wavePreview&&!previewAudible(),
                    "empty-slot NEW WAVE remains silent and unmaterialized until Start");
            buttonInput(Button::Start,true);buttonDown(Button::Start,true);buttonInput(Button::Start,false);
            require(wavePreview&&previewAudible()&&!playback->status().playing,
                    "Start auditions the unsaved candidate and held-key repeat does not toggle it off or start the song");
            noticeUntil=0;draw();screenshot(home/"wave-preview-new.bmp");SDL_RenderPresent(renderer);
            tap(Button::A);chord({Button::Right});
            require(waveNewFocus==5&&waveDrawLengthIndex==2&&wavePreview&&previewAudible(),
                    "A release and live FRAMES edits retain candidate audition");
            tap(Button::Up);tap(Button::Right);tap(Button::Right);tap(Button::A);tap(Button::Right);
            require(waveDrawPreset==2&&waveNewFocus==6,"live preview can switch to PULSE and its DUTY field");
            chord({Button::Left,Button::Left});
            const double narrowPulseFraction=previewPositiveFraction();
            chord({Button::Right,Button::Right,Button::Right});
            const double widePulseFraction=previewPositiveFraction();
            require(narrowPulseFraction<0.25&&widePulseFraction>0.60,
                    "DUTY changes replace the audible unsaved waveform, not just its thumbnail");
            require(io::encodeXm(doc)==previewSource&&!dirty&&undoStack.empty()&&redoStack.empty()&&
                    documentRevision==previewInitialRevision&&recoverySerial==previewInitialRecovery,
                    "candidate audition and live options create no sample, undo entry or recovery change");
            tap(Button::Start);
            require(!wavePreview&&!previewAudible(),"Start stops candidate audition immediately");
            tap(Button::Start);previewCreateFocus(7);tap(Button::A);
            require(!wavePresetOpen&&drawingSample&&wavePreview&&previewAudible()&&io::encodeXm(doc)==previewSource,
                    "CREATE retains listening on the resulting local draft without committing a sample");

            previewDrawFocus(3);tap(Button::A);
            require(wavePreview&&!previewAudible(),"CLEAR ALL updates the armed draft preview to silence before APPLY");
            previewDrawFocus(0);chord({Button::Up});
            require(wavePreview&&previewAudible(),"drawing a point makes the live cleared draft audible before APPLY");
            noticeUntil=0;draw();screenshot(home/"wave-preview-draw.bmp");SDL_RenderPresent(renderer);
            buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);
            require(wavePreview&&!previewAudible(),"A+B zero updates live preview without cancelling DRAW");
            chord({Button::Up});
            require(previewAudible(),"draft preview can sound again after zeroing a point");
            SDL_Event lostWaveFocus{};lostWaveFocus.type=SDL_WINDOWEVENT;lostWaveFocus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
            event(lostWaveFocus);
            require(!wavePreview&&!previewAudible(),"losing window focus disarms and stops draft audition");
            tap(Button::Start);previewDrawFocus(2);tap(Button::A);
            require(wavePresetOpen&&wavePreview&&previewAudible(),"NEW WAVE auditions its candidate while preserving the previous draft");
            const auto retainedPreviewDraft=drawingSample->pcm;tap(Button::B);
            require(!wavePresetOpen&&!wavePreview&&!previewAudible()&&drawingSample->pcm==retainedPreviewDraft,
                    "B from NEW WAVE stops audition and restores the existing draft without automatically resuming sound");
            tap(Button::Start);require(previewAudible(),"Start can audition the restored draft explicitly");
            tap(Button::B);
            require(mode==Mode::Normal&&!wavePreview&&!previewAudible()&&io::encodeXm(doc)==previewSource,
                    "cancelling DRAW stops sound and leaves the document unchanged");
            openWaveDraw();tap(Button::Start);tap(Button::B);
            require(mode==Mode::Normal&&!wavePreview&&!previewAudible()&&io::encodeXm(doc)==previewSource,
                    "cancelling creation on an empty slot also stops its unsaved candidate");

            openWaveDraw();tap(Button::Start);previewCreateFocus(7);tap(Button::A);
            require(drawingSample&&wavePreview&&previewAudible(),"a newly created draft remains audible before final APPLY");
            previewDrawFocus(4);tap(Button::A);
            require(mode==Mode::Normal&&!wavePreview&&!previewAudible()&&currentSample()&&undoStack.size()==1,
                    "APPLY stops draft audition and commits exactly the existing single DRAW edit");
            undo();require(io::encodeXm(doc)==previewSource,"one Undo restores the empty slot after auditioned drawing");

            playback->playSong();openWaveDraw();tap(Button::Start);
            require(!playback->status().playing&&!wavePreview&&!previewAudible(),
                    "Start while the song is playing keeps the global stop-first rule inside NEW WAVE");
            tap(Button::Start);
            require(wavePreview&&previewAudible(),"the next Start auditions the modal candidate after song stop");
            tap(Button::B);

            doc=makeDemoDocument();doc.instruments[0].samples[0].loopType=0;
            selectionStart=selectionEnd=0;playback->load(doc);openWaveDraw();tap(Button::Start);
            require(previewAudible(),"Start auditions an existing one-shot DRAW sample");
            previewPcm(48000);
            require(!playback->previewPlaying(),"one-shot DRAW audition naturally ends");
            tap(Button::Start);
            require(previewAudible(),"one Start restarts a naturally finished DRAW sample");
            tap(Button::B);

            playback->stop();stopWavePreview();drawingSample.reset();wavePresetOpen=false;wavePresetDraftPcm.clear();
            doc=previewSavedDocument;page=previewSavedPage;mode=Mode::Normal;
            instrument=previewSavedCursor[0];sample=previewSavedCursor[1];sampleField=previewSavedCursor[2];
            selectionStart=previewSavedCursor[3];selectionEnd=previewSavedCursor[4];selectedPattern=previewSavedCursor[5];
            order=previewSavedCursor[6];row=previewSavedCursor[7];channel=previewSavedCursor[8];field=previewSavedCursor[9];
            dirty=previewSavedDirty;followPlayback=previewSavedFollow;waveformZoom=previewSavedZoom;
            documentRevision=previewSavedRevision;recoverySerial=previewSavedRecovery;
            undoStack=std::move(previewSavedUndo);redoStack=std::move(previewSavedRedo);undoBytes=previewSavedUndoBytes;
            held.fill(false);noticeUntil=0;waveformInstrument=-1;playback->load(doc);
        }
