        // Real SDL input path and headless MilkyPlay output; isolated --ui-test
        // data directory. These checks also capture the actual application UI.
        playback->stop();doc=makeDemoDocument();doc.orders={0,0};
        edit::resizePattern(doc.patterns[0],doc.channels,64,false);
        order=1;row=19;channel=field=songField=0;page=Page::Song;mode=Mode::Normal;
        songRowFocus={{6,-1,-1,4,-1}};
        playback->load(doc);followPlayback=false;
        tap(Button::Start);
        require(playback->status().playing&&!playingPattern&&playback->status().order==1&&row==0,"Song Start begins selected order at row zero");
        changePage(Page::Pattern);
        require(playback->status().playing&&!playingPattern,"navigation preserves song transport scope");
        tap(Button::Start);require(!playback->status().playing,"Start in another view stops playback");
        for(const auto view:{Page::Song,Page::Pattern,Page::Instrument,Page::Sample}) {
            changePage(view);order=1;row=19;clearSelection();
            songAppendOrder=view==Page::Song;
            selectChord(Button::Start);
            require(!playingPattern&&playback->status().playing&&playback->status().order==0&&
                    playback->status().row==0&&followPlayback&&!songAppendOrder&&page==view&&mode==Mode::Normal,
                    "Select+Start begins the whole song in every main view, including the Song append row");
            selectChord(Button::Start);
            require(!playback->status().playing&&mode==Mode::Normal,"Select+Start while playing stops without a menu");
        }
        changePage(Page::Pattern);
        row=19;tap(Button::Start);
        require(playingPattern&&playback->status().row==0&&followPlayback,"Pattern Start loops from row zero and restores follow");
        std::vector<std::int16_t> followPcm(1200*2);
        for(int i=0;i<200&&playback->status().row<32;++i)playback->readFrames(followPcm.data(),1200);
        draw();
        require(row>=32&&patternTopRow()==32,"follow advances by a whole page after the last visible row");
        const auto beforeManualRow=row;
        tap(Button::Up);const auto editRow=row;
        require(!followPlayback&&editRow==beforeManualRow-1,"manual navigation suspends page follow");
        for(int i=0;i<15;++i)playback->readFrames(followPcm.data(),1200);
        draw();require(row==editRow,"playback never drags a manually positioned cursor");
        chord({Button::Right});
        require(playback->status().playing&&!followPlayback,"cell editing leaves transport running and page fixed");
        changePage(Page::Song);draw();
        require(playingPattern&&!followPlayback,"screen navigation neither changes scope nor re-enables follow");
        tap(Button::Start);tap(Button::Start);
        require(!playingPattern&&followPlayback&&row==0,"next Start uses Song scope and re-enables follow");
        tap(Button::Start);
        tap(Button::Start);mode=Mode::Busy;busyTitle="RENDERING WAV";
        tap(Button::Start);require(!playback->status().playing&&mode==Mode::Busy,"Start stops transport even while a background tool is visible");
        mode=Mode::Normal;
        songField=1;tap(Button::Down);require(songField==5,"Song downward navigation follows channel-setting position");
        tap(Button::Right);require(songField==3,"Song right navigation follows restart-setting position");
        tap(Button::Up);require(songField==1,"Song settings return to the prior row's remembered BPM field");
        menu("SETTINGS");
        require(mode==Mode::Menu&&menuTitle=="SETTINGS","Actions opens application settings");
        require(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.label=="CONTROLS";}),"Controls remains in Actions without a duplicate in Settings");
        require(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.label.find("FOLLOW")!=std::string::npos;}),"no playback-follow preference exists");
        tap(Button::B);require(mode==Mode::Menu&&menuTitle=="SONG ACTIONS","Settings returns to its Actions menu");tap(Button::B);
        songField=6;tap(Button::A);filename=u8"A한";
        for(int n=0;n<3;++n)tap(Button::Down);
        for(int n=0;n<8;++n)tap(Button::Right);
        require(nameCursor==44,"name editor exposes delete as a reachable A button");tap(Button::A);
        require(filename=="A","name delete preserves whole UTF-8 characters");tap(Button::B);
        noticeUntil=0;

        // Save creates a manual backup; recovery preserves edits without
        // replacing the named XM and records the original path for reopening.
        doc.name="Session QA";dirty=true;++recoverySerial;
        const auto sessionPath=home/"session-qa.xm";
        saveTo(sessionPath,FileAction::SaveXm);
        const auto diskBefore=io::readFile(sessionPath);
        changePage(Page::Pattern);row=0;field=0;
        auto changed=cell();changed.note=61;editCell(changed);flushRecovery();
        require(io::readFile(sessionPath)==diskBefore,"recovery never overwrites named XM");
        const auto sessionRecovery=io::RecoveryStore(home).latest();
        require(sessionRecovery&&sessionRecovery->document.patterns[0].at(0,0,doc.channels).note==61&&
            sessionRecovery->sourcePath==std::filesystem::absolute(sessionPath),"recovery retains the edited document and source path");
        changed.note=62;editCell(changed);lastRecoveryAt=SDL_GetTicks()-30000u;
        options.uiTest=false;pollRecovery();options.uiTest=true;
        require(recoveryWorker.valid(),"changed work is scheduled for background autosave");
        changed.note=63;editCell(changed);
        saveTo(sessionPath,FileAction::SaveXm);
        require(io::readFile(home/"Backup"/"session-qa.xm")==diskBefore,"explicit Save backs up the previous XM");
        const auto cleanRecovery=io::RecoveryStore(home).latest();
        require(cleanRecovery&&!cleanRecovery->dirty&&cleanRecovery->sourceFingerprint==io::fingerprintFile(sessionPath),"manual Save immediately replaces stale recovery with a clean checkpoint");
        require(cleanRecovery->document.patterns[0].at(0,0,doc.channels).note==63,"older in-flight autosave cannot replace newer manual-save recovery");
        auto external=doc;external.name="External edit";
        io::writeFileAtomic(sessionPath,io::encodeXm(external));restoreLastWork();
        require(!dirty&&doc.name=="External edit"&&!currentFile.empty(),"clean recovery opens newer external XM edits");
        loadEngine();changed=cell();changed.note=64;editCell(changed);flushRecovery();
        external.name="External edit two";io::writeFileAtomic(sessionPath,io::encodeXm(external));
        restoreLastWork();
        require(dirty&&currentFile.empty()&&cell().note==64,"dirty recovery detaches from an externally changed source");
        require(io::decodeXm(io::readFile(sessionPath)).name=="External edit two","external changes survive recovery");

        // A reproducible eight-channel demo for actual renderer captures.
        playback->stop();doc=makeDemoDocument();
        const auto previewPattern=doc.patterns[0];
        for(int n=1;n<5;++n)doc.patterns.push_back(previewPattern);
        doc.orders={0,1,0,2,3,2,0,4};
        for(auto& p:doc.patterns)for(int c=1;c<8;++c)for(int r=0;r<p.rows;r+=4){
            auto value=p.at(r,0,doc.channels);value.note=std::uint8_t(std::clamp(int(value.note)+(c%4)*3-(c>3?12:0),1,96));
            value.volume=std::uint8_t(0x30+(c%4)*3);p.at(r,c,doc.channels)=value;
        }
        doc.patterns[0].at(0,0,doc.channels).effect=4;doc.patterns[0].at(0,0,doc.channels).param=0x47;
        currentFile=home/"preview-song.xm";io::writeFileAtomic(currentFile,io::encodeXm(doc));
        order=2;row=channel=0;field=4;dirty=true;mode=Mode::Normal;followPlayback=false;
        normalize();playback->load(doc);noticeUntil=0;
        changePage(Page::Pattern);draw();screenshot(home/"current-pattern.bmp");SDL_RenderPresent(renderer);
        changePage(Page::Song);songField=0;playToggle();
        std::vector<std::int16_t> previewAudio(6000*2);playback->readFrames(previewAudio.data(),6000);
        songMonitorValid=false;draw();screenshot(home/"current-song-8.bmp");SDL_RenderPresent(renderer);
        require(songMonitorSnapshot.channelCount==8&&songMonitorSnapshot.channels[0].active,"Song shows actual active channel scopes");
        playToggle();resizeDocumentChannels(32);noticeUntil=0;playToggle();playback->readFrames(previewAudio.data(),6000);
        songMonitorValid=false;draw();screenshot(home/"current-song-32.bmp");SDL_RenderPresent(renderer);
        require(songMonitorSnapshot.channelCount==32,"monitor layout follows the configured 32 channels");
        playToggle();resizeDocumentChannels(1);noticeUntil=0;
        // Shrinking populated channels requests explicit confirmation.
        if(mode==Mode::Confirm)tap(Button::A);
        songMonitorValid=false;draw();
        require(doc.channels==1&&songMonitorSnapshot.channelCount==1,"channel reduction updates monitor and requires discard confirmation");
        openEditorSettings();noticeUntil=0;draw();screenshot(home/"current-settings.bmp");SDL_RenderPresent(renderer);
        mode=Mode::Normal;resizeDocumentChannels(8);noticeUntil=0;changePage(Page::Pattern);row=0;field=4;
