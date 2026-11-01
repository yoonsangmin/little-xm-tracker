            auto waveFocus=[&](int target){
                if(waveDrawFocus>=2&&target<2)tap(Button::Up);
                if(target<2) {
                    if(waveDrawFocus!=target)tap(target==0?Button::Up:Button::Down);
                } else {
                    while(waveDrawFocus<2)tap(Button::Down);
                    if(waveDrawFocus!=target)tap(target==2?Button::Left:Button::Right);
                }
                require(waveDrawFocus==target,"DRAW fields and the horizontal button row are reachable");
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
            tap(Button::Right);tap(Button::Right);tap(Button::Down);
            require(waveDrawFocus==3,"APPLY clamps at the right and bottom edges instead of wrapping");
            tap(Button::Up);require(waveDrawFocus==1,"APPLY up goes straight to EDIT POINTS, not NEW WAVE");
            tap(Button::Down);require(waveDrawFocus==3,"returning to the DRAW button row remembers APPLY");
            tap(Button::Left);tap(Button::Left);tap(Button::Up);tap(Button::Down);
            require(waveDrawFocus==2,"NEW WAVE is also remembered and left never leaves its button row");
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
            const auto beforeBlankWave=io::encodeXm(doc);openWaveDraw();
            require(wavePresetOpen&&!drawingSample&&waveNewFocus==0&&waveDrawPreset==0,
                    "an empty sample opens NEW WAVE with BLANK selected and no live draft");
            tap(Button::Up);tap(Button::Left);
            require(waveNewFocus==0,"NEW WAVE preset grid clamps at its top and left edges");
            tap(Button::Right);tap(Button::Right);tap(Button::Down);
            require(waveNewFocus==5,"first entry into a new preset row retains the source column");
            tap(Button::Left);tap(Button::Up);require(waveNewFocus==2,"returning to the upper preset row restores its last column");
            tap(Button::Down);require(waveNewFocus==4,"returning to the lower preset row restores its independent last column");
            tap(Button::Up);tap(Button::Left);tap(Button::Left);
            tap(Button::A);require(waveNewFocus==6,"A on a preset advances to FRAMES without creating PCM");
            tap(Button::Right);tap(Button::Left);
            require(waveNewFocus==6&&waveDrawLengthIndex==1,"plain horizontal input does not move a vertical FRAMES field");
            chord({Button::Right});tap(Button::B);
            require(!drawingSample&&!wavePresetOpen&&mode==Mode::Normal&&io::encodeXm(doc)==beforeBlankWave,
                    "cancelling empty NEW WAVE leaves the instrument sample-free");
            openWaveDraw();tap(Button::A);chord({Button::Right});tap(Button::Down);
            require(waveNewFocus==7&&!drawingSample,"CREATE is a separate step after preset and length selection");
            tap(Button::Down);tap(Button::Left);tap(Button::Right);
            require(waveNewFocus==7,"the single CREATE button neither wraps nor reacts to horizontal navigation");
            tap(Button::Up);require(waveNewFocus==6,"CREATE up returns directly to FRAMES");
            tap(Button::Down);tap(Button::A);
            require(!wavePresetOpen&&drawingSample&&drawingSample->pcm.size()==64&&waveDrawFocus==0&&io::encodeXm(doc)==beforeBlankWave,
                    "CREATE stages the selected length and returns to DRAW VALUE without editing the song");
            chord({Button::Up});
            require(drawingSample->pcm[0]==4096&&drawingSample->pcm[1]>0&&drawingSample->pcm[4]==0,
                    "a control-point edit draws a slope into a newly generated blank wave");
            waveFocus(3);tap(Button::A);
            require(currentSample()&&!currentSample()->sixteenBit&&currentSample()->pcm.size()==64&&
                    currentSample()->loopType==1&&currentSample()->loopStart==0&&currentSample()->loopLength==64,
                    "new waves apply as eight-bit looping PCM with their chosen frame count");

            // NEW WAVE cancellation preserves already drawn PCM and its cursor.
            selectionStart=8;selectionEnd=24;
            const auto beforeNewWave=io::encodeXm(doc);openWaveDraw();chord({Button::Up});tap(Button::Right);
            waveFocus(1);chord({Button::Right});
            const auto existingDraft=drawingSample->pcm;const int existingFrame=drawFrame;
            const int existingResolution=waveDrawResolution;
            waveFocus(2);tap(Button::A);
            require(wavePresetOpen&&drawingSample->pcm==existingDraft,"opening NEW WAVE does not clear an existing drawing draft");
            tap(Button::Down);tap(Button::A);chord({Button::Right});tap(Button::B);
            require(!wavePresetOpen&&mode==Mode::WaveDraw&&drawingSample->pcm==existingDraft&&drawFrame==existingFrame&&
                    waveDrawFirst==8&&waveDrawLast==24&&waveDrawResolution==existingResolution&&waveDrawFocus==2&&
                    io::encodeXm(doc)==beforeNewWave,"B from NEW WAVE retains the existing draft, range, cursor and editing resolution");
            tap(Button::B);require(io::encodeXm(doc)==beforeNewWave,"B from DRAW discards the retained draft without changing the song");

            // A new waveform replaces the full draft rather than filling an imported selection.
            openWaveDraw();waveFocus(2);tap(Button::A);tap(Button::Down);
            require(waveDrawPreset==3,"SQUARE is reachable in the second row of NEW WAVE");
            tap(Button::A);chord({Button::Right});tap(Button::Down);tap(Button::A);
            require(drawingSample->pcm.size()==64&&drawingSample->pcm[0]==16384&&drawingSample->pcm[32]==-16384&&
                    waveDrawFirst==0&&waveDrawLast==64&&io::encodeXm(doc)==beforeNewWave,
                    "CREATE replaces only the local draft with a complete square wave, leaving the song untouched");
            const auto squareUndoCount=undoStack.size();waveFocus(3);tap(Button::A);
            require(undoStack.size()==squareUndoCount+1&&currentSample()->loopLength==64,
                    "new waveform APPLY creates one undoable sample replacement");
            undo();require(io::encodeXm(doc)==beforeNewWave,"one Undo restores the complete previous sample and metadata");

            selectionStart=selectionEnd=0;openWaveDraw();waveFocus(2);tap(Button::A);
            tap(Button::Down);tap(Button::Right);tap(Button::Right);
            require(wavePresetOpen&&waveDrawPreset==5,"NOISE occupies the final preset tile");
            tap(Button::Right);require(waveNewFocus==5,"the right edge of the preset grid does not wrap");
            const auto firstNoiseCandidate=wavePresetDraftPcm;const auto firstNoiseSeed=wavePresetNoiseSeed;
            noticeUntil=0;draw();screenshot(home/"new-wave-native.bmp");SDL_RenderPresent(renderer);draw();
            require(wavePresetDraftPcm==firstNoiseCandidate&&wavePresetNoiseSeed==firstNoiseSeed,
                    "redrawing NEW WAVE never regenerates its noise candidate");
            for(int point=0;point<wavePresetThumbnailCounts[5];++point) {
                const auto frame=std::size_t(point)*(firstNoiseCandidate.size()-1)/std::size_t(wavePresetThumbnailCounts[5]-1);
                require(wavePresetThumbnails[5][std::size_t(point)]==firstNoiseCandidate[frame],
                        "the NOISE thumbnail samples exactly the candidate PCM that CREATE will use");
            }
            tap(Button::A);chord({Button::Right});
            require(wavePresetNoiseSeed==firstNoiseSeed&&wavePresetDraftPcm.size()==64,
                    "changing new-wave frame count preserves the selected noise seed");
            tap(Button::Up);require(waveNewFocus==5&&wavePresetNoiseSeed==firstNoiseSeed,
                    "FRAMES up remembers the selected preset tile without regenerating its noise candidate");
            tap(Button::Left);tap(Button::Right);
            require(wavePresetNoiseSeed!=firstNoiseSeed,"choosing Noise again explicitly creates a fresh candidate");
            const auto appliedNoise=wavePresetDraftPcm;tap(Button::A);tap(Button::Down);tap(Button::A);
            require(!wavePresetOpen&&drawingSample->pcm==appliedNoise&&io::encodeXm(doc)==beforeNewWave,
                    "CREATE uses the exact visible Noise candidate without altering the document");
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
