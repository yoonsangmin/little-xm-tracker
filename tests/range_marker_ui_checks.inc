        {
            const auto markerSavedDocument=doc;
            const auto markerSavedPalette=palette;
            const auto markerSavedPage=page;
            const auto markerSavedCursor=std::array<int,13>{{instrument,sample,sampleField,instrumentField,
                envelopeField,envelopePoint,keymapNote,selectionStart,selectionEnd,order,row,channel,field}};
            const bool markerSavedMap=instrumentMapView,markerSavedPan=envelopePan;
            const bool markerSavedZoom=waveformZoom,markerSavedFollow=followPlayback;
            const bool markerSavedDirty=dirty;
            const auto markerSavedRevision=documentRevision,markerSavedRecovery=recoverySerial;
            const auto markerSavedUndo=undoStack.size(),markerSavedRedo=redoStack.size();
            playback->stop();playback->stopPreview();held.fill(false);cancelPendingNote();
            doc=makeDemoDocument();palette=defaultPalette;mode=Mode::Normal;page=Page::Sample;
            instrument=1;sample=sampleField=0;instrumentMapView=false;waveformZoom=followPlayback=false;
            auto& markerSample=doc.instruments[0].samples[0];
            markerSample.name="LOOP AND SELECTION";markerSample.pcm.resize(1024);markerSample.sixteenBit=true;
            for(std::size_t n=0;n<markerSample.pcm.size();++n)
                markerSample.pcm[n]=std::int16_t(std::sin(double(n)*0.018)*18000);
            markerSample.loopType=1;markerSample.loopStart=128;markerSample.loopLength=512;
            selectionStart=256;selectionEnd=896;waveformInstrument=-1;
            int markerWidth=0,markerHeight=0;SDL_GetRendererOutputSize(renderer,&markerWidth,&markerHeight);
            const bool markerNative=markerWidth==W&&markerHeight==H;
            std::vector<std::uint32_t> markerPixels(std::size_t(W)*H);
            auto markerRgb=[](auto color){return (std::uint32_t(color.r)<<16)|(std::uint32_t(color.g)<<8)|color.b;};
            auto markerDraw=[&](const char* name=nullptr){
                const auto before=io::encodeXm(doc);noticeUntil=0;draw();
                require(io::encodeXm(doc)==before&&dirty==markerSavedDirty&&undoStack.size()==markerSavedUndo&&
                        redoStack.size()==markerSavedRedo&&documentRevision==markerSavedRevision&&recoverySerial==markerSavedRecovery,
                        "Range marker rendering and focus never edit XM, Undo or recovery state");
                if(markerNative)require(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,
                    markerPixels.data(),W*int(sizeof(std::uint32_t)))==0,"Range marker pixels are readable");
                if(name){screenshot(home/name);SDL_RenderPresent(renderer);}
            };
            auto markerHas=[&](int x,int y,auto color){
                if(!markerNative)return true;
                for(int n=0;n<12;++n)
                    if((markerPixels[std::size_t(y+n)*W+std::size_t(x)]&0xffffffu)==markerRgb(color))return true;
                return false;
            };
            auto markerAbsent=[&](int x,int y,auto color){return !markerNative||!markerHas(x,y,color);};
            auto markerAt=[&](int x,int y,auto color){return !markerNative||
                (markerPixels[std::size_t(y)*W+std::size_t(x)]&0xffffffu)==markerRgb(color);};
            auto markerSampleX=[](int frame){return 30+frame*964/1024;};
            markerDraw("range-markers-unfocused.bmp");
            require(markerHas(markerSampleX(128),111,dim)&&markerHas(markerSampleX(640),111,dim)&&
                    markerAbsent(markerSampleX(256),111,dim)&&markerAbsent(markerSampleX(896),111,dim),
                    "Inactive sample loops retain their bars while selection bars are hidden");
            require(markerAt(markerSampleX(128),96,dim)&&markerAt(markerSampleX(128),397,dim)&&
                    markerAt(500,96,sampleSelectionColor)&&markerAt(500,397,sampleSelectionColor)&&
                    markerAt(500,95,bg)&&markerAt(500,398,bg),
                    "Loop bars and selection fill reach both panel edges without spilling outside the panel");
            sampleField=6;markerDraw("range-markers-loop-focus.bmp");
            require(markerHas(markerSampleX(128),111,ink)&&markerHas(markerSampleX(640),111,amber)&&
                    markerAbsent(markerSampleX(256),111,dim),"Loop focus highlights its endpoints while selection bars stay hidden");
            require(markerAt(markerSampleX(640),96,amber)&&markerAt(markerSampleX(640),397,amber),
                    "Focused loop bars cover the same full panel height as inactive bars");
            sampleField=8;markerDraw("range-markers-selection-focus.bmp");
            require(markerHas(markerSampleX(256),111,ink)&&markerHas(markerSampleX(896),111,amber)&&
                    markerHas(markerSampleX(128),111,dim),"Selection focus leaves inactive loop boundaries visible");
            require(markerAt(markerSampleX(896),96,amber)&&markerAt(markerSampleX(896),397,amber),
                    "Focused selection bars align with the top and bottom of the selection background");
            if(markerNative)require((markerPixels[std::size_t(130)*W+500]&0xffffffu)==markerRgb(sampleSelectionColor),
                    "Selection background remains visible while its boundary focus changes");
            markerSample.loopType=0;sampleField=0;markerDraw();
            for(int markerTool:{12,14}) {
                sampleField=markerTool;markerDraw();
                require(markerHas(markerSampleX(256),111,ink)&&markerHas(markerSampleX(896),111,ink),
                        "DRAW and PROCESS retain both selection endpoint bars while their group is focused");
            }
            sampleField=0;markerDraw();
            require(markerAbsent(markerSampleX(128),111,dim),"An inactive disabled sample loop has no boundary marker");
            sampleField=5;markerDraw();
            require(markerHas(markerSampleX(128),111,amber),"Editing disabled sample loop endpoints previews their positions");
            markerSample.loopType=1;sampleField=0;selectionStart=128;selectionEnd=640;
            markerDraw("range-markers-overlap.bmp");
            sampleField=6;markerDraw();
            require(markerHas(markerSampleX(640),111,amber),"Focused loop end remains visible when it overlaps selection end");
            sampleField=8;markerDraw();
            require(markerHas(markerSampleX(640),111,amber),"Focused selection end remains visible when it overlaps loop end");

            auto markerShadeVisible=[&](int first,int last){
                if(!markerNative)return true;
                for(int x=first;x<=last;++x)
                    if((markerPixels[std::size_t(130)*W+std::size_t(x)]&0xffffffu)==markerRgb(sampleSelectionColor))return true;
                return false;
            };
            markerSample.pcm.assign(4096,0);markerSample.loopStart=1024;markerSample.loopLength=1024;
            selectionStart=1024;selectionEnd=1025;sampleField=0;waveformInstrument=-1;
            markerDraw("range-markers-narrow-overlap.bmp");
            require(markerShadeVisible(269,274),
                    "A nonempty subpixel selection keeps visible background next to an overlapping loop bar");
            require(markerAt(272,245,cyan),
                    "Extra background width for a narrow selection does not brighten unselected waveform columns");
            sampleField=8;markerDraw();
            require(markerShadeVisible(269,274),
                    "Focused coincident selection endpoints do not hide every selected background column");
            markerSample.loopStart=4095;markerSample.loopLength=1;selectionStart=4095;selectionEnd=4096;
            markerDraw("range-markers-narrow-right-edge.bmp");
            require(markerShadeVisible(989,993),"A narrow selection at sample end retains background inside the plot");
            if(markerNative)require(!markerShadeVisible(994,1003),"Minimum selection background never extends past the plot's right edge");
            markerSample.loopType=0;selectionStart=selectionEnd=2048;sampleField=0;markerDraw();
            require(markerAbsent(512,111,dim),"An unfocused insertion point has no selection caret");
            if(markerNative)require(!markerShadeVisible(30,993),"A collapsed empty range has no shaded selection");

            auto& markerOwner=doc.instruments[0];
            markerOwner.volumeEnvelope={};auto& markerVolume=markerOwner.volumeEnvelope;
            markerVolume.count=5;markerVolume.flags=5;markerVolume.loopStart=1;markerVolume.loopEnd=3;
            markerVolume.points[0]={0,40};markerVolume.points[1]={8,60};markerVolume.points[2]={24,20};
            markerVolume.points[3]={48,45};markerVolume.points[4]={64,0};
            markerOwner.panningEnvelope={};auto& markerPan=markerOwner.panningEnvelope;
            markerPan.count=4;markerPan.flags=5;markerPan.loopStart=1;markerPan.loopEnd=2;
            markerPan.points[0]={0,32};markerPan.points[1]={8,12};markerPan.points[2]={24,52};markerPan.points[3]={48,32};
            page=Page::Instrument;instrumentField=3;keymapNote=49;
            markerDraw("instrument-loop-markers.bmp");
            require(markerHas(369+8*297/64,492,dim)&&markerHas(369+48*297/64,492,dim)&&
                    markerHas(693+8*297/48,492,dim)&&markerHas(693+24*297/48,492,dim),
                    "Instrument volume and panning previews both display envelope loop boundaries");
            mode=Mode::Envelope;envelopePan=false;envelopeField=0;envelopePoint=2;
            markerDraw("envelope-loop-markers.bmp");
            const int markerEnvStart=85+8*522/64,markerEnvEnd=85+48*522/64;
            require(markerHas(markerEnvStart,151,dim)&&markerHas(markerEnvEnd,151,dim),
                    "Envelope loops stay visible while editing a point rather than a loop field");
            envelopeField=7;markerDraw("envelope-loop-start-focus.bmp");
            require(markerHas(markerEnvStart,151,amber)&&markerHas(markerEnvEnd,151,ink),
                    "Envelope loop start focus identifies the current and opposite endpoints");
            markerVolume.flags=4;envelopeField=0;markerDraw();
            require(markerHas(markerEnvStart,151,dim),"A stored enabled loop remains visible while the envelope itself is disabled");
            markerVolume.flags=1;markerDraw();
            require(markerAbsent(markerEnvStart,151,dim)&&markerAbsent(markerEnvEnd,151,dim),
                    "A disabled envelope loop is hidden outside loop editing");
            envelopeField=8;markerDraw();
            require(markerHas(markerEnvEnd,151,amber),"Editing an envelope loop end previews a disabled loop");

            doc=markerSavedDocument;palette=markerSavedPalette;page=markerSavedPage;mode=Mode::Normal;
            instrument=markerSavedCursor[0];sample=markerSavedCursor[1];sampleField=markerSavedCursor[2];
            instrumentField=markerSavedCursor[3];envelopeField=markerSavedCursor[4];envelopePoint=markerSavedCursor[5];
            keymapNote=markerSavedCursor[6];selectionStart=markerSavedCursor[7];selectionEnd=markerSavedCursor[8];
            order=markerSavedCursor[9];row=markerSavedCursor[10];channel=markerSavedCursor[11];field=markerSavedCursor[12];
            instrumentMapView=markerSavedMap;envelopePan=markerSavedPan;waveformZoom=markerSavedZoom;followPlayback=markerSavedFollow;
            waveformInstrument=-1;waveformRevision=0;noticeUntil=0;held.fill(false);playback->load(doc);
        }
