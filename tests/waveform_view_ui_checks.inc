        {
            const auto waveViewDocument=doc;
            const auto waveViewPalette=palette;
            const auto waveViewPage=page;
            const auto waveViewCursor=std::array<int,5>{{instrument,sample,sampleField,selectionStart,selectionEnd}};
            const bool waveViewZoom=waveformZoom,waveViewFollow=followPlayback;
            const auto waveViewDirty=dirty;
            const auto waveViewUndo=undoStack.size(),waveViewRedo=redoStack.size();
            const auto waveViewRevision=documentRevision,waveViewRecovery=recoverySerial;
            const auto waveViewSettings=io::readFile(home/"editor-settings.txt");
            playback->stop();playback->stopPreview();held.fill(false);followPlayback=false;
            palette=defaultPalette;themeEditing=false;mode=Mode::Normal;page=Page::Sample;
            doc=makeDemoDocument();instrument=1;sample=sampleField=0;
            selectionStart=selectionEnd=0;waveformZoom=false;noticeUntil=0;
            int waveViewWidth=0,waveViewHeight=0;SDL_GetRendererOutputSize(renderer,&waveViewWidth,&waveViewHeight);
            const bool waveNative=waveViewWidth==W&&waveViewHeight==H;
            const auto waveInk=(std::uint32_t(cyan.r)<<16)|(std::uint32_t(cyan.g)<<8)|cyan.b;
            const auto selectedWaveInk=(std::uint32_t(ink.r)<<16)|(std::uint32_t(ink.g)<<8)|ink.b;
            auto readWavePixels=[&](SDL_Rect bounds){
                std::vector<std::uint32_t> pixels(std::size_t(bounds.w)*std::size_t(bounds.h));
                require(SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,pixels.data(),bounds.w*int(sizeof(std::uint32_t)))==0,
                        "waveform pixels are readable from the production renderer");
                return pixels;
            };
            const SDL_Rect sampleGraph{30,103,965,285};
            auto sampleInkAt=[&](const auto& pixels,int x,int y,int tolerance=0,bool selected=false){
                for(int offset=-tolerance;offset<=tolerance;++offset) {
                    const int yy=y+offset-sampleGraph.y;
                    if(x>=sampleGraph.x&&x<sampleGraph.x+sampleGraph.w&&yy>=0&&yy<sampleGraph.h&&
                       (pixels[std::size_t(yy)*sampleGraph.w+std::size_t(x-sampleGraph.x)]&0xffffffu)==
                           (selected?selectedWaveInk:waveInk))return true;
                }
                return false;
            };
            auto setViewedPcm=[&](std::vector<std::int16_t> pcm){
                auto& target=*currentSample();target=Sample{};target.sixteenBit=true;target.pcm=std::move(pcm);
                selectionStart=selectionEnd=0;waveformZoom=false;waveformInstrument=-1;
            };
            setViewedPcm({-16384,16384});const auto rampPcm=currentSample()->pcm;draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                for(int x=30;x<=512;x+=17) {
                    const int y=311-int(std::lround(double(x-30)*132.0/482.0));
                    require(sampleInkAt(pixels,x,y,1),"short PCM displays a connected ramp between its actual sample times");
                }
                require(sampleInkAt(pixels,753,179)&&!sampleInkAt(pixels,753,245),
                        "the last PCM frame holds to the end boundary instead of drawing a wrap transition");
            }
            require(currentSample()->pcm==rampPcm,"drawing a connected sample waveform never interpolates into stored PCM");
            setViewedPcm({8192});draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                require(sampleInkAt(pixels,30,212)&&sampleInkAt(pixels,512,212)&&sampleInkAt(pixels,993,212),
                        "a one-frame sample remains visible across the entire sample view");
            }
            setViewedPcm({-32768,-16384,16384,32767});selectionStart=1;selectionEnd=3;
            const auto zoomPcm=currentSample()->pcm;draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                require(sampleInkAt(pixels,271,311,1,true)&&sampleInkAt(pixels,391,245,1,true)&&
                        sampleInkAt(pixels,150,344,1)&&sampleInkAt(pixels,753,114,1),
                        "Connected PCM uses text color only inside the selected columns, retaining accent outside");
            }
            screenshot(home/"wave-sample-selection-connected.bmp");SDL_RenderPresent(renderer);
            waveformZoom=true;draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                require(sampleInkAt(pixels,271,245,1,true)&&sampleInkAt(pixels,753,179,0,true),
                        "Selection zoom highlights its connected frames and held final frame in text color");
            }
            require(currentSample()->pcm==zoomPcm,"selection zoom preserves PCM outside and inside the view");
            setViewedPcm(std::vector<std::int16_t>(1928,0));currentSample()->pcm[1001]=32767;draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                require(sampleInkAt(pixels,530,114)&&sampleInkAt(pixels,530,244)&&!sampleInkAt(pixels,529,114),
                        "the long-waveform min/max overview retains a single-frame spike without spreading it into adjacent bins");
            }
            const auto spikePcm=currentSample()->pcm;selectionStart=1000;selectionEnd=1002;draw();
            if(waveNative) {
                const auto pixels=readWavePixels(sampleGraph);
                require(sampleInkAt(pixels,530,114,0,true)&&sampleInkAt(pixels,530,244,0,true)&&
                        sampleInkAt(pixels,529,245)&&sampleInkAt(pixels,531,245),
                        "Min/max PCM columns brighten only within the selection and retain outside waveform color");
            }
            require(currentSample()->pcm==spikePcm,"Selection waveform highlighting never modifies stored PCM");

            setViewedPcm(std::vector<std::int16_t>(64,0));
            edit::fillWavePreset(*currentSample(),{0,64},edit::WavePreset::Sine,16384);
            const auto drawnSource=io::encodeXm(doc);draw();screenshot(home/"wave-sample-connected.bmp");SDL_RenderPresent(renderer);
            openWaveDraw();tap(Button::Down);chord({Button::Right});
            require(waveDrawResolution==2&&waveDrawPointCount()==32,"the controller selects 32 drawing points without changing sample length");
            const auto drawPcm=drawingSample->pcm;
            require(waveDrawPointFrame(0)==0&&waveDrawPointFrame(15)==30&&waveDrawPointFrame(16)==33&&waveDrawPointFrame(31)==63,
                    "equal visual point spacing retains the original rounded PCM anchors, including the wider central frame interval");
            for(int point=0;point<15;++point)tap(Button::Right);
            require(drawFrame==30,"controller navigation selects the central anchor without changing its PCM frame");
            const auto equalPlot=waveDrawPlot();
            require(equalPlot.step==26,"32 drawing points use equal 26-pixel intervals at native logical size");
            for(int point=1;point<32;++point)
                require(waveDrawFrameX(waveDrawPointFrame(point),equalPlot)-waveDrawFrameX(waveDrawPointFrame(point-1),equalPlot)==26,
                        "every pair of adjacent draw anchors has equal visual spacing");
            noticeUntil=0;draw();screenshot(home/"wave-draw-32-equal.bmp");
            if(waveNative) {
                const SDL_Rect graph{100,168,824,224};const auto pixels=readWavePixels(graph);
                for(int point=0;point<32;++point) {
                    if(point==15)continue; // The current point is amber, not cyan.
                    const int frame=waveDrawPointFrame(point),x=equalPlot.firstX+point*26;
                    const int y=280-int(drawPcm[std::size_t(frame)])*102/32768;
                    require((pixels[std::size_t(y-graph.y)*graph.w+std::size_t(x-graph.x)]&0xffffffu)==waveInk,
                            "the production renderer draws each unselected marker on the equally spaced point grid");
                }
            }
            SDL_RenderPresent(renderer);
            require(drawingSample->pcm==drawPcm&&io::encodeXm(doc)==drawnSource,
                    "drawing-point resolution, coordinates and rendering change neither draft PCM nor song PCM");

            // Uneven PCM lengths and nonzero selections exercise the mapping
            // independently of the visually convenient 64-frame example.
            for(int length:{1,33,65})for(int first:{0,7}) {
                drawingSample->pcm.assign(std::size_t(first+length+3),1234);
                waveDrawFirst=first;waveDrawLast=first+length;drawFrame=first;
                const auto unchanged=drawingSample->pcm;
                for(int resolution=0;resolution<4;++resolution) {
                    waveDrawResolution=resolution;const auto plot=waveDrawPlot();
                    const int points=std::min(length,8<<resolution);
                    require(waveDrawPointCount()==points&&waveDrawPointFrame(0)==first&&
                            waveDrawPointFrame(points-1)==first+length-1,
                            "8/16/32/64-point modes cover the complete selected PCM range including odd lengths");
                    for(int point=0;point<points;++point) {
                        const int x=waveDrawFrameX(waveDrawPointFrame(point),plot);
                        require(x==plot.firstX+point*plot.step&&x-4>=100&&x+4<=923,
                                "all drawing-point markers have equal spacing and fit fully inside the plot");
                    }
                    int previous=waveDrawFrameX(first,plot);
                    for(int frame=first+1;frame<first+length;++frame) {
                        const int x=waveDrawFrameX(frame,plot);
                        require(x>=previous&&x<=923,"intermediate PCM follows a monotonic point-axis mapping");previous=x;
                    }
                }
                waveDrawResolution=4;
                for(int cursor:{first,first+length/2,first+length-1}) {
                    drawFrame=cursor;const auto plot=waveDrawPlot();
                    require(plot.first>=first&&plot.last<=first+length&&plot.last-plot.first<=64&&
                            cursor>=plot.first&&cursor<plot.last,"FRAME view keeps the cursor inside a bounded 64-frame time window");
                    for(int frame=plot.first;frame<plot.last;++frame) {
                        const int x=waveDrawFrameX(frame,plot);
                        require(x==plot.firstX+(frame-plot.first)*plot.step&&x-4>=100&&x+4<=923,
                                "FRAME mode gives every actual PCM frame the same interval and keeps markers inside the plot");
                    }
                }
                draw();require(drawingSample->pcm==unchanged,"plotting odd or offset ranges never rewrites draft PCM");
            }
            require(io::encodeXm(doc)==drawnSource&&dirty==waveViewDirty&&undoStack.size()==waveViewUndo&&
                    redoStack.size()==waveViewRedo&&documentRevision==waveViewRevision&&recoverySerial==waveViewRecovery&&
                    io::readFile(home/"editor-settings.txt")==waveViewSettings,
                    "waveform viewing creates no song edits, history, recovery changes or saved preferences");
            drawingSample.reset();wavePresetOpen=false;wavePresetDraftPcm.clear();
            doc=waveViewDocument;palette=waveViewPalette;page=waveViewPage;mode=Mode::Normal;
            instrument=waveViewCursor[0];sample=waveViewCursor[1];sampleField=waveViewCursor[2];
            selectionStart=waveViewCursor[3];selectionEnd=waveViewCursor[4];waveformZoom=waveViewZoom;followPlayback=waveViewFollow;
            waveDrawFocus=waveDrawResolution=waveDrawFirst=waveDrawLast=drawFrame=0;
            waveformInstrument=-1;waveformRevision=0;noticeUntil=0;held.fill(false);playback->load(doc);
        }
