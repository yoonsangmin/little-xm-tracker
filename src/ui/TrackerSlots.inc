// Lists browse stable XM numbers, including implicit empty slots.
    enum class SlotKind { Pattern, Instrument, Sample };
    SlotKind slotKind=SlotKind::Pattern;
    bool slotPaste=false,slotBackPending=false;
    // -1 browses the list; 0/1 are its independent OPEN/DELETE button row.
    int slotIndex=0,slotFocus=-1,slotLastButton=0,slotOwner=1,slotSource=0,slotSourceOwner=1;
    Page slotReturnPage=Page::Pattern;
    void openSlotList(bool instruments) {
        finishGesture();stopHeldAudition(true);playback->stopPreview();followPlayback=false;
        slotKind=instruments?SlotKind::Instrument:SlotKind::Pattern;slotPaste=false;slotBackPending=false;
        slotIndex=instruments?instrument-1:patternIndex();slotFocus=-1;slotLastButton=0;
        mode=Mode::SlotList;
    }
    void openCopyDestination(SlotKind kind) {
        finishGesture();stopHeldAudition(true);playback->stopPreview();followPlayback=false;
        slotKind=kind;slotPaste=true;slotBackPending=false;slotFocus=-1;slotLastButton=0;slotReturnPage=page;
        slotIndex=kind==SlotKind::Pattern?patternIndex():kind==SlotKind::Instrument?instrument-1:sample;
        slotOwner=slotSourceOwner=instrument;slotSource=slotIndex;
        mode=Mode::SlotList;
    }
    void cancelCopyDestination() {
        slotPaste=false;slotBackPending=false;mode=Mode::Normal;
        notify("PASTE CANCELLED / COPY KEPT");
    }
    void openSelectedSlot() {
        clearSelection();followPlayback=false;
        if(slotKind==SlotKind::Sample) {
            instrument=slotOwner;sample=slotIndex;selectionStart=selectionEnd=0;waveformZoom=false;
            changePage(slotReturnPage);
        } else if(slotKind==SlotKind::Instrument) {
            instrument=slotIndex+1;sample=0;selectionStart=selectionEnd=0;waveformZoom=false;
            instrumentField=instrumentMapView?14:0;changePage(Page::Instrument);
        } else {
            const int target=slotIndex;
            if(doc.orders[std::size_t(order)]!=target) {
                const auto found=std::find(doc.orders.begin(),doc.orders.end(),target);
                if(found!=doc.orders.end())order=int(std::distance(doc.orders.begin(),found));
            }
            songAppendOrder=false;selectPattern(target);changePage(Page::Pattern);
        }
        slotPaste=false;slotBackPending=false;normalize();
    }
    void pasteSelectedSlot() {
        slotBackPending=false;
        if(slotKind==SlotKind::Pattern)pastePatternSlot(slotIndex);
        else if(slotKind==SlotKind::Instrument)pasteInstrumentSlot(slotIndex+1);
        else pasteSampleSlot(slotOwner,slotIndex);
    }
    void deleteSelectedSlot() {
        const bool instruments=slotKind==SlotKind::Instrument;const int target=slotIndex;
        const std::string title=instruments?"DELETE INSTRUMENT "+hex(target+1)+"?":"CLEAR PATTERN "+hex(target)+"?";
        const std::string detail=instruments?"CLEAR NAME, SETTINGS AND SAMPLES. KEEP SLOT NUMBERS.":
            "CLEAR NOTES IN "+number(edit::patternUseCount(doc,target))+" ORDER(S). KEEP ROWS AND NUMBER.";
        confirm(title,detail,[this,instruments,target]{
            playback->stopPreview();
            mutate([&]{if(instruments)edit::clearInstrumentSlot(doc,target+1);else edit::clearPatternSlot(doc,target);},"DELETE");
            clearSelection();normalize();mode=Mode::SlotList;notify("SLOT CLEARED / UNDO AVAILABLE");
        });
        confirmCancelMode=Mode::SlotList;
    }
    void deleteCurrentSample() {
        const int owner=instrument,target=sample;
        confirm("DELETE SAMPLE "+hex(target)+"?","CLEAR PCM AND SETTINGS. KEEP SLOT NUMBERS AND KEY MAP.",[this,owner,target]{
            playback->stopPreview();mutate([&]{edit::clearSampleSlot(doc,owner,target);},"DELETE");
            selectionStart=selectionEnd=0;waveformZoom=false;normalize();notify("SAMPLE CLEARED / UNDO AVAILABLE");
        });
    }
    bool slotListButton(Button b) {
        if(mode!=Mode::SlotList)return false;
        if(b==Button::B){
            bUsed=true;
            if(slotPaste&&slotKind==SlotKind::Sample)slotBackPending=true;
            else if(slotPaste)cancelCopyDestination();
            else if(slotFocus>=0){slotLastButton=slotFocus;slotFocus=-1;}
            else mode=Mode::Menu;
            return true;
        }
        if(slotPaste&&slotKind==SlotKind::Sample&&held[std::size_t(Button::B)]) {
            slotBackPending=false;aUsed=true;
            if(b==Button::Left||b==Button::Right)slotOwner=std::clamp(slotOwner+(b==Button::Right?1:-1),1,128);
            return true;
        }
        if(b==Button::Select){selectUsed=true;return true;}
        if(int(b)<4) {
            const int last=slotKind==SlotKind::Pattern?255:slotKind==SlotKind::Instrument?127:15;
            if(held[std::size_t(Button::A)]) {
                aUsed=true;
                if(slotFocus<0&&(b==Button::Up||b==Button::Down))slotIndex=std::clamp(slotIndex+(b==Button::Down?16:-16),0,last);
            } else if(slotFocus<0) {
                if(b==Button::Up||b==Button::Down)slotIndex=std::clamp(slotIndex+(b==Button::Down?1:-1),0,last);
            } else if(b==Button::Up){slotLastButton=slotFocus;slotFocus=-1;}
            else if(b==Button::Left||b==Button::Right) {
                slotFocus=std::clamp(slotFocus+(b==Button::Right?1:-1),0,1);
                slotLastButton=slotFocus;
            }
        }
        return true;
    }
    bool slotListRelease(Button b) {
        if(mode!=Mode::SlotList)return false;
        if(b==Button::B&&slotPaste&&slotKind==SlotKind::Sample) {
            for(int n=0;n<4;++n)held[std::size_t(n)]=false;
            if(slotBackPending)cancelCopyDestination();
        }
        if(b==Button::A&&!aUsed) {
            aUsed=true;
            if(slotPaste)pasteSelectedSlot();
            else if(slotFocus<0)slotFocus=slotLastButton;
            else if(slotFocus==1)deleteSelectedSlot();
            else openSelectedSlot();
        }
        return true;
    }
    void drawSlotList() {
        const bool instruments=slotKind==SlotKind::Instrument,samples=slotKind==SlotKind::Sample;
        std::string hints=slotPaste?"A+UP/DOWN PAGE / A PASTE / B CANCEL":
            slotFocus<0?"A+UP/DOWN PAGE / A CHOOSE / B BACK":
            slotFocus==1?"UP OR B LIST / A DELETE":"UP OR B LIST / A OPEN";
        if(slotPaste&&samples)hints+=" / B+LEFT/RIGHT INSTRUMENT";
        const auto bounds=dialogForContent({75,70,874,626},(slotPaste?608:580)+controlHeight,hints);
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);outline(bounds.x,bounds.y,bounds.w,bounds.h,dim);
        const auto title=slotPaste?(samples?"PASTE SAMPLE TO":instruments?"PASTE INSTRUMENT TO":"PASTE PATTERN TO"):
            (instruments?"INSTRUMENT LIST":"PATTERN LIST");
        txt(103,101,title,cyan,3);
        if(samples)txt(642,109,"INSTRUMENT "+hex(slotOwner),ink);
        txt(103,147,"NUMBER",muted);txt(235,147,slotKind==SlotKind::Pattern?"ROWS / ORDER USES":"NAME",muted);
        const int first=(slotIndex/16)*16;
        for(int n=0;n<16;++n) {
            const int index=first+n,y=180+n*24;const bool active=index==slotIndex;
            if(active)rect(95,y-5,830,22,panel2);
            txt(107,y,hex(instruments?index+1:index),active&&slotFocus<0?amber:cyan);
            if(samples) {
                const auto* owner=std::size_t(slotOwner)<=doc.instruments.size()?&doc.instruments[std::size_t(slotOwner-1)]:nullptr;
                const auto* entry=owner&&std::size_t(index)<owner->samples.size()?&owner->samples[std::size_t(index)]:nullptr;
                const auto label=!entry?"--":entry->name.empty()?(entry->pcm.empty()?"--":"UNTITLED"):text::xmDisplayText(entry->name);
                txt(235,y,ui::fit(label,44),active?ink:muted);
            } else if(instruments) {
                const bool exists=std::size_t(index)<doc.instruments.size();
                const auto label=!exists?"--":doc.instruments[index].name.empty()?(doc.instruments[index].samples.empty()?"--":"UNTITLED"):text::xmDisplayText(doc.instruments[index].name);
                txt(235,y,ui::fit(label,44),active?ink:muted);
            } else {
                const bool exists=std::size_t(index)<doc.patterns.size();
                const int rows=exists?doc.patterns[index].rows:64;
                txt(235,y,number(rows)+" ROWS / "+number(edit::patternUseCount(doc,index))+" ORDER(S)",active?ink:muted);
                const bool empty=!exists||std::none_of(doc.patterns[index].cells.begin(),doc.patterns[index].cells.end(),[](const Cell& c){return c.note||c.instrument||c.volume||c.effect||c.param;});
                if(empty)txt(727,y,"EMPTY",dim);
            }
        }
        if(slotPaste) {
            const auto source=samples?"FROM "+hex(slotSourceOwner)+":"+hex(slotSource):
                std::string("FROM ")+(instruments?"INSTRUMENT ":"PATTERN ")+hex(instruments?slotSource+1:slotSource);
            txt(103,574,source,muted);
            drawButton(794,608,130,"PASTE",true);
            drawDialogFooter(bounds,hints);
            return;
        }
        for(int n=0;n<2;++n) {
            drawButton(654+n*140,580,130,n?"DELETE":"OPEN",slotFocus==n);
        }
        drawDialogFooter(bounds,hints);
    }
