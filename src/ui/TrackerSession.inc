// Application preferences are separate from XM data and document history.
    int patternTopRow() const { return (row / patternViewRows) * patternViewRows; }
    std::string patternRowLabel(int value) const {
        if(rowNumbersHex)return hex(value);
        const auto label=number(value);
        return std::string(3-label.size(),'0')+label;
    }
    int patternFirstChannel() const { return (channel / patternViewChannels) * patternViewChannels; }

    void syncPlaybackView() {
        if (!followPlayback || !transport.playing || mode != Mode::Normal || gestureBefore) return;
        if (page != Page::Song && page != Page::Pattern) return;
        if (!playingPattern) {order = std::clamp(transport.order, 0, int(doc.orders.size()) - 1);songAppendOrder=false;}
        selectedPattern=std::clamp(transport.pattern,0,255);
        row = std::clamp(transport.row, 0, int(pattern().rows) - 1);
    }

    void loadEditorSettings() {
        const auto path = home / "editor-settings.txt";
        if (!std::filesystem::exists(path)) return;
        try {
            const auto bytes = io::readFile(path);
            std::istringstream input(std::string(bytes.begin(), bytes.end()));
            std::string setting;
            while (std::getline(input,setting)) {
                std::istringstream line(setting);
                std::string key,trailing; int value;
                if(!(line>>key>>value)||line>>trailing)continue;
                if (key == "repeat_delay") repeatDelay = std::clamp(value, 150, 600);
                else if (key == "repeat_interval") repeatInterval = std::clamp(value, 35, 200);
                else if (key == "pattern_view_channels" && (value == 4 || value == 8)) patternViewChannels = value;
                else if (key == "pattern_view_rows" && (value == 16 || value == 32)) patternViewRows = value;
                else if (key == "row_numbers_hex" && (value == 0 || value == 1)) rowNumbersHex = value != 0;
                else if (key == "pattern_row_audition" && (value == 0 || value == 1)) patternRowAudition = value != 0;
                else if (key == "output_sample_rate" && (value == 44100 || value == 48000)) audioSettings.sampleRate = value;
                else if (key == "interpolation" && value >= int(Interpolation::Nearest) && value <= int(Interpolation::SincPrecise))
                    audioSettings.interpolation = static_cast<Interpolation>(value);
                else if (key == "fullscreen" && value && !options.uiTest && !options.smoke) {
                    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
                        notify("CANNOT RESTORE FULLSCREEN", true);
                }
                else if(key.compare(0,6,"theme_")==0&&value>=0&&value<=0xffffff) {
                    for(std::size_t n=0;n<themeKeys.size();++n)if(key==std::string("theme_")+themeKeys[n]) {
                        palette[n]={Uint8(value>>16),Uint8(value>>8),Uint8(value),defaultPalette[n].a};break;
                    }
                }
            }
        } catch (const std::exception&) { notify("EDITOR SETTINGS COULD NOT BE READ / USING DEFAULTS", true); }
    }

    void saveEditorSettings() {
        const auto fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        auto value = "fullscreen " + number(fullscreen ? 1 : 0) + "\nrepeat_delay " + number(repeatDelay) +
            "\nrepeat_interval " + number(repeatInterval) + "\npattern_view_channels " + number(patternViewChannels) + "\npattern_view_rows " + number(patternViewRows) +
            "\nrow_numbers_hex " + number(rowNumbersHex?1:0) + "\npattern_row_audition " + number(patternRowAudition?1:0) +
            "\noutput_sample_rate " + number(audioSettings.sampleRate) +
            "\ninterpolation " + number(int(audioSettings.interpolation)) + "\n";
        for(std::size_t n=0;n<palette.size();++n) {
            const auto c=palette[n];
            value+=std::string("theme_")+themeKeys[n]+" "+number((int(c.r)<<16)|(int(c.g)<<8)|int(c.b))+"\n";
        }
        io::writeFileAtomic(home / "editor-settings.txt", std::vector<std::uint8_t>(value.begin(), value.end()));
    }

    static const char* interpolationLabel(Interpolation value) {
        switch(value) {
        case Interpolation::Nearest:return "NEAREST";
        case Interpolation::Linear:return "LINEAR";
        case Interpolation::CubicLagrange:return "CUBIC LAGRANGE";
        case Interpolation::CubicSpline:return "CUBIC SPLINE";
        case Interpolation::Sinc:return "SINC";
        case Interpolation::SincPrecise:return "SINC PRECISE";
        }
        return "LINEAR";
    }
    void refreshAudioOutputSettings() {
        actions.clear();
        actions.push_back({std::string("OUTPUT RATE: ")+(audioSettingsDraft.sampleRate==44100?"44.1 KHZ":"48 KHZ"),
            "44.1 / 48 KHZ / APPLY TO SAVE",{},[this](int d,bool){
                audioSettingsDraft.sampleRate=d>0?48000:44100;refreshAudioOutputSettings();
            }});
        actions.push_back({std::string("INTERPOLATION: ")+interpolationLabel(audioSettingsDraft.interpolation),
            "APPLY TO SAVE / B DISCARDS CHANGES",{},[this](int d,bool){
                audioSettingsDraft.interpolation=static_cast<Interpolation>(std::clamp(int(audioSettingsDraft.interpolation)+d,
                    int(Interpolation::Nearest),int(Interpolation::SincPrecise)));
                refreshAudioOutputSettings();
            }});
        actions.push_back({"APPLY (STOPS PLAYBACK)","STOPS SONG AND PREVIEWS / SAVES FOR NEXT START",[this]{
            if(audioSettingsDraft.sampleRate==audioSettings.sampleRate&&audioSettingsDraft.interpolation==audioSettings.interpolation) {
                backMenu();notify("AUDIO SETTINGS UNCHANGED");return;
            }
            try {playback->configure(audioSettingsDraft);}
            catch(...) {audioReady=playback->isOpen();throw;}
            audioReady=playback->isOpen();audioSettings=playback->audioSettings();followPlayback=false;
            saveEditorSettings();backMenu();notify("AUDIO OUTPUT APPLIED / PLAYBACK STOPPED");
        }});
        mode=Mode::Menu;
    }
    void openAudioOutputSettings() {
        audioSettingsDraft=audioSettings;
        enterMenu("AUDIO OUTPUT",{});refreshAudioOutputSettings();
    }

    std::vector<Action> patternSettingActions() {
        const auto changed=[this]{
            saveEditorSettings();openEditorSettings();
        };
        return {
            {"PATTERN VIEW: "+number(patternViewChannels)+" CHANNELS","4 OR 8 VISIBLE CHANNELS",{},[this,changed](int d,bool){
                patternViewChannels=d>0?8:4;changed();
            }},
            {"PATTERN ROWS: "+number(patternViewRows),"16 OR 32 ROWS PER PAGE",{},[this,changed](int d,bool){
                patternViewRows=d>0?32:16;changed();
            }},
            {std::string("ROW NUMBERS: ")+(rowNumbersHex?"HEX":"DEC"),"DECIMAL / HEXADECIMAL ROW LABELS",{},[this,changed](int d,bool){
                rowNumbersHex=d>0;changed();
            }},
            {std::string("NOTE AUDITION: ")+(patternRowAudition?"WHOLE ROW":"NOTE ONLY"),"HOLD A TO LISTEN / RELEASE TO STOP",{},[this,changed](int d,bool){
                patternRowAudition=d>0;changed();
            }}
        };
    }
    void openEditorSettings() {
        finishGesture();
        const bool refreshing=mode==Mode::Menu&&menuTitle=="SETTINGS";
        const int previousIndex=refreshing?menuIndex:0;
        if(!refreshing)enterMenu("SETTINGS",{});
        actions.clear();
        const bool fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0;
        actions.push_back({fullscreen?"FULLSCREEN: ON":"FULLSCREEN: OFF","",{},[this](int d,bool){
            if(SDL_SetWindowFullscreen(window,d>0?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0)throw std::runtime_error(SDL_GetError());
            saveEditorSettings();openEditorSettings();
        }});
        const auto patternOptions=patternSettingActions();
        actions.insert(actions.end(),patternOptions.begin(),patternOptions.end());
        actions.push_back({"REPEAT DELAY: "+number(repeatDelay)+" MS","A+LEFT/RIGHT 5 MS / A+UP/DOWN 25 MS",{},[this](int d,bool coarse){
            repeatDelay=std::clamp(repeatDelay+d*(coarse?25:5),150,600);saveEditorSettings();openEditorSettings();
        }});
        actions.push_back({"REPEAT RATE: "+number(repeatInterval)+" MS","A+LEFT/RIGHT 1 MS / A+UP/DOWN 5 MS",{},[this](int d,bool coarse){
            repeatInterval=std::clamp(repeatInterval+d*(coarse?5:1),35,200);saveEditorSettings();openEditorSettings();
        }});
        actions.push_back({"AUDIO OUTPUT","OUTPUT RATE AND INTERPOLATION / APPLY TO SAVE",[this]{openAudioOutputSettings();}});
        actions.push_back({"THEME COLORS","RGB COLORS / PREVIEW, RESET AND APPLY",[this]{openThemeColors();}});
        menuIndex=std::clamp(previousIndex,0,int(actions.size())-1);mode=Mode::Menu;
    }

    void activateSongField() {
        switch (songField) {
        case 0: if(songAppendOrder)appendSongOrder();break; // Existing orders open via Select+Right.
        case 1:case 2:case 3:case 4:case 5:break; // Values use A+directions.
        case 6: rename(NameTarget::Song);break;
        case 7: if(currentFile.empty())openBrowser(FileAction::SaveXm);else saveTo(currentFile,FileAction::SaveXm);break;
        case 8: openBrowser(FileAction::SaveXm);break;
        case 9: destructive([this]{openBrowser(FileAction::OpenXm);});break;
        case 10: destructive([this]{newDocument();});break;
        }
    }

    void newDocument() {
        cancelPendingNote();if(held[std::size_t(Button::A)])aUsed=true;
        pendingAudio.reset();
        playback->stop();playingPattern=false;songAppendOrder=false;songOrderColumn=0;doc=makeDemoDocument();doc.name="UNTITLED";doc.orders={0};doc.patterns.resize(1);
        playback->setMuteMask(0);
        for(auto& c:doc.patterns[0].cells)c={};
        currentFile.clear();sourceFingerprint.reset();order=row=channel=selectedPattern=0;instrument=1;sample=0;orderClipboard.reset();orderBlockClipboard.clear();clearSelection();solo=-1;blockSelected=false;followPlayback=false;selectPending=false;
        undoStack.clear();redoStack.clear();undoBytes=0;dirty=true;++documentRevision;++recoverySerial;
        normalize();playback->load(doc);playback->setSolo(solo);lastRecoveryAt=SDL_GetTicks()-30000u;notify("NEW SONG");
    }

    void pollRecovery() {
        if(options.smoke||options.uiTest)return;
        if(recoveryWorker.valid()) {
            if(recoveryWorker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
            try {recoveryWorker.get();savedRecoverySerial=pendingRecoverySerial;}
            catch(const std::exception& e){notify("AUTOSAVE FAILED: "+std::string(e.what()),true);}
        }
        const auto now=SDL_GetTicks();
        if(recoverySerial==savedRecoverySerial||gestureBefore||pendingNoteBefore||Uint32(now-lastRecoveryAt)<30000u)return;
        // Copy on the UI thread, write on a worker. The audio callback never
        // waits for filesystem operations and the named XM is never overwritten.
        auto snapshot=doc;const auto source=currentFile;const auto directory=home;
        const auto baseline=sourceFingerprint;const bool unsaved=dirty;
        lastRecoveryAt=now;pendingRecoverySerial=recoverySerial;
        recoveryWorker=std::async(std::launch::async,[snapshot=std::move(snapshot),source,directory,baseline,unsaved]{
            io::RecoveryStore(directory).save(snapshot,source,unsaved,baseline);
        });
    }

    void flushRecovery() {
        if(recoveryWorker.valid()) {
            try {recoveryWorker.get();savedRecoverySerial=pendingRecoverySerial;}
            catch(const std::exception&) { /* Retry the current complete snapshot below. */ }
        }
        if(recoverySerial!=savedRecoverySerial) {
            io::RecoveryStore(home).save(doc,currentFile,dirty,sourceFingerprint);
            savedRecoverySerial=recoverySerial;
        }
    }

    void requestExit() {
        commitPendingNote();
        finishGesture();flushRecovery();running=false;
    }

    void restoreLastWork() {
        auto recovered=io::RecoveryStore(home).latest();
        if(!recovered)return;
        doc=std::move(recovered->document);currentFile=recovered->sourcePath;
        sourceFingerprint=recovered->sourceFingerprint;dirty=recovered->dirty;
        std::optional<io::SourceFingerprint> disk;
        try { disk=io::fingerprintFile(currentFile); }
        catch(const std::exception&) { /* Preserve recovery, detach from an unreadable source. */ }
        if(!dirty&&disk) {
            // A clean checkpoint represents a saved file, not unsaved work.
            // Honour subsequent edits made by another XM tracker.
            try {doc=io::decodeXm(io::readFile(currentFile));sourceFingerprint=disk;}
            catch(const std::exception&){disk.reset();}
        }
        if(!disk || (dirty && (!sourceFingerprint || *sourceFingerprint!=*disk))) {
            // Keep both versions. Never silently overwrite an externally
            // changed or missing original with an older recovery snapshot.
            currentFile.clear();sourceFingerprint.reset();dirty=true;
            notify("RECOVERED WORK / SOURCE CHANGED OR MISSING / USE SAVE AS");
        } else if(dirty) notify("RECOVERED LAST WORK / SAVE TO UPDATE THE XM FILE");
        browserDirectory=browserDefaultFolder(0);
        savedRecoverySerial=recoverySerial;
    }

    void drawNotice() {
        if(message.empty()||Sint32(noticeUntil-SDL_GetTicks())<=0)return;
        // Temporarily replace the footer, including its hints, so notices never
        // cover pattern rows. Draw last to keep errors visible inside dialogs.
        rect(0,736,W,32,panel);
        rect(20,736,984,1,panel2);
        txt(20,746,ui::fit(message,82),errorMessage?red:green);
    }
