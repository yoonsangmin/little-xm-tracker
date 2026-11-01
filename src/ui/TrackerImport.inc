// Staged import: decoding and option changes never replace the selected sample.
    std::optional<io::DecodedAudio> pendingAudio;
    std::filesystem::path pendingAudioPath;
    io::StereoMode importStereo=io::StereoMode::Blend;
    int importBits=16, importField=0, importInstrument=1, importSample=0;

    void openAudioImport(const std::filesystem::path& path) {
        finishGesture();
        auto decoded=io::loadAudio(path);
        playback->stopPreview();
        pendingAudio=std::move(decoded);pendingAudioPath=path;
        importInstrument=instrument;importSample=sample;
        importStereo=io::StereoMode::Blend;importBits=16;importField=0;
        followPlayback=false;aUsed=true;mode=Mode::ImportOptions;
    }

    void applyAudioImport() {
        if(!pendingAudio)throw std::runtime_error("NO AUDIO SELECTED");
        auto replacement=io::convertAudio(*pendingAudio,importStereo,importBits);
        replacement.name=text::truncateBytes(pendingAudioPath.stem().u8string(),22);
        mutate([&]{
            instrument=importInstrument;sample=importSample;
            editableSample()=std::move(replacement);
            auto& target=editableInstrument();
            if(target.name.empty())target.name=target.samples[std::size_t(sample)].name;
        },"IMPORT");
        pendingAudio.reset();selectionStart=selectionEnd=0;waveformZoom=false;
        aUsed=true;changePage(Page::Sample);
        notify("AUDIO IMPORTED / "+number(importBits)+"-BIT MONO");
    }

    bool importAudioButton(Button b,bool /*repeating*/) {
        if(mode!=Mode::ImportOptions)return false;
        if(b==Button::B){pendingAudio.reset();mode=Mode::Browser;return true;}
        if(int(b)<4&&held[std::size_t(Button::A)]){
            aUsed=true;const int d=(b==Button::Right||b==Button::Up)?1:-1;
            if(importField==0&&pendingAudio&&pendingAudio->channels==2)importStereo=static_cast<io::StereoMode>(std::clamp(int(importStereo)+d,0,2));
            else if(importField==1)importBits=d>0?16:8;
        }else if(b==Button::Up||b==Button::Down)importField=std::clamp(importField+(b==Button::Down?1:-1),0,2);
        return true;
    }
    bool importAudioRelease(Button b) {
        if(mode!=Mode::ImportOptions)return false;
        if(b==Button::A&&!aUsed&&importField==2){aUsed=true;applyAudioImport();}
        return true;
    }

    void drawAudioImport() {
        if(!pendingAudio)return;
        const std::string_view hints=importField==2?"A IMPORT / B CANCEL":"A+DPAD VALUE / B CANCEL";
        drawToolPanel("IMPORT AUDIO",568,hints);
        txt(100,175,ui::fit(pendingAudioPath.filename().u8string(),68),ink);
        const auto& source=*pendingAudio;
        const auto sourceDepth=source.sourceBits?number(source.sourceBits)+"-BIT":"DECODED AUDIO";
        txt(100,215,source.format+" / "+number(source.sampleRate)+" HZ / "+sourceDepth,muted);
        const auto tenths=source.sampleRate?source.frames()*10/source.sampleRate:0;
        const auto duration=tenths?number(tenths/10)+"."+number(tenths%10)+" SECONDS":number(source.frames())+" FRAMES";
        txt(100,249,std::string(source.channels==2?"STEREO":"MONO")+" / "+duration,muted);
        const char* mixes[]={"BLEND","LEFT","RIGHT"};
        labelValue(100,310,"CHANNEL MIX",source.channels==1?"MONO":mixes[int(importStereo)],importField==0,824);
        labelValue(100,366,"BIT DEPTH",number(importBits)+"-BIT",importField==1,824);
        const std::string target="INSTRUMENT "+hex(importInstrument)+" / SAMPLE "+hex(importSample);
        txt(100,445,"REPLACE: "+target,cyan);
        if(source.channels==2&&importStereo==io::StereoMode::Blend)
            txt(100,487,"BLEND = EQUAL LEFT / RIGHT MIX",muted);
        labelValue(100,530,"IMPORT","",importField==2,824);
        drawToolFooter(hints);
    }

    void restoreCleanupEditor(int patternId,int instrumentId,int sampleId,int patternRow) {
        playback->stop();playback->stopPreview();followPlayback=false;
        songAppendOrder=false;songOrderColumn=0;selectedPattern=patternId;instrument=instrumentId;sample=sampleId;row=patternRow;normalize();
        lastCell.instrument=doc.instruments.empty()?0:std::uint8_t(instrument);
        clipboard.reset();blockClipboard.reset();patternSlotClipboard.reset();patternSlotClipboardChannels=0;
        orderClipboard.reset();orderBlockClipboard.clear();clearSelection();
        selectionStart=selectionEnd=0;waveformZoom=false;
    }

    void cleanupSong(edit::CleanupScope scope) {
        finishGesture();
        const bool patterns=scope==edit::CleanupScope::Patterns;
        const std::string kind=patterns?"PATTERNS":"INSTRUMENTS";
        const auto plan=edit::analyzeCleanup(doc,scope);
        if(!plan.changed()){notify("NO UNUSED "+kind);return;}
        const int removed=patterns?plan.patternsRemoved:plan.instrumentsRemoved;
        confirm("CLEAN UP "+kind+"?","REMOVE "+number(removed)+" UNUSED "+kind+"; RENUMBER IDS.",[this,scope,kind]{
            if(!edit::analyzeCleanup(doc,scope).changed()){notify("NO UNUSED "+kind);return;}
            const int previousPattern=selectedPattern, previousInstrument=instrument;
            const int previousSample=sample, previousRow=row;
            playback->stop();playback->stopPreview();followPlayback=false;
            mutate([&]{
                const auto result=edit::cleanupUnused(doc,scope);
                const int mappedPattern=result.patternMap[std::size_t(previousPattern)];
                selectedPattern=mappedPattern>=0?mappedPattern:int(doc.orders[std::size_t(order)]);
                const int mappedInstrument=previousInstrument>=1&&previousInstrument<=128?
                    result.instrumentMap[std::size_t(previousInstrument)]:-1;
                instrument=mappedInstrument>0?mappedInstrument:1;
                if(mappedInstrument<=0)sample=0;
            },"CLEAN UP "+kind);
            restoreCleanupEditor(selectedPattern,instrument,sample,row);
            // Reversing a renumbering also changes the meaning of editor IDs.
            // Restore the matching cursor and discard references copied under
            // the other numbering scheme before normal history playback reload.
            auto& change=undoStack.back();
            std::function<void(Document&)> undoCleanup=[this,undoDocument=change.undo,previousPattern,previousInstrument,previousSample,previousRow](Document& d){
                undoDocument(d);restoreCleanupEditor(previousPattern,previousInstrument,previousSample,previousRow);
            };
            std::function<void(Document&)> redoCleanup=[this,redoDocument=change.redo,nextPattern=selectedPattern,nextInstrument=instrument,nextSample=sample,nextRow=row](Document& d){
                redoDocument(d);restoreCleanupEditor(nextPattern,nextInstrument,nextSample,nextRow);
            };
            change.undo=std::move(undoCleanup);change.redo=std::move(redoCleanup);
            notify("UNUSED "+kind+" REMOVED / IDS UPDATED");
        });
    }
