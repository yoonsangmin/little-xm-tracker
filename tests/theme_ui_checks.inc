        {
            // Exercise the same controller events as the device. Theme edits
            // are application preferences, never edits to the loaded XM.
            playback->stop();playback->stopPreview();clearSelection();held.fill(false);
            page=Page::Pattern;mode=Mode::Normal;menuParents.clear();followPlayback=false;
            palette=defaultPalette;themeRole=themeField=themeAction=0;saveEditorSettings();
            const auto themePreferences=io::readFile(home/"editor-settings.txt");
            const auto themeSong=io::encodeXm(doc);
            const auto themeFile=currentFile;
            const auto themeDirty=dirty;
            const auto themeUndo=undoStack.size(),themeRedo=redoStack.size(),themeUndoBytes=undoBytes;
            const auto themeRevision=documentRevision,themeRecovery=recoverySerial;
            const auto themeAudio=audioSettings;
            const auto themeCursor=std::array<int,5>{{order,selectedPattern,row,channel,field}};
            const auto sameColor=[](SDL_Color a,SDL_Color b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;};
            const auto samePalette=[&](const auto& a,const auto& b){
                return std::equal(a.begin(),a.end(),b.begin(),sameColor);
            };
            auto closeThemeMenus=[&]{
                for(int n=0;n<4&&mode==Mode::Menu;++n)tap(Button::B);
                require(mode==Mode::Normal,"theme editor returns through Settings to the underlying screen");
            };
            auto openThemeMenu=[&]{
                menu("SETTINGS");
                const auto item=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="THEME COLORS";});
                require(item!=actions.end(),"Settings exposes Theme Colors alongside the other application settings");
                const int target=int(std::distance(actions.begin(),item));
                while(menuIndex<target)tap(Button::Down);
                while(menuIndex>target)tap(Button::Up);
                tap(Button::A);
                require(mode==Mode::ThemeColors&&themeField==0&&themeAction==0,
                        "Theme Colors opens on the role row with its action position reset");
            };
            auto themeButtons=[&]{
                for(int n=0;n<6&&themeField<5;++n)tap(Button::Down);
                require(themeField==5,"the final theme action row is reachable below the file buttons");
            };
            auto themeApply=[&]{
                themeButtons();tap(Button::Right);tap(Button::Right);tap(Button::A);
                require(mode==Mode::Menu&&menuTitle=="SETTINGS","Apply returns the theme editor to Settings");
            };

            playback->setMuteMask(2);playback->playSong();
            std::array<std::int16_t,4096> themePcm{};playback->readFrames(themePcm.data(),themePcm.size()/2);
            const auto themeTransport=playback->status();
            openThemeMenu();tap(Button::Up);tap(Button::A);chord({Button::Left});
            require(themeField==0&&themeRole==0&&samePalette(themeDraft,defaultPalette),
                    "role navigation clamps at the first color and A alone does not edit a value");
            for(std::size_t n=1;n<themeDraft.size();++n) {
                chord({Button::Right});require(themeRole==int(n),"every theme role is reachable with A+Right");
            }
            chord({Button::Right,Button::Up});
            require(themeRole==12,"role navigation clamps at the final overlay color");
            tap(Button::Down);chord({Button::Right});
            require(themeDraft[12].r==defaultPalette[12].r+1&&themeDraft[12].a==225,
                    "editing overlay RGB preserves its fixed translucency");
            tap(Button::B);require(mode==Mode::Menu&&menuTitle=="SETTINGS","B cancels the theme draft back to Settings");
            closeThemeMenus();openThemeMenu();
            require(themeRole==12&&samePalette(themeDraft,palette),"reopening remembers the color role but discards canceled edits");
            for(int n=0;n<12;++n)chord({Button::Left});
            tap(Button::Down);chord({Button::Right,Button::Up});
            require(themeDraft[0].r==defaultPalette[0].r+17&&themeField==1,
                    "A+horizontal changes RGB by one and A+vertical changes it by sixteen without moving focus");
            for(int n=0;n<18;++n)chord({Button::Up});
            require(themeDraft[0].r==255,"RGB increments clamp at 255");
            for(int n=0;n<18;++n)chord({Button::Down});
            require(themeDraft[0].r==0,"RGB decrements clamp at zero");
            require(samePalette(palette,defaultPalette)&&io::readFile(home/"editor-settings.txt")==themePreferences,
                    "draft color edits neither recolor the application nor save preferences");
            tap(Button::B);closeThemeMenus();openThemeMenu();
            require(samePalette(themeDraft,defaultPalette),"canceling saturated RGB edits leaves no draft residue");

            tap(Button::Down);chord({Button::Right});tap(Button::Up);chord({Button::Right});
            tap(Button::Down);chord({Button::Right});themeButtons();tap(Button::Left);tap(Button::A);
            require(sameColor(themeDraft[1],defaultPalette[1])&&!sameColor(themeDraft[0],defaultPalette[0]),
                    "Reset Color changes only the selected draft role");
            tap(Button::Right);tap(Button::A);
            require(samePalette(themeDraft,defaultPalette)&&io::readFile(home/"editor-settings.txt")==themePreferences,
                    "Reset All restores the draft only, without saving");
            tap(Button::Right);tap(Button::Right);tap(Button::Down);
            require(themeField==5&&themeAction==2,"button row navigation clamps at its bottom and right edges");
            tap(Button::Up);require(themeField==4&&themeAction==2,"Up leaves a horizontal button row immediately");
            tap(Button::Down);require(themeField==5&&themeAction==2,"returning to buttons restores their last horizontal position");
            while(themeField>0)tap(Button::Up);
            chord({Button::Left});
            // Give every saved role a nondefault component, including both
            // selection backgrounds and the modal overlay.
            for(std::size_t n=0;n<themeDraft.size();++n) {
                require(themeRole==int(n),"theme draft edits target the intended color role");
                tap(Button::Down);tap(Button::Down);chord({Button::Right});
                tap(Button::Up);tap(Button::Up);
                if(n+1<themeDraft.size())chord({Button::Right});
            }
            const auto appliedTheme=themeDraft;
            noticeUntil=0;draw();screenshot(home/"theme-colors.bmp");SDL_RenderPresent(renderer);
            require(samePalette(palette,defaultPalette),"drawing the live theme preview restores the applied palette");
            themeApply();closeThemeMenus();
            require(samePalette(palette,appliedTheme),"Apply commits every edited theme role");
            palette=defaultPalette;loadEditorSettings();
            require(samePalette(palette,appliedTheme),"all theme colors and overlay alpha survive a preference reload");
            noticeUntil=0;draw();screenshot(home/"theme-pattern.bmp");
            int themeWidth=0,themeHeight=0;SDL_GetRendererOutputSize(renderer,&themeWidth,&themeHeight);
            if(themeWidth==W&&themeHeight==H) {
                const SDL_Rect pixel{0,100,1,1};std::uint32_t color=0;
                require(SDL_RenderReadPixels(renderer,&pixel,SDL_PIXELFORMAT_ARGB8888,&color,sizeof(color))==0,
                        "the applied theme background can be read from the production renderer");
                const auto expected=(std::uint32_t(palette[0].r)<<16)|(std::uint32_t(palette[0].g)<<8)|palette[0].b;
                require((color&0xffffffu)==expected,"the Pattern screen uses the applied background color");
            }
            SDL_RenderPresent(renderer);

            // A regular file cannot contain editor-settings.txt. This forces a
            // real atomic-write failure without depending on Unix permissions.
            openThemeMenu();tap(Button::Down);chord({Button::Right});
            themeButtons();tap(Button::Right);tap(Button::Right);
            const auto persistedTheme=io::readFile(home/"editor-settings.txt");
            const auto themeHome=home,blockedThemeHome=home/"theme-settings-not-a-directory";
            io::writeFileAtomic(blockedThemeHome,std::vector<std::uint8_t>{'x'});
            home=blockedThemeHome;bool failedThemeSave=false;
            try {tap(Button::A);}catch(const std::exception&){failedThemeSave=true;}
            home=themeHome;
            require(failedThemeSave&&mode==Mode::ThemeColors&&samePalette(palette,appliedTheme),
                    "a failed theme save rolls back the applied palette and keeps the editor open");
            require(io::readFile(home/"editor-settings.txt")==persistedTheme,"failed theme persistence preserves the previous settings file");
            tap(Button::B);closeThemeMenus();

            const auto oldThemeRows=patternViewRows;
            const std::string malformedTheme="theme_background invalid\ntheme_panel -1\ntheme_highlight 16777216\n"
                "theme_text 123 trailing\ntheme_overlay 99999999999999999999999999\n"
                "theme_background 66051\ntheme_overlay 263430\npattern_view_rows 16\ntheme_accent 197637\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(malformedTheme.begin(),malformedTheme.end()));
            loadEditorSettings();
            require(sameColor(palette[1],appliedTheme[1])&&sameColor(palette[2],appliedTheme[2])&&sameColor(palette[3],appliedTheme[3]),
                    "negative, out-of-range and trailing-text theme values are ignored");
            require(sameColor(palette[0],SDL_Color{1,2,3,255})&&sameColor(palette[12],SDL_Color{4,5,6,225})&&
                    sameColor(palette[6],SDL_Color{3,4,5,255})&&patternViewRows==16,
                    "malformed or overflowing theme lines do not prevent later valid theme and ordinary settings from loading");
            const auto afterThemeTransport=playback->status();
            require(afterThemeTransport.playing&&afterThemeTransport.order==themeTransport.order&&
                    afterThemeTransport.row==themeTransport.row&&afterThemeTransport.elapsedSeconds==themeTransport.elapsedSeconds&&
                    playback->muteMask()==2&&playback->audioSettings().sampleRate==themeAudio.sampleRate&&
                    playback->audioSettings().interpolation==themeAudio.interpolation,
                    "theme preview, apply, reset, cancel and save failure preserve playback position, mutes and audio configuration");
            require(io::encodeXm(doc)==themeSong&&dirty==themeDirty&&currentFile==themeFile&&
                    undoStack.size()==themeUndo&&redoStack.size()==themeRedo&&undoBytes==themeUndoBytes&&
                    documentRevision==themeRevision&&recoverySerial==themeRecovery&&
                    std::array<int,5>{{order,selectedPattern,row,channel,field}}==themeCursor,
                    "theme preferences leave song data, dirty state, history, recovery and editor cursor unchanged");

            io::writeFileAtomic(home/"editor-settings.txt",themePreferences);loadEditorSettings();
            require(samePalette(palette,defaultPalette)&&patternViewRows==oldThemeRows,
                    "theme regressions restore the original preferences and default palette");
            themeDraft=palette;themeRole=themeField=themeAction=0;
            playback->stop();playback->setMuteMask(0);noticeUntil=0;
        }
