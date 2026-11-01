        {
            const auto synthSavedDocument=doc;
            const auto synthSavedPage=page;
            const auto synthSavedCursor=std::array<int,10>{{instrument,sample,sampleField,selectionStart,selectionEnd,
                selectedPattern,order,row,channel,field}};
            const bool synthSavedDirty=dirty,synthSavedFollow=followPlayback,synthSavedZoom=waveformZoom;
            const auto synthSavedRevision=documentRevision,synthSavedRecovery=recoverySerial;
            const auto synthSavedUndoBytes=undoBytes;
            auto synthSavedUndo=std::move(undoStack),synthSavedRedo=std::move(redoStack);
            undoStack.clear();redoStack.clear();undoBytes=0;
            playback->stop();stopWavePreview();held.fill(false);
            aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;selectPending=selectUsed=false;
            channelModifier=false;channelChord=0;cutChordUsed=modalBackHeld=false;
            doc=makeDemoDocument();doc.instruments[0]=Instrument{};
            page=Page::Sample;mode=Mode::Normal;instrument=1;sample=sampleField=0;
            selectionStart=selectionEnd=0;waveformZoom=false;followPlayback=false;dirty=false;
            normalize();playback->load(doc);
            const auto synthSource=io::encodeXm(doc);
            const auto synthInitialRevision=documentRevision,synthInitialRecovery=recoverySerial;
            auto synthVisible=[&](int target){
                for(const auto& controls:waveNewRows())
                    if(std::find(controls.begin(),controls.end(),target)!=controls.end())return true;
                return false;
            };
            auto synthFocus=[&](int target){
                for(int step=0;step<32&&waveNewFocus!=target;++step) {
                    const auto rows=waveNewRows();
                    int fromRow=-1,toRow=-1,fromColumn=0,toColumn=0;
                    for(std::size_t r=0;r<rows.size();++r)
                        for(std::size_t c=0;c<rows[r].size();++c) {
                            if(rows[r][c]==waveNewFocus){fromRow=int(r);fromColumn=int(c);}
                            if(rows[r][c]==target){toRow=int(r);toColumn=int(c);}
                        }
                    require(fromRow>=0&&toRow>=0,"synthesis focus never occupies a hidden field");
                    tap(fromRow!=toRow?(fromRow<toRow?Button::Down:Button::Up):
                        (fromColumn<toColumn?Button::Right:Button::Left));
                }
                require(waveNewFocus==target,"synthesis fields are reachable using controller row navigation");
            };
            auto synthPeak=[&]{
                std::vector<std::int16_t> pcm(4096*2);
                playback->readFrames(pcm.data(),256); // Discard the short preview-start ramp.
                playback->readFrames(pcm.data(),4096);
                int peak=0;for(const auto value:pcm)peak=std::max(peak,std::abs(int(value)));
                return peak;
            };
            auto synthShot=[&](const char* name){noticeUntil=0;draw();screenshot(home/name);SDL_RenderPresent(renderer);};

            openWaveDraw();
            require(wavePresetOpen&&synthVisible(6)&&synthVisible(15)&&synthVisible(16)&&
                    !synthVisible(10)&&!synthVisible(13)&&!synthVisible(14),
                    "neutral tone creation hides inactive LIMIT, CUTOFF and Q without hiding PHASE controls");
            const auto neutralSine=wavePresetDraftPcm;
            tap(Button::Start);const int neutralPeak=synthPeak();
            require(wavePreview&&neutralPeak>0,"synthesis edits begin with an audible unsaved sine candidate");
            synthFocus(9);tap(Button::A);tap(Button::Left);tap(Button::Right);
            require(waveNewFocus==9&&waveSynth.drivePercent==100&&wavePresetDraftPcm==neutralSine,
                    "DRIVE uses A plus direction; A alone or plain row navigation leaves its value unchanged");
            chord({Button::Down,Button::Down});
            require(waveSynth.drivePercent==0&&wavePreview&&synthPeak()==0&&
                    std::all_of(wavePresetDraftPcm.begin(),wavePresetDraftPcm.end(),[](std::int16_t value){return value==0;}),
                    "DRIVE zero immediately silences the listening candidate without stopping audition");
            chord({Button::Up,Button::Up});
            require(waveSynth.drivePercent==100&&wavePresetDraftPcm==neutralSine&&synthPeak()>0,
                    "returning DRIVE to its default regenerates the original waveform without accumulated processing");
            chord({Button::Up,Button::Up});
            require(waveSynth.drivePercent==200&&synthPeak()>neutralPeak,
                    "raising DRIVE changes the actual unsaved preview audio");
            synthFocus(8);chord({Button::Right});
            require(waveSynth.distortion==edit::WaveDistortion::Clip&&synthVisible(10)&&waveNewFocus==8,
                    "enabling distortion reveals LIMIT without moving the cursor");
            const auto clippedSine=wavePresetDraftPcm;
            chord({Button::Right});const auto foldedSine=wavePresetDraftPcm;
            chord({Button::Right});const auto wrappedSine=wavePresetDraftPcm;
            require(clippedSine!=foldedSine&&foldedSine!=wrappedSine&&clippedSine!=wrappedSine&&wavePreview&&synthPeak()>0,
                    "CLIP, FOLD and WRAP replace the listening PCM with distinct nonlinear shapes");
            chord({Button::Right});require(waveSynth.distortion==edit::WaveDistortion::Wrap,"distortion modes clamp without wrapping");
            synthFocus(10);chord({Button::Down,Button::Down,Button::Down});
            require(waveSynth.limitPercent==70&&wavePresetDraftPcm!=wrappedSine,"LIMIT edits reshape the current wrapped waveform");
            synthFocus(11);tap(Button::Down);tap(Button::Up);
            require(waveNewFocus==11,"returning to the LIMIT/VSHIFT row restores its last horizontal position");
            chord({Button::Up});require(waveSynth.vshiftPercent==10,"VSHIFT supports coarse vertical adjustment");
            chord({Button::Down});
            synthFocus(8);chord({Button::Left,Button::Left,Button::Left});
            require(!synthVisible(10)&&waveNewFocus==8,"turning distortion off removes LIMIT without invalidating focus");
            synthFocus(9);chord({Button::Down,Button::Down});
            require(wavePresetDraftPcm==neutralSine,"hidden LIMIT has no effect with distortion OFF");

            synthFocus(12);chord({Button::Right});
            require(waveSynth.filter==edit::WaveFilter::LowPass&&synthVisible(13)&&synthVisible(14)&&waveNewFocus==12,
                    "enabling a filter reveals its CUTOFF/Q row without moving the cursor");
            synthFocus(14);tap(Button::Down);tap(Button::Up);
            require(waveNewFocus==14,"filter parameter row remembers Q independently of the phase row");
            chord({Button::Up});require(waveSynth.resonanceTenths==17,"Q displays a resonance value edited in tenths");
            chord({Button::Down});
            synthFocus(13);chord({Button::Down,Button::Down,Button::Left,Button::Left,Button::Left,Button::Left});
            require(waveSynth.cutoffPercent==1&&wavePreview&&synthPeak()<neutralPeak/2,
                    "lowering low-pass CUTOFF attenuates the actual listening waveform");
            synthFocus(12);chord({Button::Right,Button::Right,Button::Right});
            require(waveSynth.filter==edit::WaveFilter::AllPass&&wavePresetDraftPcm!=neutralSine,
                    "ALLPASS is a selectable phase-changing filter rather than a bypass alias");
            chord({Button::Left,Button::Left,Button::Left,Button::Left});
            require(waveSynth.filter==edit::WaveFilter::Off&&!synthVisible(13)&&!synthVisible(14)&&wavePresetDraftPcm==neutralSine,
                    "turning the filter off restores the source while retaining but ignoring cutoff and Q values");
            synthFocus(15);chord({Button::Right});synthFocus(16);
            chord({Button::Up,Button::Up,Button::Up});
            require(waveSynth.phase==edit::WavePhase::Warp&&waveSynth.phaseAmount==30&&wavePresetDraftPcm!=neutralSine,
                    "PHASE mode and AMOUNT create a new PCM shape before saving");
            const auto warpSine=wavePresetDraftPcm;
            synthFocus(15);chord({Button::Right});const auto resyncSine=wavePresetDraftPcm;
            chord({Button::Right});
            require(waveSynth.phase==edit::WavePhase::ResyncRaw&&wavePresetDraftPcm!=resyncSine&&resyncSine!=warpSine,
                    "both filtered RESYNC and raw RESYN2 are reachable and generate distinct PCM");
            chord({Button::Left,Button::Left});
            synthFocus(12);chord({Button::Right});
            synthFocus(13);chord({Button::Up});synthFocus(14);chord({Button::Up,Button::Up});
            synthFocus(8);chord({Button::Right,Button::Right});
            synthFocus(9);chord({Button::Up,Button::Right,Button::Right,Button::Right,Button::Right,Button::Right});
            synthFocus(16);chord({Button::Right,Button::Right,Button::Right,Button::Right,Button::Right});
            require(io::encodeXm(doc)==synthSource&&!dirty&&undoStack.empty()&&redoStack.empty()&&
                    documentRevision==synthInitialRevision&&recoverySerial==synthInitialRecovery,
                    "all synthesis edits and listening remain outside document, recovery and undo state");
            synthShot("new-wave-synth-full.bmp");
            const auto shapedCandidate=wavePresetDraftPcm;
            synthFocus(7);tap(Button::A);
            require(drawingSample&&drawingSample->pcm==shapedCandidate&&wavePreview&&synthPeak()>0&&io::encodeXm(doc)==synthSource,
                    "CREATE keeps the exact shaped candidate and its audition as a local drawing draft");
            tap(Button::Down);tap(Button::Down);tap(Button::Right);tap(Button::Right);tap(Button::A);
            require(mode==Mode::Normal&&!wavePreview&&currentSample()&&currentSample()->pcm==shapedCandidate&&undoStack.size()==1,
                    "APPLY commits the processed loop once and stops audition");
            const auto shapedReopened=io::decodeXm(io::encodeXm(doc));
            require(shapedReopened.instruments[0].samples[0].pcm==shapedCandidate&&shapedReopened.instruments[0].samples[0].loopLength==shapedCandidate.size(),
                    "the baked synth result saves as ordinary XM PCM and a full loop");
            undo();require(io::encodeXm(doc)==synthSource,"one Undo removes the entire newly generated shaped sample");

            openWaveDraw();synthFocus(16);chord({Button::Up,Button::Up,Button::Up,Button::Up,Button::Up});
            synthFocus(4);
            require(!synthVisible(6)&&!synthVisible(15)&&!synthVisible(16)&&wavePresetDraftPcm.size()==32767,
                    "NOISE hides tone-only levels, frames and phase controls while retaining its full period");
            Sample rawNoise;rawNoise.sixteenBit=false;rawNoise.pcm.resize(32767);
            edit::fillNoisePreset(rawNoise,{0,rawNoise.pcm.size()},edit::NoisePreset::Long);
            require(wavePresetDraftPcm==rawNoise.pcm,"a retained tone PHASE amount does not alter NOISE");
            synthFocus(12);chord({Button::Right});
            require(synthVisible(13)&&synthVisible(14)&&wavePresetDraftPcm!=rawNoise.pcm,
                    "NOISE still supports filter processing despite hiding tone-specific controls");
            synthFocus(13);chord({Button::Down});synthShot("new-wave-synth-noise.bmp");
            synthFocus(0);require(waveSynth.phaseAmount==50&&synthVisible(16),"returning to a tone retains its phase setting");
            tap(Button::Start);tap(Button::B);
            require(mode==Mode::Normal&&!wavePreview&&!currentSample()&&io::encodeXm(doc)==synthSource,
                    "canceling a shaped creation preserves the empty slot and stops listening");

            playback->stop();stopWavePreview();drawingSample.reset();wavePresetOpen=false;wavePresetDraftPcm.clear();
            doc=synthSavedDocument;page=synthSavedPage;mode=Mode::Normal;
            instrument=synthSavedCursor[0];sample=synthSavedCursor[1];sampleField=synthSavedCursor[2];
            selectionStart=synthSavedCursor[3];selectionEnd=synthSavedCursor[4];selectedPattern=synthSavedCursor[5];
            order=synthSavedCursor[6];row=synthSavedCursor[7];channel=synthSavedCursor[8];field=synthSavedCursor[9];
            dirty=synthSavedDirty;followPlayback=synthSavedFollow;waveformZoom=synthSavedZoom;
            documentRevision=synthSavedRevision;recoverySerial=synthSavedRecovery;
            undoStack=std::move(synthSavedUndo);redoStack=std::move(synthSavedRedo);undoBytes=synthSavedUndoBytes;
            held.fill(false);noticeUntil=0;waveformInstrument=-1;playback->load(doc);
        }
