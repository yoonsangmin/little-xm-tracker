// Instrument settings and key map share the same owned sample slots.
    bool instrumentMapView=false;
    int instrumentTopFocus=8,instrumentEnvelopeFocus=10,instrumentMappingFocus=2;
    bool keymapSelected=false;
    int keymapSelectStage=0,keymapAnchor=49,keymapSelectionOwner=0;
    std::vector<std::uint8_t> keymapClipboard;
    bool keymapContext() const { return page==Page::Instrument&&instrumentMapView&&instrumentField==14; }
    bool hasKeymapSelection() const { return keymapContext()&&keymapSelected&&keymapSelectionOwner==instrument; }
    std::pair<int,int> keymapRange() const {
        return hasKeymapSelection()?std::pair<int,int>{std::min(keymapAnchor,keymapNote)-1,std::max(keymapAnchor,keymapNote)}:
            std::pair<int,int>{keymapNote-1,keymapNote};
    }
    std::array<std::uint8_t,96> keymapValues() {
        const auto* owner=currentInstrument();
        return owner?owner->noteToSample:std::array<std::uint8_t,96>{};
    }
    void assignKeymap(const std::array<std::uint8_t,96>& values) {
        auto& owner=editableInstrument(true);
        const auto count=std::size_t(*std::max_element(values.begin(),values.end()))+1;
        if(owner.samples.size()<count)owner.samples.resize(count);
        owner.noteToSample=values;
    }
    void copyKeymap() {
        const auto values=keymapValues();const auto range=keymapRange();
        keymapClipboard.assign(values.begin()+range.first,values.begin()+range.second);
        notify("KEY MAP COPIED / SELECT+A PASTE");
    }
    void cutKeymap() {
        const auto before=keymapValues();auto values=before;const auto range=keymapRange();
        copyKeymap();std::fill(values.begin()+range.first,values.begin()+range.second,0);
        if(values!=before)mutate([&]{assignKeymap(values);},"CUT KEY MAP");
        notify("KEY MAP CUT / RESET TO SAMPLE 00");
    }
    void pasteKeymap() {
        if(keymapClipboard.empty())throw std::runtime_error("COPY KEY MAP FIRST");
        const int first=keymapNote-1;
        if(std::size_t(first)+keymapClipboard.size()>96)throw std::runtime_error("PASTE EXCEEDS 96 NOTES");
        const auto before=keymapValues();auto values=before;
        std::copy(keymapClipboard.begin(),keymapClipboard.end(),values.begin()+first);
        if(values!=before)mutate([&]{assignKeymap(values);},"PASTE KEY MAP");
        notify("KEY MAP PASTED");
    }
    void selectKeymap() {
        if(!hasKeymapSelection()) {
            keymapSelected=true;keymapSelectionOwner=instrument;keymapAnchor=keymapNote;keymapSelectStage=1;
        } else if(keymapSelectStage==1) {
            keymapAnchor=((keymapNote-1)/24)*24+1;keymapNote=keymapAnchor+23;keymapSelectStage=2;
        } else if(keymapSelectStage==2) {
            keymapAnchor=1;keymapNote=96;keymapSelectStage=3;
        } else {cancelSelection();return;}
        notify("KEY MAP SELECTED / UP-DOWN EXTEND / B COPY");
    }
    void toggleInstrumentMap() {
        finishGesture();clearSelection();stopHeldAudition(true);playback->stopPreview();
        instrumentMappingFocus=12;
        instrumentMapView=!instrumentMapView;instrumentField=instrumentMapView?14:12;
    }
    bool instrumentPanelDirection(Button b,bool editing) {
        const bool vertical=b==Button::Up||b==Button::Down;
        const int sign=(b==Button::Up||b==Button::Right)?1:-1;
        if(editing) {
            if(instrumentField==14) {
                aUsed=true;
                const auto before=keymapValues();auto values=before;const auto range=keymapRange();
                for(int n=range.first;n<range.second;++n)values[std::size_t(n)]=std::uint8_t(std::clamp(int(values[std::size_t(n)])+sign,0,15));
                if(values!=before) {
                    beginGesture("KEY MAP EDIT");assignKeymap(values);
                }
                return true;
            }
            if(instrumentField==13) {
                aUsed=true;sample=std::clamp(sample+sign,0,15);
                selectionStart=selectionEnd=0;waveformZoom=false;return true;
            }
            return false;
        }
        if(instrumentField==8||instrumentField==9)instrumentTopFocus=instrumentField;
        if(instrumentField==2||instrumentField==12)instrumentMappingFocus=instrumentField;
        if(instrumentField==10||instrumentField==11)instrumentEnvelopeFocus=instrumentField;
        if(instrumentField==13) {
            if(b==Button::Up&&sample==0)instrumentField=8;
            else if(vertical){sample=std::clamp(sample+(b==Button::Down?1:-1),0,15);selectionStart=selectionEnd=0;waveformZoom=false;}
            else if(b==Button::Right)instrumentField=instrumentMapView?14:0;
        } else if(instrumentMapView&&instrumentField==14) {
            if(b==Button::Up){if(keymapNote>1)--keymapNote;else if(!hasKeymapSelection())instrumentField=instrumentTopFocus;}
            if(b==Button::Down)keymapNote=std::min(96,keymapNote+1);
            if(b==Button::Left&&!hasKeymapSelection())instrumentField=13;
        } else if(instrumentField==12) {
            if(b==Button::Left)instrumentField=2;
            if(b==Button::Up)instrumentField=1;
            if(b==Button::Down)instrumentField=3;
        } else if(instrumentField==8||instrumentField==9) {
            if(b==Button::Left)instrumentField=8;
            if(b==Button::Right)instrumentField=9;
            if(b==Button::Down)instrumentField=instrumentField==8?13:instrumentMapView?14:0;
        } else {
            if(b==Button::Up)instrumentField=instrumentField==0?instrumentTopFocus:instrumentField==3?instrumentMappingFocus:instrumentField<8?instrumentField-1:7;
            if(b==Button::Down)instrumentField=instrumentField==1?instrumentMappingFocus:instrumentField<7?instrumentField+1:instrumentField==7?instrumentEnvelopeFocus:instrumentField;
            if(b==Button::Left)instrumentField=instrumentField==11?10:13;
            if(b==Button::Right){if(instrumentField==2)instrumentField=12;else if(instrumentField==10)instrumentField=11;}
        }
        if(instrumentField==8||instrumentField==9)instrumentTopFocus=instrumentField;
        if(instrumentField==2||instrumentField==12)instrumentMappingFocus=instrumentField;
        if(instrumentField==10||instrumentField==11)instrumentEnvelopeFocus=instrumentField;
        return true;
    }
    std::string instrumentSampleName(int index) {
        const auto* owner=currentInstrument();
        if(!owner||std::size_t(index)>=owner->samples.size())return "--";
        const auto& value=owner->samples[std::size_t(index)];
        if(value.name.empty())return value.pcm.empty()?"--":"UNTITLED";
        return text::xmDisplayText(value.name);
    }
    void drawInstrument() {
        auto* i=currentInstrument();
        rect(20,52,858,controlHeight,instrumentField==8?panel2:bg);
        if(instrumentField==8)rect(20,52,focusBarWidth,controlHeight,amber);
        txt(34,52+controlTextY,"NAME",instrumentField==8?ink:muted);
        txt(112,52+controlTextY,i?ui::fit(text::xmDisplayText(i->name),62):"EMPTY INSTRUMENT",instrumentField==8?amber:cyan);
        drawButton(886,52,118,"LOAD",instrumentField==9);
        rect(20,104,326,616,panel);txt(32,116,"SLOT  NAME",muted);
        for(int n=0;n<16;++n) {
            const int y=152+n*32;const bool selected=n==sample;
            if(selected){rect(28,y-6,310,28,panel2);rect(28,y-6,3,28,instrumentField==13?amber:cyan);}
            txt(40,y,hex(n),selected?(instrumentField==13?amber:cyan):muted);
            txt(88,y,ui::fit(instrumentSampleName(n),20),selected?ink:muted);
        }
        const auto used=i?std::count_if(i->samples.begin(),i->samples.end(),[](const Sample& value){return !value.pcm.empty();}):0;
        txt(32,696,number(int(used))+"/16 USED",muted);
        Instrument empty;if(!i)i=&empty;
        if(instrumentMapView) {
            rect(362,104,642,30,panel2);txt(374,112,"NOTE",muted);txt(482,112,"SAMPLE",muted);txt(602,112,"NAME",muted);
            const int first=((keymapNote-1)/24)*24;
            const auto range=keymapRange();
            for(int n=0;n<24;++n) {
                const int key=first+n,y=142+n*23;const bool active=key==keymapNote-1;
                const bool selected=hasKeymapSelection()&&key>=range.first&&key<range.second;
                if(selected)rect(478,y-3,36,23,selectionColor);
                const int mapped=i->noteToSample[std::size_t(key)];
                txt(374,y,noteName(key+1),ink);
                if(active&&instrumentField==14)rect(478,y-3,36,20,amber);
                txt(484,y,hex(mapped),active&&instrumentField==14?bg:cyan);
                txt(602,y,ui::fit(instrumentSampleName(mapped),32),muted);
            }
            txt(374,704,noteName(first+1)+" ... "+noteName(first+24),muted);
            txt(788,704,"24 / 96 NOTES",muted);
            return;
        }
        const std::array<std::string,8> labels={"SAMPLE SLOT","MAP NOTE","MAPPED SAMPLE","FADEOUT","VIBRATO DEPTH","VIBRATO RATE","VIBRATO TYPE","VIBRATO SWEEP"};
        const std::array<std::string,8> values={hex(sample),noteName(keymapNote),hex(i->noteToSample[keymapNote-1]),hex(i->fadeout,4),hex(i->vibratoDepth),hex(i->vibratoRate),hex(i->vibratoType),hex(i->vibratoSweep)};
        for(int n=0;n<8;++n)labelValue(362,104+n*42,labels[n],values[n],instrumentField==n,n==2?516:642);
        drawButton(886,188,118,"KEY MAP",instrumentField==12);
        const auto preview=playback->instrumentPreviewStatus();
        const bool previewMatches=preview.active&&preview.instrument==instrument;
        txt(372,460,"VOLUME ENVELOPE",instrumentField==10?amber:muted);
        envelope(369,490,305,216,i->volumeEnvelope,previewMatches?preview.volumeTick:-1);
        txt(696,460,"PANNING ENVELOPE",instrumentField==11?amber:muted);
        envelope(693,490,305,216,i->panningEnvelope,previewMatches?preview.panningTick:-1);
        if(instrumentField==10)outline(366,450,311,260,amber);
        if(instrumentField==11)outline(690,450,311,260,amber);
    }
