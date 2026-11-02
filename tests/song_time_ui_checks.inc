        {
            const auto previousSongTimeDocument=doc;
            playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
            doc=makeDemoDocument();doc.orders={0,0};doc.restart=0;
            page=Page::Song;mode=Mode::Normal;songField=songOrderColumn=0;songAppendOrder=false;
            selectedPattern=order=row=channel=field=0;instrument=1;sample=0;solo=-1;
            held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            clearSelection();selectionStart=selectionEnd=0;followPlayback=playingPattern=false;
            menuParents.clear();undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;
            normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);
            int timeOutputWidth=0,timeOutputHeight=0;
            SDL_GetRendererOutputSize(renderer,&timeOutputWidth,&timeOutputHeight);
            auto expectTransportText=[&](std::string_view expected,bool active,bool stopMarker=false){
                noticeUntil=0;draw();
                if(timeOutputWidth!=W||timeOutputHeight!=H)return;
                // This covers all transport text but starts after the fixed
                // BPM/LR-meter area. Compare real bitmap glyphs, not a second
                // copy of the production state-formatting implementation.
                const SDL_Rect area{768,6,236,32};
                std::vector<std::uint32_t> actual(std::size_t(area.w)*area.h),reference(actual.size());
                require(SDL_RenderReadPixels(renderer,&area,SDL_PIXELFORMAT_ARGB8888,actual.data(),area.w*int(sizeof(std::uint32_t)))==0,
                        "Transport header pixels are readable from the actual renderer");
                rect(area.x,area.y,area.w,area.h,panel);
                const int x=1000-ui::textWidth(expected);
                require(x>=area.x,"Song time fits beside the fixed BPM and stereo meters");
                if(stopMarker)rect(x-18,17,7,7,muted);
                txt(x,14,expected,active?green:muted);
                require(SDL_RenderReadPixels(renderer,&area,SDL_PIXELFORMAT_ARGB8888,reference.data(),area.w*int(sizeof(std::uint32_t)))==0&&actual==reference,
                        "Header shows exactly the requested transport status, including or omitting time");
            };
            tap(Button::Start);
            const int songTimeFrames=playback->audioSettings().sampleRate*5/2;
            std::vector<std::int16_t> songTimePcm(std::size_t(songTimeFrames)*2);
            playback->readFrames(songTimePcm.data(),std::size_t(songTimeFrames));
            require(playback->status().playing&&!playingPattern&&playback->status().elapsedSeconds>=2.0&&
                    playback->status().elapsedSeconds<3.0,"Song time advances from actual delivered playback frames");
            const std::array<const char*,4> timeScreens{{"song-time-song.bmp","song-time-pattern.bmp","song-time-instrument.bmp","song-time-sample.bmp"}};
            for(int screen=0;screen<4;++screen){
                if(screen)selectChord(Button::Right);
                require(page==static_cast<Page>(screen)&&playback->status().playing&&!playingPattern,
                        "Changing editor tabs leaves whole-song playback active");
                expectTransportText("PLAY SONG  00:02",true);
                draw();screenshot(home/timeScreens[std::size_t(screen)]);SDL_RenderPresent(renderer);
            }
            playback->stop();playingPattern=false;
            changePage(Page::Song);expectTransportText("STOP",false,true);
            changePage(Page::Sample);tap(Button::Start);
            require(playback->previewPlaying()&&!playback->status().playing,"Sample Start enters the independent listen mode");
            expectTransportText("LISTEN",true);tap(Button::Start);
            changePage(Page::Instrument);tap(Button::Start);expectTransportText("LISTEN",true);tap(Button::Start);
            changePage(Page::Pattern);clearSelection();tap(Button::Start);
            require(playback->status().playing&&playingPattern,"Pattern Start uses pattern transport");
            expectTransportText("PLAY PATTERN",true);tap(Button::Start);
            blockSelected=true;blockPattern=selectedPattern;blockRow=0;row=3;blockChannel=channel=0;blockField=field=0;
            tap(Button::Start);
            require(playback->status().loopFirstRow==0&&playback->status().loopEndRow==4,"Selected rows enter their own playback loop");
            expectTransportText("PLAY LOOP",true);tap(Button::Start);clearSelection();
            // A one-row transport can coexist with a stale song-mode flag;
            // the header must inspect its actual row-audition state too.
            playingPattern=false;playback->playPattern(0,0,true);
            require(playback->status().playing&&playback->status().rowAudition,"One-row transport identifies its audition mode");
            expectTransportText("PLAY ROW",true);playback->stop();

            playback->stopPreview();doc=previousSongTimeDocument;mode=Mode::Normal;page=Page::Pattern;
            selectedPattern=order=row=channel=field=0;instrument=1;sample=0;songField=0;
            clearSelection();selectionStart=selectionEnd=0;followPlayback=playingPattern=false;held.fill(false);
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;normalize();playback->load(doc);
        }
