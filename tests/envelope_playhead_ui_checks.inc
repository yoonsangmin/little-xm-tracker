        {
            const auto envSavedDocument=doc;
            const auto envSavedPage=page;
            const auto envSavedPalette=palette;
            const auto envSavedCursor=std::array<int,15>{{instrument,sample,instrumentField,keymapNote,
                selectedPattern,order,row,channel,field,envelopeField,envelopePoint,envelopeButtonColumn,
                selectionStart,selectionEnd,sampleField}};
            const bool envSavedDirty=dirty,envSavedFollow=followPlayback,envSavedMap=instrumentMapView;
            const auto envSavedRevision=documentRevision,envSavedRecovery=recoverySerial;
            const auto envSavedUndoBytes=undoBytes;
            const auto envSavedMutes=playback->muteMask();const int envSavedSolo=solo;
            auto envSavedUndo=std::move(undoStack),envSavedRedo=std::move(redoStack);
            playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
            held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            undoStack.clear();redoStack.clear();undoBytes=0;clearSelection();
            doc=makeDemoDocument();palette=defaultPalette;
            auto& envOwner=doc.instruments[0];envOwner.name="ENVELOPE PLAYHEAD";
            envOwner.volumeEnvelope={};envOwner.volumeEnvelope.count=3;envOwner.volumeEnvelope.flags=1;
            envOwner.volumeEnvelope.points[0]={0,64};envOwner.volumeEnvelope.points[1]={32,45};envOwner.volumeEnvelope.points[2]={96,16};
            envOwner.panningEnvelope={};envOwner.panningEnvelope.count=3;envOwner.panningEnvelope.flags=1;
            envOwner.panningEnvelope.points[0]={0,12};envOwner.panningEnvelope.points[1]={16,52};envOwner.panningEnvelope.points[2]={48,32};
            envOwner.samples[0].loopType=1;envOwner.samples[0].loopStart=0;
            envOwner.samples[0].loopLength=std::uint32_t(envOwner.samples[0].pcm.size());
            envOwner.noteToSample.fill(0);doc.instruments.push_back(envOwner);
            page=Page::Instrument;mode=Mode::Normal;instrument=1;sample=0;keymapNote=49;
            instrumentMapView=false;instrumentField=10;solo=-1;
            selectedPattern=order=row=channel=field=selectionStart=selectionEnd=0;
            followPlayback=dirty=false;normalize();playback->load(doc);playback->setMuteMask(0);
            const auto envSource=io::encodeXm(doc);
            const auto envRevision=documentRevision,envRecovery=recoverySerial;
            const auto envVolume=doc.instruments[0].volumeEnvelope,envPan=doc.instruments[0].panningEnvelope;
            auto envAdvance=[&]{std::vector<std::int16_t> pcm(4096*2);playback->readFrames(pcm.data(),4096);};
            int envOutputWidth=0,envOutputHeight=0;SDL_GetRendererOutputSize(renderer,&envOutputWidth,&envOutputHeight);
            const bool envNative=envOutputWidth==W&&envOutputHeight==H;
            const auto envInk=(std::uint32_t(ink.r)<<16)|(std::uint32_t(ink.g)<<8)|ink.b;
            const SDL_Rect envVolumeGraph{369,490,305,216},envPanGraph{693,490,305,216},envEditorGraph{85,150,530,220};
            auto envDraw=[&]{
                const auto before=playback->instrumentPreviewStatus();noticeUntil=0;draw();
                const auto after=playback->instrumentPreviewStatus();
                require(before.active==after.active&&before.instrument==after.instrument&&
                        before.volumeTick==after.volumeTick&&before.panningTick==after.panningTick,
                        "Drawing envelope playheads does not advance or change the preview voice");
            };
            auto envBar=[&](SDL_Rect graph,const Envelope& shape,int tick){
                const int expected=tick<0?-1:graph.x+std::clamp(tick,0,int(shape.points[shape.count-1].x))*
                    (graph.w-8)/std::max(1,int(shape.points[shape.count-1].x));
                if(!envNative)return expected;
                const SDL_Rect stripe{graph.x,graph.y+1,graph.w,1};
                std::vector<std::uint32_t> pixels(std::size_t(stripe.w));
                require(SDL_RenderReadPixels(renderer,&stripe,SDL_PIXELFORMAT_ARGB8888,pixels.data(),stripe.w*int(sizeof(std::uint32_t)))==0,
                        "Envelope playhead pixels are readable from the production renderer");
                int found=-1;
                for(int x=0;x<stripe.w;++x)if((pixels[std::size_t(x)]&0xffffffu)==envInk){
                    require(found<0,"An envelope graph contains only one playback bar");found=graph.x+x;
                }
                require(found==expected,"Envelope playback bar matches the actual tick on that graph, or is absent when inactive");
                if(expected>=0){
                    const SDL_Rect marker{expected-2,graph.y+graph.h-5,5,5};std::array<std::uint32_t,25> tip{};
                    require(SDL_RenderReadPixels(renderer,&marker,SDL_PIXELFORMAT_ARGB8888,tip.data(),5*int(sizeof(std::uint32_t)))==0&&
                            std::all_of(tip.begin(),tip.end(),[&](std::uint32_t pixel){return (pixel&0xffffffu)==envInk;}),
                            "The playback bar has its five-pixel marker below the envelope curve");
                }
                return found;
            };
            auto envMain=[&](bool visible){
                const auto preview=playback->instrumentPreviewStatus();envDraw();
                envBar(envVolumeGraph,envVolume,visible?preview.volumeTick:-1);
                envBar(envPanGraph,envPan,visible?preview.panningTick:-1);
            };

            envMain(false);tap(Button::Start);envAdvance();
            const auto envFirst=playback->instrumentPreviewStatus();
            require(envFirst.active&&envFirst.instrument==1&&envFirst.volumeTick>0&&envFirst.panningTick>0,
                    "Instrument Start exposes both real envelope positions after headless audio playback");
            envMain(true);screenshot(home/"instrument-envelope-playhead.bmp");SDL_RenderPresent(renderer);
            envAdvance();const auto envLater=playback->instrumentPreviewStatus();envMain(true);
            require(envLater.volumeTick>envFirst.volumeTick&&envLater.panningTick>envFirst.panningTick,
                    "Both instrument envelope playheads advance with audio frames");
            tap(Button::Start);require(!playback->instrumentPreviewStatus().active,"Instrument Start stops its envelope playheads");envMain(false);
            playback->previewNote(2,49);envAdvance();
            require(playback->instrumentPreviewStatus().active&&playback->instrumentPreviewStatus().instrument==2,
                    "The mismatch fixture auditions another instrument");envMain(false);

            tap(Button::A);require(mode==Mode::Envelope&&!envelopePan,"The volume envelope button opens the real editor");
            tap(Button::Start);envAdvance();const auto envEditorFirst=playback->instrumentPreviewStatus();
            envDraw();const int envEditorX=envBar(envEditorGraph,envVolume,envEditorFirst.volumeTick);
            screenshot(home/"envelope-editor-playhead.bmp");SDL_RenderPresent(renderer);
            envAdvance();const auto envEditorLater=playback->instrumentPreviewStatus();envDraw();
            require(envBar(envEditorGraph,envVolume,envEditorLater.volumeTick)>envEditorX&&envelopePoint==0,
                    "Editor playback advances independently of the selected edit point");
            selectChord(Button::Right);require(envelopePan,"The envelope editor switches to panning");
            envDraw();envBar(envEditorGraph,envPan,playback->instrumentPreviewStatus().panningTick);
            tap(Button::Start);envDraw();envBar(envEditorGraph,envPan,-1);
            tap(Button::Start);envAdvance();tap(Button::B);
            require(mode==Mode::Normal&&!playback->instrumentPreviewStatus().active,"Closing the envelope editor stops its audition and playheads");
            envMain(false);

            for(int kind=0;kind<3;++kind){
                if(kind==0)playback->previewNote(2,49);
                else if(kind==1)playback->previewSample(1,0,49);
                else playback->previewRow(0,0);
                envAdvance();require(playback->previewPlaying(),"The unrelated preview fixture is actively playing");
                envMain(false);mode=Mode::Envelope;envelopePan=false;envDraw();envBar(envEditorGraph,envVolume,-1);
                mode=Mode::Normal;playback->stopPreview();
            }
            require(io::encodeXm(doc)==envSource&&!dirty&&undoStack.empty()&&redoStack.empty()&&undoBytes==0&&
                    documentRevision==envRevision&&recoverySerial==envRecovery,
                    "Envelope playhead rendering, navigation and audition leave XM, Undo and recovery unchanged");

            doc.instruments[0].volumeEnvelope.flags=doc.instruments[0].panningEnvelope.flags=0;
            playback->load(doc);tap(Button::Start);envAdvance();
            require(playback->instrumentPreviewStatus().active,"Disabled envelopes still allow the instrument to sound");
            envMain(false);mode=Mode::Envelope;envelopePan=false;envDraw();envBar(envEditorGraph,envVolume,-1);
            mode=Mode::Normal;playback->stopPreview();
            doc.instruments[0].volumeEnvelope=envVolume;doc.instruments[0].panningEnvelope=envPan;
            require(io::encodeXm(doc)==envSource&&!dirty&&undoStack.empty()&&redoStack.empty()&&
                    documentRevision==envRevision&&recoverySerial==envRecovery,
                    "Rendering disabled envelopes does not edit the document or history");

            playback->stop();playback->stopPreview();doc=envSavedDocument;page=envSavedPage;mode=Mode::Normal;palette=envSavedPalette;
            instrument=envSavedCursor[0];sample=envSavedCursor[1];instrumentField=envSavedCursor[2];keymapNote=envSavedCursor[3];
            selectedPattern=envSavedCursor[4];order=envSavedCursor[5];row=envSavedCursor[6];channel=envSavedCursor[7];field=envSavedCursor[8];
            envelopeField=envSavedCursor[9];envelopePoint=envSavedCursor[10];envelopeButtonColumn=envSavedCursor[11];
            selectionStart=envSavedCursor[12];selectionEnd=envSavedCursor[13];sampleField=envSavedCursor[14];
            dirty=envSavedDirty;followPlayback=envSavedFollow;instrumentMapView=envSavedMap;solo=envSavedSolo;
            documentRevision=envSavedRevision;recoverySerial=envSavedRecovery;
            undoStack=std::move(envSavedUndo);redoStack=std::move(envSavedRedo);undoBytes=envSavedUndoBytes;
            held.fill(false);noticeUntil=0;waveformInstrument=-1;playback->load(doc);
            if(solo>=0)playback->setSolo(solo);else playback->setMuteMask(envSavedMutes);
        }
