// Included inside TrackerApp::Impl. Analysis is read-only until an explicit apply.
    std::shared_ptr<const Sample> analysisSource;
    edit::FrameRange analysisRange{};
    std::uint64_t analysisRevision=0;
    int analysisInstrument=1,analysisSample=0,pitchField=0;
    edit::PitchEstimate pitchEstimate;
    std::optional<edit::ChopResult> chopDraft;
    int chopCount=8,chopField=0,chopSlice=0,chopNewInstrument=0;
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
        chopCount=std::clamp(chopCount,2,int(std::min<std::size_t>(16,analysisRange.last-analysisRange.first)));
        chopField=chopSlice=0;chopCurrent=false;chopSnap=false;chopNewInstrument=freeChopInstrument();
        cacheAnalysisWave();mode=Mode::ChopSettings;
    }
    void buildChopPreview() {
        requireAnalysisSource();chopDraft.reset();chopSlice=0;
        const auto source=analysisSource;const auto range=analysisRange;
        const edit::ChopOptions settings{chopCount,chopSnap,49};
        auto result=std::make_shared<edit::ChopResult>();
        startWorker("BUILDING CHOP PREVIEW",[source,range,settings,result]{*result=edit::chopSample(*source,range,settings);},[this,result]{
            requireAnalysisSource();chopDraft=std::move(*result);chopField=4;mode=Mode::ChopSettings;
        });
    }
    void applySampleChop() {
        requireAnalysisSource();
        if(!chopDraft){notify("BUILD PREVIEW BEFORE APPLYING CHOP",true);return;}
        const int destination=chopCurrent?analysisInstrument:chopNewInstrument;
        if(!destination){notify("NO EMPTY INSTRUMENT; CHOOSE CURRENT OR FREE A SLOT",true);return;}
        const Instrument empty;
        const auto& current=chopCurrent?doc.instruments.at(std::size_t(analysisInstrument-1)):empty;
        const bool replace=chopCurrent&&edit::emptySampleSlots(current)<int(chopDraft->slices.size());
        auto plan=std::make_shared<edit::ChopPlacement>(edit::placeChop(current,*chopDraft,replace));
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
    bool analysisButton(Button b,bool /*repeating*/) {
        if(mode!=Mode::PitchResult&&mode!=Mode::ChopSettings)return false;
        if(b==Button::B){playback->stopPreview();clearAnalysisDrafts();mode=Mode::Normal;return true;}
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
            if(b==Button::Up||b==Button::Down)chopField=std::clamp(chopField+(b==Button::Down?1:-1),0,6);
        }
        else if(chopField==0||chopField==1) {
            const int previousCount=chopCount;const bool previousSnap=chopSnap;
            if(chopField==0)chopCount=std::clamp(chopCount+sign,2,int(std::min<std::size_t>(16,analysisRange.last-analysisRange.first)));
            else chopSnap=sign>0;
            if(chopCount!=previousCount||chopSnap!=previousSnap){chopDraft.reset();chopSlice=0;playback->stopPreview();}
        } else if(chopField==2)chopCurrent=sign>0;
        else if(chopField==4&&chopDraft)chopSlice=std::clamp(chopSlice+sign,0,int(chopDraft->slices.size())-1);
        return true;
    }
    bool analysisRelease(Button b) {
        if(mode!=Mode::PitchResult&&mode!=Mode::ChopSettings)return false;
        if(b!=Button::A||aUsed)return true;
        aUsed=true;
        if(mode==Mode::PitchResult) {
            if(pitchField==0)listenAnalysisRange(analysisRange.first,analysisRange.last);else applyDetectedPitch();
        } else if(chopField==3)buildChopPreview();
        else if(chopField==5) {
            if(!chopDraft){notify("BUILD PREVIEW BEFORE LISTENING",true);return true;}
            listenAnalysisRange(chopDraft->boundaries[std::size_t(chopSlice)],chopDraft->boundaries[std::size_t(chopSlice+1)]);
        } else if(chopField==6)applySampleChop();
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
        const std::string_view hints=chopField==3?"A BUILD PREVIEW / B CANCEL":
            chopField==5?"A LISTEN / B CANCEL":chopField==6?"A APPLY CHOP / B CANCEL":"A+DPAD EDIT / B CANCEL";
        drawToolPanel("CHOP SAMPLE",634,hints);
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
        txt(100,299,"FRAMES "+number(analysisRange.first)+"-"+number(analysisRange.last)+" / C-4 TO "+noteName(48+chopCount),muted);
        txt(100,324,chopCurrent?"FILLED SLOTS USE CURRENT INSTRUMENT SETTINGS.":"ORIGINAL KEPT.",muted);
        const int free=edit::emptySampleSlots(doc.instruments.at(std::size_t(analysisInstrument-1)));
        const std::string target=chopCurrent?"CURRENT "+hex(analysisInstrument)+" / "+number(free)+" FREE":
            chopNewInstrument?"NEW "+hex(chopNewInstrument):"NEW / NO EMPTY INSTRUMENT";
        const std::array<std::string,7> labels={"SLICE COUNT","BOUNDARIES","DESTINATION","BUILD PREVIEW","SELECT SLICE","LISTEN SLICE","APPLY CHOP"};
        const std::array<std::string,7> values={number(chopCount),chopSnap?"SNAP TO ATTACK":"EXACT EQUAL",target,
            chopDraft?number(chopDraft->snappedBoundaries)+" BOUNDARIES SNAPPED":"REQUIRED",
            chopDraft?number(chopSlice+1)+" / "+number(chopCount):"--","",
            chopCurrent?(free<chopCount?"REPLACE WHOLE INSTRUMENT":"FILL EMPTY SLOTS"):"CREATE NEW INSTRUMENT"};
        for(int n=0;n<7;++n)labelValue(100,350+n*41,labels[std::size_t(n)],values[std::size_t(n)],chopField==n,824);
        drawToolFooter(hints);
    }
