            auto waveFocus=[&](int target){
                if(waveDrawFocus>=2&&target<2)tap(Button::Up);
                if(target<2) {
                    if(waveDrawFocus!=target)tap(target==0?Button::Up:Button::Down);
                } else {
                    while(waveDrawFocus<2)tap(Button::Down);
                    for(int step=0;step<3&&waveDrawFocus!=target;++step)
                        tap(target<waveDrawFocus?Button::Left:Button::Right);
                }
                require(waveDrawFocus==target,"DRAW fields and the horizontal button row are reachable");
            };
            auto waveCreateFocus=[&](int target){
                for(int step=0;step<32&&waveNewFocus!=target;++step) {
                    const auto rows=waveNewRows();
                    int fromRow=-1,toRow=-1,fromColumn=0,toColumn=0;
                    for(std::size_t r=0;r<rows.size();++r)
                        for(std::size_t c=0;c<rows[r].size();++c) {
                            if(rows[r][c]==waveNewFocus){fromRow=int(r);fromColumn=int(c);}
                            if(rows[r][c]==target){toRow=int(r);toColumn=int(c);}
                        }
                    require(fromRow>=0&&toRow>=0,"NEW WAVE navigation uses only visible fields");
                    tap(fromRow!=toRow?(fromRow<toRow?Button::Down:Button::Up):
                        (fromColumn<toColumn?Button::Right:Button::Left));
                }
                require(waveNewFocus==target,"NEW WAVE visible fields and CREATE are controller reachable");
            };
            const auto beforeDraw=io::encodeXm(doc);openWaveDraw();chord({Button::Right});tap(Button::B);
            require(!drawingSample&&io::encodeXm(doc)==beforeDraw,"cancelling DRAW keeps original PCM");
            openWaveDraw();const int firstDrawFrame=drawFrame;
            tap(Button::Up);require(waveDrawFocus==0,"DRAW starts on VALUE and never wraps at the top");
            tap(Button::Right);const int secondDrawFrame=drawFrame;
            require(secondDrawFrame>firstDrawFrame,"VALUE plain right moves the selected point without graph focus");
            const auto beforeValue=drawingSample->pcm;chord({Button::Right});
            require(drawFrame==secondDrawFrame&&drawingSample->pcm[std::size_t(drawFrame)]!=beforeValue[std::size_t(drawFrame)],
                    "A plus a direction changes VALUE without moving its point");
            tap(Button::Down);tap(Button::Right);
            require(waveDrawFocus==1&&drawFrame>secondDrawFrame,"EDIT POINTS plain right also moves the selected point");
            const auto beforeResolution=drawingSample->pcm;chord({Button::Right});
            require(waveDrawResolution==2&&drawingSample->pcm==beforeResolution,
                    "changing editing resolution never resizes or changes the draft PCM");
            tap(Button::Down);require(waveDrawFocus==2,"down enters the first DRAW button initially");
            tap(Button::Right);tap(Button::Right);tap(Button::Right);tap(Button::Down);
            require(waveDrawFocus==3,"APPLY clamps at the right and bottom edges instead of wrapping");
            tap(Button::Up);require(waveDrawFocus==1,"APPLY up goes straight to EDIT POINTS");
            tap(Button::Down);require(waveDrawFocus==3,"returning to the DRAW button row remembers APPLY");
            tap(Button::Left);tap(Button::Left);tap(Button::Left);tap(Button::Up);tap(Button::Down);
            require(waveDrawFocus==2,"CLEAR ALL is also remembered and left never leaves its button row");
            tap(Button::B);

            openWaveDraw();buttonInput(Button::A,true);tap(Button::B);buttonInput(Button::A,false);chord({Button::Right});
            require(waveDrawPointCount()==16&&drawingSample->pcm[1]!=currentSample()->pcm[1],
                    "coarse drawing interpolates neighbouring PCM instead of requiring per-frame edits");
            const auto beforeApplyCount=undoStack.size();waveFocus(3);tap(Button::A);
            require(!drawingSample&&mode==Mode::Normal&&currentSample()->pcm[0]==256&&undoStack.size()==beforeApplyCount+1,
                    "DRAW APPLY commits exactly one edit and consumes its A release");
            undo();require(io::encodeXm(doc)==beforeDraw,"one Undo restores the complete sample before drawing");

            // Empty slots start in generation settings without allocating a song sample.
            *currentInstrument()=Instrument{};sample=0;normalize();playback->load(doc);
            const auto beforeEmptyWave=io::encodeXm(doc);openWaveDraw();
            require(wavePresetOpen&&!drawingSample&&waveNewFocus==0&&waveDrawPreset==0,
                    "an empty sample opens NEW WAVE with SINE selected and no live draft");
            tap(Button::Up);tap(Button::Left);
            require(waveNewFocus==0,"NEW WAVE preset row clamps at its top and left edges");
            waveCreateFocus(4);tap(Button::Right);
            require(waveNewFocus==4,"the final NOISE tile does not wrap to SINE");
            waveCreateFocus(2);tap(Button::Down);
            require(waveNewFocus==5||waveNewFocus==6,"down leaves the preset row directly for its options");
            tap(Button::Up);require(waveNewFocus==2,"returning to the preset row restores its last selected shape");
            waveCreateFocus(0);
            tap(Button::A);require(waveNewFocus==5,"A on a preset advances to FRAMES without creating PCM");
            tap(Button::Right);require(waveNewFocus==6,"plain right selects LEVELS in the same parameter row");tap(Button::Left);
            require(waveNewFocus==5&&waveDrawLengthIndex==1,"plain horizontal navigation does not change FRAMES");
            chord({Button::Right});tap(Button::B);
            require(!drawingSample&&!wavePresetOpen&&mode==Mode::Normal&&io::encodeXm(doc)==beforeEmptyWave,
                    "cancelling empty NEW WAVE leaves the instrument sample-free");
            openWaveDraw();tap(Button::A);chord({Button::Right});tap(Button::Right);
            require(waveNewFocus==6&&!drawingSample,"SINE exposes LEVELS beside FRAMES without creating PCM");
            const auto fullLevelSine=wavePresetDraftPcm;tap(Button::A);
            require(wavePresetDraftPcm==fullLevelSine,"A alone on LEVELS does not edit the setting or create PCM");
            chord({Button::Left});
            auto sineLevels=wavePresetDraftPcm;std::sort(sineLevels.begin(),sineLevels.end());
            sineLevels.erase(std::unique(sineLevels.begin(),sineLevels.end()),sineLevels.end());
            require(waveLevelIndex==0&&sineLevels.size()==16&&wavePresetDraftPcm!=fullLevelSine&&
                    std::all_of(wavePresetDraftPcm.begin(),wavePresetDraftPcm.end(),[](std::int16_t value){return value%256==0;}),
                    "LEVELS 16 produces sixteen eight-bit-compatible amplitudes instead of the full-resolution sine");
            noticeUntil=0;draw();screenshot(home/"new-wave-levels-native.bmp");SDL_RenderPresent(renderer);
            waveCreateFocus(16);tap(Button::Down);
            require(waveNewFocus==7&&!drawingSample,"CREATE follows the visible synthesis parameter rows");
            tap(Button::Down);tap(Button::Left);tap(Button::Right);
            require(waveNewFocus==7,"the single CREATE button neither wraps nor reacts to horizontal navigation");
            tap(Button::Up);require(waveNewFocus==16,"CREATE up restores the last horizontal position in the PHASE row");
            tap(Button::Down);tap(Button::A);
            require(!wavePresetOpen&&drawingSample&&drawingSample->pcm.size()==64&&waveDrawFocus==0&&io::encodeXm(doc)==beforeEmptyWave,
                    "CREATE stages the selected length and returns to DRAW VALUE without editing the song");
            waveFocus(2);tap(Button::A);
            require(std::all_of(drawingSample->pcm.begin(),drawingSample->pcm.end(),[](std::int16_t value){return value==0;}),
                    "CLEAR ALL supplies an empty drawing canvas without a BLANK preset");
            waveFocus(0);chord({Button::Up});
            require(drawingSample->pcm[0]==4096&&drawingSample->pcm[1]>0&&drawingSample->pcm[4]==0,
                    "a control-point edit draws a slope after clearing the generated waveform");
            waveFocus(3);tap(Button::A);
            require(currentSample()&&!currentSample()->sixteenBit&&currentSample()->pcm.size()==64&&
                    currentSample()->loopType==1&&currentSample()->loopStart==0&&currentSample()->loopLength==64,
                    "new waves apply as eight-bit looping PCM with their chosen frame count");

            // The top-level creator replaces a full sample only after APPLY.
            // Cancelling it preserves the existing sample, range and history.
            selectionStart=8;selectionEnd=24;
            const auto beforeNewWave=io::encodeXm(doc);
            const auto beforeNewUndo=undoStack.size(),beforeNewRedo=redoStack.size();
            const auto beforeNewRevision=documentRevision,beforeNewRecovery=recoverySerial;
            const bool beforeNewDirty=dirty;
            openNewWave();
            require(mode==Mode::WaveDraw&&wavePresetOpen&&!drawingSample&&io::encodeXm(doc)==beforeNewWave,
                    "NEW WAVE opens its own candidate without opening or changing a DRAW draft");
            waveCreateFocus(5);chord({Button::Right});tap(Button::B);
            require(!wavePresetOpen&&!drawingSample&&mode==Mode::Normal&&selectionStart==8&&selectionEnd==24&&
                    io::encodeXm(doc)==beforeNewWave&&undoStack.size()==beforeNewUndo&&redoStack.size()==beforeNewRedo&&
                    documentRevision==beforeNewRevision&&recoverySerial==beforeNewRecovery&&dirty==beforeNewDirty,
                    "B from NEW WAVE returns to SAMPLE and preserves PCM, selection, save state and edit history");

            openWaveDraw();const auto beforeRangeDraw=drawingSample->pcm;chord({Button::Up});
            require(waveDrawFirst==8&&waveDrawLast==24&&drawingSample->pcm!=beforeRangeDraw&&
                    std::equal(beforeRangeDraw.begin(),beforeRangeDraw.begin()+8,drawingSample->pcm.begin())&&
                    std::equal(beforeRangeDraw.begin()+24,beforeRangeDraw.end(),drawingSample->pcm.begin()+24),
                    "DRAW still edits only the selected range and preserves PCM outside both endpoints");
            tap(Button::B);require(io::encodeXm(doc)==beforeNewWave,"cancelling a range drawing keeps the source sample unchanged");

            // Clearing is a whole-draft operation, even while editing a partial range.
            openWaveDraw();tap(Button::Right);
            const auto clearDraft=*drawingSample;const int clearCursor=drawFrame;
            const auto beforeClearUndo=undoStack.size();waveFocus(2);tap(Button::A);
            require(waveDrawFocus==2&&drawFrame==clearCursor&&waveDrawFirst==8&&waveDrawLast==24&&
                    drawingSample->pcm.size()==clearDraft.pcm.size()&&
                    std::all_of(drawingSample->pcm.begin(),drawingSample->pcm.end(),[](std::int16_t value){return value==0;}),
                    "CLEAR ALL zeros PCM outside and inside the selected range while retaining its cursor and frame count");
            require(drawingSample->name==clearDraft.name&&drawingSample->sixteenBit==clearDraft.sixteenBit&&
                    drawingSample->loopType==clearDraft.loopType&&drawingSample->loopStart==clearDraft.loopStart&&
                    drawingSample->loopLength==clearDraft.loopLength&&drawingSample->relativeNote==clearDraft.relativeNote&&
                    drawingSample->finetune==clearDraft.finetune&&drawingSample->volume==clearDraft.volume&&
                    drawingSample->panning==clearDraft.panning&&undoStack.size()==beforeClearUndo&&io::encodeXm(doc)==beforeNewWave,
                    "CLEAR ALL retains sample metadata and creates no document change or Undo before APPLY");
            noticeUntil=0;draw();screenshot(home/"wave-draw-clear-native.bmp");SDL_RenderPresent(renderer);
            tap(Button::Up);require(waveDrawFocus==1,"CLEAR ALL up leaves the horizontal button row immediately");
            tap(Button::Down);require(waveDrawFocus==2,"returning to the button row remembers CLEAR ALL");
            tap(Button::B);require(io::encodeXm(doc)==beforeNewWave,"canceling a cleared draft keeps every original frame");
            openWaveDraw();waveFocus(2);tap(Button::A);waveFocus(3);tap(Button::A);
            require(undoStack.size()==beforeClearUndo+1&&
                    std::all_of(currentSample()->pcm.begin(),currentSample()->pcm.end(),[](std::int16_t value){return value==0;}),
                    "applying CLEAR ALL commits exactly one undoable edit");
            undo();require(io::encodeXm(doc)==beforeNewWave,"one Undo restores all PCM and metadata after CLEAR ALL");

            // A new waveform replaces the full draft rather than filling an imported selection.
            selectionStart=8;selectionEnd=24;openNewWave();tap(Button::Right);tap(Button::Right);
            require(waveDrawPreset==2,"PULSE replaces the former fixed-duty SQUARE preset");
            tap(Button::A);
            require(waveNewFocus==5&&wavePresetDraftPcm.size()==32,
                    "a fresh PULSE creator starts on its default thirty-two-frame setting");
            chord({Button::Right});
            require(wavePresetDraftPcm.size()==64,"A+Right explicitly sets PULSE to sixty-four frames before editing DUTY");
            tap(Button::Right);
            require(waveNewFocus==6&&waveDutyIndex==2,"PULSE exposes DUTY beside FRAMES, initially at fifty percent");
            const auto halfPulse=wavePresetDraftPcm;tap(Button::A);tap(Button::Left);tap(Button::Right);
            require(wavePresetDraftPcm==halfPulse&&waveNewFocus==6,"DUTY changes only with A plus a direction");
            chord({Button::Left,Button::Left,Button::Left});
            require(waveDutyIndex==0&&std::count_if(wavePresetDraftPcm.begin(),wavePresetDraftPcm.end(),[](std::int16_t value){return value>0;})==8,
                    "twelve-and-a-half percent DUTY uses exactly eight high frames in a sixty-four-frame loop and clamps at its minimum");
            noticeUntil=0;draw();screenshot(home/"new-wave-pulse-native.bmp");SDL_RenderPresent(renderer);
            waveCreateFocus(7);tap(Button::A);
            require(drawingSample->pcm.size()==64&&drawingSample->pcm[0]==16384&&drawingSample->pcm[8]==-16384&&
                    waveDrawFirst==0&&waveDrawLast==64&&io::encodeXm(doc)==beforeNewWave,
                    "CREATE replaces only the local draft with the selected pulse width, leaving the song untouched");
            const auto pulseUndoCount=undoStack.size();waveFocus(3);tap(Button::A);
            require(undoStack.size()==pulseUndoCount+1&&currentSample()->loopLength==64,
                    "new waveform APPLY creates one undoable sample replacement");
            undo();require(io::encodeXm(doc)==beforeNewWave,"one Undo restores the complete previous sample and metadata");

            selectionStart=selectionEnd=0;openNewWave();
            waveCreateFocus(4);
            require(wavePresetOpen&&waveDrawPreset==4,"NOISE occupies the final real preset tile");
            tap(Button::Right);require(waveNewFocus==4,"the right edge of the preset row does not wrap");
            const auto firstNoiseCandidate=wavePresetDraftPcm;
            require(firstNoiseCandidate.size()==32767&&waveNoiseType==1,"NOISE starts with a complete long LFSR period");
            noticeUntil=0;draw();screenshot(home/"new-wave-native.bmp");SDL_RenderPresent(renderer);draw();
            require(wavePresetDraftPcm==firstNoiseCandidate,"redrawing NEW WAVE preserves its full noise candidate");
            tap(Button::A);require(waveNewFocus==5&&waveNewRows()[1]==std::vector<int>{5},
                    "NOISE exposes TYPE without tone-only FRAMES or LEVELS controls");
            tap(Button::Right);tap(Button::A);
            require(waveNoiseType==1&&wavePresetDraftPcm==firstNoiseCandidate,"plain direction and A alone leave NOISE TYPE unchanged");
            chord({Button::Left,Button::Left});
            require(waveNoiseType==0&&wavePresetDraftPcm.size()==127,"SHORT selects a full short noise period and clamps at the first type");
            const auto shortNoise=wavePresetDraftPcm;
            tap(Button::Down);require(waveNewFocus==8||waveNewFocus==9,"NOISE skips the hidden second base field on the way to synthesis controls");
            tap(Button::Up);require(waveNewFocus==5,"up returns directly to TYPE for NOISE");
            tap(Button::Up);require(waveNewFocus==4&&wavePresetDraftPcm==shortNoise,
                    "TYPE up remembers the selected NOISE tile without changing its candidate");
            waveCreateFocus(7);tap(Button::A);
            require(drawingSample&&drawingSample->pcm==shortNoise&&drawingSample->loopLength==127&&waveDrawResolution==4,
                    "SHORT CREATE uses the complete candidate and starts in individual-frame editing");
            tap(Button::B);openNewWave();waveCreateFocus(4);tap(Button::A);chord({Button::Left});chord({Button::Right,Button::Right});
            require(waveNoiseType==1&&wavePresetDraftPcm==firstNoiseCandidate,
                    "LONG remains deterministic when returning from SHORT and clamps at the final type");
            noticeUntil=0;draw();screenshot(home/"new-wave-noise-native.bmp");SDL_RenderPresent(renderer);
            const auto appliedNoise=wavePresetDraftPcm;waveCreateFocus(7);tap(Button::A);
            require(!wavePresetOpen&&drawingSample->pcm==appliedNoise&&drawingSample->loopLength==32767&&waveDrawResolution==4&&
                    io::encodeXm(doc)==beforeNewWave,"LONG CREATE preserves the full period in a FRAME draft without altering the document");
            noticeUntil=0;draw();screenshot(home/"wave-draw-noise-native.bmp");SDL_RenderPresent(renderer);
            const auto beforeNoiseUndo=undoStack.size();waveFocus(3);tap(Button::A);
            require(undoStack.size()==beforeNoiseUndo+1&&currentSample()->pcm==appliedNoise,
                    "final APPLY commits Noise as one Undo operation");
            const auto noiseReopened=io::decodeXm(io::encodeXm(doc));
            require(noiseReopened.instruments[std::size_t(instrument-1)].samples[0].pcm==appliedNoise,
                    "generated Noise survives XM saving and reopening as identical PCM");
            undo();require(io::encodeXm(doc)==beforeNewWave,"one Undo restores the sample before the entire Noise workflow");

            openWaveDraw();waveFocus(1);chord({Button::Right,Button::Right,Button::Right});waveFocus(0);
            require(waveDrawResolution==4,"FRAME-level editing remains available after coarse drawing");
            const auto frameBefore=drawingSample->pcm;chord({Button::Right});
            require(drawingSample->pcm[0]!=frameBefore[0]&&drawingSample->pcm[1]==frameBefore[1],
                    "FRAME mode changes only the selected PCM frame");
            tap(Button::B);
            openWaveDraw();drawingSample->pcm.resize(33);waveDrawFirst=0;waveDrawLast=33;
            waveDrawResolution=2;drawFrame=0;
            for(int point=1;point<32;++point)tap(Button::Right);
            require(drawFrame==32&&waveDrawPointIndex()==31,
                    "point navigation reaches the final anchor for nondivisible PCM lengths");
            tap(Button::B);
            mode=Mode::EchoSettings;echoField=2;tap(Button::Left);tap(Button::Right);
            require(echoField==2,"ECHO horizontal navigation no longer moves through vertical fields");
            tap(Button::Up);require(echoField==1,"ECHO vertical navigation still selects the adjacent row");tap(Button::B);
