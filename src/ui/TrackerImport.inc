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
        const std::string_view hints=importField==2?"A IMPORT / B CANCEL":"A+DPAD EDIT / B CANCEL";
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
