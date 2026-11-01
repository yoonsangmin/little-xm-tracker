// Included inside TrackerApp::Impl. Presets belong to the editor, not the song.
    edit::EnvelopePresets envelopePresets=edit::defaultEnvelopePresets();
    int envelopePresetSlot=0,envelopePresetField=0;
    std::array<int,3> envelopePresetColumns{{-1,-1,0}};

    void loadEnvelopePresetBank() {
        auto defaults=edit::defaultEnvelopePresets();
        try {
            const auto path=home/"envelope-presets.dat";
            if(std::filesystem::exists(path)) {
                if(std::filesystem::file_size(path)>16384)throw std::runtime_error("PRESET FILE TOO LARGE");
                defaults=edit::decodeEnvelopePresets(io::readFile(path));
            }
            envelopePresets=std::move(defaults);
        } catch(const std::exception&) {
            envelopePresets=edit::defaultEnvelopePresets();
            notify("ENVELOPE PRESETS UNREADABLE / USING BUILT-INS",true);
        }
    }
    void commitEnvelopePresetBank(edit::EnvelopePresets next) {
        io::writeFileAtomic(home/"envelope-presets.dat",edit::encodeEnvelopePresets(next));
        envelopePresets=std::move(next);
    }
    const edit::EnvelopePreset& selectedEnvelopePreset() const {
        return (envelopePan?envelopePresets.panning:envelopePresets.volume).at(std::size_t(envelopePresetSlot));
    }
    void openEnvelopePresets() {
        finishGesture();envelopePresetSlot=std::clamp(envelopePresetSlot,0,5);
        envelopePresetField=envelopePresetSlot;envelopePresetColumns={{-1,-1,0}};
        envelopePresetColumns[std::size_t(envelopePresetField/3)]=envelopePresetField%3;
        envelopeButtonColumn=2;mode=Mode::EnvelopePresets;
    }
    void loadEnvelopePresetSlot() {
        const auto preset=selectedEnvelopePreset();
        mutate([&]{edit::applyEnvelopePreset(editableInstrument(true),envelopePan,preset);});
        envelopePoint=0;envelopeField=11;mode=Mode::Envelope;
        notify("PRESET "+number(envelopePresetSlot+1)+" LOADED / UNDO AVAILABLE");
    }
    void saveEnvelopePresetSlot() {
        const auto* source=currentInstrument();
        if(!source)throw std::runtime_error("NO INSTRUMENT TO SAVE");
        auto next=envelopePresets;
        (envelopePan?next.panning:next.volume).at(std::size_t(envelopePresetSlot))=edit::captureEnvelopePreset(*source,envelopePan);
        commitEnvelopePresetBank(std::move(next));
        mode=Mode::EnvelopePresets;
        notify("CURRENT ENVELOPE SAVED TO PRESET "+number(envelopePresetSlot+1));
    }
    void resetEnvelopePresetSlot() {
        auto next=envelopePresets;const auto defaults=edit::defaultEnvelopePresets();
        (envelopePan?next.panning:next.volume).at(std::size_t(envelopePresetSlot))=
            (envelopePan?defaults.panning:defaults.volume).at(std::size_t(envelopePresetSlot));
        commitEnvelopePresetBank(std::move(next));
        mode=Mode::EnvelopePresets;notify("PRESET RESTORED / LOAD TO APPLY");
    }
    void envelopePresetsButton(Button b,bool repeating) {
        if(b==Button::B&&!repeating){bUsed=false;return;}
        if(held[std::size_t(Button::B)]){aUsed=true;return;}
        if(held[std::size_t(Button::Select)]) {
            if(b==Button::Left||b==Button::Right){
                envelopePan=b==Button::Right;envelopePoint=0;envelopePresetSlot=envelopePresetField=0;
                envelopePresetColumns={{0,-1,0}};
            }
            aUsed=true;return;
        }
        if(int(b)>=4)return;
        if(held[std::size_t(Button::A)])aUsed=true;
        const int focusRow=envelopePresetField/3,column=envelopePresetField%3;
        envelopePresetColumns[std::size_t(focusRow)]=column;
        if(b==Button::Up&&focusRow==2)envelopePresetField=envelopePresetSlot;
        else if(b==Button::Up||b==Button::Down) {
            const int nextRow=std::clamp(focusRow+(b==Button::Down?1:-1),0,2);
            const int remembered=envelopePresetColumns[std::size_t(nextRow)];
            envelopePresetField=nextRow*3+(remembered<0?column:remembered);
        } else if(b==Button::Left||b==Button::Right)
            envelopePresetField=focusRow*3+std::clamp(column+(b==Button::Right?1:-1),0,2);
        envelopePresetColumns[std::size_t(envelopePresetField/3)]=envelopePresetField%3;
        if(envelopePresetField<6)envelopePresetSlot=envelopePresetField;
    }
    bool envelopePresetsRelease(Button b) {
        if(b==Button::B){if(!bUsed){mode=Mode::Envelope;envelopeField=11;}return true;}
        if(b!=Button::A||aUsed)return true;
        aUsed=true;
        // Choosing a card enters the action row without losing a top-row
        // target by navigating through another preset on the way there.
        if(envelopePresetField<6) {
            envelopePresetSlot=envelopePresetField;
            envelopePresetField=6+envelopePresetColumns[2];
        } else if(envelopePresetField==6)loadEnvelopePresetSlot();
        else if(envelopePresetField==7)saveEnvelopePresetSlot();
        else {
            confirm("RESET PRESET "+number(envelopePresetSlot+1)+"?","RESTORE THIS SAVED SLOT TO ITS BUILT-IN SHAPE?",
                [this]{mode=Mode::EnvelopePresets;resetEnvelopePresetSlot();});
            confirmCancelMode=Mode::EnvelopePresets;
        }
        return true;
    }
    void drawEnvelopePresets() {
        const char* hints=envelopePresetField<6?
            "A CHOOSE / B BACK / SELECT+LEFT/RIGHT VOLUME/PAN":
            envelopePresetField==6?"A LOAD / B BACK / SELECT+LEFT/RIGHT VOLUME/PAN":
            envelopePresetField==7?"A SAVE CURRENT / B BACK / SELECT+LEFT/RIGHT VOLUME/PAN":
            "A RESET SLOT / B BACK / SELECT+LEFT/RIGHT VOLUME/PAN";
        drawToolPanel(envelopePan?"PANNING ENVELOPE PRESETS":"VOLUME ENVELOPE PRESETS",548+controlHeight,hints);
        const auto& bank=envelopePan?envelopePresets.panning:envelopePresets.volume;
        for(int n=0;n<6;++n) {
            const int x=85+(n%3)*284,y=145+(n/3)*155;
            const bool focus=envelopePresetField==n,selected=envelopePresetSlot==n;
            rect(x,y,264,140,focus?panel2:bg);outline(x,y,264,140,selected?amber:dim);
            const auto& preset=bank[std::size_t(n)];
            txt(x+12,y+12,number(n+1)+" "+(preset.custom?"CUSTOM":edit::envelopePresetName(envelopePan,std::size_t(n))),focus?amber:ink);
            envelope(x+14,y+39,236,76,preset.envelope);
            txt(x+12,y+119,(preset.envelope.flags&1)?"ON":"OFF",muted,1);
            if(preset.envelope.flags&2)txt(x+47,y+119,"SUSTAIN",muted,1);
            if(preset.envelope.flags&4)txt(x+116,y+119,"LOOP",muted,1);
        }
        txt(85,470,"SELECTED SLOT "+number(envelopePresetSlot+1)+(selectedEnvelopePreset().custom?" / CUSTOM":" / BUILT-IN"),amber);
        txt(85,503,envelopePan?"PAN SHAPE + SUSTAIN / LOOP":"VOL SHAPE + SUSTAIN / LOOP + FADEOUT + AUTO VIBRATO",muted);
        const char* labels[]={"LOAD","SAVE CURRENT","RESET SLOT"};
        for(int n=0;n<3;++n)drawButton(85+n*284,548,264,labels[n],envelopePresetField==n+6);
        drawToolFooter(hints);
    }
