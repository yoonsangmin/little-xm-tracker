        {
            // --ui-test uses headless playback: exercise the real controls and
            // persistence without opening or relying on an audio device.
            playback->stop();audioSettings={};playback->configure(audioSettings);saveEditorSettings();
            doc=makeDemoDocument();page=Page::Song;mode=Mode::Normal;songField=0;
            order=row=selectedPattern=channel=field=0;clearSelection();held.fill(false);
            normalize();playback->load(doc);playback->setSolo(-1);playback->setMuteMask(2);solo=-1;
            const auto audioSong=io::encodeXm(doc);const auto audioDirty=dirty;
            const auto audioUndo=undoStack.size(),audioRedo=redoStack.size(),audioUndoBytes=undoBytes;
            const auto audioRevision=documentRevision,audioRecovery=recoverySerial;
            const auto audioFile=currentFile;
            const auto sameAudio=[](const AudioSettings& a,const AudioSettings& b){
                return a.sampleRate==b.sampleRate&&a.interpolation==b.interpolation;
            };
            auto openAudioMenu=[&]{
                menu("SETTINGS");
                const auto item=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="AUDIO OUTPUT";});
                require(item!=actions.end(),"Song Settings exposes Audio Output");
                const int target=int(std::distance(actions.begin(),item));
                while(menuIndex<target)tap(Button::Down);
                while(menuIndex>target)tap(Button::Up);
                tap(Button::A);
                require(mode==Mode::Menu&&menuTitle=="AUDIO OUTPUT"&&menuIndex==0,"Audio Output opens its draft submenu");
            };
            const auto initialSettings=io::readFile(home/"editor-settings.txt");
            playback->playSong();followPlayback=true;
            openAudioMenu();chord({Button::Left});tap(Button::Down);chord({Button::Right});
            require(audioSettingsDraft.sampleRate==44100&&audioSettingsDraft.interpolation==Interpolation::CubicLagrange,
                    "audio menu adjusts draft output rate and interpolation with A+directions");
            require(sameAudio(audioSettings,AudioSettings{})&&sameAudio(playback->audioSettings(),audioSettings)&&playback->status().playing,
                    "draft audio edits leave the applied engine and playback unchanged");
            require(io::readFile(home/"editor-settings.txt")==initialSettings,"draft audio edits are not persisted");
            noticeUntil=0;draw();screenshot(home/"audio-output-settings.bmp");SDL_RenderPresent(renderer);
            tap(Button::B);require(mode==Mode::Menu&&menuTitle=="SETTINGS","B returns from Audio Output to Settings");
            tap(Button::B);openAudioMenu();
            require(sameAudio(audioSettingsDraft,audioSettings)&&playback->status().playing,"B discards the draft and reopens with applied values");
            tap(Button::Down);tap(Button::Down);tap(Button::A);
            require(playback->status().playing&&io::readFile(home/"editor-settings.txt")==initialSettings,
                    "applying unchanged audio settings preserves playback and preferences");
            tap(Button::B);openAudioMenu();chord({Button::Left});tap(Button::Down);chord({Button::Left,Button::Left});
            const std::array<Interpolation,6> modes={Interpolation::Nearest,Interpolation::Linear,Interpolation::CubicLagrange,
                Interpolation::CubicSpline,Interpolation::Sinc,Interpolation::SincPrecise};
            for(std::size_t n=0;n<modes.size();++n) {
                require(audioSettingsDraft.interpolation==modes[n]&&actions[1].label==std::string("INTERPOLATION: ")+interpolationLabel(modes[n]),
                        "all six interpolation modes are reachable and named in the menu");
                chord({Button::Right});
            }
            require(audioSettingsDraft.interpolation==Interpolation::SincPrecise,"interpolation stays within the supported six modes");
            tap(Button::Down);tap(Button::A);
            require(mode==Mode::Menu&&menuTitle=="SETTINGS"&&audioSettings.sampleRate==44100&&audioSettings.interpolation==Interpolation::SincPrecise,
                    "Apply commits the selected audio settings and returns to Settings");
            require(sameAudio(playback->audioSettings(),audioSettings)&&!playback->status().playing&&!followPlayback&&audioReady==playback->isOpen(),
                    "applying changed audio settings stops playback and synchronizes engine availability");
            require(playback->muteMask()==2,"applying audio output preserves session channel mutes");
            tap(Button::B);
            const auto appliedAudio=audioSettings;
            audioSettings={};loadEditorSettings();
            require(sameAudio(audioSettings,appliedAudio),"output rate and interpolation survive preference reload");
            Playback restoredAudio(audioSettings);
            require(sameAudio(restoredAudio.audioSettings(),appliedAudio),"startup can construct the engine with the saved audio settings");
            const std::string invalidAudio="output_sample_rate 0\noutput_sample_rate 96000\ninterpolation -1\ninterpolation 6\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(invalidAudio.begin(),invalidAudio.end()));
            loadEditorSettings();require(sameAudio(audioSettings,appliedAudio),"unsupported rate and interpolation preferences are ignored");
            audioSettings={};loadEditorSettings();
            require(sameAudio(audioSettings,AudioSettings{}),"invalid saved audio settings keep the 48 kHz linear defaults");
            audioSettings=appliedAudio;saveEditorSettings();
            const auto persistedAudio=io::readFile(home/"editor-settings.txt");
            openAudioMenu();audioSettingsDraft.sampleRate=96000;audioReady=true;
            tap(Button::Down);tap(Button::Down);
            bool rejectedAudio=false;
            try {tap(Button::A);}catch(const std::exception&){rejectedAudio=true;}
            require(rejectedAudio&&sameAudio(audioSettings,appliedAudio)&&sameAudio(playback->audioSettings(),appliedAudio)&&audioReady==playback->isOpen(),
                    "failed audio configuration keeps applied settings and refreshes audio availability");
            require(io::readFile(home/"editor-settings.txt")==persistedAudio,"failed audio configuration never replaces saved preferences");

            // Deliberately stale dialog defaults and a pending draft must not
            // override the currently applied audio settings of a WAV render.
            renderOptions={};renderOptions.maxSeconds=0.2;renderOptions.tailSeconds=0;
            renderOptions.audioSettings={48000,Interpolation::Nearest};audioSettingsDraft=renderOptions.audioSettings;
            auto expectedRender=renderOptions;expectedRender.audioSettings=appliedAudio;
            const auto audioRenderPath=home/"audio-settings-render.wav";
            startRender(audioRenderPath);
            renderOptions.maxSeconds=0.4;renderOptions.audioSettings={48000,Interpolation::Linear};
            const auto audioRenderStarted=SDL_GetTicks();
            while(worker.valid()&&SDL_GetTicks()-audioRenderStarted<10000u){pollTools();SDL_Delay(1);}
            require(!worker.valid()&&mode==Mode::Normal,"audio settings render completes through the worker");
            const auto referencePath=home/"audio-settings-reference.wav";
            renderWav(doc,referencePath,expectedRender);
            const auto renderedAudio=io::readFile(audioRenderPath);
            require(renderedAudio==io::readFile(referencePath),"render captures applied audio settings and duration when it starts");
            require(renderedAudio.size()>28&&renderedAudio[24]==0x44&&renderedAudio[25]==0xac&&renderedAudio[26]==0&&renderedAudio[27]==0,
                    "song WAV uses the applied 44.1 kHz output rate");
            require(io::encodeXm(doc)==audioSong&&dirty==audioDirty&&undoStack.size()==audioUndo&&redoStack.size()==audioRedo&&undoBytes==audioUndoBytes&&
                    documentRevision==audioRevision&&recoverySerial==audioRecovery&&currentFile==audioFile&&order==0&&row==0&&channel==0,
                    "audio preferences and render leave song data, edit history, recovery, file path and cursor unchanged");

            // Restore the documented defaults through the same Apply route.
            openAudioMenu();chord({Button::Right});tap(Button::Down);
            chord({Button::Left,Button::Left,Button::Left,Button::Left});tap(Button::Down);tap(Button::A);tap(Button::B);
            require(sameAudio(audioSettings,AudioSettings{})&&sameAudio(playback->audioSettings(),audioSettings),"audio defaults can be restored from the menu");
            playback->setMuteMask(0);renderOptions={};
        }
