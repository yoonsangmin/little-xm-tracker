// Whole-song cleanup is prepared off-document, then committed as one edit.
    edit::TrimOptions trimOptions;
    std::optional<edit::TrimResult> trimPreview;
    std::uint64_t trimPreviewRevision=0;
    int trimField=0;

    void openTrim() {
        finishGesture();trimPreview.reset();trimField=0;mode=Mode::TrimSettings;
    }
    bool& trimOption(int index) {
        switch(index) {
        case 0:return trimOptions.unusedPatterns;
        case 1:return trimOptions.unusedInstruments;
        case 2:return trimOptions.unusedSamples;
        case 3:return trimOptions.unusedChannels;
        case 4:return trimOptions.sampleTails;
        default:return trimOptions.convertTo8Bit;
        }
    }
    std::array<int,5> trimCursor() const {return {{selectedPattern,instrument,sample,row,channel}};}
    void restoreTrimCursor(const std::array<int,5>& cursor) {
        channel=cursor[4];restoreCleanupEditor(cursor[0],cursor[1],cursor[2],cursor[3]);
        keymapClipboard.clear();
        if(solo>=doc.channels){solo=-1;playback->setSolo(-1);}
    }
    void applyTrim() {
        if(!trimPreview||trimPreviewRevision!=documentRevision)
            throw std::runtime_error("SONG CHANGED / OPEN CLEAN UP AGAIN");
        const auto previous=trimCursor();auto next=previous;
        const auto& plan=*trimPreview;
        next[0]=plan.ids.patternMap.at(std::size_t(previous[0]));
        if(next[0]<0)next[0]=plan.document.orders.at(std::size_t(order));
        next[1]=plan.ids.instrumentMap.at(std::size_t(previous[1]));
        next[2]=plan.sampleMap.at(std::size_t(previous[1])).at(std::size_t(previous[2]));
        if(next[1]<1){next[1]=1;next[2]=0;}
        if(next[2]<0)next[2]=0;
        next[4]=std::min(next[4],int(plan.document.channels)-1);
        const auto revision=documentRevision;
        playback->stop();playback->stopPreview();followPlayback=false;
        try {mutate([&]{doc=std::move(trimPreview->document);},"CLEAN UP SONG");}
        catch(...){trimPreview.reset();restoreTrimCursor(previous);mode=Mode::TrimSettings;throw;}
        restoreTrimCursor(next);
        if(documentRevision!=revision&&!undoStack.empty()) {
            auto& change=undoStack.back();
            auto undoCleanup=[this,undoDocument=change.undo,previous](Document& d){undoDocument(d);restoreTrimCursor(previous);};
            auto redoCleanup=[this,redoDocument=change.redo,next](Document& d){redoDocument(d);restoreTrimCursor(next);};
            change.undo=std::move(undoCleanup);change.redo=std::move(redoCleanup);
        }
        trimPreview.reset();mode=Mode::Normal;notify("SONG CLEANED UP / UNDO AVAILABLE");
    }
    void requestTrim() {
        if(!trimPreview||trimPreviewRevision!=documentRevision){
            trimPreview=edit::prepareTrim(doc,trimOptions);trimPreviewRevision=documentRevision;
        }
        if(!trimPreview->changed()){notify("NOTHING TO CLEAN UP");return;}
        const auto& plan=*trimPreview;std::string summary;
        const auto append=[&](int count,const char* label){
            if(count){if(!summary.empty())summary+=" / ";summary+=number(count)+" "+label;}
        };
        append(plan.ids.patternsRemoved,"UNUSED PATTERNS");
        append(plan.ids.instrumentsRemoved,"UNUSED INSTRUMENTS");
        append(plan.samplesRemoved,"UNUSED SAMPLES");
        append(plan.channelsRemoved,"UNUSED CHANNELS");
        append(plan.samplesTrimmed,"SAMPLE TAILS");
        append(plan.samplesConverted,"SAMPLES TO 8-BIT");
        if(summary.empty())summary="RENUMBER USED SLOTS";
        summary+=" / XM: "+std::to_string(plan.beforeBytes)+" B -> "+std::to_string(plan.afterBytes)+" B";
        summary+=" / SAVE: "+std::to_string(plan.beforeBytes-plan.afterBytes)+" B";
        confirm("CLEAN UP SONG?",summary,[this]{applyTrim();});confirmCancelMode=Mode::TrimSettings;
    }
    bool trimButton(Button b) {
        if(mode!=Mode::TrimSettings)return false;
        if(b==Button::B){trimPreview.reset();mode=Mode::Menu;return true;}
        if(int(b)<4&&held[std::size_t(Button::A)]) {
            aUsed=true;
            if(trimField<6){
                const bool enabled=b==Button::Right||b==Button::Up;
                auto& option=trimOption(trimField);
                if(option!=enabled){option=enabled;trimPreview.reset();}
            }
        }else if(b==Button::Up||b==Button::Down)trimField=std::clamp(trimField+(b==Button::Down?1:-1),0,6);
        return true;
    }
    bool trimRelease(Button b) {
        if(mode!=Mode::TrimSettings)return false;
        if(b==Button::A&&!aUsed&&trimField==6){aUsed=true;requestTrim();}
        return true;
    }
    void drawTrim() {
        const char* labels[]={"UNUSED PATTERNS","UNUSED INSTRUMENTS","UNUSED SAMPLES","UNUSED CHANNELS","DATA AFTER LOOP","CONVERT TO 8-BIT"};
        const char* details[]={"KEEP PATTERNS USED BY ORDERS / RENUMBER IDS",
            "KEEP INSTRUMENTS USED BY RETAINED PATTERNS",
            "KEEP SAMPLES MAPPED BY ANY OF THE 96 KEYS",
            "REMOVE EMPTY TRAILING CHANNELS / KEEP MIDDLE GAPS",
            "REMOVE DATA AFTER ENABLED LOOP ENDS",
            "8-BIT REDUCES PRECISION / KEEPS SAMPLE LENGTH",
            "REVIEW CHANGES AND BYTES SAVED BEFORE CONFIRMING"};
        const char* hints=trimField==6?"A REVIEW / B BACK":"A+RIGHT/UP ON / A+LEFT/DOWN OFF / B BACK";
        // Compact toggles follow the Actions list; the execution button keeps
        // the shared form height and the footer keeps its usual spacing.
        constexpr int left=153,width=718,rowTop=146,rowHeight=31,rowPitch=34;
        constexpr int buttonTop=rowTop+5*rowPitch+rowHeight+16;
        toolDialogBounds=dialogForContent({129,77,766,0},buttonTop+controlHeight,hints,details[trimField]);
        overlay();rect(toolDialogBounds.x,toolDialogBounds.y,toolDialogBounds.w,toolDialogBounds.h,panel);
        outline(toolDialogBounds.x,toolDialogBounds.y,toolDialogBounds.w,toolDialogBounds.h,dim);
        txt(154,102,"CLEAN UP SONG",cyan,3);
        for(int n=0;n<6;++n) {
            const int top=rowTop+n*rowPitch,textTop=top+(rowHeight-dialogGlyphHeight)/2;
            const bool selected=trimField==n;const char* value=trimOption(n)?"ON":"OFF";
            rect(left,top,width,rowHeight,selected?panel2:panel);
            if(selected)rect(left,top,focusBarWidth,rowHeight,amber);
            txt(left+14,textTop,labels[n],selected?ink:muted);
            txt(left+width-ui::textWidth(value)-14,textTop,value,selected?amber:cyan);
        }
        drawButton(left,buttonTop,width,"CLEAN UP",trimField==6);
        drawToolFooter(hints,details[trimField]);
    }
