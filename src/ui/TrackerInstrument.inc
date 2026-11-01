// Instrument settings and key map share the same owned sample slots.
    bool instrumentMapView=false;
    int instrumentTopFocus=8,instrumentEnvelopeFocus=10;
    void toggleInstrumentMap() {
        finishGesture();stopHeldAudition(true);playback->stopPreview();
        instrumentTopFocus=12;
        instrumentMapView=!instrumentMapView;instrumentField=instrumentMapView?14:0;
    }
    bool instrumentPanelDirection(Button b,bool editing) {
        const bool vertical=b==Button::Up||b==Button::Down;
        const int sign=(b==Button::Up||b==Button::Right)?1:-1;
        if(editing) {
            if(instrumentField==14) {
                aUsed=true;
                const auto* existing=currentInstrument();
                const int old=existing?existing->noteToSample[keymapNote-1]:0;
                const int value=std::clamp(old+sign,0,15);
                if(value!=old) {
                    beginGesture();auto& target=editableInstrument(true);
                    if(target.samples.size()<=std::size_t(value))target.samples.resize(std::size_t(value+1));
                    target.noteToSample[keymapNote-1]=std::uint8_t(value);
                }
                return true;
            }
            if(instrumentField==13) {
                aUsed=true;sample=std::clamp(sample+sign,0,15);
                selectionStart=selectionEnd=0;waveformZoom=false;return true;
            }
            return false;
        }
        if(instrumentField==8||instrumentField==9||instrumentField==12)instrumentTopFocus=instrumentField;
        if(instrumentField==10||instrumentField==11)instrumentEnvelopeFocus=instrumentField;
        if(instrumentField==13) {
            if(vertical){sample=std::clamp(sample+(b==Button::Down?1:-1),0,15);selectionStart=selectionEnd=0;waveformZoom=false;}
            else if(b==Button::Right)instrumentField=instrumentMapView?14:0;
        } else if(instrumentMapView&&instrumentField==14) {
            if(b==Button::Up){if(keymapNote>1)--keymapNote;else instrumentField=instrumentTopFocus;}
            if(b==Button::Down)keymapNote=std::min(96,keymapNote+1);
            if(b==Button::Left)instrumentField=13;
        } else if(instrumentField==12) {
            if(b==Button::Left)instrumentField=9;
            if(b==Button::Down)instrumentField=instrumentMapView?14:0;
        } else if(instrumentField==8||instrumentField==9) {
            if(b==Button::Left)instrumentField=instrumentField==9?8:13;
            if(b==Button::Right)instrumentField=instrumentField==8?9:12;
            if(b==Button::Down)instrumentField=instrumentMapView?14:0;
        } else {
            if(b==Button::Up)instrumentField=instrumentField==0?instrumentTopFocus:instrumentField<8?instrumentField-1:7;
            if(b==Button::Down)instrumentField=instrumentField<7?instrumentField+1:instrumentField==7?instrumentEnvelopeFocus:instrumentField;
            if(b==Button::Left)instrumentField=instrumentField==11?10:13;
            if(b==Button::Right&&instrumentField==10)instrumentField=11;
        }
        if(instrumentField==8||instrumentField==9||instrumentField==12)instrumentTopFocus=instrumentField;
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
        txt(24,64,"SAMPLES IN "+hex(instrument),cyan);
        txt(362,64,"INSTRUMENT "+hex(instrument),muted);
        rect(20,96,326,624,panel);txt(32,112,"SLOT  NAME",muted);
        for(int n=0;n<16;++n) {
            const int y=148+n*32;const bool selected=n==sample;
            if(selected){rect(28,y-6,310,28,panel2);rect(28,y-6,3,28,instrumentField==13?amber:cyan);}
            txt(40,y,hex(n),selected?(instrumentField==13?amber:cyan):muted);
            txt(88,y,ui::fit(instrumentSampleName(n),20),selected?ink:muted);
        }
        txt(32,696,"16 SAMPLE SLOTS",muted);
        auto* i=currentInstrument();
        labelValue(362,96,"NAME",i?ui::fit(text::xmDisplayText(i->name),21):"EMPTY",instrumentField==8,374);
        const auto button=[&](int x,int width,const char* title,int focus){
            drawButton(x,96,width,title,instrumentField==focus);
        };
        button(748,124,"LOAD XI",9);button(884,120,instrumentMapView?"SETTINGS":"KEY MAP",12);
        Instrument empty;if(!i)i=&empty;
        if(instrumentMapView) {
            rect(362,146,642,30,panel2);txt(374,154,"NOTE",muted);txt(482,154,"SAMPLE",muted);txt(602,154,"NAME",muted);
            const int first=((keymapNote-1)/24)*24;
            for(int n=0;n<24;++n) {
                const int key=first+n,y=184+n*21;const bool active=key==keymapNote-1;
                if(active)rect(362,y-3,642,21,panel2);
                const int mapped=i->noteToSample[std::size_t(key)];
                txt(374,y,noteName(key+1),active?amber:ink);
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
        for(int n=0;n<8;++n)labelValue(366,146+n*39,labels[n],values[n],instrumentField==n,638);
        txt(372,486,"VOLUME ENVELOPE",instrumentField==10?amber:muted);envelope(369,516,305,190,i->volumeEnvelope);
        txt(696,486,"PANNING ENVELOPE",instrumentField==11?amber:muted);envelope(693,516,305,190,i->panningEnvelope);
        if(instrumentField==10)outline(366,476,311,234,amber);
        if(instrumentField==11)outline(690,476,311,234,amber);
    }
