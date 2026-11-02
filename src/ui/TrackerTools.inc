// Included inside TrackerApp::Impl: editor commands and modal tools.
    template<class Fn> void mutate(Fn fn,std::string label={}) {
        finishGesture();auto before=std::make_shared<Document>(doc);
        if(label.empty())label=contextEditLabel();
        try { fn();documentChange(before,std::move(label)); }
        catch (...) {doc=*before;normalize();throw;}
    }
    Pattern& editablePattern() {
        const auto p=std::size_t(patternIndex());
        while(doc.patterns.size()<=p){Pattern empty;empty.cells.resize(std::size_t(doc.channels)*64);doc.patterns.push_back(std::move(empty));}
        return doc.patterns[p];
    }
    Instrument& editableInstrument(bool withSample=false) {
        if(instrument<1||instrument>128)throw std::out_of_range("Instrument slot");
        if(doc.instruments.size()<std::size_t(instrument))doc.instruments.resize(std::size_t(instrument));
        auto& result=doc.instruments[std::size_t(instrument-1)];
        // XM only writes envelope/vibrato metadata when a sample header exists.
        if(withSample&&result.samples.empty())result.samples.emplace_back();
        return result;
    }
    Sample& editableSample() {
        if(sample<0||sample>15)throw std::out_of_range("Sample slot");
        auto& i=editableInstrument();
        if(i.samples.size()<=std::size_t(sample))i.samples.resize(std::size_t(sample+1));
        return i.samples[std::size_t(sample)];
    }
    template<class Fn> void mutatePattern(Fn fn,std::string label={}) {
        if(label.empty())label=contextEditLabel();
        if(std::size_t(patternIndex())<doc.patterns.size()) {
            mutate([&]{fn(editablePattern());},std::move(label));return;
        }
        Pattern draft=blankPattern;
        fn(draft);
        const bool changed=draft.rows!=64||std::any_of(draft.cells.begin(),draft.cells.end(),[](const Cell& c){
            return c.note||c.instrument||c.volume||c.effect||c.param;
        });
        if(changed)mutate([&]{editablePattern()=std::move(draft);},std::move(label));
    }
    edit::BlockRange selectedBlock() const {
        if(blockSelected&&blockPattern==patternIndex()) {
            const int first=std::min(channel*5+field,blockChannel*5+blockField);
            const int last=std::max(channel*5+field,blockChannel*5+blockField);
            return {std::size_t(std::min(row,blockRow)),std::size_t(std::max(row,blockRow)+1),
                std::size_t(first/5),std::size_t(last/5+1),std::size_t(first%5),std::size_t(last%5+1)};
        }
        return {std::size_t(row),std::size_t(row+1),std::size_t(channel),std::size_t(channel+1)};
    }
    edit::FrameRange selectedFrames(bool wholeIfEmpty=true) {
        auto* s=currentSample();if(!s)return {0,0};
        if(selectionEnd==selectionStart&&wholeIfEmpty)return edit::wholeSample(*s);
        return {std::size_t(selectionStart),std::size_t(selectionEnd)};
    }
    void copyPattern() {
        if(blockSelected&&blockPattern==patternIndex()){
            blockClipboard=edit::copyBlock(pattern(),doc.channels,selectedBlock());clipboard.reset();
            copiedMask=copyMask;
            notify("BLOCK COPIED / "+number(blockClipboard->rows)+" ROWS / "+number(blockClipboard->channels)+" CHANNELS");
        }else{clipboard=cell();blockClipboard.reset();copiedMask=copyMask;notify("CELL COPIED / SELECT+A PASTE");}
    }
    void pastePattern() {
        const auto mask=activePasteMask();
        if(blockClipboard){mutatePattern([&](Pattern& p){edit::pasteMaskedBlock(p,doc.channels,std::size_t(row),std::size_t(channel),*blockClipboard,mask);},"PASTE");notify("BLOCK PASTED");}
        else if(clipboard){editCell(edit::mergeCell(cell(),*clipboard,mask),"PASTE");notify("CELL PASTED");}
        else notify("SELECT+B, THEN B TO COPY FIRST");
    }
    void transformPcm(edit::SampleTransform operation,double gain=1.0) {
        const auto range=selectedFrames();mutate([&]{edit::transformSample(*currentSample(),range,operation,gain);});notify("SAMPLE EDIT APPLIED / UNDO AVAILABLE");
    }
    void openNumber(std::string title,int value,int lo,int hi,std::function<void(int)> apply,int coarse=16) {
        numberTitle=std::move(title);numberValue=value;numberMin=lo;numberMax=hi;numberStep=coarse;numberCommit=std::move(apply);numberField=0;mode=Mode::Number;
    }
    void rename(NameTarget target) {
        nameTarget=target;
        if(target==NameTarget::Song)filename=doc.name;
        else if(target==NameTarget::Instrument)filename=currentInstrument()?currentInstrument()->name:"";
        else filename=currentSample()?currentSample()->name:"";
        nameCursor=0;nameRowColumn.fill(-1);mode=Mode::Name;
    }
    void enterMenu(std::string title,std::vector<Action> next) {
        menuParents.push_back({actions,menuIndex,menuTitle});actions=std::move(next);menuIndex=0;menuTitle=std::move(title);mode=Mode::Menu;
    }
    void backMenu() {
        if(menuParents.empty()){mode=Mode::Normal;return;}
        auto back=std::move(menuParents.back());menuParents.pop_back();actions=std::move(back.actions);menuIndex=back.index;menuTitle=std::move(back.title);mode=Mode::Menu;
    }
    void buildToolMenus() {
        // Keep command implementations shared by menus and deterministic UI tests.
        std::vector<Action> original=std::move(actions);
        auto take=[&](std::initializer_list<const char*> labels){
            std::vector<Action> result;
            for(auto label:labels){auto found=std::find_if(original.begin(),original.end(),[&](const Action& a){return a.label==label;});if(found!=original.end())result.push_back(*found);}
            return result;
        };
        auto add=[](std::vector<Action>& list,std::string title,std::string detail,std::function<void()> fn){list.push_back({std::move(title),std::move(detail),std::move(fn)});};
        auto project=take({"SAVE XM","SAVE XM AS","OPEN XM","RENDER WAV","CLEAN UP PATTERNS","CLEAN UP INSTRUMENTS","NEW SONG"});
        add(project,"RENAME SONG","20 BYTE XM SONG TITLE / ASCII INPUT",[this]{rename(NameTarget::Song);});
        add(project,"MINIMISE ALL INSTRUMENTS","TRIM ALL SAMPLE TAILS AFTER LOOP END",[this]{minimiseSamples(2);});
        auto song=take({"INSERT ORDER","DUPLICATE PATTERN","NEW EMPTY PATTERN","MOVE ORDER UP","MOVE ORDER DOWN"});
        auto patternTools=take({"CLEAR FIELD"});
        add(patternTools,"MARK BLOCK START","MOVE CURSOR, THEN B COPIES THE RECTANGLE",[this]{blockSelected=true;blockRow=row;blockChannel=channel;blockField=field;blockPattern=patternIndex();notify("BLOCK MARKED / MOVE CURSOR / B COPY");});
        add(patternTools,"SELECT WHOLE PATTERN","SELECT EVERY ROW AND CHANNEL",[this]{blockSelected=true;blockRow=0;blockChannel=0;blockField=0;blockPattern=patternIndex();row=pattern().rows-1;channel=doc.channels-1;field=4;notify("WHOLE PATTERN SELECTED");});
        add(patternTools,"CANCEL BLOCK SELECTION","RETURN TO SINGLE CELL EDITING",[this]{blockSelected=false;notify("BLOCK SELECTION CLEARED");});
        add(patternTools,"COPY BLOCK","COPY SELECTED FIELDS USING THE COPY MASK",[this]{copyPattern();});
        add(patternTools,"PASTE BLOCK","PASTE AT CURSOR; OVERFLOW IS REJECTED",[this]{pastePattern();});
        add(patternTools,"CLEAR BLOCK","CLEAR SELECTED CELLS; UNDO AVAILABLE",[this]{const auto range=selectedBlock();mutatePattern([&](Pattern& p){edit::clearBlock(p,doc.channels,range);});});
        for(int semitones:{-12,-1,1,12})add(patternTools,"TRANSPOSE "+std::string(semitones>0?"+":"")+number(semitones),"TRANSPOSE NOTES IN SELECTION; PRESERVE EFFECTS",[this,semitones]{const auto range=selectedBlock();mutatePattern([&](Pattern& p){edit::transposeBlock(p,doc.channels,range,semitones);});});
        add(patternTools,"INSERT ROW","SHIFT SELECTED CHANNELS DOWN FROM CURSOR",[this]{insertPatternRow(false);});
        add(patternTools,"DELETE ROW","SHIFT SELECTED CHANNELS UP FROM CURSOR",[this]{const auto range=selectedBlock();mutatePattern([&](Pattern& p){edit::deleteRow(p,doc.channels,row,range.firstChannel,range.lastChannel);});});

        auto instrumentTools=take({"IMPORT XI","EXPORT XI"});
        add(instrumentTools,"MINIMISE INSTRUMENT","TRIM THIS INSTRUMENT'S SAMPLE TAILS AFTER LOOP END",[this]{minimiseSamples(1);});
        add(instrumentTools,"RENAME INSTRUMENT","22 BYTE XI / XM NAME / ASCII INPUT",[this]{rename(NameTarget::Instrument);});
        add(instrumentTools,"VOLUME ENVELOPE","EDIT POINTS, SUSTAIN AND LOOP MARKERS",[this]{openEnvelope(false);});
        add(instrumentTools,"PANNING ENVELOPE","EDIT POINTS, SUSTAIN AND LOOP MARKERS",[this]{openEnvelope(true);});
        add(instrumentTools,"MAP RANGE FIRST NOTE","",[this]{openNumber("MAP FIRST NOTE (1=C-0)",mapFirst,1,96,[this](int n){mapFirst=n;mapLast=std::max(mapLast,n);},12);});
        add(instrumentTools,"MAP RANGE LAST NOTE","",[this]{openNumber("MAP LAST NOTE (96=B-7)",mapLast,mapFirst,96,[this](int n){mapLast=n;},12);});
        add(instrumentTools,"ASSIGN RANGE TO SAMPLE","MAP CHOSEN NOTE RANGE TO THE CURRENT SAMPLE SLOT",[this]{
            mutate([&]{editableSample();for(int n=mapFirst;n<=mapLast;++n)currentInstrument()->noteToSample[n-1]=std::uint8_t(sample);});notify(noteName(mapFirst)+" TO "+noteName(mapLast)+" MAPPED TO SAMPLE "+hex(sample));
        });
        auto sampleTools=take({"IMPORT AUDIO","TRIM SAMPLE TO SELECTION"});
        add(sampleTools,"DELETE SAMPLE","CLEAR THIS SLOT; KEEP OTHER SAMPLES AND KEY MAP",[this]{deleteCurrentSample();});
        add(sampleTools,"MINIMISE SAMPLE","REMOVE PCM AFTER LOOP END / IGNORE SELECTION",[this]{minimiseSamples(0);});
        add(sampleTools,"EXPORT SAMPLE WAV","SAVE THE WHOLE SAMPLE OR SELECTED PCM RANGE",[this]{openSampleExport();});
        add(sampleTools,"RENAME SAMPLE","22 BYTE XM SAMPLE NAME / ASCII INPUT",[this]{rename(NameTarget::Sample);});
        add(sampleTools,"SELECT WHOLE SAMPLE","SELECT EVERY PCM FRAME",[this]{auto* s=currentSample();if(!s)throw std::runtime_error("NO SAMPLE");selectionStart=0;selectionEnd=int(s->pcm.size());});
        add(sampleTools,"ZOOM TO SELECTION","",[this]{if(selectionEnd<=selectionStart)throw std::runtime_error("SELECT A PCM RANGE FIRST");waveformZoom=true;});
        add(sampleTools,"SHOW WHOLE WAVEFORM","",[this]{waveformZoom=false;});
        add(sampleTools,"COPY PCM","COPY SELECTION; EMPTY SELECTION MEANS ALL",[this]{pcmClipboard=edit::copyPcm(*currentSampleChecked(),selectedFrames());notify("PCM COPIED");});
        add(sampleTools,"CUT PCM","COPY THEN DELETE SELECTED FRAMES",[this]{const auto range=selectedFrames();pcmClipboard=edit::copyPcm(*currentSample(),range);mutate([&]{edit::deletePcm(*currentSample(),range);});selectionEnd=selectionStart;});
        add(sampleTools,"PASTE PCM","REPLACE SELECTION OR INSERT AT ITS START",[this]{if(!pcmClipboard)throw std::runtime_error("COPY PCM FIRST");const auto range=selectedFrames(false);mutate([&]{edit::pastePcm(*currentSample(),range,*pcmClipboard);});selectionEnd=selectionStart+int(pcmClipboard->pcm.size());normalize();});
        add(sampleTools,"DELETE PCM","DELETE SELECTED FRAMES; UNDO AVAILABLE",[this]{const auto range=selectedFrames();mutate([&]{edit::deletePcm(*currentSample(),range);});selectionEnd=selectionStart;});
        const std::array<std::pair<const char*,edit::SampleTransform>,7> transforms={{{"REVERSE",edit::SampleTransform::Reverse},{"NORMALIZE",edit::SampleTransform::Normalize},{"FADE IN",edit::SampleTransform::FadeIn},{"FADE OUT",edit::SampleTransform::FadeOut},{"SILENCE",edit::SampleTransform::Silence},{"INVERT POLARITY",edit::SampleTransform::Invert},{"REMOVE DC",edit::SampleTransform::RemoveDc}}};
        for(const auto& t:transforms)add(sampleTools,t.first,"APPLY TO SELECTION; EMPTY SELECTION MEANS ALL",[this,op=t.second]{transformPcm(op);});
        add(sampleTools,"GAIN PERCENT","100 = ORIGINAL / 200 = DOUBLE AMPLITUDE",[this]{openNumber("GAIN PERCENT",100,0,1000,[this](int n){transformPcm(edit::SampleTransform::Gain,double(n)/100);},10);});
        add(sampleTools,"CONVERT TO 8-BIT","QUANTIZE PCM TO 8-BIT PRECISION",[this]{currentSampleChecked();mutate([&]{edit::convertSampleDepth(*currentSample(),false);});});
        add(sampleTools,"CONVERT TO 16-BIT","",[this]{currentSampleChecked();mutate([&]{edit::convertSampleDepth(*currentSample(),true);});});
        add(sampleTools,"RECORD MICROPHONE","CHOOSE DEVICE; RECORDING STARTS ONLY ON A",[this]{openRecording();});
        addEditorExtras(patternTools,instrumentTools,sampleTools);
        auto select=[](const std::vector<Action>& source,std::initializer_list<const char*> labels){
            std::vector<Action> result;
            for(const auto* label:labels){auto found=std::find_if(source.begin(),source.end(),[&](const Action& a){return a.label==label;});if(found!=source.end())result.push_back(*found);}
            return result;
        };
        auto groupAction=[this](std::string title,std::string desc,std::vector<Action> contents){
            return Action{title,desc,[this,title,contents=std::move(contents)]{enterMenu(title,contents);}};
        };
        actions=take({"UNDO","REDO"});
        if(page==Page::Song){
            actions.push_back(groupAction("ORDER MANAGEMENT","INSERT / DUPLICATE / EMPTY / MOVE",song));
            actions.push_back(groupAction("PROJECT","RENDER / CLEAN UP / MINIMISE",select(project,{"RENDER WAV","CLEAN UP PATTERNS","CLEAN UP INSTRUMENTS","MINIMISE ALL INSTRUMENTS"})));
        }else if(page==Page::Pattern){
            actions.push_back({"PATTERN LIST","ALL 256 NUMBERS / OPEN OR CLEAR CONTENTS",[this]{openSlotList(false);}});
            const auto rows=select(patternTools,{"INSERT ROW","DELETE ROW","EXPAND PATTERN","SHRINK PATTERN"});
            const auto transpose=select(patternTools,{"TRANSPOSE -12","TRANSPOSE -1","TRANSPOSE +1","TRANSPOSE +12"});
            auto tools=select(patternTools,{"COPY PATTERN","PASTE PATTERN","COPY / PASTE MASKS"});
            tools.insert(tools.begin()+2,groupAction("ROWS","INSERT / DELETE / EXPAND / SHRINK",rows));
            tools.insert(tools.begin()+3,groupAction("TRANSPOSE","NOTES IN THE CURRENT SELECTION",transpose));
            actions.push_back(groupAction("PATTERN TOOLS","WHOLE PATTERN / ROW AND BLOCK OPERATIONS",std::move(tools)));
        }else if(page==Page::Instrument){
            actions.push_back({"INSTRUMENT LIST","ALL 128 NUMBERS / OPEN OR CLEAR CONTENTS",[this]{openSlotList(true);}});
            actions.push_back(groupAction("SAMPLE MANAGEMENT","CURRENT INSTRUMENT / COPY / PASTE / SWAP / DELETE",select(sampleTools,{"COPY SAMPLE SLOT","PASTE SAMPLE SLOT","SWAP SAMPLE SLOT","DELETE SAMPLE","EXPORT SAMPLE WAV"})));
            actions.push_back(groupAction("INSTRUMENT MANAGEMENT","COPY / PASTE / SWAP / EXPORT / MINIMISE",select(instrumentTools,{"COPY INSTRUMENT","PASTE INSTRUMENT","SWAP INSTRUMENT","EXPORT XI","MINIMISE INSTRUMENT"})));
            actions.push_back(groupAction("KEY MAP RANGE","ASSIGN SEVERAL KEYS AT ONCE",select(instrumentTools,{"MAP RANGE FIRST NOTE","MAP RANGE LAST NOTE","ASSIGN RANGE TO SAMPLE"})));
        }else{
            actions.push_back(groupAction("SAMPLE MANAGEMENT","COPY / PASTE / SWAP / DELETE / EXPORT / RECORD",select(sampleTools,{"COPY SAMPLE SLOT","PASTE SAMPLE SLOT","SWAP SAMPLE SLOT","DELETE SAMPLE","EXPORT SAMPLE WAV","RECORD MICROPHONE"})));
            auto processing=select(sampleTools,{"TRIM SAMPLE TO SELECTION","MINIMISE SAMPLE","REVERSE","CONVERT TO 8-BIT","CONVERT TO 16-BIT","ZOOM TO SELECTION","SHOW WHOLE WAVEFORM","EXPORT SAMPLE WAV"});
            processing.insert(processing.begin(),groupAction("AMPLITUDE","LEVEL / FADES / POLARITY / DC",select(sampleTools,{"NORMALIZE","GAIN PERCENT","FADE IN","FADE OUT","SILENCE","INVERT POLARITY","REMOVE DC"})));
            actions.push_back(groupAction("PCM PROCESSING","EDIT PCM / TRIM SELECTION / MINIMISE LOOP TAIL",std::move(processing)));
            for(const auto& a:select(sampleTools,{"ADVANCED PCM"}))actions.push_back(a);
        }
        actions.push_back({"SETTINGS","APPLICATION DISPLAY / INPUT / AUDIO",[this]{openEditorSettings();}});
        for(const auto& a:take({"CONTROLS"}))actions.push_back(a);
        if(page==Page::Song)for(const auto& a:take({"EXIT"}))actions.push_back(a);
        const char* names[]={"SONG ACTIONS","PATTERN ACTIONS","INSTRUMENT ACTIONS","SAMPLE ACTIONS"};
        menuTitle=names[int(page)];menuParents.clear();menuIndex=0;
    }
    void openSampleProcessing() {
        openMenu(true);
        const auto rootActions=actions;
        const auto group=std::find_if(rootActions.begin(),rootActions.end(),[](const Action& a){return a.label=="PCM PROCESSING";});
        if(group==rootActions.end())throw std::logic_error("Sample processing is unavailable on this screen");
        group->run();
        // The screen button opens the processing menu directly. Keep Undo/Redo
        // at the top and allow advanced tools without discarding the PCM range.
        actions.insert(actions.begin(),rootActions.begin(),rootActions.begin()+2);
        for(const auto& action:rootActions)
            if(action.label=="ADVANCED PCM")actions.push_back(action);
        menuParents.clear();menuTitle="SAMPLE PROCESS";menuIndex=0;
        pcmSelecting=false;pcmSelectStage=0;sampleField=14;
    }
    void openSampleExport() {
        const auto* s=currentSampleChecked();
        std::vector<Action> choices;
        choices.push_back({"WHOLE SAMPLE",number(s->pcm.size())+" FRAMES",[this]{openBrowser(FileAction::ExportWav);}});
        choices.push_back({"SELECTED RANGE",selectionEnd>selectionStart?
            number(selectionEnd-selectionStart)+" FRAMES":"SELECT A PCM RANGE FIRST",[this]{
                if(selectionEnd<=selectionStart)throw std::runtime_error("SELECT A PCM RANGE FIRST");
                openBrowser(FileAction::ExportWavRange);
            }});
        enterMenu("EXPORT SAMPLE WAV",std::move(choices));
    }
    Sample* currentSampleChecked(){auto* s=currentSample();if(!s)throw std::runtime_error("NO SAMPLE");return s;}
    void resizeCurrentPattern(int count,bool discard=false) {
        try{mutatePattern([&](Pattern& p){edit::resizePattern(p,doc.channels,std::uint16_t(count),discard);});blockSelected=false;notify("PATTERN LENGTH UPDATED");}
        catch(const std::exception& e){if(discard||lower(e.what()).find("discard")==std::string::npos)throw;confirm("SHORTEN PATTERN?","ROWS PAST THE NEW LENGTH MAY CONTAIN NOTES.",[this,count]{resizeCurrentPattern(count,true);});}
    }
    void resizeDocumentChannels(int count,bool discard=false) {
        try{mutate([&]{edit::resizeChannels(doc,std::uint16_t(count),discard);});if(solo>=count)solo=-1;blockSelected=false;notify("CHANNEL COUNT UPDATED");}
        catch(const std::exception& e){if(discard||lower(e.what()).find("discard")==std::string::npos)throw;confirm("REMOVE CHANNEL DATA?","CHANNELS PAST THE NEW COUNT MAY CONTAIN NOTES.",[this,count]{resizeDocumentChannels(count,true);});}
    }
    void insertPatternRow(bool discard) {
        const auto range=selectedBlock();
        try{mutatePattern([&](Pattern& p){edit::insertRow(p,doc.channels,row,range.firstChannel,range.lastChannel,discard);});}
        catch(const std::exception& e){if(discard||lower(e.what()).find("discard")==std::string::npos)throw;confirm("DROP LAST ROW?","INSERTION WILL DISCARD THE LAST ROW IN THIS RANGE.",[this]{insertPatternRow(true);});}
    }
    Envelope emptyEnvelope;
    const Envelope& currentEnvelope() const {
        if(instrument>int(doc.instruments.size()))return emptyEnvelope;
        const auto& i=doc.instruments[std::size_t(instrument-1)];return envelopePan?i.panningEnvelope:i.volumeEnvelope;
    }
    Envelope& editableEnvelope(){auto& i=editableInstrument(true);return envelopePan?i.panningEnvelope:i.volumeEnvelope;}
    int envelopeButtonColumn=0;
    void openEnvelope(bool pan){envelopePan=pan;envelopeField=0;envelopePoint=0;envelopeButtonColumn=0;mode=Mode::Envelope;}
    void editEnvelope(Button b) {
        auto& e=currentEnvelope();const int sign=(b==Button::Right||b==Button::Up)?1:-1;const bool vertical=b==Button::Up||b==Button::Down;
        aUsed=true;
        if(envelopeField==0){envelopePoint=std::clamp(envelopePoint+sign,0,std::max(0,int(e.count)-1));return;}
        mutate([&]{
            auto& env=editableEnvelope();if(!env.count)edit::insertEnvelopePoint(env,0,{0,std::uint16_t(envelopePan?32:64)});
            envelopePoint=std::clamp(envelopePoint,0,int(env.count)-1);
            if(envelopeField==1||envelopeField==2){
                auto point=env.points[envelopePoint];
                if(envelopeField==1){
                    const int lo=envelopePoint?env.points[envelopePoint-1].x:0;
                    const int hi=envelopePoint+1<env.count?std::min(32767,int(env.points[envelopePoint+1].x)):32767;
                    if(lo>hi||point.x>32767)throw std::runtime_error("IMPORTED TICK ABOVE 32767 / POINT PRESERVED; MOVE OR DELETE IT IN THE SOURCE TRACKER");
                    point.x=std::uint16_t(std::clamp(int(point.x)+sign*(vertical?16:1),lo,hi));
                }
                else point.y=std::uint16_t(std::clamp(int(point.y)+sign*(vertical?8:1),0,64));
                edit::setEnvelopePoint(env,std::size_t(envelopePoint),point);
            }else if(envelopeField==3||envelopeField==4||envelopeField==6){const int bit=envelopeField==3?1:envelopeField==4?2:4;edit::setEnvelopeFlags(env,std::uint8_t(sign>0?env.flags|bit:env.flags&~bit));}
            else {auto sustain=env.sustain,start=env.loopStart,end=env.loopEnd;
                if(envelopeField==5)sustain=std::uint8_t(std::clamp(int(sustain)+sign,0,int(env.count)-1));
                if(envelopeField==7)start=std::uint8_t(std::clamp(int(start)+sign,0,int(end)));
                if(envelopeField==8)end=std::uint8_t(std::clamp(int(end)+sign,int(start),int(env.count)-1));
                edit::setEnvelopeMarkers(env,sustain,start,end);
            }
        });
    }
    std::optional<EnvPoint> envelopeClipboard;
    void insertEnvelopePointUi(bool paste=false) {
        mutate([&]{auto& e=editableEnvelope();const auto index=e.count?std::size_t(envelopePoint+1):0;
            EnvPoint point=paste&&envelopeClipboard?*envelopeClipboard:e.count?e.points[envelopePoint]:EnvPoint{0,std::uint16_t(envelopePan?32:64)};
            const int lo=index?e.points[index-1].x:0;
            const int hi=index<e.count?e.points[index].x:32767;
            point.x=std::uint16_t(std::clamp(paste?int(point.x):lo+8,lo,hi));
            edit::insertEnvelopePoint(e,index,point);envelopePoint=int(index);
        });
    }
    void cutEnvelopePoint() {
        const auto& e=currentEnvelope();if(!e.count)return;
        envelopeClipboard=e.points[envelopePoint];
        mutate([&]{edit::deleteEnvelopePoint(editableEnvelope(),std::size_t(envelopePoint));
            envelopePoint=std::clamp(envelopePoint,0,std::max(0,int(currentEnvelope().count)-1));});
    }
    void envelopeButton(Button b,bool repeating) {
        if((b==Button::A||b==Button::B)&&held[std::size_t(Button::A)]&&held[std::size_t(Button::B)]){
            aUsed=bUsed=true;if(!cutChordUsed&&!repeating){cutChordUsed=true;cutEnvelopePoint();}return;
        }
        if(b==Button::B){bUsed=false;return;}
        if(held[std::size_t(Button::Select)]){
            if(b==Button::Left||b==Button::Right){envelopePan=b==Button::Right;envelopePoint=0;}
            if(b==Button::A&&!repeating){aUsed=true;if(envelopeClipboard)insertEnvelopePointUi(true);}
            return;
        }
        if(b==Button::A&&!repeating&&envelopeField<9)startHeldNoteAudition();
        if(int(b)<4){
            if(held[std::size_t(Button::A)]){aUsed=true;if(envelopeField<9)editEnvelope(b);}
            else if(b==Button::Up||b==Button::Down) {
                if(envelopeField>=9) {
                    envelopeButtonColumn=envelopeField-9;
                    if(b==Button::Up)envelopeField=8;
                } else if(b==Button::Up)envelopeField=std::max(0,envelopeField-1);
                else envelopeField=envelopeField==8?9+envelopeButtonColumn:envelopeField+1;
            } else if(envelopeField>=9) {
                envelopeField=std::clamp(envelopeField+(b==Button::Right?1:-1),9,11);
                envelopeButtonColumn=envelopeField-9;
            }
            else envelopePoint=std::clamp(envelopePoint+(b==Button::Right?1:-1),0,std::max(0,int(currentEnvelope().count)-1));
        }
    }
    void startWorker(std::string title,std::function<void()> task,std::function<void()> completion={}) {
        if(worker.valid())throw std::runtime_error("ANOTHER AUDIO TASK IS RUNNING");
        cancelWorker=false;workerSeconds=0;workerMessage.clear();busyTitle=std::move(title);held.fill(false);aUsed=true;mode=Mode::Busy;
        workerCompletion=std::move(completion);
        try{worker=std::async(std::launch::async,[this,task=std::move(task)]{
            try{task();}catch(const std::exception& e){std::lock_guard<std::mutex> lock(workerMutex);workerMessage="ERROR: "+std::string(e.what());}
        });}catch(...){workerCompletion={};mode=Mode::Normal;throw;}
    }
    void openRenderSettings() {
        const int last=int(doc.orders.size())-1;
        if(renderOptions.lastOrder>=0) {
            renderOptions.lastOrder=std::clamp(renderOptions.lastOrder,0,last);
            renderOptions.firstOrder=std::clamp(renderOptions.firstOrder,0,renderOptions.lastOrder);
        } else renderOptions.firstOrder=0;
        renderField=0;mode=Mode::RenderSettings;
    }
    void startRender(const std::filesystem::path& path) {
        auto snapshot=doc;auto settings=renderOptions;settings.audioSettings=audioSettings;
        startWorker("RENDERING WAV",[this,path,snapshot=std::move(snapshot),settings]{
            const auto result=renderWav(snapshot,path,settings,[this](const RenderProgress& progress){std::lock_guard<std::mutex> lock(workerMutex);workerSeconds=progress.seconds;return !cancelWorker.load();});
            std::lock_guard<std::mutex> lock(workerMutex);
            workerMessage=result.cancelled?"RENDER CANCELLED":result.limitReached?"DURATION LIMIT REACHED / PARTIAL WAV: "+path.filename().u8string():"WAV SAVED: "+path.filename().u8string();
        });
    }
    void pollTools() {
        if(worker.valid()&&worker.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            worker.get();mode=Mode::Normal;held.fill(false);aUsed=true;
            auto completion=std::move(workerCompletion);workerCompletion={};
            std::string result;{std::lock_guard<std::mutex> lock(workerMutex);result=workerMessage;}
            const bool failed=result.find("ERROR:")==0;
            if(!result.empty())notify(result,failed||result.find("LIMIT REACHED")!=std::string::npos);
            if(completion&&!failed){if(cancelWorker.load())notify("ANALYSIS CANCELLED");else completion();}
            if(completion&&(failed||cancelWorker.load()))clearAnalysisDrafts();
        }
        if(mode==Mode::Recording&&captureStarted)captureStatus=capture.status();
    }
    void openRecording() {
        capture.cancel();pendingRecording.reset();captureDevices=Capture::devices();captureDevice=0;captureField=0;captureStatus={};captureStarted=false;mode=Mode::Recording;
    }
    void finishRecording() {
        auto recording=pendingRecording?std::move(*pendingRecording):capture.finish();pendingRecording.reset();captureStarted=false;
        if(recording.pcm.empty()){notify("RECORDING IS EMPTY / EXISTING SAMPLE KEPT",true);return;}
        recording.name="MIC RECORDING";
        confirm("KEEP RECORDING?","REPLACE CURRENT SAMPLE? UNDO RETAINS THE OLD SAMPLE.",[this,recording=std::move(recording)]()mutable{
            mutate([&]{
                auto& target=editableInstrument();if(target.name.empty())target.name="MIC RECORDING";
                editableSample()=std::move(recording);
            });selectionStart=selectionEnd=0;changePage(Page::Sample);notify("MICROPHONE SAMPLE ADDED");
        });
    }
    bool toolButton(Button b,bool repeating) {
        if(mode==Mode::Busy){if(b==Button::B)cancelWorker=true;return true;}
        if(mode==Mode::Envelope){envelopeButton(b,repeating);return true;}
        if(mode==Mode::EnvelopePresets){envelopePresetsButton(b,repeating);return true;}
        if(mode==Mode::Number){
            if(int(b)<4&&held[std::size_t(Button::A)]){
                aUsed=true;if(numberField==0)numberValue=std::clamp(numberValue+((b==Button::Right||b==Button::Up)?1:-1)*((b==Button::Up||b==Button::Down)?numberStep:1),numberMin,numberMax);
            }else if(b==Button::Up||b==Button::Down)numberField=b==Button::Down?1:0;
            if(b==Button::B)mode=Mode::Normal;
            return true;
        }
        if(mode==Mode::RenderSettings){
            if(int(b)<4&&held[std::size_t(Button::A)]){
                aUsed=true;const int d=(b==Button::Right||b==Button::Up)?1:-1;const bool coarse=b==Button::Up||b==Button::Down;
                if(renderField==0) {
                    if(d>0&&renderOptions.lastOrder<0)
                        renderOptions.firstOrder=renderOptions.lastOrder=std::clamp(order,0,int(doc.orders.size())-1);
                    else if(d<0){renderOptions.firstOrder=0;renderOptions.lastOrder=-1;}
                }
                if(renderField==1&&renderOptions.lastOrder>=0)renderOptions.firstOrder=std::clamp(renderOptions.firstOrder+d*(coarse?16:1),0,renderOptions.lastOrder);
                if(renderField==2&&renderOptions.lastOrder>=0)renderOptions.lastOrder=std::clamp(renderOptions.lastOrder+d*(coarse?16:1),renderOptions.firstOrder,int(doc.orders.size())-1);
                if(renderField==3)renderOptions.maxSeconds=std::clamp(renderOptions.maxSeconds+d*(coarse?300.:60.),60.,7200.);
                if(renderField==4)renderOptions.tailSeconds=std::clamp(renderOptions.tailSeconds+d*(coarse?5:1),0.,30.);
                if(renderField==5)renderOptions.repeats=std::clamp(renderOptions.repeats+d,1,16);
            }else if(b==Button::Up||b==Button::Down) {
                const int d=b==Button::Down?1:-1;
                renderField=std::clamp(renderField+d,0,6);
                if(renderOptions.lastOrder<0&&(renderField==1||renderField==2))renderField=d>0?3:0;
            }
            if(b==Button::B)mode=Mode::Normal;
            return true;
        }
        if(mode==Mode::Recording){
            if(b==Button::B){capture.cancel();pendingRecording.reset();captureStarted=false;mode=Mode::Normal;return true;}
            if(!captureStarted&&!pendingRecording){
                if(int(b)<4&&held[std::size_t(Button::A)]){
                    aUsed=true;const int d=(b==Button::Right||b==Button::Up)?1:-1;
                    if(captureField==0)captureDevice=std::clamp(captureDevice+d,0,std::max(0,int(captureDevices.size())-1));
                    if(captureField==1)captureLimit=std::clamp(captureLimit+d*((b==Button::Up||b==Button::Down)?30:5),5,300);
                }else if(b==Button::Up||b==Button::Down)captureField=std::clamp(captureField+(b==Button::Down?1:-1),0,2);
            }
            return true;
        }
        return false;
    }
    bool toolRelease(Button b) {
        if(mode==Mode::EnvelopePresets)return envelopePresetsRelease(b);
        if(mode==Mode::Envelope){
            if(b==Button::B){if(!bUsed){mode=Mode::Normal;playback->stopPreview();}return true;}
            if(b==Button::A&&!aUsed&&envelopeField>=9){aUsed=true;if(envelopeField==9)insertEnvelopePointUi();else if(envelopeField==10)cutEnvelopePoint();else openEnvelopePresets();return true;}
            return false;
        }
        if(mode!=Mode::Number&&mode!=Mode::RenderSettings&&mode!=Mode::Recording)return false;
        if(b!=Button::A||aUsed)return true;
        if(mode==Mode::Number&&numberField==1){aUsed=true;mode=Mode::Normal;auto fn=numberCommit;fn(numberValue);}
        else if(mode==Mode::RenderSettings&&renderField==6){aUsed=true;openBrowser(FileAction::RenderWav);}
        else if(mode==Mode::Recording){
            if(captureStarted||pendingRecording){aUsed=true;finishRecording();}
            else if(captureField==2){
                aUsed=true;if(captureDevices.empty())throw std::runtime_error("NO RECORDING DEVICE");
                playback->stop();capture.start(captureDevices.at(captureDevice).index,captureLimit);captureStarted=true;captureStatus=capture.status();
            }
        }
        return true;
    }
    SDL_Rect toolDialogBounds{60,65,904,623};
    void drawToolPanel(std::string_view title,int contentBottom,std::string_view hints,
                       std::string_view detail={},int minHintLines=1) {
        toolDialogBounds=dialogForContent({60,65,904,623},contentBottom,hints,detail,minHintLines);
        overlay();rect(toolDialogBounds.x,toolDialogBounds.y,toolDialogBounds.w,toolDialogBounds.h,panel);
        outline(toolDialogBounds.x,toolDialogBounds.y,toolDialogBounds.w,toolDialogBounds.h,dim);
        txt(85,93,title,cyan,3);
    }
    void drawToolFooter(std::string_view hints,std::string_view detail={}) {
        drawDialogFooter(toolDialogBounds,hints,detail);
    }
    void drawEnvelopeEditor() {
        const char* hints="A+DPAD EDIT / A+B CUT / SELECT+A PASTE / B BACK / SELECT+LEFT/RIGHT VOLUME/PAN / HOLD A LISTEN / RELEASE A STOP";
        drawToolPanel(envelopePan?"PANNING ENVELOPE":"VOLUME ENVELOPE",584+controlHeight,hints);const auto& e=currentEnvelope();
        envelope(85,150,530,220,e);
        if(e.count){const int maxX=std::max(1,int(e.points[e.count-1].x));const auto& p=e.points[std::size_t(envelopePoint)];outline(85+int(p.x)*522/maxX-5,150+214-int(p.y)*210/64-5,11,11,amber);}
        txt(85,392,"PT   TICK   VALUE",muted);
        for(int n=0;n<e.count;++n){const int col=n/6,r=n%6;txt(85+col*268,424+r*31,hex(n)+"   "+hex(e.points[n].x,4)+"   "+hex(e.points[n].y),n==envelopePoint?amber:ink);}
        const std::array<std::string,9> labels={"POINT","TICK","VALUE","ENABLED","SUSTAIN","SUSTAIN PT","LOOP","LOOP START","LOOP END"};
        const EnvPoint point=e.count?e.points[std::size_t(envelopePoint)]:EnvPoint{};
        const std::array<std::string,9> values={hex(envelopePoint),hex(point.x,4),hex(point.y),(e.flags&1)?"ON":"OFF",(e.flags&2)?"ON":"OFF",hex(e.sustain),(e.flags&4)?"ON":"OFF",hex(e.loopStart),hex(e.loopEnd)};
        for(int n=0;n<9;++n)labelValue(636,150+n*49,labels[n],values[n],envelopeField==n,300);
        const char* buttons[]={"INSERT","DELETE","PRESET"};
        for(int n=0;n<3;++n)drawButton(636+n*102,584,96,buttons[n],envelopeField==n+9);
        drawToolFooter(hints);
    }
    void drawToolModes() {
        if(mode==Mode::EnvelopePresets){drawEnvelopePresets();return;}
        if(mode==Mode::Envelope){drawEnvelopeEditor();return;}
        if(mode==Mode::Number) {
            const std::string hints=numberField==1?"A APPLY / B CANCEL":
                "A+LEFT/RIGHT 1 / A+UP/DOWN "+number(numberStep)+" / B CANCEL";
            drawToolPanel(numberTitle,410+controlHeight,hints);
            txt(110,230,number(numberValue),numberField==0?amber:ink,5);
            txt(110,345,"RANGE "+number(numberMin)+" TO "+number(numberMax),muted);
            labelValue(110,410,"APPLY","",numberField==1,300);
            drawToolFooter(hints);
        }
        if(mode==Mode::RenderSettings){
            const char* hints=renderField==6?"A CHOOSE FILE / B CANCEL":"A+DPAD EDIT / B CANCEL";
            drawToolPanel("WAV RENDER SETTINGS",527+controlHeight,hints);
            const bool range=renderOptions.lastOrder>=0;
            const std::array<std::string,7> labels={"SCOPE","FIRST ORDER","LAST ORDER (INCLUDED)","MAX DURATION (SECONDS)","TAIL SECONDS","TOTAL PASSES","CHOOSE FILE"};
            const std::array<std::string,7> values={range?"ORDER RANGE":"FULL SONG",range?hex(renderOptions.firstOrder):"--",range?hex(renderOptions.lastOrder):"--",number(int(renderOptions.maxSeconds)),number(int(renderOptions.tailSeconds)),number(renderOptions.repeats),""};
            for(int n=0;n<6;++n)labelValue(100,164+n*49,labels[n],values[n],renderField==n,824);
            txt(100,467,std::string("OUTPUT: ")+(audioSettings.sampleRate==44100?"44.1":"48")+" KHZ / "+
                interpolationLabel(audioSettings.interpolation)+" / 16-BIT STEREO",muted);
            txt(100,493,range?"FIRST ORDER ROW 0 THROUGH LAST ORDER; THEN TAIL.":"WHOLE SONG; DURATION LIMIT ALSO INCLUDES THE TAIL.",muted);
            labelValue(100,527,labels[6],values[6],renderField==6,824);
            drawToolFooter(hints);
        }
        if(mode==Mode::Busy) {
            const char* hints=cancelWorker?"CANCELLING...":"B CANCEL";
            drawToolPanel(busyTitle,workerCompletion?334:281,hints);
            double seconds;{std::lock_guard<std::mutex> lock(workerMutex);seconds=workerSeconds;}
            txt(110,260,workerCompletion?"ANALYZING PCM":number(int(seconds))+" AUDIO SECONDS",amber,3);
            if(workerCompletion)txt(110,320,"PREVIEW ONLY / SAMPLE UNCHANGED",muted);
            drawToolFooter(hints);
        }
        if(mode==Mode::Recording) {
            const char* hints=(captureStarted||pendingRecording)?"A FINISH AND REVIEW / B DISCARD":
                captureField==2?"A RECORD / B BACK":"A+DPAD EDIT / B BACK";
            drawToolPanel("MICROPHONE RECORDING",509,hints);
            labelValue(100,175,"DEVICE",captureDevices.empty()?"NONE":
                ui::fit(captureDevices[std::size_t(captureDevice)].name,43),captureField==0,824);
            labelValue(100,231,"LIMIT SECONDS",number(captureLimit),captureField==1,824);
            if(!captureStarted&&!pendingRecording)labelValue(100,278,"START RECORDING","",captureField==2,824);
            txt(100,330,(captureStarted||pendingRecording)?(captureStatus.recording?"RECORDING":"RECORDING STOPPED"):
                "MICROPHONE IS OFF",captureStatus.recording?red:cyan,3);
            txt(100,380,number(int(captureStatus.seconds))+" SECONDS / "+number(captureStatus.frames)+" FRAMES",ink);
            rect(100,435,824,30,bg);
            rect(100,435,int(824*std::clamp(captureStatus.peak,0.f,1.f)),30,captureStatus.peak>=0.99f?red:green);
            txt(100,495,"CLIPPED FRAMES: "+number(captureStatus.clippedFrames),muted);
            if(captureStatus.limitReached) {
                constexpr std::string_view limitNotice="TIME LIMIT REACHED";
                txt(924-ui::textWidth(limitNotice),495,limitNotice,amber);
            }
            drawToolFooter(hints);
        }
    }
