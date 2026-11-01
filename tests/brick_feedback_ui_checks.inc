        {
            // Real input regressions for provisional note entry and shared menus.
            auto resetFeedbackFixture=[&]{
                cancelPendingNote();gestureBefore.reset();playback->stop();playback->stopPreview();
                doc=makeDemoDocument();doc.name="Brick feedback";doc.orders={0};doc.restart=0;
                for(auto& value:doc.patterns[0].cells)value={};
                page=Page::Pattern;mode=Mode::Normal;selectedPattern=order=row=channel=field=0;
                instrument=1;sample=0;songField=songOrderColumn=0;songAppendOrder=false;solo=-1;
                instrumentField=sampleField=0;held.fill(false);aUsed=bUsed=true;
                aPreviewActive=aPreviewBlocked=modalBackHeld=cutChordUsed=channelModifier=false;
                selectPending=selectUsed=false;channelChord=0;followPlayback=playingPattern=false;
                clearSelection();selectionStart=selectionEnd=0;waveformZoom=false;
                clipboard.reset();blockClipboard.reset();orderClipboard.reset();orderBlockClipboard.clear();
                patternSlotClipboard.reset();patternSlotClipboardChannels=0;
                copyMask={};pasteMask={};copiedMask={};confirmed={};menuParents.clear();
                lastCell={53,1,0x40,4,0x47};patternRowAudition=false;
                currentFile.clear();sourceFingerprint.reset();dirty=false;
                undoStack.clear();redoStack.clear();undoBytes=0;
                normalize();playback->load(doc);playback->setSolo(-1);playback->setMuteMask(0);
            };
            resetFeedbackFixture();
            for(const auto& label:{std::string(120,'A'),std::string(u8"길이가아주긴파일이름을확인하는창입니다길이가아주긴파일이름을확인하는창입니다")}) {
                const auto lines=dialogLines(label,758);
                require(std::all_of(lines.begin(),lines.end(),[](const std::string& line){return text::isValidUtf8(line)&&ui::textWidth(line)<=758;}),
                        "long ASCII and Unicode filenames wrap inside modal bounds without splitting characters");
            }
            const auto beforeNote=io::encodeXm(doc);const auto beforeNoteSerial=recoverySerial;
            buttonInput(Button::A,true);
            require(cell().note==53&&cell().instrument==1&&pendingNoteBefore&&undoStack.empty()&&!dirty&&recoverySerial==beforeNoteSerial,
                    "A-down shows an empty-cell note immediately without committing history or recovery");
            buttonInput(Button::A,false);
            require(cell().note==53&&!pendingNoteBefore&&undoStack.size()==1&&dirty&&recoverySerial==beforeNoteSerial+1,
                    "releasing A commits the displayed note exactly once");
            undo();require(io::encodeXm(doc)==beforeNote,"one Undo removes the immediately displayed note");

            for(bool releaseAFirst:{false,true}) {
                resetFeedbackFixture();cell()={0,0,0x35,4,0x47};playback->load(doc);
                const auto beforeCut=io::encodeXm(doc);const auto cutSerial=recoverySerial;
                buttonInput(Button::A,true);buttonInput(Button::B,true);
                require(!pendingNoteBefore&&cell().note==0,"A-first cut discards the provisional note before reading its source");
                buttonInput(releaseAFirst?Button::A:Button::B,false);buttonInput(releaseAFirst?Button::B:Button::A,false);
                require(io::encodeXm(doc)==beforeCut&&undoStack.empty()&&!dirty&&recoverySerial==cutSerial&&clipboard&&clipboard->note==0,
                        "cutting an empty note has no inserted-note history or clipboard residue in either release order");

                resetFeedbackFixture();const auto beforePaste=io::encodeXm(doc);
                const Cell pasted{65,1,0x39,4,0x47};clipboard=pasted;
                buttonInput(Button::A,true);buttonInput(Button::Select,true);
                require(!pendingNoteBefore&&same(cell(),pasted)&&undoStack.size()==1,
                        "A-first Select paste replaces the provisional note with one actual edit");
                buttonInput(releaseAFirst?Button::A:Button::Select,false);buttonInput(releaseAFirst?Button::Select:Button::A,false);
                require(mode==Mode::Normal&&same(cell(),pasted)&&undoStack.size()==1,
                        "releasing the paste chord neither opens Actions nor inserts a second note");
                undo();require(io::encodeXm(doc)==beforePaste,"one Undo reverses an A-first paste completely");
            }
            resetFeedbackFixture();const auto beforeDirection=io::encodeXm(doc);
            buttonInput(Button::A,true);tap(Button::Right);buttonInput(Button::A,false);
            require(cell().note==54&&undoStack.size()==1&&!pendingNoteBefore,
                    "A-first pitch adjustment uses the remembered note and creates only the adjusted note edit");
            undo();require(io::encodeXm(doc)==beforeDirection,"one Undo reverses an A-first pitch edit without leaving its provisional note");

            resetFeedbackFixture();
            auto loadedDuringHold=makeDemoDocument();loadedDuringHold.name="Loaded during hold";loadedDuringHold.orders={0};
            loadedDuringHold.patterns[0].at(0,0,loadedDuringHold.channels)={72,1,0x30,0,0};
            const auto loadedPath=home/"brick-feedback-load.xm";
            const auto loadedBytes=io::encodeXm(loadedDuringHold);io::writeFileAtomic(loadedPath,loadedBytes);
            buttonInput(Button::A,true);
            openDocument(loadedPath);
            require(!pendingNoteBefore&&io::encodeXm(doc)==loadedBytes&&currentFile==std::filesystem::absolute(loadedPath)&&!dirty&&undoStack.empty(),
                    "loading XM while A is held clears the pending snapshot and loads a clean new document");
            buttonInput(Button::A,false);
            require(io::encodeXm(doc)==loadedBytes&&currentFile==std::filesystem::absolute(loadedPath)&&!dirty&&undoStack.empty(),
                    "A release after loading cannot resurrect the previous song or edit the newly loaded song");

            resetFeedbackFixture();buttonInput(Button::A,true);
            destructive([this]{newDocument();});buttonInput(Button::A,false);
            require(!pendingNoteBefore&&doc.name=="UNTITLED"&&currentFile.empty()&&undoStack.empty()&&
                    std::all_of(doc.patterns[0].cells.begin(),doc.patterns[0].cells.end(),[](const Cell& value){return !value.note&&!value.instrument;}),
                    "New Song consumes an outstanding provisional input and starts with clean note state");

            resetFeedbackFixture();const auto beforeSettings=io::encodeXm(doc);
            const char* actionTitles[]={"SONG ACTIONS","PATTERN ACTIONS","INSTRUMENT ACTIONS","SAMPLE ACTIONS"};
            for(int screen=0;screen<4;++screen) {
                changePage(static_cast<Page>(screen));menu("SETTINGS");
                require(mode==Mode::Menu&&menuTitle=="SETTINGS"&&menuParents.size()==1&&
                        std::none_of(actions.begin(),actions.end(),[](const Action& action){return action.label=="CONTROLS";}),
                        "Settings is reachable from every screen and contains no duplicate Controls action");
                tap(Button::B);
                require(mode==Mode::Menu&&menuTitle==actionTitles[screen]&&menuParents.empty()&&
                        std::count_if(actions.begin(),actions.end(),[](const Action& action){return action.label=="SETTINGS";})==1&&
                        std::none_of(actions.begin(),actions.end(),[](const Action& action){return action.label=="OPTIONS";})&&
                        std::count_if(actions.begin(),actions.end(),[](const Action& action){return action.label=="CONTROLS";})==1,
                        "B from Settings returns to the originating Actions menu with one Settings and one Controls entry");
                tap(Button::B);require(mode==Mode::Normal&&page==static_cast<Page>(screen),"another B returns to the originating editor screen");
            }
            require(io::encodeXm(doc)==beforeSettings&&undoStack.empty()&&!dirty,"browsing shared settings leaves the song and its history untouched");

            // Compare the rendered shared header only at native logical size.
            int feedbackWidth=0,feedbackHeight=0;SDL_GetRendererOutputSize(renderer,&feedbackWidth,&feedbackHeight);
            if(feedbackWidth==W&&feedbackHeight==H) {
                playingPattern=true;transport=playback->status();(void)playback->takeOutputMeter();
                std::vector<std::uint32_t> expectedHeader;
                const SDL_Rect headerArea{496,0,W-496,44};
                for(int screen=0;screen<4;++screen) {
                    changePage(static_cast<Page>(screen));outputPeakDisplay={};outputClipUntil={};drawHeader();
                    std::vector<std::uint32_t> pixels(std::size_t(headerArea.w)*std::size_t(headerArea.h));
                    require(SDL_RenderReadPixels(renderer,&headerArea,SDL_PIXELFORMAT_ARGB8888,pixels.data(),headerArea.w*int(sizeof(std::uint32_t)))==0,
                            "shared-header pixels are readable from the actual renderer");
                    if(screen==0)expectedHeader=pixels;
                    else require(pixels==expectedHeader,"BPM, output meters and stopped transport retain the same pixels across editor tabs");
                }
            }

            // Final review images use production rendering and populated music.
            resetFeedbackFixture();doc=makeDemoDocument();doc.name="BRICK FIRST SONG";
            doc.orders={0,0,1,0};doc.patterns.push_back(doc.patterns[0]);doc.restart=0;
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x40,4,0x47};
            doc.patterns[1].at(0,0,doc.channels)={56,1,0x38,0,0};
            patternViewChannels=4;patternViewRows=32;rowNumbersHex=true;field=3;
            normalize();playback->load(doc);
            auto feedbackShot=[&](const char* filename){noticeUntil=0;draw();screenshot(home/filename);SDL_RenderPresent(renderer);};
            feedbackShot("brick-feedback-pattern.bmp");
            blockSelected=true;blockPattern=selectedPattern;blockRow=2;blockChannel=0;blockField=0;
            row=7;channel=1;field=4;
            feedbackShot("brick-feedback-selection.bmp");
            if(feedbackWidth==W&&feedbackHeight==H) {
                draw();
                const SDL_Rect gap{295,88+3*20+19,1,1};
                std::uint32_t pixel=0;
                require(SDL_RenderReadPixels(renderer,&gap,SDL_PIXELFORMAT_ARGB8888,&pixel,sizeof(pixel))==0&&
                        (pixel&0x00ffffffu)==0x0022414cu,
                        "selected range fills both row and channel gaps, not only the text backgrounds");
            }
#include "selection_bounds_ui_checks.inc"
            clearSelection();row=channel=0;field=3;
            changePage(Page::Song);songField=0;order=0;tap(Button::Start);
            std::vector<std::int16_t> monitorFrames(4096*2);playback->readFrames(monitorFrames.data(),4096);
            songMonitorValid=false;feedbackShot("brick-feedback-song.bmp");tap(Button::Start);
            menu("VOLUME ENVELOPE");
            require(mode==Mode::Envelope,"the final envelope review uses the actual instrument editor");
            feedbackShot("brick-feedback-envelope.bmp");tap(Button::B);
            doc.patterns.push_back(doc.patterns[0]);playback->load(doc);
            menu("CLEAN UP PATTERNS");
            require(mode==Mode::Confirm,"the final cleanup review uses the real confirmation dialog");
            feedbackShot("brick-feedback-confirm.bmp");tap(Button::B);
            openBrowser(FileAction::SaveXm);feedbackShot("brick-feedback-browser.bmp");tap(Button::B);
            changePage(Page::Sample);instrument=1;sample=0;selectionStart=selectionEnd=0;
            openWaveDraw();openWavePresets();prepareWavePreset(1);createWaveDraft();
            feedbackShot("brick-feedback-wave.bmp");tap(Button::B);
#include "dialog_layout_ui_checks.inc"
            changePage(Page::Pattern);row=channel=0;field=3;noticeUntil=0;
        }
