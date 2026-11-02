// Included inside TrackerApp::Impl. Analysis is read-only until an explicit apply.
    std::shared_ptr<const Sample> analysisSource;
    edit::FrameRange analysisRange{};
    std::uint64_t analysisRevision=0;
    int analysisInstrument=1,analysisSample=0,pitchField=0;
    edit::PitchEstimate pitchEstimate;
    // Editing cuts only changes this small plan. PCM is built on Apply.
    struct ChopDraft {
        std::vector<std::size_t> boundaries;
        int snappedBoundaries=0;
        bool manual=false;
    };
    std::optional<ChopDraft> chopDraft;
    int chopCount=8,chopField=0,chopSlice=0,chopNewInstrument=0;
    std::array<int,6> chopRowColumns{};
    static constexpr std::array<std::array<int,2>,6> chopRows{{{0,1},{2,-1},{3,-1},{4,7},{8,9},{6,-1}}};
    bool chopSnap=false,chopCurrent=false;
    std::array<std::pair<std::int16_t,std::int16_t>,824> analysisWave{};

    void clearAnalysisDrafts() { analysisSource.reset();chopDraft.reset(); }
    void requireAnalysisSource() const {
        if(!analysisSource||documentRevision!=analysisRevision)
            throw std::runtime_error("SAMPLE CHANGED; OPEN THE ANALYSIS AGAIN");
    }
    static bool untouchedEnvelope(const Envelope& value) {
        return !value.count&&!value.flags&&!value.sustain&&!value.loopStart&&!value.loopEnd&&
            std::all_of(value.points.begin(),value.points.end(),[](const EnvPoint& p){return !p.x&&!p.y;});
    }
    static bool untouchedInstrument(const Instrument& value) {
        if(!value.name.empty()||value.fadeout||value.vibratoType||value.vibratoSweep||value.vibratoDepth||value.vibratoRate||
            !untouchedEnvelope(value.volumeEnvelope)||!untouchedEnvelope(value.panningEnvelope))return false;
        if(std::any_of(value.noteToSample.begin(),value.noteToSample.end(),[](std::uint8_t n){return n!=0;})||
            std::any_of(value.extension.begin(),value.extension.end(),[](std::uint8_t n){return n!=0;}))return false;
        return std::all_of(value.samples.begin(),value.samples.end(),[](const Sample& s){
            return s.pcm.empty()&&s.name.empty()&&s.sixteenBit&&s.volume==64&&s.panning==128&&
                !s.loopType&&!s.loopStart&&!s.loopLength&&!s.relativeNote&&!s.finetune&&!s.reserved;
        });
    }
    int freeChopInstrument() const {
        std::array<bool,129> referenced{};
        for(const auto& p:doc.patterns)for(const auto& c:p.cells)
            if(c.instrument<=128)referenced[c.instrument]=true;
        for(int id=1;id<=128;++id) {
            if(id==analysisInstrument||referenced[std::size_t(id)])continue;
            if(std::size_t(id)>doc.instruments.size()||untouchedInstrument(doc.instruments[std::size_t(id-1)]))return id;
        }
        return 0;
    }
    void captureAnalysisSource() {
        finishGesture();
        const auto* source=currentSampleChecked();
        const auto range=selectedFrames();
        if(range.first>=range.last||range.last>source->pcm.size())throw std::runtime_error("SELECT NONEMPTY PCM FIRST");
        clearAnalysisDrafts();
        analysisSource=std::make_shared<Sample>(*source);analysisRange=range;
        analysisInstrument=instrument;analysisSample=sample;analysisRevision=documentRevision;
        playback->stopPreview();
    }
    void openPitchDetection() {
        captureAnalysisSource();pitchEstimate={};pitchField=0;
        auto result=std::make_shared<edit::PitchEstimate>();
        const auto source=analysisSource;const auto range=analysisRange;
        startWorker("DETECTING SAMPLE PITCH",[source,range,result]{*result=edit::detectSamplePitch(*source,range);},[this,result]{
            requireAnalysisSource();pitchEstimate=*result;mode=Mode::PitchResult;
        });
    }
    void listenAnalysisRange(std::size_t first,std::size_t last) {
        requireAnalysisSource();playback->stopPreview();
        playback->previewSample(analysisInstrument,analysisSample,49,int(first),int(last));
    }
    void applyDetectedPitch() {
        requireAnalysisSource();
        if(!pitchEstimate.reliable){notify("NO STABLE PITCH; SELECT A SUSTAINED SINGLE NOTE",true);return;}
        const int nextNote=pitchEstimate.relativeNote,nextFine=pitchEstimate.finetune;
        playback->stopPreview();
        mutate([&]{auto& target=doc.instruments.at(std::size_t(analysisInstrument-1)).samples.at(std::size_t(analysisSample));
            target.relativeNote=std::int8_t(nextNote);target.finetune=std::int8_t(nextFine);});
        mode=Mode::Normal;clearAnalysisDrafts();notify("TUNED TO C-4 / PCM KEPT / UNDO AVAILABLE");
    }
    void cacheAnalysisWave() {
        const auto& pcm=analysisSource->pcm;const auto length=analysisRange.last-analysisRange.first;
        for(std::size_t x=0;x<analysisWave.size();++x) {
            const auto first=analysisRange.first+length*x/analysisWave.size();
            const auto last=std::min(analysisRange.last,std::max(first+1,analysisRange.first+length*(x+1)/analysisWave.size()));
            auto low=pcm[first],high=pcm[first];
            const auto step=std::max<std::size_t>(1,(last-first)/48);
            for(auto frame=first;frame<last;frame+=step){low=std::min(low,pcm[frame]);high=std::max(high,pcm[frame]);}
            analysisWave[x]={low,high};
        }
    }
    void openSampleChop() {
        captureAnalysisSource();
        if(analysisRange.last-analysisRange.first<2){clearAnalysisDrafts();throw std::runtime_error("CHOP NEEDS AT LEAST TWO PCM FRAMES");}
        chopCount=std::clamp(chopCount,1,int(std::min<std::size_t>(16,analysisRange.last-analysisRange.first)));
        chopField=chopSlice=0;chopCurrent=false;chopSnap=false;chopNewInstrument=freeChopInstrument();
        chopRowColumns.fill(0);
        cacheAnalysisWave();mode=Mode::ChopSettings;
    }
    void buildChopPreview() {
        requireAnalysisSource();chopDraft.reset();chopSlice=0;
        const auto source=analysisSource;const auto range=analysisRange;
        const edit::ChopOptions settings{chopCount,chopSnap,49};
        auto result=std::make_shared<edit::ChopResult>();
        startWorker("BUILDING CHOP PREVIEW",[source,range,settings,result]{*result=edit::chopSample(*source,range,settings);},[this,result]{
            requireAnalysisSource();chopDraft=ChopDraft{std::move(result->boundaries),result->snappedBoundaries,false};
            chopField=4;chopRowColumns[3]=0;mode=Mode::ChopSettings;
        });
    }
    int chopSliceCount() const {return chopDraft?int(chopDraft->boundaries.size())-1:chopCount;}
    bool requireChopDraft() {
        requireAnalysisSource();
        if(chopDraft)return true;
        notify("BUILD PREVIEW BEFORE EDITING CUTS",true);return false;
    }
    void changedChopCuts() {
        playback->stopPreview();chopCount=chopSliceCount();
        chopSlice=std::clamp(chopSlice,0,chopCount-1);
        chopDraft->manual=true;chopDraft->snappedBoundaries=0;
    }
    void moveChopCut(int direction,bool coarse) {
        if(!requireChopDraft())return;
        auto& cuts=chopDraft->boundaries;const auto cut=std::size_t(chopSlice+1);
        if(cut+1>=cuts.size()){notify("ENDPOINT IS FIXED / SELECT AN EARLIER SLICE");return;}
        const auto step=coarse?std::max<std::size_t>(1,(analysisRange.last-analysisRange.first)/100):1;
        const auto next=std::size_t(std::clamp(std::int64_t(cuts[cut])+std::int64_t(direction)*std::int64_t(step),
            std::int64_t(cuts[cut-1]+1),std::int64_t(cuts[cut+1]-1)));
        if(next!=cuts[cut]){cuts[cut]=next;changedChopCuts();}
    }
    void addChopCut() {
        if(!requireChopDraft())return;
        if(chopSliceCount()>=16){notify("MAXIMUM 16 SLICES",true);return;}
        auto& cuts=chopDraft->boundaries;const auto cut=std::size_t(chopSlice+1);
        const auto length=cuts[cut]-cuts[cut-1];
        if(length<2){notify("SLICE IS TOO SHORT TO SPLIT",true);return;}
        cuts.insert(cuts.begin()+std::ptrdiff_t(cut),cuts[cut-1]+length/2);
        changedChopCuts();notify("CUT ADDED / ADJUST CUT FRAME");
    }
    void removeChopCut() {
        if(!requireChopDraft())return;
        auto& cuts=chopDraft->boundaries;const auto cut=std::size_t(chopSlice+1);
        if(cut+1>=cuts.size()){notify("ENDPOINT IS FIXED / SELECT AN EARLIER SLICE");return;}
        cuts.erase(cuts.begin()+std::ptrdiff_t(cut));
        changedChopCuts();notify("CUT REMOVED / ADJACENT SLICES MERGED");
    }
    void navigateChop(Button b) {
        int selectedRow=0,selectedColumn=0;
        for(std::size_t r=0;r<chopRows.size();++r)for(int c=0;c<2;++c)
            if(chopRows[r][std::size_t(c)]==chopField){selectedRow=int(r);selectedColumn=c;}
        chopRowColumns[std::size_t(selectedRow)]=selectedColumn;
        if(b==Button::Left||b==Button::Right) {
            const int last=chopRows[std::size_t(selectedRow)][1]<0?0:1;
            selectedColumn=std::clamp(selectedColumn+(b==Button::Right?1:-1),0,last);
        } else {
            selectedRow=std::clamp(selectedRow+(b==Button::Down?1:-1),0,int(chopRows.size())-1);
            selectedColumn=chopRowColumns[std::size_t(selectedRow)];
        }
        chopRowColumns[std::size_t(selectedRow)]=selectedColumn;
        chopField=chopRows[std::size_t(selectedRow)][std::size_t(selectedColumn)];
    }
    void applySampleChop() {
        requireAnalysisSource();
        if(!chopDraft){notify("BUILD PREVIEW BEFORE APPLYING CHOP",true);return;}
        const int destination=chopCurrent?analysisInstrument:chopNewInstrument;
        if(!destination){notify("NO EMPTY INSTRUMENT; CHOOSE CURRENT OR FREE A SLOT",true);return;}
        const Instrument empty;
        const auto& current=chopCurrent?doc.instruments.at(std::size_t(analysisInstrument-1)):empty;
        const bool replace=chopCurrent&&edit::emptySampleSlots(current)<chopSliceCount();
        const auto result=edit::chopSampleAtBoundaries(*analysisSource,analysisRange,chopDraft->boundaries);
        auto plan=std::make_shared<edit::ChopPlacement>(edit::placeChop(current,result,replace));
        if(!chopCurrent||replace)plan->instrument.name=analysisSource->name.empty()?"CHOP":text::truncateBytes(analysisSource->name,16)+" CHOP";
        // Match the document codec's aggregate PCM budget before offering apply.
        std::size_t total=0;
        for(std::size_t i=0;i<doc.instruments.size();++i)if(i!=std::size_t(destination-1))
            for(const auto& s:doc.instruments[i].samples)total+=s.pcm.size();
        for(const auto& s:plan->instrument.samples)total+=s.pcm.size();
        if(total>32u*1024u*1024u)throw std::runtime_error("CHOP WOULD EXCEED THE SONG'S 32M FRAME LIMIT");
        const int sourceInstrument=analysisInstrument,sourceSample=analysisSample;
        const int firstFrame=int(analysisRange.first),lastFrame=int(analysisRange.last);
        auto apply=[this,plan,destination,sourceInstrument,sourceSample,firstFrame,lastFrame]{
            requireAnalysisSource();playback->stopPreview();
            const int firstSlot=plan->slots.front(),count=int(plan->slots.size());
            const auto previousRevision=documentRevision;
            mutate([&]{
                if(doc.instruments.size()<std::size_t(destination))doc.instruments.resize(std::size_t(destination));
                doc.instruments[std::size_t(destination-1)]=std::move(plan->instrument);
            });
            instrument=destination;sample=firstSlot;keymapNote=49;selectionStart=selectionEnd=0;waveformZoom=false;
            if(documentRevision!=previousRevision&&!undoStack.empty()) {
                auto& change=undoStack.back();
                change.undo=[this,undoDocument=change.undo,sourceInstrument,sourceSample,firstFrame,lastFrame](Document& d){
                    undoDocument(d);instrument=sourceInstrument;sample=sourceSample;selectionStart=firstFrame;selectionEnd=lastFrame;waveformZoom=false;
                };
                change.redo=[this,redoDocument=change.redo,destination,firstSlot](Document& d){
                    redoDocument(d);instrument=destination;sample=firstSlot;keymapNote=49;selectionStart=selectionEnd=0;waveformZoom=false;
                };
            }
            mode=Mode::Normal;clearAnalysisDrafts();notify(number(count)+" SLICES IN INSTRUMENT "+hex(destination)+" / C-4 UP / UNDO AVAILABLE");
        };
        if(replace) {
            confirm("REPLACE CURRENT INSTRUMENT "+hex(destination)+"?",
                "REPLACE ALL SAMPLES, KEY MAP AND ENVELOPES. UNDO RESTORES THEM.",std::move(apply));
            confirmCancelMode=Mode::ChopSettings;
        } else apply();
    }
    bool analysisButton(Button b,bool repeating) {
        if(mode!=Mode::PitchResult&&mode!=Mode::ChopSettings)return false;
        if(b==Button::B){playback->stopPreview();clearAnalysisDrafts();mode=Mode::Normal;return true;}
        if(mode==Mode::ChopSettings&&b==Button::Start) {
            if(!repeating) {
                if(playback->previewPlaying())playback->stopPreview();
                else if(!chopDraft)notify("BUILD PREVIEW BEFORE LISTENING",true);
                else listenAnalysisRange(chopDraft->boundaries[std::size_t(chopSlice)],chopDraft->boundaries[std::size_t(chopSlice+1)]);
            }
            return true;
        }
        if(int(b)>=4)return true;
        const bool editing=held[std::size_t(Button::A)];
        const int sign=(b==Button::Right||b==Button::Up)?1:-1;
        if(editing)aUsed=true;
        if(mode==Mode::PitchResult) {
            if(!editing&&(b==Button::Up||b==Button::Down))
                pitchField=std::clamp(pitchField+(b==Button::Down?1:-1),0,1);
            return true;
        }
        if(!editing) {
            navigateChop(b);
        }
        else if(chopField==0||chopField==1) {
            const int previousCount=chopCount;const bool previousSnap=chopSnap;
            if(chopField==0)chopCount=std::clamp(chopCount+sign,1,int(std::min<std::size_t>(16,analysisRange.last-analysisRange.first)));
            else chopSnap=sign>0;
            if(chopCount!=previousCount||chopSnap!=previousSnap){chopDraft.reset();chopSlice=0;playback->stopPreview();}
        } else if(chopField==2)chopCurrent=sign>0;
        else if(chopField==4&&chopDraft){
            const int next=std::clamp(chopSlice+sign,0,chopSliceCount()-1);
            if(next!=chopSlice){chopSlice=next;playback->stopPreview();}
        } else if(chopField==7)moveChopCut(sign,b==Button::Up||b==Button::Down);
        return true;
    }
    bool analysisRelease(Button b) {
        if(mode!=Mode::PitchResult&&mode!=Mode::ChopSettings)return false;
        if(b!=Button::A||aUsed)return true;
        aUsed=true;
        if(mode==Mode::PitchResult) {
            if(pitchField==0)listenAnalysisRange(analysisRange.first,analysisRange.last);else applyDetectedPitch();
        } else if(chopField==3)buildChopPreview();
        else if(chopField==6)applySampleChop();
        else if(chopField==8)addChopCut();
        else if(chopField==9)removeChopCut();
        return true;
    }
    static std::string decimalValue(double value,int places=1) {
        char result[64];std::snprintf(result,sizeof(result),"%.*f",places,value);return result;
    }
    void drawAnalysisModes() {
        if(mode==Mode::PitchResult) {
            const std::string_view hints=pitchField==0?"A LISTEN / B BACK":"A APPLY / B BACK";
            drawToolPanel("SAMPLE PITCH",540,hints);
            txt(100,157,"ANALYZED: "+number(analysisRange.last-analysisRange.first)+" FRAMES / C-4 PLAYBACK",muted);
            txt(100,213,pitchEstimate.reliable?noteName(pitchEstimate.note)+"  "+
                (pitchEstimate.cents>=0?"+":"")+decimalValue(pitchEstimate.cents)+" CENTS":"NO STABLE PITCH",pitchEstimate.reliable?amber:red,3);
            txt(100,272,"FREQUENCY "+decimalValue(pitchEstimate.frequency)+" HZ   CONFIDENCE "+
                number(int(std::lround(pitchEstimate.confidence*100)))+"%",muted);
            txt(100,324,"TUNE SAMPLE TO C-4",ink);
            txt(100,367,pitchEstimate.reliable?"RELATIVE NOTE "+number(pitchEstimate.relativeNote)+"   FINETUNE "+number(pitchEstimate.finetune):
                "SELECT A STEADY SINGLE NOTE AND ANALYZE AGAIN.",cyan);
            txt(100,416,"TUNING CHANGES PLAYBACK SPEED. PCM IS UNCHANGED.",muted);
            labelValue(100,450,"LISTEN SOURCE","",pitchField==0,824);
            labelValue(100,502,"APPLY TUNING",pitchEstimate.reliable?"":"UNAVAILABLE",pitchField==1,824);
            drawToolFooter(hints);
        }
        if(mode!=Mode::ChopSettings||!analysisSource)return;
        const std::string hints=std::string(chopField==3?"A BUILD PREVIEW / B CANCEL":
            chopField==6?"A APPLY CHOP / B CANCEL":
            chopField==8?"A SPLIT SELECTED SLICE / B CANCEL":chopField==9?"A MERGE WITH NEXT SLICE / B CANCEL":
            chopField==7?"A+LEFT/RIGHT 1 FRAME / A+UP/DOWN COARSE / B CANCEL":"A+DPAD EDIT / B CANCEL")+" / START LISTEN/STOP";
        drawToolPanel("CHOP SAMPLE",584,hints);
        const int x=100,y=150,width=824,height=136;
        rect(x,y,width,height,bg);line(x,y+height/2,x+width,y+height/2,dim);
        const auto length=analysisRange.last-analysisRange.first;
        auto position=[&](std::size_t frame){return x+int((frame-analysisRange.first)*std::size_t(width-1)/length);};
        if(chopDraft) {
            const int first=position(chopDraft->boundaries[std::size_t(chopSlice)]);
            const int last=position(chopDraft->boundaries[std::size_t(chopSlice+1)]);
            rect(first,y,std::max(1,last-first),height,panel2);
        }
        for(int n=0;n<width;++n) {
            const auto values=analysisWave[std::size_t(n)];
            line(x+n,y+height/2-int(values.second)*(height/2-5)/32768,
                x+n,y+height/2-int(values.first)*(height/2-5)/32768,cyan);
        }
        for(int n=0;n<=chopCount;++n) {
            const auto frame=chopDraft?chopDraft->boundaries[std::size_t(n)]:analysisRange.first+length*std::size_t(n)/std::size_t(chopCount);
            const int px=position(frame);line(px,y,px,y+height,chopDraft?amber:dim);
        }
        if(chopDraft) {
            const int px=position(chopDraft->boundaries[std::size_t(chopSlice+1)]);
            rect(px-2,y-5,5,5,amber);
        }
        const auto preview=playback->samplePreviewStatus();
        if(preview.active&&preview.instrument==analysisInstrument&&preview.sample==analysisSample&&
            preview.frame>=analysisRange.first&&preview.frame<analysisRange.last) {
            const int px=position(preview.frame);line(px,y,px,y+height,ink);rect(px-2,y+height,5,5,ink);
        }
        txt(100,299,"FRAMES "+number(analysisRange.first)+"-"+number(analysisRange.last)+" / "+
            (chopDraft&&chopDraft->manual?"MANUAL CUTS":"C-4 TO "+noteName(48+chopCount)),muted);
        const int free=edit::emptySampleSlots(doc.instruments.at(std::size_t(analysisInstrument-1)));
        const std::string target=chopCurrent?"CURRENT "+hex(analysisInstrument)+" / "+number(free)+" FREE":
            chopNewInstrument?"NEW "+hex(chopNewInstrument):"NEW / NO EMPTY INSTRUMENT";
        labelValue(100,326,"AUTO SLICES",number(chopCount),chopField==0,400);
        labelValue(512,326,"BOUNDARIES",chopSnap?"ATTACK SNAP":"EQUAL",chopField==1,412);
        labelValue(100,370,"DESTINATION",target,chopField==2,824);
        labelValue(100,414,chopDraft?"REBUILD PREVIEW":"BUILD PREVIEW",chopDraft?"REPLACE CUTS":"REQUIRED",chopField==3,824);
        labelValue(100,458,"SLICE",chopDraft?number(chopSlice+1)+" / "+number(chopCount):"--",chopField==4,400);
        const auto cutFrame=chopDraft?number(chopDraft->boundaries[std::size_t(chopSlice+1)]):"--";
        labelValue(512,458,"CUT FRAME",chopDraft&&chopSlice+1==chopCount?"END FIXED":cutFrame,chopField==7,412);
        drawButton(100,502,400,"ADD CUT",chopField==8);drawButton(512,502,412,"REMOVE CUT",chopField==9);
        drawButton(100,546,824,"APPLY CHOP",chopField==6);
        drawToolFooter(hints);
    }
