        {
            auto resetUndoLabels=[&]{
                cancelPendingNote();gestureBefore.reset();gestureLabel.clear();playback->stop();playback->stopPreview();
                doc=makeDemoDocument();doc.orders={0};doc.restart=0;page=Page::Pattern;mode=Mode::Normal;
                selectedPattern=order=row=channel=field=0;instrument=1;sample=0;
                instrumentField=sampleField=songField=songOrderColumn=0;songAppendOrder=false;
                held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
                cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
                clearSelection();selectionStart=selectionEnd=0;followPlayback=dirty=false;solo=-1;
                clipboard.reset();blockClipboard.reset();copyMask={};pasteMask={};copiedMask={};
                pendingAudio.reset();drawingSample.reset();confirmed={};menuParents.clear();
                undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            };
            auto expectUndoLabel=[&](const std::string& label,const std::vector<std::uint8_t>& original){
                menu("UNDO");
                require(message=="UNDO / "+label&&io::encodeXm(doc)==original,
                        "Undo restores the document and describes the original operation after changing tabs");
                menu("REDO");require(message=="REDO / "+label,"Redo retains the same operation description");
            };
            const char* defaults[]={"SONG EDIT","PATTERN EDIT","INSTRUMENT EDIT","SAMPLE EDIT"};
            for(int screen=0;screen<4;++screen) {
                resetUndoLabels();changePage(static_cast<Page>(screen));
                const auto original=io::encodeXm(doc);
                if(screen==0){songField=1;chord({Button::Right});}
                else if(screen==1)chord({Button::Right});
                else if(screen==2){instrumentField=3;chord({Button::Right});}
                else {sampleField=0;chord({Button::Left});}
                require(undoStack.size()==1&&undoStack.back().label==defaults[screen],
                        "ordinary edits capture the originating screen in their history description");
                changePage(static_cast<Page>((screen+1)%4));expectUndoLabel(defaults[screen],original);
            }

            resetUndoLabels();page=Page::Song;const auto beforeGesture=io::encodeXm(doc);
            beginGesture();++doc.bpm;
            page=Page::Instrument;beginGesture("DELETE");finishGesture();
            require(undoStack.size()==1&&undoStack.back().label=="SONG EDIT",
                    "a gesture keeps its first description even if the current view or later begin label changes");
            changePage(Page::Sample);expectUndoLabel("SONG EDIT",beforeGesture);
            undo();const auto retainedRedo=redoStack.size();const auto noopRecovery=recoverySerial;
            mutate([]{},"DELETE");editCell(cell(),"CUT");beginGesture("DRAW");finishGesture();
            require(undoStack.empty()&&redoStack.size()==retainedRedo&&recoverySerial==noopRecovery,
                    "no-op operations never create labeled history or discard a pending Redo");
            bool invalidEditRejected=false;
            try{mutate([&]{doc.speed=0;},"DRAW");}catch(const std::exception&){invalidEditRejected=true;}
            require(invalidEditRejected&&io::encodeXm(doc)==beforeGesture&&undoStack.empty()&&redoStack.size()==retainedRedo,
                    "a rejected labeled edit leaves the song and existing history intact");

            resetUndoLabels();const auto beforeCut=io::encodeXm(doc);
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::B,false);buttonInput(Button::A,false);
            require(undoStack.size()==1,"an A-first Cut has one labeled transaction");
            changePage(Page::Song);expectUndoLabel("CUT",beforeCut);

            resetUndoLabels();const auto beforePaste=io::encodeXm(doc);
            clipboard=Cell{65,1,0x39,4,0x47};selectChord(Button::A);
            changePage(Page::Instrument);expectUndoLabel("PASTE",beforePaste);

            resetUndoLabels();changePage(Page::Sample);const auto beforeDelete=io::encodeXm(doc);
            deleteCurrentSample();tap(Button::B);
            require(undoStack.empty()&&io::encodeXm(doc)==beforeDelete,"cancelling Delete creates no labeled history");
            deleteCurrentSample();tap(Button::A);
            changePage(Page::Pattern);expectUndoLabel("DELETE",beforeDelete);

            resetUndoLabels();changePage(Page::Sample);const auto beforeDraw=io::encodeXm(doc);
            openWaveDraw();drawingSample->pcm[0]=256;tap(Button::B);
            require(undoStack.empty()&&io::encodeXm(doc)==beforeDraw,"cancelling a waveform draft creates no Draw history");
            openWaveDraw();drawingSample->pcm[0]=256;applyWaveDraft();
            changePage(Page::Song);expectUndoLabel("DRAW",beforeDraw);

            resetUndoLabels();changePage(Page::Song);doc.patterns.push_back(doc.patterns[0]);playback->load(doc);
            const auto beforePatternCleanup=io::encodeXm(doc);
            cleanupSong(edit::CleanupScope::Patterns);tap(Button::A);
            changePage(Page::Sample);expectUndoLabel("CLEAN UP PATTERNS",beforePatternCleanup);
            resetUndoLabels();changePage(Page::Song);doc.instruments.push_back(doc.instruments[0]);playback->load(doc);
            const auto beforeInstrumentCleanup=io::encodeXm(doc);
            cleanupSong(edit::CleanupScope::Instruments);tap(Button::A);
            changePage(Page::Pattern);expectUndoLabel("CLEAN UP INSTRUMENTS",beforeInstrumentCleanup);

            resetUndoLabels();changePage(Page::Sample);const auto beforeImport=io::encodeXm(doc);
            auto labelImportSample=*currentSample();labelImportSample.pcm={-16384,0,16384,0};
            labelImportSample.loopType=0;labelImportSample.loopStart=labelImportSample.loopLength=0;
            const auto labelImportPath=home/"undo-label-import.wav";
            io::writeFileAtomic(labelImportPath,io::encodeWavMono(labelImportSample));
            openAudioImport(labelImportPath);importField=2;tap(Button::A);
            changePage(Page::Instrument);expectUndoLabel("IMPORT",beforeImport);
            resetUndoLabels();
        }
