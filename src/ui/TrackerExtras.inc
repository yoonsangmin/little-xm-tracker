// Included inside TrackerApp::Impl. FT2 editing tools use the app-owned model.
    // DRAW: VALUE, EDIT POINTS, then NEW WAVE / CLEAR ALL / APPLY.
    int waveDrawFocus=0,waveDrawButton=2,waveDrawLengthIndex=1,waveNewFocus=0;
    int waveDrawInstrument=1, waveDrawSample=0;
    int waveDrawResolution=1,waveDrawPreset=0,waveDrawFirst=0,waveDrawLast=0;
    bool waveDrawNew=false;
    bool wavePresetOpen=false,wavePreview=false;
    int waveDutyIndex=2,waveLevelIndex=1,waveNoiseType=1;
    edit::WaveSynthOptions waveSynth{};
    std::vector<std::int16_t> wavePresetDraftPcm;
    std::array<std::array<std::int16_t,96>,5> wavePresetThumbnails{};
    std::array<int,5> wavePresetThumbnailCounts{};
    std::array<int,8> waveNewRowColumns{{-1,-1,-1,-1,-1,-1,-1,-1}};
    bool extrasBPending=false,extrasBUsed=false;
    std::array<int,5> maskRowColumns{{-1,-1,-1,-1,-1}};

    edit::PasteMask activePasteMask() const {
        auto mask=pasteMask;
        mask.note=mask.note&&copiedMask.note;
        mask.instrument=mask.instrument&&copiedMask.instrument;
        mask.volume=mask.volume&&copiedMask.volume;
        mask.effect=mask.effect&&copiedMask.effect;
        mask.param=mask.param&&copiedMask.param;
        return mask;
    }
    static bool& maskField(edit::PasteMask& mask,int index) {
        switch(index){case 0:return mask.note;case 1:return mask.instrument;case 2:return mask.volume;
            case 3:return mask.effect;case 4:return mask.param;default:return mask.skipEmpty;}
    }
    void changePatternSpacing(bool expand,bool discard=false) {
        try {
            mutatePattern([&](Pattern& p){if(expand)edit::expandPattern(p,doc.channels,discard);
                else edit::shrinkPattern(p,doc.channels,discard);});
            blockSelected=false;normalize();
            notify(expand?"PATTERN EXPANDED / EFFECT BYTES KEPT":"PATTERN SHRUNK / EFFECT BYTES KEPT");
        } catch(const std::exception& e) {
            if(discard||lower(e.what()).find("discard")==std::string::npos)throw;
            confirm(expand?"EXPAND AND DROP END ROWS?":"SHRINK AND DROP ODD ROWS?",
                "DROPPED ROWS CONTAIN DATA. ALL ORDERS USING THIS PATTERN CHANGE.",
                [this,expand]{changePatternSpacing(expand,true);});
        }
    }
    void copyPatternSlot() {
        patternSlotClipboard=pattern();patternSlotClipboardChannels=doc.channels;
        openCopyDestination(SlotKind::Pattern);
    }
    void pastePatternSlot(int target=-1) {
        if(!patternSlotClipboard)throw std::runtime_error("COPY A PATTERN FIRST");
        if(patternSlotClipboardChannels!=doc.channels)throw std::runtime_error("PATTERN CLIPBOARD CHANNEL COUNT DOES NOT MATCH SONG");
        const int destination=target<0?patternIndex():target;
        const bool fromList=mode==Mode::SlotList&&slotPaste;
        auto apply=[this,destination,fromList]{
            mutate([&]{edit::replacePattern(doc,destination,*patternSlotClipboard,patternSlotClipboardChannels);});
            clearSelection();normalize();
            if(fromList)openSelectedSlot();
            notify("PATTERN PASTED / ROWS REPLACED / ORDER REFERENCES KEPT");
        };
        if(std::size_t(destination)<doc.patterns.size())confirm("REPLACE PATTERN "+hex(destination)+"?",
            "REPLACE ALL ROWS / "+number(edit::patternUseCount(doc,destination))+" ORDER(S) USE THIS PATTERN. UNDO AVAILABLE.",apply);
        else apply();
        if(fromList&&mode==Mode::Confirm)confirmCancelMode=Mode::SlotList;
    }
    void copyInstrumentSlot() {
        if(!currentInstrument())throw std::runtime_error("NO INSTRUMENT");
        instrumentClipboard=*currentInstrument();
        openCopyDestination(SlotKind::Instrument);
    }
    void pasteInstrumentSlot(int target=-1) {
        if(!instrumentClipboard)throw std::runtime_error("COPY AN INSTRUMENT FIRST");
        const int destination=target<0?instrument:target;
        const bool fromList=mode==Mode::SlotList&&slotPaste;
        auto apply=[this,destination,fromList]{
            mutate([&]{edit::replaceInstrument(doc,destination,*instrumentClipboard);});
            instrument=destination;sample=0;selectionStart=selectionEnd=0;waveformZoom=false;
            if(fromList)openSelectedSlot();
            notify("INSTRUMENT PASTED / PATTERN REFERENCES KEPT");
        };
        if(std::size_t(destination)<=doc.instruments.size())confirm("REPLACE INSTRUMENT "+hex(destination)+"?",
            "REPLACE NAME, MAPPING, ENVELOPES AND ALL SAMPLES. UNDO AVAILABLE.",apply);
        else apply();
        if(fromList&&mode==Mode::Confirm)confirmCancelMode=Mode::SlotList;
    }
    void copySampleSlot() {
        sampleClipboard=*currentSampleChecked();openCopyDestination(SlotKind::Sample);
    }
    void pasteSampleSlot(int owner=-1,int target=-1) {
        if(!sampleClipboard)throw std::runtime_error("COPY A SAMPLE SLOT FIRST");
        const int destinationInstrument=owner<0?instrument:owner,destinationSample=target<0?sample:target;
        const bool fromList=mode==Mode::SlotList&&slotPaste;
        auto apply=[this,destinationInstrument,destinationSample,fromList]{
            mutate([&]{edit::replaceSample(doc,destinationInstrument,destinationSample,*sampleClipboard);});
            selectionStart=selectionEnd=0;waveformZoom=false;
            if(fromList)openSelectedSlot();
            notify("SAMPLE SLOT PASTED / KEY MAPPING KEPT");
        };
        const bool exists=std::size_t(destinationInstrument)<=doc.instruments.size()&&
            std::size_t(destinationSample)<doc.instruments[std::size_t(destinationInstrument-1)].samples.size();
        if(exists)confirm("REPLACE SAMPLE "+hex(destinationSample)+" / INSTRUMENT "+hex(destinationInstrument)+"?",
            "REPLACE PCM, NAME, LOOP AND TUNING. KEY MAPPING STAYS IN PLACE.",apply);
        else apply();
        if(fromList&&mode==Mode::Confirm)confirmCancelMode=Mode::SlotList;
    }
    void swapInstrumentSlot() {
        if(!currentInstrument())throw std::runtime_error("NO INSTRUMENT");
        const int source=instrument;
        openNumber("SWAP WITH INSTRUMENT (DECIMAL)",source,1,int(doc.instruments.size()),[this,source](int destination){
            if(source==destination){notify("SAME INSTRUMENT / NO CHANGE");return;}
            confirm("SWAP INSTRUMENT "+hex(source)+" / "+hex(destination)+"?",
                "EXCHANGE CONTENTS. PATTERN INSTRUMENT NUMBERS STAY IN PLACE.",[this,source,destination]{
                    mutate([&]{edit::swapInstruments(doc,source,destination);});
                    selectionStart=selectionEnd=0;waveformZoom=false;notify("INSTRUMENT CONTENTS SWAPPED");
                });
        });
    }
    void swapSampleSlot() {
        currentSampleChecked();const int sourceInstrument=instrument,sourceSample=sample;
        openNumber("SWAP WITH INSTRUMENT (DECIMAL)",instrument,1,int(doc.instruments.size()),[this,sourceInstrument,sourceSample](int destinationInstrument){
            const auto count=int(doc.instruments.at(std::size_t(destinationInstrument-1)).samples.size());
            if(!count)throw std::runtime_error("DESTINATION INSTRUMENT HAS NO SAMPLE SLOTS");
            openNumber("SWAP WITH SAMPLE (DECIMAL)",0,0,count-1,[this,sourceInstrument,sourceSample,destinationInstrument](int destinationSample){
                if(sourceInstrument==destinationInstrument&&sourceSample==destinationSample){notify("SAME SAMPLE / NO CHANGE");return;}
                confirm("SWAP SAMPLE CONTENTS?","SAMPLE NUMBERS AND BOTH INSTRUMENT KEY MAPS STAY IN PLACE.",
                    [this,sourceInstrument,sourceSample,destinationInstrument,destinationSample]{
                        mutate([&]{edit::swapSamples(doc,sourceInstrument,sourceSample,destinationInstrument,destinationSample);});
                        selectionStart=selectionEnd=0;waveformZoom=false;notify("SAMPLE CONTENTS SWAPPED");
                    });
            },1);
        });
    }
    void openSampleMorph() {
        auto* inst=currentInstrument();
        if(!inst||inst->samples.size()<3)throw std::runtime_error("MORPH NEEDS TWO ENDPOINTS AND A SLOT BETWEEN THEM");
        const int first=sample,last=int(inst->samples.size())-1;
        if(first+2>last)throw std::runtime_error("SELECT AN EARLIER SAMPLE AS THE FIRST MORPH ENDPOINT");
        openNumber("MORPH LAST SAMPLE (DECIMAL)",last,first+2,last,[this,first](int lastSlot){
            confirm("MORPH SAMPLES "+hex(first)+" TO "+hex(lastSlot)+"?",
                "REPLACE INTERMEDIATE SLOTS WITH PCM BLENDS. ENDPOINTS ARE KEPT.",[this,first,lastSlot]{
                    mutate([&]{edit::morphSamples(*currentInstrument(),std::size_t(first),std::size_t(lastSlot));});
                    notify("INTERMEDIATE SAMPLES MORPHED / ENDPOINTS KEPT");
                });
        },1);
    }
    int waveDrawPointCount() const {
        const int length=waveDrawLast-waveDrawFirst;
        return waveDrawResolution==4?length:std::min(length,8<<waveDrawResolution);
    }
    void stopWavePreview() {
        wavePreview=false;
        playback->stopPreview();
    }
    void refreshWavePreview() {
        if(!wavePreview)return;
        try {
            if(wavePresetOpen) {
                Sample candidate;candidate.sixteenBit=false;
                candidate.loopType=1;candidate.loopLength=std::uint32_t(wavePresetDraftPcm.size());
                candidate.pcm=wavePresetDraftPcm;
                playback->previewSample(candidate);
            } else if(drawingSample)playback->previewSample(*drawingSample);
            else stopWavePreview();
        } catch(...) {stopWavePreview();throw;}
    }
    void toggleWavePreview() {
        if(wavePreview&&playback->previewPlaying())stopWavePreview();
        else {wavePreview=true;refreshWavePreview();}
    }
    std::string wavePreviewHint(std::string_view hint) const {
        const auto split=hint.rfind(" / B ");
        const std::string listen=wavePreview&&playback->previewPlaying()?" / START STOP":" / START LISTEN";
        return split==std::string_view::npos?std::string(hint)+listen:
            std::string(hint.substr(0,split))+listen+std::string(hint.substr(split));
    }
    void drawWavePreviewStatus() {
        if(wavePreview&&playback->previewPlaying())txt(924-ui::textWidth("LISTENING"),99,"LISTENING",cyan);
    }
    int waveDrawPointFrame(int index) const {
        const int count=waveDrawPointCount();
        if(count<=1)return waveDrawFirst;
        return waveDrawFirst+int((std::int64_t(index)*(waveDrawLast-waveDrawFirst-1)+(count-1)/2)/(count-1));
    }
    int waveDrawPointIndex() const {
        const int count=waveDrawPointCount(),span=waveDrawLast-waveDrawFirst-1;
        if(count<=1||span<=0)return 0;
        return std::clamp(int((std::int64_t(drawFrame-waveDrawFirst)*(count-1)+span/2)/span),0,count-1);
    }
    struct WaveDrawPlot {int first,last,firstX,step;};
    WaveDrawPlot waveDrawPlot() const {
        int first=waveDrawFirst,last=waveDrawLast;
        if(waveDrawResolution==4) {
            first=std::clamp(drawFrame-32,waveDrawFirst,std::max(waveDrawFirst,waveDrawLast-64));
            last=std::min(waveDrawLast,first+64);
        }
        const int points=waveDrawResolution==4?last-first:waveDrawPointCount();
        // Fit an integer number of equal point intervals inside the graph,
        // leaving room for the selected point's nine-pixel marker.
        const int step=points>1?(824-9)/(points-1):0;
        return {first,last,100+(823-step*std::max(0,points-1))/2,step};
    }
    int waveDrawFrameX(int frame,const WaveDrawPlot& plot) const {
        frame=std::clamp(frame,plot.first,plot.last-1);
        if(waveDrawResolution==4)return plot.firstX+(frame-plot.first)*plot.step;
        const int points=waveDrawPointCount();
        if(points<=1)return plot.firstX;
        // SMOOTH uses an edit-point axis. Keep the existing PCM anchors and
        // map every intermediate frame through the same interval as its dot.
        // FRAME and the main SAMPLE view retain their actual frame time axis.
        int lo=0,hi=points-1;
        while(lo+1<hi) {
            const int middle=(lo+hi)/2;
            if(waveDrawPointFrame(middle)<=frame)lo=middle;else hi=middle;
        }
        const int a=waveDrawPointFrame(lo),b=waveDrawPointFrame(lo+1);
        return plot.firstX+lo*plot.step+int(std::int64_t(frame-a)*plot.step/(b-a));
    }
    void drawWaveValue(int value) {
        if(!drawingSample)return;
        const int point=waveDrawPointIndex();
        drawFrame=waveDrawPointFrame(point);
        const int left=waveDrawResolution==4?drawFrame:waveDrawPointFrame(std::max(0,point-1));
        const int right=waveDrawResolution==4?drawFrame:waveDrawPointFrame(std::min(waveDrawPointCount()-1,point+1));
        edit::drawPcmStroke(*drawingSample,std::size_t(left),std::size_t(drawFrame),std::size_t(right),value);
        refreshWavePreview();
    }
    static const char* wavePresetName(int preset) {
        static constexpr const char* names[]={"SINE","TRIANGLE","PULSE","SAW","NOISE"};
        return names[std::clamp(preset,0,4)];
    }
    // Stable field IDs keep focus and each row's column when optional controls
    // disappear. Only visible rows participate in controller navigation.
    std::vector<std::vector<int>> waveNewRows() const {
        std::vector<std::vector<int>> rows{{0,1,2,3,4}};
        rows.push_back(waveDrawPreset==4?std::vector<int>{5}:std::vector<int>{5,6});
        rows.push_back({8,9});
        rows.push_back(waveSynth.distortion==edit::WaveDistortion::Off?std::vector<int>{11}:std::vector<int>{10,11});
        rows.push_back({12});
        if(waveSynth.filter!=edit::WaveFilter::Off)rows.push_back({13,14});
        if(waveDrawPreset!=4)rows.push_back({15,16});
        rows.push_back({7});
        return rows;
    }
    static int waveNewRowKey(int focus) {
        if(focus<5)return 0;
        if(focus==5||focus==6)return 1;
        if(focus==7)return 7;
        if(focus<10)return 2;
        if(focus<12)return 3;
        if(focus==12)return 4;
        return focus<15?5:6;
    }
    void moveWaveNewFocus(Button b) {
        const auto rows=waveNewRows();
        std::size_t r=0,c=0;
        for(;r<rows.size();++r) {
            const auto item=std::find(rows[r].begin(),rows[r].end(),waveNewFocus);
            if(item!=rows[r].end()){c=std::size_t(item-rows[r].begin());break;}
        }
        if(r==rows.size()){waveNewFocus=waveDrawPreset;return;}
        waveNewRowColumns[std::size_t(waveNewRowKey(waveNewFocus))]=int(c);
        if(b==Button::Left)c=c?c-1:0;
        else if(b==Button::Right)c=std::min(c+1,rows[r].size()-1);
        else {
            r=b==Button::Up?(r?r-1:0):std::min(r+1,rows.size()-1);
            const int remembered=waveNewRowColumns[std::size_t(waveNewRowKey(rows[r][0]))];
            c=std::min(remembered<0?c:std::size_t(remembered),rows[r].size()-1);
        }
        waveNewFocus=rows[r][c];
        waveNewRowColumns[std::size_t(waveNewRowKey(waveNewFocus))]=int(c);
        if(waveNewFocus<5&&waveNewFocus!=waveDrawPreset)prepareWavePreset(waveNewFocus);
    }
    edit::NoisePreset waveNoisePreset() const {
        return waveNoiseType==0?edit::NoisePreset::Short:edit::NoisePreset::Long;
    }
    int waveCandidateFrames() const {
        return waveDrawPreset==4?int(edit::noisePresetFrames(waveNoisePreset())):16<<waveDrawLengthIndex;
    }
    Sample makeWaveCandidate(int preset) const {
        auto options=waveSynth;
        constexpr int duties[]={1,2,4,6};
        options.preset=static_cast<edit::WavePreset>(preset);options.noise=waveNoisePreset();
        options.frames=std::size_t(16<<waveDrawLengthIndex);options.dutyEighths=duties[waveDutyIndex];
        options.levels=preset==2?256:waveLevelIndex==0?16:256;
        return edit::synthesizeWave(options);
    }
    void wavePresetThumbnail(int index,const std::vector<std::int16_t>& pcm) {
        const int count=std::min(96,int(pcm.size()));
        wavePresetThumbnailCounts[std::size_t(index)]=count;
        for(int n=0;n<count;++n) {
            const auto frame=count>1?std::size_t(std::uint64_t(n)*(pcm.size()-1)/std::size_t(count-1)):0;
            wavePresetThumbnails[std::size_t(index)][std::size_t(n)]=pcm[frame];
        }
    }
    void prepareWavePreset(int index) {
        index=std::clamp(index,0,4);
        auto candidate=makeWaveCandidate(index);
        wavePresetThumbnail(index,candidate.pcm);
        wavePresetDraftPcm=std::move(candidate.pcm);
        waveDrawPreset=index;
        if(wavePresetOpen)refreshWavePreview();
    }
    void openWavePresets() {
        // NEW WAVE owns only a creation candidate. Opening or cancelling it
        // must not replace the PCM or cursor in an existing drawing draft.
        for(int n=0;n<5;++n) {
            const auto icon=makeWaveCandidate(n);
            wavePresetThumbnail(n,icon.pcm);
        }
        wavePresetOpen=false;
        prepareWavePreset(waveDrawPreset);
        waveNewFocus=waveDrawPreset;waveNewRowColumns[0]=waveDrawPreset;
        wavePresetOpen=true;extrasBPending=extrasBUsed=false;
        refreshWavePreview();
    }
    void closeWavePresets() {
        stopWavePreview();
        wavePresetOpen=false;
        wavePresetDraftPcm=std::vector<std::int16_t>{};
        waveDrawFocus=waveDrawButton=2;
        if(!drawingSample)mode=Mode::Normal;
    }
    void moveWaveDrawFocus(int direction) {
        if(direction<0)waveDrawFocus=waveDrawFocus>=2?1:0;
        else if(waveDrawFocus<2)waveDrawFocus=waveDrawFocus==0?1:waveDrawButton;
    }
    void createWaveDraft() {
        const int length=waveCandidateFrames();
        if(wavePresetDraftPcm.size()!=std::size_t(length))
            throw std::runtime_error("CHOOSE A WAVE PRESET BEFORE CREATING");
        Sample next;
        next.name="DRAWN WAVE";next.sixteenBit=false;
        next.loopType=1;next.loopStart=0;next.loopLength=length;
        // Preserve the exact preview, including Noise. This remains a draft
        // until the separate DRAW APPLY action commits one undoable edit.
        next.pcm=std::move(wavePresetDraftPcm);
        drawingSample=std::move(next);
        waveDrawNew=true;drawFrame=0;waveDrawFocus=0;
        waveDrawFirst=0;waveDrawLast=length;waveDrawResolution=waveDrawPreset==4?4:1;
        wavePresetOpen=false;wavePresetDraftPcm=std::vector<std::int16_t>{};
        extrasBPending=extrasBUsed=false;
        refreshWavePreview();
    }
    void changeWaveOption(int direction,bool coarse=false) {
        bool changed=false;
        auto change=[&](int& value,int minimum,int maximum,int step=1){
            const int next=std::clamp(value+direction*step,minimum,maximum);
            changed=next!=value;value=next;
        };
        switch(waveNewFocus) {
        case 5: if(waveDrawPreset==4)change(waveNoiseType,0,1);else change(waveDrawLengthIndex,0,4);break;
        case 6: if(waveDrawPreset==2)change(waveDutyIndex,0,3);else change(waveLevelIndex,0,1);break;
        case 8: {
            int value=int(waveSynth.distortion);change(value,0,3);waveSynth.distortion=static_cast<edit::WaveDistortion>(value);break;
        }
        case 9: change(waveSynth.drivePercent,0,800,coarse?50:5);break;
        case 10: change(waveSynth.limitPercent,1,100,coarse?10:1);break;
        case 11: change(waveSynth.vshiftPercent,-100,100,coarse?10:1);break;
        case 12: {
            int value=int(waveSynth.filter);change(value,0,4);waveSynth.filter=static_cast<edit::WaveFilter>(value);break;
        }
        case 13: change(waveSynth.cutoffPercent,1,99,coarse?10:1);break;
        case 14: change(waveSynth.resonanceTenths,1,100,coarse?10:1);break;
        case 15: {
            int value=int(waveSynth.phase);change(value,0,3);waveSynth.phase=static_cast<edit::WavePhase>(value);break;
        }
        case 16: change(waveSynth.phaseAmount,0,100,coarse?10:1);break;
        default:return;
        }
        if(!changed)return;
        // Keep every thumbnail consistent with the retained per-shape options.
        for(int n=0;n<5;++n)wavePresetThumbnail(n,makeWaveCandidate(n).pcm);
        prepareWavePreset(waveDrawPreset);
    }
    void openWaveDraw() {
        stopWavePreview();
        waveDrawInstrument=instrument;waveDrawSample=sample;waveDrawFocus=0;
        waveDrawResolution=1;waveDrawPreset=0;waveDrawLengthIndex=1;waveDrawButton=2;
        waveDutyIndex=2;waveLevelIndex=1;waveNoiseType=1;
        waveSynth={};waveNewRowColumns.fill(-1);
        wavePresetOpen=false;wavePresetDraftPcm=std::vector<std::int16_t>{};
        drawingSample.reset();waveDrawNew=false;
        mode=Mode::WaveDraw;
        if(auto* pcm=currentSample();pcm&&!pcm->pcm.empty()) {
            Sample next=*pcm;drawingSample=std::move(next);
            const bool selected=selectionStart>=0&&selectionEnd>selectionStart&&selectionEnd<=int(pcm->pcm.size());
            waveDrawFirst=selected?selectionStart:0;waveDrawLast=selected?selectionEnd:int(pcm->pcm.size());
            drawFrame=std::clamp(selectionStart,waveDrawFirst,waveDrawLast-1);
            drawFrame=waveDrawPointFrame(waveDrawPointIndex());
        } else openWavePresets();
    }
    void applyWaveDraft() {
        if(!drawingSample)return;
        stopWavePreview();
        mutate([&]{edit::replaceSample(doc,waveDrawInstrument,waveDrawSample,*drawingSample);},"DRAW");
        instrument=waveDrawInstrument;sample=waveDrawSample;
        selectionStart=selectionEnd=0;waveformZoom=false;
        drawingSample.reset();mode=Mode::Normal;
        notify("WAVEFORM APPLIED / SAMPLE PREVIEW AVAILABLE");
    }
    void addEditorExtras(std::vector<Action>& patternTools,std::vector<Action>& instrumentTools,
                         std::vector<Action>& sampleTools) {
        auto add=[](std::vector<Action>& list,std::string title,std::string detail,std::function<void()> fn){
            list.push_back({std::move(title),std::move(detail),std::move(fn)});
        };
        add(patternTools,"COPY / PASTE MASKS","CHOOSE FIELDS AND WHETHER EMPTY CELLS OVERWRITE",[this]{maskRow=maskColumn=0;maskRowColumns.fill(-1);mode=Mode::PasteMasks;});
        add(patternTools,"COPY PATTERN","COPY WHOLE PATTERN AND CHOOSE DESTINATION NUMBER",[this]{copyPatternSlot();});
        add(patternTools,"PASTE PATTERN","REPLACE ENTIRE PATTERN AND ROW COUNT; KEEP ORDER REFERENCES",[this]{pastePatternSlot();});
        add(patternTools,"EXPAND PATTERN","DOUBLE ROW SPACING; EFFECT VALUES STAY UNCHANGED",[this]{changePatternSpacing(true);});
        add(patternTools,"SHRINK PATTERN","KEEP EVEN ROWS; CONFIRM BEFORE DROPPING NOTES",[this]{changePatternSpacing(false);});
        add(instrumentTools,"COPY INSTRUMENT","COPY WHOLE INSTRUMENT AND CHOOSE DESTINATION NUMBER",[this]{copyInstrumentSlot();});
        add(instrumentTools,"PASTE INSTRUMENT","REPLACE ENTIRE INSTRUMENT; KEEP PATTERN REFERENCES",[this]{pasteInstrumentSlot();});
        add(instrumentTools,"SWAP INSTRUMENT","EXCHANGE TWO SLOT CONTENTS; KEEP PATTERN REFERENCES",[this]{swapInstrumentSlot();});
        add(sampleTools,"COPY SAMPLE SLOT","COPY WHOLE SAMPLE AND CHOOSE DESTINATION SLOT",[this]{copySampleSlot();});
        add(sampleTools,"PASTE SAMPLE SLOT","REPLACE CURRENT SAMPLE SLOT; KEEP KEY MAPPING",[this]{pasteSampleSlot();});
        add(sampleTools,"SWAP SAMPLE SLOT","EXCHANGE SAMPLE CONTENTS WITHIN OR BETWEEN INSTRUMENTS",[this]{swapSampleSlot();});
        std::vector<Action> advanced;
        add(advanced,"DETECT PITCH","ANALYZE SELECTED PCM OR WHOLE SAMPLE; REVIEW BEFORE TUNING",[this]{openPitchDetection();});
        add(advanced,"CHOP SAMPLE","UP TO 16 SLICES / AUTO SPLIT AND EDIT CUTS",[this]{openSampleChop();});
        add(advanced,"LOOP CROSSFADE","SELECT ACROSS LOOP START; MATCHING LOOP END RANGE MUST FIT",[this]{
            const auto range=selectedFrames(false);mutate([&]{edit::crossfadeLoop(*currentSample(),range);});notify("LOOP CROSSFADE APPLIED");
        });
        add(advanced,"RESAMPLE","CHANGE PCM RATE; RETUNE TO KEEP PLAYBACK PITCH",[this]{
            currentSampleChecked();openNumber("RESAMPLE SEMITONES",0,-36,36,[this](int shift){
                mutate([&]{edit::resampleSample(*currentSample(),shift);});
                selectionStart=selectionEnd=0;waveformZoom=false;notify("SAMPLE RESAMPLED / TUNING ADJUSTED");
            },12);
        });
        add(advanced,"MIX PCM","BLEND COPIED PCM AT SELECTION START; 50 = EQUAL MIX",[this]{
            currentSampleChecked();if(!pcmClipboard)throw std::runtime_error("COPY PCM FIRST");
            openNumber("SOURCE MIX PERCENT",50,0,100,[this](int balance){
                mutate([&]{edit::mixPcm(*currentSample(),std::size_t(selectionStart),*pcmClipboard,balance);});notify("PCM MIXED");
            },10);
        });
        add(advanced,"ECHO","PCM DELAY COPIES / SET DELAY, DECAY AND TAIL",[this]{
            if(currentSampleChecked()->pcm.empty())throw std::runtime_error("IMPORT PCM BEFORE ADDING ECHO");
            echoField=0;mode=Mode::EchoSettings;
        });
        add(advanced,"MORPH SAMPLE SLOTS","BLEND TWO MATCHING PCM ENDPOINTS INTO INTERMEDIATE SLOTS",[this]{openSampleMorph();});
        add(sampleTools,"ADVANCED PCM","PITCH / CHOP / CROSSFADE / RESAMPLE / MIX / ECHO / MORPH",[this,advanced=std::move(advanced)]{
            currentSampleChecked();enterMenu("ADVANCED PCM",advanced);
        });
    }
    bool isExtrasMode() const {
        return mode==Mode::PasteMasks||
            mode==Mode::WaveDraw||mode==Mode::EchoSettings;
    }
    bool extrasButton(Button b,bool repeating) {
        if(!isExtrasMode())return false;
        if(mode==Mode::WaveDraw&&b==Button::Start) {
            if(!repeating)toggleWavePreview();
            return true;
        }
        if(mode==Mode::WaveDraw&&wavePresetOpen) {
            if(b==Button::B&&!repeating){extrasBPending=true;extrasBUsed=false;aUsed=true;return true;}
            if(held[std::size_t(Button::B)]){aUsed=true;return true;}
            if(int(b)<4) {
                if(held[std::size_t(Button::A)]) {
                    aUsed=true;
                    changeWaveOption((b==Button::Right||b==Button::Up)?1:-1,b==Button::Up||b==Button::Down);
                } else {
                    moveWaveNewFocus(b);
                }
            }
            return true;
        }
        if(b==Button::B&&!repeating) {
            extrasBPending=true;extrasBUsed=false;
            if(held[std::size_t(Button::A)]) {
                aUsed=true;
                if(mode==Mode::WaveDraw&&drawingSample) {
                    if(waveDrawFocus==0)drawWaveValue(0);
                    extrasBUsed=true;
                }
            }
            return true;
        }
        if(b==Button::A&&!repeating&&held[std::size_t(Button::B)]) {
            aUsed=true;
            if(mode==Mode::WaveDraw&&drawingSample) {
                if(waveDrawFocus==0)drawWaveValue(0);
                extrasBUsed=true;
            }
            return true;
        }
        const bool arrow=int(b)<4;
        const bool edit=held[std::size_t(Button::A)];
        const bool positive=b==Button::Right||b==Button::Up;
        const bool vertical=b==Button::Up||b==Button::Down;
        const int sign=positive?1:-1;
        if(arrow&&edit)aUsed=true;
        if(mode==Mode::PasteMasks) {
            if(arrow) {
                if(edit) {
                    if(maskRow<6)maskField(maskColumn?pasteMask:copyMask,maskRow)=positive;
                } else {
                    if(vertical) {
                        if(maskRow<5)maskRowColumns[std::size_t(maskRow)]=maskColumn;
                        maskRow=std::clamp(maskRow+(b==Button::Down?1:-1),0,6);
                        if(maskRow<5&&maskRowColumns[std::size_t(maskRow)]>=0)maskColumn=maskRowColumns[std::size_t(maskRow)];
                        else if(maskRow==5)maskColumn=1;
                    } else if(maskRow<5) {
                        maskColumn=b==Button::Right?1:0;
                        maskRowColumns[std::size_t(maskRow)]=maskColumn;
                    }
                }
            }
            return true;
        }
        if(mode==Mode::WaveDraw) {
            if(!drawingSample)return true;
            if(arrow) {
                if(edit) {
                    if(waveDrawFocus==0) {
                        const int step=vertical?4096:(drawingSample->sixteenBit&&waveDrawResolution==4?32:256);
                        const int value=std::clamp(int(drawingSample->pcm[std::size_t(drawFrame)])+sign*step,-32768,32767);
                        drawWaveValue(value);
                    } else if(waveDrawFocus==1) {
                        waveDrawResolution=std::clamp(waveDrawResolution+sign,0,4);
                        drawFrame=waveDrawPointFrame(waveDrawPointIndex());
                    }
                } else if(waveDrawFocus<2&&!vertical) {
                    const int jump=held[std::size_t(Button::Select)]?(waveDrawResolution==4?64:std::max(1,waveDrawPointCount()/4)):1;
                    drawFrame=waveDrawPointFrame(std::clamp(waveDrawPointIndex()+sign*jump,0,waveDrawPointCount()-1));
                } else if(vertical)moveWaveDrawFocus(b==Button::Down?1:-1);
                else if(waveDrawFocus>=2)waveDrawButton=waveDrawFocus=std::clamp(waveDrawFocus+sign,2,4);
            }
            return true;
        }
        if(mode==Mode::EchoSettings) {
            if(arrow) {
                if(edit) {
                    if(echoField==0)echoDelay=std::clamp(echoDelay+sign*(vertical?256:1),1,1024*1024);
                    else if(echoField==1)echoRepeats=std::clamp(echoRepeats+sign*(vertical?8:1),1,64);
                    else if(echoField==2)echoDecay=std::clamp(echoDecay+sign*(vertical?10:1),0,100);
                    else if(echoField==3)echoTail=positive;
                } else if(vertical)echoField=std::clamp(echoField+(b==Button::Down?1:-1),0,4);
            }
            return true;
        }
        return true;
    }
    // Parent calls this before its normal A-release action. In value fields a
    // plain A tap is intentionally inert; only named buttons commit on release.
    bool extrasRelease(Button b) {
        if(!isExtrasMode())return false;
        if(mode==Mode::WaveDraw&&wavePresetOpen) {
            if(b==Button::B) {
                const bool close=extrasBPending&&!extrasBUsed;
                extrasBPending=extrasBUsed=false;
                if(close){aUsed=true;closeWavePresets();}
                return true;
            }
            if(b==Button::A&&!aUsed) {
                aUsed=true;
                if(waveNewFocus<5){waveNewFocus=5;waveNewRowColumns[1]=0;}
                else if(waveNewFocus==7)createWaveDraft();
            }
            return true;
        }
        if(b==Button::B) {
            const bool close=extrasBPending&&!extrasBUsed;
            extrasBPending=false;extrasBUsed=false;
            if(close) {
                aUsed=true;
                if(mode==Mode::WaveDraw){stopWavePreview();drawingSample.reset();wavePresetOpen=false;wavePresetDraftPcm=std::vector<std::int16_t>{};}
                mode=Mode::Normal;
            }
            return true;
        }
        if(b!=Button::A||aUsed)return true;
        aUsed=true;
        if(mode==Mode::PasteMasks&&maskRow==6) {
            copyMask={};pasteMask={};
        } else if(mode==Mode::WaveDraw) {
            if(waveDrawFocus==2)openWavePresets();
            else if(waveDrawFocus==3&&drawingSample) {
                std::fill(drawingSample->pcm.begin(),drawingSample->pcm.end(),0);
                refreshWavePreview();
            }
            else if(waveDrawFocus==4)applyWaveDraft();
        } else if(mode==Mode::EchoSettings&&echoField==4) {
            mutate([&]{edit::echoSample(*currentSampleChecked(),std::size_t(echoDelay),echoRepeats,echoDecay,echoTail);});
            mode=Mode::Normal;notify("ECHO APPLIED / UNDO AVAILABLE");
        }
        return true;
    }
    std::pair<std::string,std::string> waveNewFieldText(int focus) const {
        static constexpr const char* duties[]={"12.5%","25%","50%","75%"};
        static constexpr const char* distortions[]={"OFF","CLIP","FOLD","WRAP"};
        static constexpr const char* filters[]={"OFF","LOWPASS","HIGHPASS","BANDPASS","ALLPASS"};
        static constexpr const char* phases[]={"PINCH","WARP","RESYNC","RESYN2"};
        switch(focus) {
        case 5:return waveDrawPreset==4?std::pair<std::string,std::string>{"TYPE",waveNoiseType==0?"SHORT":"LONG"}:
                                       std::pair<std::string,std::string>{"FRAMES",number(waveCandidateFrames())};
        case 6:return waveDrawPreset==2?std::pair<std::string,std::string>{"DUTY",duties[waveDutyIndex]}:
                                       std::pair<std::string,std::string>{"LEVELS",waveLevelIndex==0?"16":"256"};
        case 8:return {"DIST",distortions[int(waveSynth.distortion)]};
        case 9:return {"DRIVE",number(waveSynth.drivePercent)+"%"};
        case 10:return {"LIMIT",number(waveSynth.limitPercent)+"%"};
        case 11:return {"VSHIFT",(waveSynth.vshiftPercent>0?"+":"")+number(waveSynth.vshiftPercent)+"%"};
        case 12:return {"FILTER",filters[int(waveSynth.filter)]};
        case 13:return {"CUTOFF",number(waveSynth.cutoffPercent)+"%"};
        case 14:return {"Q",number(waveSynth.resonanceTenths/10)+"."+number(waveSynth.resonanceTenths%10)};
        case 15:return {"PHASE",phases[int(waveSynth.phase)]};
        case 16:return {"AMOUNT",number(waveSynth.phaseAmount)+"%"};
        default:return {};
        }
    }
    std::string waveNewDetail() const {
        if(waveNewFocus<5)return "ONE LOOP / OPTIONS ARE BAKED INTO SAMPLE PCM";
        switch(waveNewFocus) {
        case 5:return waveDrawPreset==4?number(waveCandidateFrames())+" FRAMES / "+
                       (waveNoiseType==0?"SHORT REPEAT / TONAL":"LONG REPEAT / NOISY"):"FRAMES - SAMPLES IN ONE WAVE CYCLE";
        case 6:return waveDrawPreset==2?"DUTY - HIGH PORTION OF THE PULSE CYCLE":"LEVELS - AMPLITUDE STEPS BAKED INTO PCM";
        case 7:return "CREATE A DRAWING DRAFT / APPLY IN DRAW TO KEEP IT";
        case 8:return "DIST - CLAMP, FOLD OR WRAP PEAKS AT THE LIMIT";
        case 9:return "DRIVE - INPUT GAIN BEFORE FILTER AND DISTORTION";
        case 10:return "LIMIT - HEIGHT WHERE PEAKS CLIP, FOLD OR WRAP";
        case 11:return "VSHIFT - VERTICAL OFFSET BEFORE DISTORTION";
        case 12:return waveSynth.filter==edit::WaveFilter::AllPass?"ALLPASS - CHANGE PHASE WHILE RETAINING FREQUENCIES":
                       "FILTER - SHAPE FREQUENCIES IN THE GENERATED LOOP";
        case 13:return "CUTOFF - PERCENT OF THE SAMPLE NYQUIST FREQUENCY";
        case 14:return "Q - FILTER RESONANCE / NARROWER BAND AT HIGHER VALUES";
        case 15:
            switch(waveSynth.phase) {
            case edit::WavePhase::Pinch:return "PINCH - COMPRESS THE WAVE INTO A SHORTER PART";
            case edit::WavePhase::Warp:return "WARP - STRETCH ONE HALF AND COMPRESS THE OTHER";
            case edit::WavePhase::Resync:return "RESYNC - REPEAT A COMPRESSED WAVE / REDUCED ALIASING";
            case edit::WavePhase::ResyncRaw:return "RESYN2 - REPEAT A COMPRESSED WAVE / RAW ALIASING";
            }
            return {};
        case 16:return "AMOUNT - HORIZONTAL WAVE SHAPING / 0% IS UNCHANGED";
        default:return {};
        }
    }
    void drawWavePresets() {
        const auto hints=wavePreviewHint(waveNewFocus<5?"A CHOOSE / B BACK":waveNewFocus==7?"A CREATE / B BACK":
            "A+LEFT/RIGHT FINE / A+UP/DOWN COARSE / B BACK");
        const auto detail=waveNewDetail();
        const auto rows=waveNewRows();
        const int buttonY=274+int(rows.size()-2)*44+12;
        drawToolPanel("NEW WAVE",buttonY+controlHeight,hints,detail);
        drawWavePreviewStatus();
        for(int n=0;n<5;++n) {
            const int x=100+n*167,y=146;
            const bool selected=n==waveDrawPreset,focused=n==waveNewFocus;
            const auto color=focused?amber:selected?cyan:muted;
            rect(x,y,156,112,selected?panel2:bg);outline(x,y,156,112,focused?amber:selected?cyan:dim);
            txt(x+12,y+12,wavePresetName(n),color);
            const int count=wavePresetThumbnailCounts[std::size_t(n)];
            const auto& pcm=wavePresetThumbnails[std::size_t(n)];
            const int plotX=x+12,plotY=y+40,width=132,height=60;
            line(plotX,plotY+height/2,plotX+width,plotY+height/2,dim);
            auto px=[&](int point){return plotX+point*(width-1)/std::max(1,count-1);};
            auto py=[&](int point){return plotY+height/2-int(pcm[std::size_t(point)])*(height/2-2)/32768;};
            for(int point=1;point<count;++point)line(px(point-1),py(point-1),px(point),py(point),selected?cyan:muted);
            if(count==1)rect(plotX,py(0)-1,width,2,selected?cyan:muted);
        }
        for(std::size_t r=1;r+1<rows.size();++r) {
            const int y=274+int(r-1)*44,width=rows[r].size()==1?824:404;
            for(std::size_t c=0;c<rows[r].size();++c) {
                const auto text=waveNewFieldText(rows[r][c]);
                labelValue(100+int(c)*420,y,text.first,text.second,waveNewFocus==rows[r][c],width);
            }
        }
        drawButton(100,buttonY,824,"CREATE",waveNewFocus==7);
        drawToolFooter(hints,detail);
    }
    void drawExtrasModes() {
        if(mode==Mode::PasteMasks) {
            const char* hints=maskRow==6?"A RESET MASKS / B BACK":"A+RIGHT/UP ON / A+LEFT/DOWN OFF / B BACK";
            drawToolPanel("COPY / PASTE MASKS",535+controlHeight,hints);
            const char* labels[]={"NOTE","INSTRUMENT","VOLUME","EFFECT","PARAMETER","SKIP EMPTY"};
            constexpr int columnX=440,columnStep=220,cellWidth=180;
            const char* headings[]={"COPY","PASTE"};
            for(int c=0;c<2;++c)
                txt(columnX+c*columnStep+(cellWidth-ui::textWidth(headings[c]))/2,153,headings[c],cyan);
            for(int r=0;r<6;++r) {
                const int y=193+r*50;txt(100,y+controlTextY,labels[r],r==maskRow?ink:muted);
                for(int c=0;c<2;++c) {
                    if(r==5&&c==0)continue;
                    const bool selected=r==maskRow&&c==maskColumn;
                    const int x=columnX+c*columnStep;
                    const char* value=maskField(c?pasteMask:copyMask,r)?"ON":"OFF";
                    rect(x,y,cellWidth,controlHeight,selected?panel2:bg);
                    if(selected)outline(x,y,cellWidth,controlHeight,amber);
                    txt(x+(cellWidth-ui::textWidth(value))/2,y+controlTextY,value,selected?amber:cyan);
                }
            }
            txt(100,501,"COPY MASK IS CAPTURED WHEN COPYING.",muted);
            drawButton(100,535,824,"RESET MASKS",maskRow==6);
            drawToolFooter(hints);
            return;
        }
        if(mode==Mode::WaveDraw) {
            if(wavePresetOpen){drawWavePresets();return;}
            if(!drawingSample)return;
            const auto hint=wavePreviewHint(waveDrawFocus==0?"LEFT/RIGHT POINT / A+DPAD EDIT / A+B ZERO / B CANCEL":
                waveDrawFocus==1?"LEFT/RIGHT POINT / A+DPAD EDIT POINTS / B CANCEL":
                waveDrawFocus==2?"A NEW WAVE / B CANCEL":waveDrawFocus==3?"A ZERO ENTIRE SAMPLE / B CANCEL":"A APPLY / B CANCEL");
            drawToolPanel("DRAW WAVEFORM",548+controlHeight,hint);const auto& pcm=drawingSample->pcm;
            drawWavePreviewStatus();
            const auto plot=waveDrawPlot();
            const int first=plot.first,last=plot.last;
            const int count=last-first,plotX=100,plotY=168,plotWidth=824,plotHeight=224;
            rect(plotX,plotY,plotWidth,plotHeight,bg);line(plotX,plotY+112,plotX+plotWidth,plotY+112,dim);
            auto px=[&](int frame){return waveDrawFrameX(frame,plot);};
            auto py=[&](int frame){return plotY+112-int(pcm[std::size_t(frame)])*102/32768;};
            const int segments=std::min(count-1,plotWidth-1);
            int previous=first;
            for(int n=1;n<=segments;++n) {
                const int frame=first+int(std::int64_t(n)*(count-1)/segments);
                line(px(previous),py(previous),px(frame),py(frame),cyan);previous=frame;
            }
            if(waveDrawResolution==4) {
                for(int n=first;n<last;++n)rect(px(n)-2,py(n)-2,4,4,cyan);
            } else for(int point=0;point<waveDrawPointCount();++point) {
                const int frame=waveDrawPointFrame(point);rect(px(frame)-3,py(frame)-3,6,6,cyan);
            }
            line(px(drawFrame),plotY,px(drawFrame),plotY+plotHeight,amber);
            rect(px(drawFrame)-4,py(drawFrame)-4,9,9,amber);
            txt(100,137,"POINT "+number(waveDrawPointIndex()+1)+" / "+number(waveDrawPointCount())+"   FRAME "+number(drawFrame),muted);
            const auto length=number(waveDrawLast-waveDrawFirst)+" FRAMES";
            txt(924-ui::textWidth(length),137,length,muted);
            labelValue(100,430,"VALUE",number(pcm[std::size_t(drawFrame)]),waveDrawFocus==0,824);
            labelValue(100,478,"EDIT POINTS",waveDrawResolution==4?"FRAME":number(waveDrawPointCount())+" / SMOOTH",waveDrawFocus==1,824);
            drawButton(100,548,260,"NEW WAVE",waveDrawFocus==2);
            drawButton(382,548,260,"CLEAR ALL",waveDrawFocus==3);
            drawButton(664,548,260,"APPLY",waveDrawFocus==4);
            drawToolFooter(hint);
            return;
        }
        if(mode==Mode::EchoSettings) {
            const char* hints=echoField==4?"A APPLY / B CANCEL":
                "A+LEFT/RIGHT FINE / A+UP/DOWN COARSE / B CANCEL";
            drawToolPanel("ECHO",442+controlHeight,hints);
            labelValue(100,170,"DELAY FRAMES",number(echoDelay),echoField==0,824);
            labelValue(100,230,"ECHO COUNT",number(echoRepeats),echoField==1,824);
            labelValue(100,290,"DECAY PERCENT",number(echoDecay),echoField==2,824);
            labelValue(100,350,"EXTEND TAIL",echoTail?"ON":"OFF",echoField==3,824);
            txt(100,408,"WHOLE SAMPLE / LOOP MARKERS KEPT",muted);
            drawButton(100,442,824,"APPLY",echoField==4);
            drawToolFooter(hints);
        }
    }
