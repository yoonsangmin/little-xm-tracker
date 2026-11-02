        {
            // A-down displays/auditions a new note immediately. A-first chords
            // replace that provisional edit without leaving extra history.
            finishGesture();playback->stop();audioSettings={};playback->configure(audioSettings);
            doc=makeDemoDocument();doc.channels=2;doc.patterns[0].rows=8;doc.patterns[0].cells.assign(16,{});
            doc.instruments[0].volumeEnvelope.flags=3;doc.instruments[0].volumeEnvelope.sustain=1;
            doc.patterns[0].at(0,0,2)={37,1,0x50,0x0f,200};
            doc.patterns[0].at(2,0,2)={49,1,0x30,0x0e,0xc2};
            doc.patterns[0].at(2,1,2)={56,1,0x40,0x08,0xe0};
            doc.patterns[0].at(3,0,2)={73,1,0x50,0,0};
            doc.patterns[0].at(4,1,2)={56,1,0x40,0,0};
            page=Page::Pattern;mode=Mode::Normal;selectedPattern=order=channel=field=0;row=2;instrument=1;sample=0;solo=-1;
            held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
            selectPending=selectUsed=channelModifier=false;channelChord=0;
            clearSelection();undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;followPlayback=false;
            lastCell={49,1,0x40,0,0};patternRowAudition=false;normalize();playback->load(doc);
            playback->setMuteMask(0);playback->setSolo(-1);saveEditorSettings();
            const auto auditionDocument=io::encodeXm(doc);const auto auditionRevision=documentRevision,auditionRecovery=recoverySerial;
            const auto auditionFile=currentFile;
            auto unchangedAuditionDocument=[&]{
                require(io::encodeXm(doc)==auditionDocument&&!dirty&&undoStack.empty()&&redoStack.empty()&&undoBytes==0&&
                        documentRevision==auditionRevision&&recoverySerial==auditionRecovery&&currentFile==auditionFile,
                        "audition and settings leave existing notes, undo, dirty and recovery state untouched");
            };
            auto pullAudition=[](Playback& engine,std::size_t frames){
                std::vector<std::int16_t> pcm(frames*2);engine.readFrames(pcm.data(),frames);return pcm;
            };
            auto auditionAudible=[](const std::vector<std::int16_t>& pcm){
                return std::any_of(pcm.begin(),pcm.end(),[](std::int16_t value){return value!=0;});
            };
            auto auditionSilent=[&]{
                // Allow a brief de-click ramp, but never an envelope release tail.
                pullAudition(*playback,512);
                require(!auditionAudible(pullAudition(*playback,2048)),"ending audition silences PCM without an envelope release tail");
            };
            auto openAuditionSetting=[&]{
                menu("SETTINGS");
                const auto found=std::find_if(actions.begin(),actions.end(),[](const Action& action){return action.label.rfind("NOTE AUDITION: ",0)==0;});
                require(found!=actions.end(),"Song Settings exposes the note audition preference");
                const int target=int(std::distance(actions.begin(),found));
                while(menuIndex<target)tap(Button::Down);
                while(menuIndex>target)tap(Button::Up);
            };

            Playback noteReference(audioSettings);noteReference.load(doc);noteReference.previewNote(1,49);
            buttonInput(Button::A,true);
            require(!playback->status().playing&&!playback->status().rowAudition,"Note Only starts on A-down outside song transport");
            auto heldNotePcm=pullAudition(*playback,3000);
            require(auditionAudible(heldNotePcm)&&heldNotePcm==pullAudition(noteReference,3000),
                    "A-down immediately plays only the cursor instrument note without row effects");
            heldNotePcm=pullAudition(*playback,18000);
            require(auditionAudible(heldNotePcm)&&heldNotePcm==pullAudition(noteReference,18000),
                    "holding A keeps sample and envelope clocks running beyond a row duration");
            unchangedAuditionDocument();buttonInput(Button::A,false);auditionSilent();unchangedAuditionDocument();

            openAuditionSetting();
            require(actions[menuIndex].label=="NOTE AUDITION: NOTE ONLY","note audition defaults to Note Only");
            tap(Button::A);require(!patternRowAudition,"A alone does not change an adjustable audition preference");
            chord({Button::Right});
            require(patternRowAudition&&actions[menuIndex].label=="NOTE AUDITION: WHOLE ROW",
                    "A+Right enables Whole Row audition from Song Settings");
            noticeUntil=0;draw();screenshot(home/"row-audition-settings.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);
            const auto savedAuditionSettings=io::readFile(home/"editor-settings.txt");
            patternRowAudition=false;loadEditorSettings();require(patternRowAudition,"Whole Row survives preference reload");
            const std::string badAuditionSettings="pattern_row_audition -1\npattern_row_audition 2\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(badAuditionSettings.begin(),badAuditionSettings.end()));
            loadEditorSettings();require(patternRowAudition,"invalid audition values preserve the current preference");
            patternRowAudition=false;loadEditorSettings();require(!patternRowAudition,"invalid audition values retain the Note Only default");
            io::writeFileAtomic(home/"editor-settings.txt",savedAuditionSettings);loadEditorSettings();unchangedAuditionDocument();

            changePage(Page::Pattern);row=2;channel=field=0;playback->load(doc);
            const auto beforeRowPreview=playback->status();
            Playback rowReference(audioSettings);rowReference.load(doc);rowReference.previewRow(0,2);buttonInput(Button::A,true);
            require(aPreviewActive&&!playback->status().playing&&!playback->status().rowAudition&&
                    playback->status().row==beforeRowPreview.row&&playback->status().bpm==beforeRowPreview.bpm&&
                    playback->status().elapsedSeconds==beforeRowPreview.elapsedSeconds&&!followPlayback,
                    "Whole Row starts on A-down without changing song transport or elapsed time");
            const auto initialRowPcm=pullAudition(*playback,3000);
            require(auditionAudible(initialRowPcm)&&initialRowPcm==pullAudition(rowReference,3000),
                    "held Whole Row contains both channels and their effects without a duplicate isolated note");
            const auto heldRowPcm=pullAudition(*playback,21000);
            require(heldRowPcm==pullAudition(rowReference,21000)&&
                    std::any_of(heldRowPcm.end()-2048,heldRowPcm.end(),[](std::int16_t value){return value!=0;})&&
                    aPreviewActive&&!playback->status().playing&&row==2&&
                    playback->status().elapsedSeconds==beforeRowPreview.elapsedSeconds,
                    "held Whole Row continues its voices after the row boundary without advancing or repeating row triggers");
            unchangedAuditionDocument();buttonInput(Button::A,false);auditionSilent();
            require(!aPreviewActive&&!playback->status().playing&&!playback->status().rowAudition,"A-up ends row audition");
            unchangedAuditionDocument();

            rowReference.stopPreview();rowReference.previewRow(0,2);buttonInput(Button::A,true);
            require(aPreviewActive&&!playback->status().playing&&playback->status().elapsedSeconds==beforeRowPreview.elapsedSeconds,
                    "a fresh A-down retriggers held row audition without changing song time");
            require(pullAudition(*playback,2400)==pullAudition(rowReference,2400),"held row retrigger starts from fresh voices");
            tap(Button::Right);
            require(cell().note==50&&aPreviewActive&&!playback->status().playing,
                    "A+pitch edits the note before immediately retriggering the whole row");
            rowReference.setCell(0,2,0,cell());rowReference.stopPreview();rowReference.previewRow(0,2);
            require(pullAudition(*playback,2400)==pullAudition(rowReference,2400),
                    "held pitch edit auditions the updated note and the other channel's original effects");
            buttonInput(Button::A,false);auditionSilent();require(undoStack.size()==1&&cell().note==50,"pitch-edit release commits one undo and does not insert again");
            playback->playSong();pullAudition(*playback,2400);
            require(playback->status().bpm==200,"earlier row establishes a different tempo for the preview reset check");
            playback->stop();const auto stoppedSong=playback->status();rowReference.load(doc);rowReference.previewRow(0,2);buttonInput(Button::A,true);
            require(aPreviewActive&&!playback->status().playing&&playback->status().bpm==stoppedSong.bpm&&
                    playback->status().row==stoppedSong.row&&playback->status().elapsedSeconds==stoppedSong.elapsedSeconds&&
                    pullAudition(*playback,3000)==pullAudition(rowReference,3000),
                    "held row uses fresh tempo for PCM while leaving the preceding song transport untouched");
            buttonInput(Button::A,false);auditionSilent();

            doc.patterns[0].at(2,0,2).note=97;playback->load(doc);rowReference.load(doc);
            rowReference.previewRow(0,2);buttonInput(Button::A,true);
            const auto offRowPcm=pullAudition(*playback,4000);
            require(auditionAudible(offRowPcm)&&offRowPcm==pullAudition(rowReference,4000),"Whole Row on OFF still auditions other channels");
            buttonInput(Button::A,false);auditionSilent();
            playback->load(doc);playback->setMuteMask(2);buttonInput(Button::A,true);
            require(!auditionAudible(pullAudition(*playback,4000)),"held Whole Row honors a muted sounding channel");
            buttonInput(Button::A,false);playback->setMuteMask(0);playback->setSolo(0);solo=0;buttonInput(Button::A,true);
            require(!auditionAudible(pullAudition(*playback,4000)),"held Whole Row honors solo on an OFF channel");
            buttonInput(Button::A,false);playback->setSolo(-1);playback->setMuteMask(0);solo=-1;

            for(bool wholeRow:{false,true}) {
                patternRowAudition=wholeRow;row=4;cell()={0,0,0x35,0x08,0x20};playback->load(doc);lastCell={53,1,0x40,0,0};
                const auto emptyBefore=io::encodeXm(doc);const auto emptyUndo=undoStack.size();
                auto prospectiveNote=cell();prospectiveNote.note=53;prospectiveNote.instrument=1;
                rowReference.load(doc);
                if(wholeRow)rowReference.previewRow(0,4,0,&prospectiveNote);else rowReference.previewNote(1,53);
                buttonInput(Button::A,true);
                require(cell().note==53&&cell().instrument==1&&io::encodeXm(doc)!=emptyBefore&&undoStack.size()==emptyUndo,
                        "A-down displays the new note immediately while retaining one pending undo transaction");
                const auto emptyPcm=pullAudition(*playback,3000);
                require(auditionAudible(emptyPcm)&&emptyPcm==pullAudition(rowReference,3000),
                        "empty-cell held audition previews the prospective note with preserved volume and row effects");
                buttonInput(Button::A,false);auditionSilent();
                require(cell().note==53&&cell().instrument==1&&undoStack.size()==emptyUndo+1,
                        "A-up commits the visible note once while ending its preview");
            }

            // A one-shot can finish while held; release must not restart it.
            patternRowAudition=false;row=2;cell().note=49;doc.instruments[0].samples[0].loopType=0;playback->load(doc);
            buttonInput(Button::A,true);pullAudition(*playback,6000);
            require(!auditionAudible(pullAudition(*playback,2000)),"a non-looping preview ends naturally while A remains held");
            buttonInput(Button::A,false);auditionSilent();doc.instruments[0].samples[0].loopType=1;playback->load(doc);

            for(bool wholeRow:{false,true}) {
                patternRowAudition=wholeRow;
                // Restart preserves the native mixer tick phase. Each option
                // needs the same fresh core as its independent PCM reference.
                playback->load(doc);
                Playback continuousReference(audioSettings);continuousReference.load(doc);
                playback->playPattern(0,2);continuousReference.playPattern(0,2);playingPattern=true;followPlayback=true;
                require(pullAudition(*playback,1000)==pullAudition(continuousReference,1000),"continuous preview guard starts from matching PCM");
                const auto continuing=playback->status();const auto beforeContinuous=io::encodeXm(doc);const auto continuousUndo=undoStack.size();
                buttonInput(Button::A,true);
                require(playback->status().playing&&!playback->status().rowAudition&&
                        playback->status().elapsedSeconds==continuing.elapsedSeconds&&!followPlayback&&
                        io::encodeXm(doc)==beforeContinuous&&undoStack.size()==continuousUndo,
                        "A-down during continuous playback preserves transport and notes while pausing follow");
                require(pullAudition(*playback,1000)==pullAudition(continuousReference,1000),"held A adds no voice during continuous playback");
                buttonInput(Button::A,false);
                require(playback->status().playing&&pullAudition(*playback,1000)==pullAudition(continuousReference,1000),
                        "A-up preserves continuous transport and its existing voices");
                buttonInput(Button::A,true);tap(Button::Right);continuousReference.setCell(0,2,0,cell());
                require(playback->status().playing&&!playback->status().rowAudition&&
                        pullAudition(*playback,1000)==pullAudition(continuousReference,1000),
                        "held pitch editing changes pattern data without adding an audition during playback");
                buttonInput(Button::A,false);
                require(playback->status().playing&&pullAudition(*playback,1000)==pullAudition(continuousReference,1000),
                        "pitch-edit A-up does not stop continuous playback");
                buttonInput(Button::A,true);tap(Button::Start);buttonInput(Button::A,false);auditionSilent();
                require(!playback->status().playing,"A-held Start stops continuous playback without release retrigger");
                buttonInput(Button::A,true);tap(Button::Start);
                require(playback->status().playing&&!playback->status().rowAudition,
                        "Start during either held audition starts ordinary pattern playback");
                buttonInput(Button::A,false);require(playback->status().playing,"A-up after Start leaves the new song transport playing");
                tap(Button::Start);auditionSilent();
            }

            for(bool wholeRow:{false,true}) {
                patternRowAudition=wholeRow;row=5;lastCell={53,1,0x40,0,0};
                const auto interruptedDoc=io::encodeXm(doc);const auto interruptedUndo=undoStack.size();
                buttonInput(Button::A,true);SDL_Event focusLost{};focusLost.type=SDL_WINDOWEVENT;focusLost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;event(focusLost);
                buttonInput(Button::A,false);auditionSilent();
                require(io::encodeXm(doc)==interruptedDoc&&undoStack.size()==interruptedUndo&&!held[std::size_t(Button::A)],
                        "focus loss ends held preview and cancels the pending empty-note insertion");
                buttonInput(Button::A,true);changePage(Page::Instrument);buttonInput(Button::A,false);auditionSilent();
                require(page==Page::Instrument&&mode==Mode::Normal&&io::encodeXm(doc)==interruptedDoc&&undoStack.size()==interruptedUndo,
                        "page change ends preview without inserting a note or activating the destination field");
                changePage(Page::Pattern);row=5;channel=field=0;
                buttonInput(Button::A,true);tap(Button::Start);buttonInput(Button::A,false);
                require(playback->status().playing&&!aPreviewActive&&io::encodeXm(doc)==interruptedDoc&&undoStack.size()==interruptedUndo,
                        "Start from an empty held note begins playback without committing the pending note on release");
                tap(Button::Start);auditionSilent();
            }

            for(bool wholeRow:{false,true}) {
                patternRowAudition=wholeRow;row=2;cell()={49,1,0x30,0x0e,0xc2};playback->load(doc);copyMask={};
                const auto cutUndo=undoStack.size();buttonInput(Button::A,true);
                require(auditionAudible(pullAudition(*playback,2000)),"cut chord starts with an audible held preview");
                buttonInput(Button::B,true);auditionSilent();
                require(!aPreviewActive&&cell().note==0,"A+B stops held audition as soon as the cut chord is recognized");
                buttonInput(Button::B,false);buttonInput(Button::A,false);auditionSilent();
                require(cell().note==0&&undoStack.size()==cutUndo+1,"cut-chord release never reinserts or retriggers the removed note");
            }

            // Instrument, sample and envelope listening uses a Start toggle.
            // Pattern note entry keeps its independent A-held lifecycle above.
            patternRowAudition=false;keymapNote=49;instrument=1;sample=0;
            for(int auditionView=0;auditionView<3;++auditionView) {
                if(auditionView==0){changePage(Page::Instrument);instrumentField=0;}
                else if(auditionView==1){changePage(Page::Sample);sampleField=0;selectionStart=selectionEnd=0;pcmSelecting=false;}
                else{menu("VOLUME ENVELOPE");envelopeField=0;}
                const auto beforeView=io::encodeXm(doc);const auto viewUndo=undoStack.size();
                tap(Button::A);auditionSilent();
                tap(Button::Start);
                require(!playback->status().playing&&!aPreviewActive&&auditionAudible(pullAudition(*playback,3000)),
                        "Start auditions instrument, sample and envelope independently of pattern transport and A-held preview");
                buttonInput(Button::A,true);
                require(auditionAudible(pullAudition(*playback,3000)),"Pressing A while listening does not stop a Start audition");
                buttonInput(Button::A,false);
                require(auditionAudible(pullAudition(*playback,3000)),"Releasing A leaves the Start audition playing");
                tap(Button::Start);auditionSilent();
                playback->playSong(0,0);pullAudition(*playback,1000);
                tap(Button::Start);
                require(!playback->status().playing,"Start in an audition view stops an active song first");
                auditionSilent();tap(Button::Start);
                require(!playback->status().playing&&auditionAudible(pullAudition(*playback,3000)),
                        "A fresh Start after stopping transport begins the current audition");
                if(auditionView==2){tap(Button::B);require(mode==Mode::Normal,"B closes the envelope audition context");}
                else changePage(Page::Pattern);
                auditionSilent();
                require(io::encodeXm(doc)==beforeView&&undoStack.size()==viewUndo,
                        "Start audition, A release and context cancellation leave document and history unchanged");
            }

            for(Page auditionPage:{Page::Instrument,Page::Sample}) {
                changePage(auditionPage);instrumentField=sampleField=0;selectionStart=selectionEnd=0;pcmSelecting=false;
                tap(Button::Start);require(auditionAudible(pullAudition(*playback,1000)),"Main audition starts before the song shortcut");
                buttonInput(Button::Select,true);tap(Button::Start);buttonInput(Button::Select,false);
                require(mode==Mode::Normal&&playback->status().playing&&playback->status().order==0&&playback->status().row==0&&!playingPattern,
                        "Select+Start from Instrument or Sample replaces audition with the whole song from its beginning");
                tap(Button::Start);auditionSilent();
            }

            changePage(Page::Sample);sampleField=0;selectionStart=selectionEnd=0;pcmSelecting=false;
            const auto originalAuditionSample=doc.instruments[0].samples[0];
            auto& longerAuditionSample=doc.instruments[0].samples[0];
            longerAuditionSample.pcm.resize(1024);
            for(std::size_t n=0;n<longerAuditionSample.pcm.size();++n)
                longerAuditionSample.pcm[n]=originalAuditionSample.pcm[n%originalAuditionSample.pcm.size()];
            longerAuditionSample.loopType=0;longerAuditionSample.loopLength=1024;playback->load(doc);
            tap(Button::Start);
            require(auditionAudible(pullAudition(*playback,256)),"Start begins a one-shot sample without forcing a loop");
            pullAudition(*playback,6000);auditionSilent();
            require(!playback->samplePreviewStatus().active,"One-shot sample preview reaches its natural end");
            tap(Button::Start);
            require(auditionAudible(pullAudition(*playback,256)),"One Start retriggers a naturally finished one-shot instead of only clearing stale toggle state");
            tap(Button::Start);auditionSilent();
            doc.instruments[0].samples[0].loopType=1;playback->load(doc);
            selectionStart=128;selectionEnd=896;tap(Button::Start);pullAudition(*playback,128);
            const auto selectedAudition=playback->samplePreviewStatus();
            require(selectedAudition.active&&selectedAudition.instrument==1&&selectedAudition.sample==0&&
                    selectedAudition.frame>=128&&selectedAudition.frame<896,
                    "Start in Sample auditions the selected PCM range without a separate Play button");
            tap(Button::Start);auditionSilent();selectionStart=selectionEnd=0;
            tap(Button::Start);pullAudition(*playback,3000);
            const auto beforeVolumeUndo=undoStack.size();const int previousVolume=currentSample()->volume;
            chord({Button::Left});
            require(currentSample()->volume==previousVolume-1&&undoStack.size()==beforeVolumeUndo+1&&
                    playback->samplePreviewStatus().active&&auditionAudible(pullAudition(*playback,1000)),
                    "Committing a sample volume edit retriggers the updated Start audition and keeps listening after A release");
            tap(Button::Start);auditionSilent();
            tap(Button::Start);require(auditionAudible(pullAudition(*playback,1000)),"Sample audition is active before focus loss");
            SDL_Event auditionFocusLost{};auditionFocusLost.type=SDL_WINDOWEVENT;auditionFocusLost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
            event(auditionFocusLost);auditionSilent();
            doc.instruments[0].samples[0]=originalAuditionSample;playback->load(doc);

            for(Page target:{Page::Pattern,Page::Song}) {
                changePage(target);clearSelection();tap(Button::Select);
                require(mode==Mode::Menu&&std::none_of(actions.begin(),actions.end(),[](const Action& action){
                    return action.label=="PLAYBACK"||action.label=="PLAY CURRENT ROW"||action.label=="CLEAR MUTES / SOLO";
                }),"removed row/clear actions leave no empty Playback group");
                tap(Button::B);
            }
            openAuditionSetting();if(patternRowAudition)chord({Button::Left});
            tap(Button::B);saveEditorSettings();patternRowAudition=true;loadEditorSettings();
            require(!patternRowAudition,"Note Only remains an explicit saved preference after held-preview tests");
            playback->stop();playback->stopPreview();doc=makeDemoDocument();page=Page::Pattern;mode=Mode::Normal;
            order=row=selectedPattern=channel=field=0;solo=-1;clearSelection();held.fill(false);
            aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;selectPending=selectUsed=channelModifier=false;channelChord=0;
            undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;
            normalize();playback->load(doc);playback->setMuteMask(0);playback->setSolo(-1);followPlayback=false;
        }
