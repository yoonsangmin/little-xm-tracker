        {
            const auto previousTrimDocument=doc;
            const auto previousTrimOptions=trimOptions;
            cancelPendingNote();gestureBefore.reset();playback->stop();playback->stopPreview();
            doc=makeDemoDocument();doc.channels=8;doc.orders={2,0,2};doc.restart=2;
            doc.patterns.resize(4);
            for(auto& p:doc.patterns){p.rows=32;p.cells.assign(std::size_t(p.rows)*doc.channels,{});}
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x40,0,0};
            doc.patterns[1].at(0,0,doc.channels)={55,2,0x30,0,0};
            doc.patterns[2].at(0,0,doc.channels)={61,3,0x38,4,0x47};
            doc.patterns[2].at(2,3,doc.channels)={0,0,0,0x0a,1};
            doc.patterns[3].at(0,7,doc.channels)={67,4,0x28,0,0};
            doc.instruments.resize(4,doc.instruments[0]);
            for(std::size_t id=0;id<doc.instruments.size();++id){
                auto& i=doc.instruments[id];i.name="CLEANUP "+number(id+1);i.noteToSample.fill(2);
                i.samples.resize(4,i.samples[0]);
                for(auto& s:i.samples){
                    s.sixteenBit=true;s.loopType=1;s.loopStart=4;s.loopLength=8;s.pcm.resize(32);
                    for(std::size_t n=0;n<s.pcm.size();++n)s.pcm[n]=std::int16_t(int(n)*503-7999);
                }
            }
            page=Page::Song;mode=Mode::Normal;songField=songOrderColumn=0;songAppendOrder=false;
            selectedPattern=2;order=0;row=5;channel=7;field=0;instrument=3;sample=2;solo=-1;
            held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            clearSelection();selectionStart=selectionEnd=0;followPlayback=false;dirty=false;
            menuParents.clear();undoStack.clear();redoStack.clear();undoBytes=0;
            trimOptions={};trimPreview.reset();trimField=0;
            normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);
            lastCell=doc.patterns[2].at(0,0,doc.channels);clipboard=lastCell;orderClipboard=2;
            blockClipboard=edit::copyBlock(doc.patterns[2],doc.channels,{0,1,0,1});
            patternSlotClipboard=doc.patterns[2];patternSlotClipboardChannels=doc.channels;
            const auto beforeTrim=io::encodeXm(doc);
            const auto trimRevision=documentRevision,trimRecovery=recoverySerial;
            const auto trimFile=currentFile;
            const auto optionValues=[&]{return std::array<bool,6>{{trimOptions.unusedPatterns,trimOptions.unusedInstruments,
                trimOptions.unusedSamples,trimOptions.unusedChannels,trimOptions.sampleTails,trimOptions.convertTo8Bit}};};
            const auto focusTrim=[&](int target){
                while(trimField<target)tap(Button::Down);
                while(trimField>target)tap(Button::Up);
            };
            const auto reopenTrim=[&]{mode=Mode::Normal;menuParents.clear();menu("CLEAN UP SONG");
                require(mode==Mode::TrimSettings,"Song Project opens the unified cleanup settings");};
            const auto expectTrimSizes=[&]{
                require(trimPreview&&trimPreview->beforeBytes==beforeTrim.size()&&
                        trimPreview->afterBytes==io::encodeXm(trimPreview->document).size(),
                        "Cleanup estimates exact encoded XM sizes without changing the source");
                const auto& result=*trimPreview;
                const std::string sizes=" / XM: "+std::to_string(result.beforeBytes)+" B -> "+
                    std::to_string(result.afterBytes)+" B / SAVE: "+std::to_string(result.beforeBytes-result.afterBytes)+" B";
                require(confirmDetail.find(sizes)!=std::string::npos,
                        "Cleanup confirmation contains the exact original size, new size and bytes saved");
            };

            menu("PROJECT");
            require(mode==Mode::Menu&&menuTitle=="PROJECT","Song Project remains the cleanup entry point");
            for(const char* obsolete:{"CLEAN UP PATTERNS","CLEAN UP INSTRUMENTS","MINIMISE ALL INSTRUMENTS"})
                require(std::none_of(actions.begin(),actions.end(),[&](const Action& a){return a.label==obsolete;}),
                        "Project has no duplicate cleanup or whole-song minimise actions");
            const auto cleanupEntry=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="CLEAN UP SONG";});
            require(cleanupEntry!=actions.end(),"Project lists one Clean Up Song action");
            const int cleanupIndex=int(std::distance(actions.begin(),cleanupEntry));
            while(menuIndex<cleanupIndex)tap(Button::Down);
            while(menuIndex>cleanupIndex)tap(Button::Up);
            tap(Button::A);
            require(mode==Mode::TrimSettings&&optionValues()==std::array<bool,6>{{true,true,true,true,true,false}},
                    "Cleanup enables lossless housekeeping by default and leaves eight-bit conversion off");
            tap(Button::Up);tap(Button::Left);
            require(trimField==0,"Cleanup option navigation clamps at the first row");
            for(int n=0;n<6;++n){
                focusTrim(n);const auto unchanged=optionValues();tap(Button::A);
                require(optionValues()==unchanged,"Plain A never toggles a cleanup option");
                chord({Button::Left});require(!optionValues()[std::size_t(n)],"A+Left disables the focused cleanup option");
                chord({Button::Up});require(optionValues()[std::size_t(n)],"A+Up enables the focused cleanup option");
                chord({Button::Down});require(!optionValues()[std::size_t(n)],"A+Down disables the focused cleanup option");
                chord({Button::Right});require(optionValues()[std::size_t(n)],"A+Right enables the focused cleanup option");
            }
            focusTrim(6);const auto buttonOptions=optionValues();
            tap(Button::Right);tap(Button::Left);tap(Button::Down);
            require(trimField==6&&mode==Mode::TrimSettings&&!trimPreview&&optionValues()==buttonOptions,
                    "The single Clean Up button ignores horizontal navigation and clamps at the bottom");
            tap(Button::Up);require(trimField==5,"Up from Clean Up returns directly to the last option row");
            tap(Button::Down);require(trimField==6,"Down returns to the single Clean Up button");
            noticeUntil=0;draw();screenshot(home/"trim-song-options.bmp");SDL_RenderPresent(renderer);

            playback->playPattern(2,0);playingPattern=true;tap(Button::A);
            require(mode==Mode::Confirm&&confirmTitle=="CLEAN UP SONG?"&&trimPreview&&trimPreview->changed(),
                    "Clean Up calculates automatically and opens confirmation without a separate Calculate step");
            expectTrimSizes();
            require(io::encodeXm(doc)==beforeTrim&&undoStack.empty()&&!dirty&&documentRevision==trimRevision&&recoverySerial==trimRecovery&&
                    playback->status().playing&&currentFile==trimFile&&clipboard&&orderClipboard&&blockClipboard&&patternSlotClipboard,
                    "Preparing confirmation preserves the live song, transport, save target, clipboards and edit history");
            require(trimPreview->document.channels==4&&trimPreview->document.patterns.size()==2&&trimPreview->document.instruments.size()==2&&
                    trimPreview->document.instruments[1].samples.size()==1&&trimPreview->document.instruments[1].samples[0].pcm.size()==12&&
                    !trimPreview->document.instruments[1].samples[0].sixteenBit,
                    "The unified preview applies all six requested operations to the retained song");
            for(const char* affected:{"2 UNUSED PATTERNS","2 UNUSED INSTRUMENTS","6 UNUSED SAMPLES","4 UNUSED CHANNELS","2 SAMPLE TAILS","2 SAMPLES TO 8-BIT"})
                require(confirmDetail.find(affected)!=std::string::npos,"Cleanup confirmation describes each affected category and count");
            noticeUntil=0;draw();screenshot(home/"trim-song-confirm.bmp");SDL_RenderPresent(renderer);
            const auto compactTrimBytes=trimPreview->afterBytes;
            tap(Button::B);
            require(mode==Mode::TrimSettings&&io::encodeXm(doc)==beforeTrim&&undoStack.empty()&&!dirty&&recoverySerial==trimRecovery&&
                    playback->status().playing&&clipboard&&orderClipboard&&blockClipboard&&patternSlotClipboard,
                    "Declining cleanup returns to its options and preserves playback and document state");
            focusTrim(4);chord({Button::Left});require(!trimPreview,"Changing a cleanup option invalidates the canceled result");
            focusTrim(6);tap(Button::A);
            require(mode==Mode::Confirm&&trimPreview&&trimPreview->afterBytes>compactTrimBytes&&
                    confirmDetail.find("SAMPLE TAILS")==std::string::npos&&io::encodeXm(doc)==beforeTrim&&
                    undoStack.empty()&&!dirty&&playback->status().playing,
                    "Clean Up recalculates changed options and their confirmation while the original song keeps playing");
            expectTrimSizes();tap(Button::B);
            focusTrim(4);chord({Button::Right});require(!trimPreview,"Re-enabling tail removal also invalidates the canceled result");
            const auto retainedOptions=optionValues();tap(Button::B);
            require(mode==Mode::Menu&&optionValues()==retainedOptions&&!trimPreview&&playback->status().playing&&io::encodeXm(doc)==beforeTrim,
                    "B leaves cleanup for its menu, retaining session options but discarding the preview");
            reopenTrim();require(optionValues()==retainedOptions&&!trimPreview,"Reopening cleanup retains option choices without retaining stale estimates");
            focusTrim(6);tap(Button::A);require(mode==Mode::Confirm,"Cleanup still confirms after reopening");
            tap(Button::A);
            require(mode==Mode::Normal&&!playback->status().playing&&undoStack.size()==1&&undoStack.back().label=="CLEAN UP SONG"&&dirty,
                    "Accepted cleanup stops playback and records one named Undo transaction");
            require(doc.orders==std::vector<std::uint8_t>({1,0,1})&&doc.restart==2&&doc.channels==4&&
                    selectedPattern==1&&instrument==2&&sample==0&&channel==3&&row==5,
                    "Cleanup follows compacted pattern, instrument and sample IDs and clamps the channel cursor");
            require(!clipboard&&!orderClipboard&&!blockClipboard&&!patternSlotClipboard&&lastCell.instrument==2,
                    "Cleanup clears copied references that would refer to different compacted IDs");
            const auto afterTrim=io::encodeXm(doc);
            menu("UNDO");
            require(io::encodeXm(doc)==beforeTrim&&selectedPattern==2&&instrument==3&&sample==2&&channel==7&&row==5&&
                    message=="UNDO / CLEAN UP SONG","One Undo restores the entire original song and matching editor targets");
            menu("REDO");
            require(io::encodeXm(doc)==afterTrim&&selectedPattern==1&&instrument==2&&sample==0&&channel==3&&row==5&&
                    message=="REDO / CLEAN UP SONG","One Redo restores the complete cleanup and its compacted editor targets");
            const auto trimmedHistory=undoStack.size(),trimmedRecovery=recoverySerial;
            reopenTrim();focusTrim(6);tap(Button::A);
            require(mode!=Mode::Confirm&&undoStack.size()==trimmedHistory&&io::encodeXm(doc)==afterTrim&&recoverySerial==trimmedRecovery,
                    "Repeating cleanup on an already-clean song creates neither a warning nor an empty Undo transaction");

            playback->stop();playback->stopPreview();doc=previousTrimDocument;trimOptions=previousTrimOptions;trimPreview.reset();
            mode=Mode::Normal;page=Page::Song;selectedPattern=order=row=channel=field=0;instrument=1;sample=0;songField=0;
            menuParents.clear();confirmed={};clearSelection();selectionStart=selectionEnd=0;followPlayback=false;playingPattern=false;
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;normalize();playback->load(doc);
        }
