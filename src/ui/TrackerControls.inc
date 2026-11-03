// Eight-button editor: D-pad, A, B, Start and Select. Chords are consumed
// until release so their modifier keys cannot also trigger a tap action.
    bool bUsed=false, cutChordUsed=false;
    bool channelModifier=false;
    int channelChord=0; // 1: mute, 2: solo; consume until both secondary keys are up.
    int songMonitorReturnField=7;
    int blockSelectStage=0, orderBlockStart=0, orderSelectStage=0, pcmSelectStage=0;
    // pcmSelecting remembers the Select+B cycle only; it never captures D-pad navigation.
    bool orderBlockSelected=false, pcmSelecting=false;
    std::vector<std::uint8_t> orderBlockClipboard;

    static int sampleRangeStep(int length,bool coarse,Uint32 heldMs) {
        if(coarse) {
            const int scale=heldMs>=1400?16:heldMs>=600?4:1;
            return std::min(std::max(1,length),std::max(1,length/100)*scale);
        }
        // Preserve single-frame taps; sustained movement covers PCM at roughly
        // one plotted pixel per repeat, rather than crawling through long files.
        return heldMs>=1400?std::max(16,length/1024):heldMs>=600?std::max(4,length/4096):1;
    }
    void resetSampleField() {
        auto* s=currentSample();
        if(sampleField==7||sampleField==8) {
            if(sampleField==7)selectionStart=0;
            else {selectionStart=selectionEnd=0;clearSelection();}
            normalize();rememberSampleFocus();notify("SELECTION VALUE RESET");
            return;
        }
        if(!s||sampleField<0||sampleField>6)return;
        mutate([&]{
            switch(sampleField) {
            case 0:s->volume=0;break;
            case 1:s->panning=0;break;
            case 2:s->finetune=0;break;
            case 3:s->relativeNote=0;break;
            case 4:s->loopType=0;break;
            case 5:
                if(!s->loopStart)break;
                s->loopLength=std::uint32_t(std::min(std::uint64_t(s->loopStart)+s->loopLength,
                    std::uint64_t(s->pcm.size())));
                s->loopStart=0;
                if(!s->loopLength)s->loopType=0;
                break;
            case 6:
                // An end at frame zero cannot follow a nonzero start.
                s->loopStart=s->loopLength=0;s->loopType=0;
                break;
            }
        },"RESET SAMPLE VALUE");
        notify("SAMPLE VALUE RESET");
    }

    static std::uint8_t adjustVolumeColumn(std::uint8_t value,int direction,bool coarse) {
        // Skip XM's reserved byte ranges. Within commands, horizontal edits
        // change only x; vertical edits choose the command, preserving x.
        if(value>=0x60) {
            const int command=value>>4, amount=value&15;
            if(coarse&&command==6&&direction<0)return 0x50;
            return std::uint8_t(coarse?((std::clamp(command+direction,6,15)<<4)|amount):
                ((command<<4)|std::clamp(amount+direction,0,15)));
        }
        const int volume=std::clamp(int(value),0x10,0x50);
        const int next=volume+direction*(coarse?16:1);
        if(coarse&&next>0x50)return std::uint8_t(0x60|(volume&15));
        return std::uint8_t(std::clamp(next,0x10,0x50));
    }

    bool channelControlContext() const {
        return page==Page::Pattern||(page==Page::Song&&songField==12);
    }
    int songMonitorColumns() const { return doc.channels<=2?doc.channels:doc.channels<=16?4:8; }
    void enterSongMonitor() {
        songMonitorReturnField=songField;songField=12;
        clearSelection();followPlayback=false;
    }
    void navigateSongMonitor(Button b,bool stayInMonitor=false) {
        const int columns=songMonitorColumns();
        if(b==Button::Up) {
            if(channel>=columns)channel-=columns;
            else if(!stayInMonitor)songField=songMonitorReturnField;
        } else if(b==Button::Down&&channel+columns<doc.channels)channel+=columns;
        else if(b==Button::Left&&channel%columns>0)--channel;
        else if(b==Button::Right&&channel%columns+1<columns&&channel+1<doc.channels)++channel;
    }
    bool channelChordDown(Button b,bool repeating) {
        if(channelChord) {
            // Wait for Select/Start release before rearming a held B. Overlapping
            // secondary keys and repeats must not leak into selection or transport.
            aUsed=bUsed=selectUsed=true;selectPending=false;
            return true;
        }
        if(mode!=Mode::Normal||!channelControlContext()||!channelModifier||
           !held[std::size_t(Button::B)]||held[std::size_t(Button::A)]||repeating)return false;
        if(b==Button::Select)channelChord=1;
        else if(b==Button::Start&&!held[std::size_t(Button::Select)])channelChord=2;
        else return false;
        aUsed=bUsed=selectUsed=true;selectPending=false;
        if(channelChord==1) {
            const bool wasMuted=(playback->muteMask()&(std::uint32_t{1}<<channel))!=0;
            playback->setMuted(channel,!wasMuted);
            solo=-1;
            notify("CHANNEL "+channelNumber(channel)+(wasMuted?" / UNMUTED":" / MUTED"));
        } else {
            solo=solo==channel?-1:channel;playback->setSolo(solo);
            notify(solo<0?"ALL CHANNELS / MUTES CLEARED":"SOLO CHANNEL "+channelNumber(channel));
        }
        songMonitorValid=false;
        return true;
    }

    void clearSelection() {
        blockSelected=false;blockSelectStage=0;orderBlockSelected=false;orderSelectStage=0;pcmSelecting=false;pcmSelectStage=0;
        keymapSelected=false;keymapSelectStage=0;keymapSelectionOwner=0;
    }
    bool hasSelection() const {
        if(page==Page::Pattern)return blockSelected&&blockPattern==patternIndex();
        if(page==Page::Song)return orderBlockSelected;
        if(page==Page::Instrument)return hasKeymapSelection();
        return page==Page::Sample&&(pcmSelecting||selectionEnd>selectionStart);
    }
    void cancelSelection() {
        if(page==Page::Sample)selectionStart=selectionEnd=0;
        clearSelection();notify("SELECTION CANCELLED");
    }
    void copyCurrent() {
        if(page==Page::Pattern) {
            if(blockSelected)copyPattern();
            else {
                clipboard=cell();blockClipboard.reset();copiedMask={false,false,false,false,false,false};
                maskField(copiedMask,field)=true;notify("FIELD COPIED / SELECT+A PASTE");
            }
        } else if(page==Page::Song&&(songField==0||orderBlockSelected)&&!songAppendOrder) {
            const int first=orderBlockSelected?std::min(order,orderBlockStart):order;
            const int last=orderBlockSelected?std::max(order,orderBlockStart):order;
            orderBlockClipboard.assign(doc.orders.begin()+first,doc.orders.begin()+last+1);
            orderClipboard=doc.orders[std::size_t(first)];notify("ORDERS COPIED / SELECT+A PASTE");
        } else if(keymapContext()) {
            copyKeymap();
        } else if(page==Page::Sample) {
            pcmClipboard=edit::copyPcm(*currentSampleChecked(),selectedFrames());
            selectionStart=selectionEnd=0;
            notify("PCM COPIED / PROCESS > PASTE PCM");
        }
        clearSelection();
    }
    void markSelection() {
        followPlayback=false;
        if(page==Page::Pattern) {
            if(!blockSelected||blockPattern!=patternIndex()) {
                blockSelected=true;blockSelectStage=1;blockRow=row;blockChannel=channel;blockField=field;blockPattern=patternIndex();
            } else if(blockSelectStage<2) {
                blockRow=0;row=pattern().rows-1;blockChannel=channel;blockField=field;blockSelectStage=2;
            } else if(blockSelectStage<3) {
                blockRow=0;row=pattern().rows-1;blockChannel=0;blockField=0;channel=doc.channels-1;field=4;blockSelectStage=3;
            } else {cancelSelection();return;}
            notify("SELECTED / DPAD EXTEND / B COPY / A+B CUT");
        } else if(page==Page::Song&&songField==0&&!songAppendOrder) {
            if(!orderBlockSelected){orderBlockSelected=true;orderBlockStart=order;orderSelectStage=1;}
            else if(orderSelectStage<2){orderBlockStart=0;order=int(doc.orders.size())-1;orderSelectStage=2;}
            else {cancelSelection();return;}
            notify("ORDERS SELECTED / UP-DOWN EXTEND / B COPY");
        } else if(keymapContext()) {
            selectKeymap();
        } else if(page==Page::Sample) {
            auto* s=currentSample();if(!s||s->pcm.empty())return;
            if(!pcmSelecting){
                if(selectionStart==selectionEnd){selectionStart=0;selectionEnd=int(s->pcm.size());}
                pcmSelecting=true;pcmSelectStage=1;
            }else if(pcmSelectStage<2){selectionStart=0;selectionEnd=int(s->pcm.size());pcmSelectStage=2;}
            else {cancelSelection();return;}
            sampleField=8;rememberSampleFocus();notify("PCM SELECTED / A+DPAD RANGE / B COPY / PROCESS TOOLS");
        }
    }
    void pastePcmSelection() {
        if(!pcmClipboard)throw std::runtime_error("COPY PCM FIRST");
        const auto range=selectedFrames(false);
        std::size_t inserted=0;
        if(!pcmClipboard->pcm.empty()||range.last>range.first)mutate([&]{inserted=edit::pastePcm(editableSample(),range,*pcmClipboard);},"PASTE");
        selectionEnd=selectionStart+int(inserted);normalize();clearSelection();notify("PCM PASTED");
    }
    void pasteCurrent() {
        finishGesture();
        if(page==Page::Pattern)pastePattern();
        else if(page==Page::Song&&songField==0) {
            if(orderBlockClipboard.empty()){pasteOrder();return;}
            const int destination=songOrderCursor();
            if(std::size_t(destination)+orderBlockClipboard.size()>256)throw std::runtime_error("PASTE EXCEEDS 256 ORDERS");
            mutate([&]{
                const auto end=std::size_t(destination)+orderBlockClipboard.size();
                if(doc.orders.size()<end)doc.orders.resize(end,0);
                std::copy(orderBlockClipboard.begin(),orderBlockClipboard.end(),doc.orders.begin()+destination);order=destination;songAppendOrder=false;
            },"PASTE");notify("ORDERS PASTED");
        } else if(keymapContext()) {
            pasteKeymap();
        }
        clearSelection();
    }
    void cutCurrent() {
        finishGesture();followPlayback=false;
        if(page==Page::Pattern) {
            const Cell previous=cell();
            if(blockSelected) {
                const auto range=selectedBlock();copyPattern();
                mutatePattern([&](Pattern& p){for(auto r=range.firstRow;r<range.lastRow;++r)
                    for(auto c=range.firstChannel;c<range.lastChannel;++c) {
                        auto mask=copiedMask;mask.skipEmpty=false;
                        const auto fields=edit::selectedFields(range,c);
                        for(int f=0;f<5;++f)if(!(fields&(1u<<f)))maskField(mask,f)=false;
                        p.at(r,c,doc.channels)=edit::mergeCell(p.at(r,c,doc.channels),Cell{},mask);
                    }},"CUT");
            } else {
                copyCurrent();auto mask=copiedMask;mask.skipEmpty=false;
                editCell(edit::mergeCell(cell(),Cell{},mask),"CUT");
            }
            lastCell=previous;notify("CUT / SELECT+A PASTE");
        } else if(page==Page::Song&&(songField==0||orderBlockSelected)&&!songAppendOrder) {
            const int first=orderBlockSelected?std::min(order,orderBlockStart):order;
            const int last=orderBlockSelected?std::max(order,orderBlockStart):order;
            copyCurrent();
            mutate([&]{
                if(last-first+1==int(doc.orders.size())){doc.orders={0};doc.restart=0;}
                else for(int at=last;at>=first;--at)edit::removeOrder(doc,at);
            },"CUT");order=std::min(first,int(doc.orders.size())-1);notify("ORDERS CUT / PATTERN CONTENTS KEPT");
        } else if(page==Page::Sample) {
            resetSampleField();return;
        } else if(page==Page::Instrument&&instrumentField==13) {
            deleteCurrentSample();
        } else if(keymapContext()) {
            cutKeymap();
        } else if(page==Page::Instrument&&instrumentField<8&&currentInstrument()) {
            if(instrumentField==0)sample=0;
            else if(instrumentField==1)keymapNote=49;
            else mutate([&]{auto& i=*currentInstrument();switch(instrumentField){
                case 2:i.noteToSample[keymapNote-1]=0;break;case 3:i.fadeout=0;break;
                case 4:i.vibratoDepth=0;break;case 5:i.vibratoRate=0;break;
                case 6:i.vibratoType=0;break;case 7:i.vibratoSweep=0;break;
            }});
            notify("VALUE RESET");
        }
        clearSelection();
    }
    void navigateWithB(Button b) {
        finishGesture();bUsed=true;
        const bool vertical=b==Button::Up||b==Button::Down;
        if(keymapContext()&&vertical) {
            playback->stopPreview();keymapNote=std::clamp(keymapNote+(b==Button::Down?24:-24),1,96);
            return;
        }
        if(page==Page::Song&&songField!=12&&!vertical)return;
        if(page!=Page::Pattern||vertical||hasSelection())followPlayback=false;
        playback->stopPreview();clearSelection();
        const int direction=(b==Button::Down||b==Button::Right)?1:-1;
        if(page==Page::Pattern) {
            if(vertical)movePatternPage(direction);
            else channel=std::clamp(channel+direction*patternViewChannels,0,int(doc.channels)-1);
        } else if(page==Page::Song) {
            if(songField==12)navigateSongMonitor(b,true);
            else if(songField==0&&vertical)moveSongOrderCursor(direction*songOrderPageRows);
            else if(vertical)moveSongOrderCursor(direction);
        }
        else if((page==Page::Instrument&&vertical)||(page==Page::Sample&&!vertical)) {
            instrument=std::clamp(instrument+direction,1,128);sample=0;
            selectionStart=selectionEnd=0;waveformZoom=false;
        } else {
            sample=std::clamp(sample+direction,0,15);
            selectionStart=selectionEnd=0;waveformZoom=false;
        }
    }
    void activateInstrumentField() {
        if(instrumentField==12)toggleInstrumentMap();
        else if(instrumentField==13){clearSelection();sampleField=0;changePage(Page::Sample);}
        else if(instrumentField==14)return;
        else if(instrumentField==8)rename(NameTarget::Instrument);
        else if(instrumentField==9)openBrowser(FileAction::ImportXi);
        else if(instrumentField==10||instrumentField==11)openEnvelope(instrumentField==11);
    }
    void activateSampleField() {
        if(sampleField==14)openSampleProcessing();
        else if(sampleField==9)rename(NameTarget::Sample);
        else if(sampleField==10)openBrowser(FileAction::ImportAudio);
        else if(sampleField==11)openNewWave();
        else if(sampleField==12)openWaveDraw();
    }
