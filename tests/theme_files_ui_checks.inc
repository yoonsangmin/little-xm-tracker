        {
            const auto savedThemePreferences=io::readFile(home/"editor-settings.txt");
            const auto savedThemePalette=palette;
            const auto themeFileSong=io::encodeXm(doc);
            const auto themeFileDirty=dirty;
            const auto themeFileUndo=undoStack.size(),themeFileRedo=redoStack.size();
            const auto themeFileRevision=documentRevision,themeFileRecovery=recoverySerial;
            const auto themeFileCurrent=currentFile;
            const auto paletteRgb=[](const auto& colors){
                io::Theme result{};
                for(std::size_t n=0;n<result.size();++n)
                    result[n]=(std::uint32_t(colors[n].r)<<16)|(std::uint32_t(colors[n].g)<<8)|colors[n].b;
                return result;
            };
            require(themeDirectory==contentDirectory/"themes","theme files use the fixed themes folder beside the portable content");
            for(const char* name:{"default.theme","amber.theme","midnight.theme","mono.theme"})
                require(io::decodeTheme(io::readFile(themeDirectory/name)).size()==palette.size(),"all four shipped presets are available as valid theme files");
            const auto token=number(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            const auto sharedPath=themeDirectory/("qa-shared-"+token+".THEME");
            auto shared=io::defaultTheme;shared[0]=0x22152d;shared[6]=0xe9a5ff;
            io::writeFileAtomic(sharedPath,io::encodeTheme(shared));
            const auto malformedPath=themeDirectory/("qa-malformed-"+token+".theme");
            io::writeFileAtomic(malformedPath,{'b','a','d'});
            io::writeFileAtomic(themeDirectory/("qa-ignored-"+token+".txt"),{'x'});
            const auto blockedName="qa-blocked-"+token;
            const auto blockedPath=themeDirectory/(blockedName+".theme");
            std::filesystem::create_directory(blockedPath);
            io::writeFileAtomic(blockedPath/"keep",{'x'});
            for(int n=0;n<16;++n)io::writeFileAtomic(themeDirectory/("qa-page-"+token+"-"+number(n)+".theme"),io::encodeTheme(shared));

            clearSelection();mode=Mode::Normal;held.fill(false);menuParents.clear();themeRole=0;
            menu("SETTINGS");
            const auto themeItem=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label=="THEME COLORS";});
            require(themeItem!=actions.end(),"Theme Colors is reachable from Settings for file operations");
            const int themeTarget=int(std::distance(actions.begin(),themeItem));
            while(menuIndex<themeTarget)tap(Button::Down);
            while(menuIndex>themeTarget)tap(Button::Up);
            tap(Button::A);
            auto openThemePicker=[&](bool savingTheme){
                require(mode==Mode::ThemeColors,"theme file operations start in the color editor");
                while(themeField<4)tap(Button::Down);
                while(themeField>4)tap(Button::Up);
                tap(savingTheme?Button::Right:Button::Left);tap(Button::A);
                require(mode==Mode::ThemeFiles&&themeSaving==savingTheme&&themeFileIndex==0&&themeEditing,
                        "theme Load and Save open their own picker at the first entry");
            };
            auto focusThemePath=[&](const std::filesystem::path& path){
                const auto found=std::find(themeFiles.begin(),themeFiles.end(),path);
                require(found!=themeFiles.end(),"requested theme file is visible in the picker");
                const int target=int(std::distance(themeFiles.begin(),found))+(themeSaving?1:0);
                while(themeFileIndex<target)tap(Button::Down);
                while(themeFileIndex>target)tap(Button::Up);
            };
            openThemePicker(false);
            require(std::all_of(themeFiles.begin(),themeFiles.end(),[&](const auto& path){
                return path.parent_path()==themeDirectory&&std::filesystem::is_regular_file(path)&&lower(path.extension().u8string())==".theme";
            }),"theme picker stays in its fixed folder and filters other extensions and directories");
            tap(Button::Left);require(themeFileIndex==0,"theme page-left clamps at the first file");
            tap(Button::Right);require(themeFileIndex==13,"theme page-right advances thirteen entries");
            tap(Button::Left);require(themeFileIndex==0,"theme page-left returns one page");
            for(std::size_t n=0;n<themeFiles.size();++n)tap(Button::Right);
            require(themeFileIndex==int(themeFiles.size())-1,"repeated theme paging clamps at the last file without wrapping");
            focusThemePath(sharedPath);tap(Button::A);
            require(mode==Mode::ThemeColors&&paletteRgb(themeDraft)==shared&&paletteRgb(palette)==paletteRgb(savedThemePalette)&&
                    io::readFile(home/"editor-settings.txt")==savedThemePreferences,
                    "loading a shared theme changes only the draft until Apply");
            while(themeField>1)tap(Button::Up);
            chord({Button::Right});
            const auto firstSavedTheme=paletteRgb(themeDraft);
            openThemePicker(true);tap(Button::A);
            require(mode==Mode::Name&&nameTarget==NameTarget::Theme&&filename.empty(),"New Theme opens an empty gamepad filename editor");
            filename="../outside";bool badThemeName=false;
            try {tap(Button::Start);}catch(const std::exception&){badThemeName=true;buttonInput(Button::Start,false);}
            require(badThemeName&&mode==Mode::Name,"theme filenames cannot escape the fixed directory");
            const auto roundtripName="qa-roundtrip-"+token;
            const auto roundtripPath=themeDirectory/(roundtripName+".theme");
            filename=roundtripName;tap(Button::Start);
            require(mode==Mode::ThemeColors&&io::decodeTheme(io::readFile(roundtripPath))==firstSavedTheme,
                    "New Theme saves all draft colors in a portable theme file");

            while(themeField>1)tap(Button::Up);
            chord({Button::Right});const auto overwrittenTheme=paletteRgb(themeDraft);
            openThemePicker(true);focusThemePath(roundtripPath);tap(Button::A);
            require(mode==Mode::Confirm,"saving an existing theme asks before overwriting");
            tap(Button::B);
            require(mode==Mode::ThemeFiles&&paletteRgb(themeDraft)==overwrittenTheme&&
                    io::decodeTheme(io::readFile(roundtripPath))==firstSavedTheme,
                    "canceling overwrite retains the draft and the existing file");
            tap(Button::A);require(mode==Mode::Confirm,"overwrite cancellation preserves the selected file");tap(Button::A);
            require(mode==Mode::ThemeColors&&io::decodeTheme(io::readFile(roundtripPath))==overwrittenTheme,
                    "confirming overwrite saves the edited theme to its existing file");
            openThemePicker(false);focusThemePath(malformedPath);
            const int malformedIndex=themeFileIndex;bool rejectedThemeFile=false;
            try {tap(Button::A);}catch(const std::exception&){rejectedThemeFile=true;}
            require(rejectedThemeFile&&mode==Mode::ThemeFiles&&themeFileIndex==malformedIndex&&paletteRgb(themeDraft)==overwrittenTheme,
                    "malformed theme load preserves the complete draft and picker selection");
            tap(Button::B);require(mode==Mode::ThemeColors,"B returns from theme files to the color editor");

            openThemePicker(true);tap(Button::A);filename=blockedName;tap(Button::Start);
            require(mode==Mode::Confirm,"an existing destination is confirmed before a theme write");
            bool themeWriteFailed=false;
            try {tap(Button::A);}catch(const std::exception&){themeWriteFailed=true;buttonInput(Button::A,false);}
            require(themeWriteFailed&&mode==Mode::ThemeFiles&&paletteRgb(themeDraft)==overwrittenTheme&&
                    io::readFile(blockedPath/"keep")==std::vector<std::uint8_t>{'x'},
                    "theme write failure returns to the picker without discarding the draft or damaging the destination");
            tap(Button::B);openThemePicker(false);focusThemePath(roundtripPath);tap(Button::A);
            require(paletteRgb(themeDraft)==overwrittenTheme&&paletteRgb(palette)==paletteRgb(savedThemePalette)&&
                    io::readFile(home/"editor-settings.txt")==savedThemePreferences,
                    "saved themes reload exactly while file operations leave the applied palette and preferences unchanged");
            tap(Button::Down);tap(Button::Right);tap(Button::Right);tap(Button::A);
            require(mode==Mode::Menu&&!themeEditing&&paletteRgb(palette)==overwrittenTheme,"Apply activates the loaded theme and exits theme editing");
            palette=defaultPalette;loadEditorSettings();
            require(paletteRgb(palette)==overwrittenTheme,"an applied shared theme survives an application preference reload");
            require(io::encodeXm(doc)==themeFileSong&&dirty==themeFileDirty&&currentFile==themeFileCurrent&&
                    undoStack.size()==themeFileUndo&&redoStack.size()==themeFileRedo&&
                    documentRevision==themeFileRevision&&recoverySerial==themeFileRecovery,
                    "theme load, save, overwrite and failures never change the song or its history");

            io::writeFileAtomic(home/"editor-settings.txt",savedThemePreferences);palette=savedThemePalette;loadEditorSettings();
            themeDraft=palette;themeEditing=false;themeSaving=false;themeRole=themeField=themeAction=themeFileAction=themeFileIndex=0;
            themeFiles.clear();nameTarget=NameTarget::File;filename.clear();confirmed={};
            mode=Mode::Normal;held.fill(false);menuParents.clear();noticeUntil=0;
        }
