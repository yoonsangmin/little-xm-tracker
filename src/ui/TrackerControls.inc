// Eight-button editor: D-pad, A, B, Start and Select. Chords are consumed
// until release so their modifier keys cannot also trigger a tap action.
    bool bUsed=false, cutChordUsed=false;
    bool channelModifier=false;
    int channelChord=0; // 1: mute, 2: solo; consume until both secondary keys are up.
    int songMonitorReturnField=7;
    int blockSelectStage=0, orderBlockStart=0, orderSelectStage=0, pcmSelectStage=0;
    bool orderBlockSelected=false, pcmSelecting=false;
    std::vector<std::uint8_t> orderBlockClipboard;

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
    }
    bool hasSelection() const {
        if(page==Page::Pattern)return blockSelected&&blockPattern==patternIndex();
        if(page==Page::Song)return orderBlockSelected;
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
        } else if(page==Page::Sample) {
            pcmClipboard=edit::copyPcm(*currentSampleChecked(),selectedFrames());
            selectionStart=selectionEnd=0;
            notify("PCM COPIED / SELECT+A PASTE");
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
        } else if(page==Page::Sample) {
            auto* s=currentSample();if(!s||s->pcm.empty())return;
            if(!pcmSelecting){
                if(selectionStart==selectionEnd){selectionStart=0;selectionEnd=int(s->pcm.size());}
                pcmSelecting=true;pcmSelectStage=1;
            }else if(pcmSelectStage<2){selectionStart=0;selectionEnd=int(s->pcm.size());pcmSelectStage=2;}
            else {cancelSelection();return;}
            sampleField=8;notify("PCM SELECTED / DPAD END / B COPY / A+B CUT");
        }
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
        } else if(page==Page::Sample) {
            if(!pcmClipboard)throw std::runtime_error("COPY PCM FIRST");
            const auto range=selectedFrames(false);
            if(!pcmClipboard->pcm.empty()||range.last>range.first)mutate([&]{edit::pastePcm(editableSample(),range,*pcmClipboard);},"PASTE");
            selectionEnd=selectionStart+int(pcmClipboard->pcm.size());normalize();notify("PCM PASTED");
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
        } else if(page==Page::Sample&&currentSample()) {
            const auto range=selectedFrames();
            pcmClipboard=edit::copyPcm(*currentSampleChecked(),range);
            mutate([&]{edit::deletePcm(*currentSample(),range);},"CUT");selectionEnd=selectionStart;
            notify("PCM CUT / SELECT+A PASTE");
        } else if(page==Page::Instrument&&instrumentField==13) {
            deleteCurrentSample();
        } else if(page==Page::Instrument&&instrumentField==14&&currentInstrument()) {
            mutate([&]{currentInstrument()->noteToSample[keymapNote-1]=0;});notify("KEY MAP RESET TO SAMPLE 00");
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
        finishGesture();followPlayback=false;bUsed=true;playback->stopPreview();clearSelection();
        const bool vertical=b==Button::Up||b==Button::Down;
        const int direction=(b==Button::Down||b==Button::Right)?1:-1;
        if(page==Page::Pattern) {
            if(vertical)movePatternPage(direction);
            else channel=std::clamp(channel+direction*patternViewChannels,0,int(doc.channels)-1);
        } else if(page==Page::Song) {
            if(songField==12)navigateSongMonitor(b,true);
            else moveSongOrderCursor(direction*(vertical?1:8));
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
        else auditionNote();
    }
    void activateSampleField() {
        if(pcmSelecting||sampleField==14)openSampleProcessing();
        else if(sampleField==9)rename(NameTarget::Sample);
        else if(sampleField==10)openBrowser(FileAction::ImportAudio);
        else if(sampleField==11)openWaveDraw();
        else if(sampleField==13) {
            if(selectionEnd<=selectionStart)throw std::runtime_error("SELECT A PCM RANGE FIRST");
            mutate([&]{auto& s=*currentSampleChecked();s.loopStart=selectionStart;s.loopLength=selectionEnd-selectionStart;s.loopType=1;});
        } else if(currentSample()) {
            const bool range=sampleField==12&&selectionEnd>selectionStart;
            playback->previewSample(instrument,sample,keymapNote,range?selectionStart:0,range?selectionEnd:-1);
        }
    }
