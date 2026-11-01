    void openThemeColors() {
        finishGesture();themeDraft=palette;themeField=themeAction=themeFileAction=0;
        themeEditing=true;mode=Mode::ThemeColors;
    }

    bool themeButton(Button b) {
        if(mode==Mode::ThemeFiles) {
            if(b==Button::B){mode=Mode::ThemeColors;return true;}
            const int count=int(themeFiles.size())+(themeSaving?1:0);
            if(b==Button::Up||b==Button::Down||b==Button::Left||b==Button::Right) {
                const int step=b==Button::Left||b==Button::Right?13:1;
                themeFileIndex=std::clamp(themeFileIndex+(b==Button::Down||b==Button::Right?step:-step),0,std::max(0,count-1));
                if(held[std::size_t(Button::A)])aUsed=true;
            }
            return true;
        }
        if(mode!=Mode::ThemeColors)return false;
        if(b==Button::B){themeEditing=false;mode=Mode::Menu;return true;}
        if(int(b)>=4)return true;
        const bool horizontal=b==Button::Left||b==Button::Right;
        if(held[std::size_t(Button::A)]) {
            aUsed=true;
            const int direction=b==Button::Right||b==Button::Up?1:-1;
            if(themeField==0)themeRole=std::clamp(themeRole+direction,0,int(themeDraft.size())-1);
            else if(themeField<4) {
                auto& color=themeDraft[std::size_t(themeRole)];
                auto& component=themeField==1?color.r:themeField==2?color.g:color.b;
                component=Uint8(std::clamp(int(component)+direction*(horizontal?1:16),0,255));
            }
        } else if(horizontal) {
            if(themeField==4)themeFileAction=std::clamp(themeFileAction+(b==Button::Right?1:-1),0,1);
            if(themeField==5)themeAction=std::clamp(themeAction+(b==Button::Right?1:-1),0,2);
        } else themeField=std::clamp(themeField+(b==Button::Down?1:-1),0,5);
        return true;
    }

    bool themeRelease(Button b) {
        if(mode==Mode::ThemeFiles) {
            if(b==Button::A&&!aUsed){aUsed=true;chooseThemeFile();}
            return true;
        }
        if(mode!=Mode::ThemeColors)return false;
        if(b!=Button::A||aUsed||themeField<4)return true;
        aUsed=true;
        if(themeField==4){openThemeFiles(themeFileAction==1);return true;}
        if(themeAction==0)themeDraft[std::size_t(themeRole)]=defaultPalette[std::size_t(themeRole)];
        else if(themeAction==1)themeDraft=defaultPalette;
        else {
            const auto previous=palette;
            palette=themeDraft;
            try {saveEditorSettings();}
            catch(...) {palette=previous;throw;}
            themeEditing=false;mode=Mode::Menu;
            notify("THEME COLORS SAVED");
        }
        return true;
    }

    void openThemeFiles(bool savingTheme) {
        std::filesystem::create_directories(themeDirectory);
        std::vector<std::filesystem::path> found;
        for(const auto& entry:std::filesystem::directory_iterator(themeDirectory))
            if(!entry.is_symlink()&&entry.is_regular_file()&&lower(entry.path().extension().u8string())==".theme")found.push_back(entry.path());
        std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.filename().u8string()<b.filename().u8string();});
        themeFiles=std::move(found);themeFileIndex=0;themeSaving=savingTheme;mode=Mode::ThemeFiles;
    }

    void saveThemeFile(const std::filesystem::path& path) {
        // Recover to the picker if a write fails, retaining the complete draft.
        mode=Mode::ThemeFiles;
        io::Theme rgb{};
        for(std::size_t n=0;n<rgb.size();++n) {
            const auto c=themeDraft[n];rgb[n]=(std::uint32_t(c.r)<<16)|(std::uint32_t(c.g)<<8)|c.b;
        }
        io::writeFileAtomic(path,io::encodeTheme(rgb));
        mode=Mode::ThemeColors;notify("THEME FILE SAVED / APPLY TO USE");
    }

    void requestThemeSave(const std::filesystem::path& path) {
        if(std::filesystem::exists(path)) {
            confirm("OVERWRITE THEME?",path.filename().u8string(),[this,path]{saveThemeFile(path);});
            confirmCancelMode=Mode::ThemeFiles;
        } else saveThemeFile(path);
    }

    void commitThemeName() {
        auto name=filename;
        const auto first=name.find_first_not_of(' '),last=name.find_last_not_of(" .");
        if(first==std::string::npos||last==std::string::npos||last<first)throw std::runtime_error("ENTER A THEME FILENAME");
        name=name.substr(first,last-first+1);
        if(name=="."||name==".."||name.size()>48||name.find_first_of("/\\:")!=std::string::npos)
            throw std::runtime_error("INVALID THEME FILENAME");
        requestThemeSave(themeDirectory/(name+".theme"));
    }

    void chooseThemeFile() {
        if(themeSaving&&themeFileIndex==0) {
            nameTarget=NameTarget::Theme;filename="";nameCursor=0;nameRowColumn.fill(-1);mode=Mode::Name;return;
        }
        const int index=themeFileIndex-(themeSaving?1:0);
        if(index<0||index>=int(themeFiles.size()))return;
        const auto path=themeFiles[std::size_t(index)];
        if(themeSaving){requestThemeSave(path);return;}
        if(std::filesystem::file_size(path)>65536)throw std::runtime_error("THEME FILE IS TOO LARGE");
        const auto loaded=io::decodeTheme(io::readFile(path));
        themeDraft=colorPalette(loaded);mode=Mode::ThemeColors;
        notify("THEME LOADED / APPLY TO USE");
    }

    void drawThemeFiles() {
        const int count=int(themeFiles.size())+(themeSaving?1:0);
        const auto bounds=dialogForContent({85,77,854,0},177+12*34,"LEFT/RIGHT PAGE / A SELECT / B CANCEL","FOLDER: THEMES / BESIDE THE EXECUTABLE");
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);outline(bounds.x,bounds.y,bounds.w,bounds.h,dim);
        txt(109,102,themeSaving?"SAVE THEME":"LOAD THEME",cyan,3);
        txt(779,112,count?number(themeFileIndex+1)+" / "+number(count):"0 / 0",muted);
        const int first=std::clamp(themeFileIndex-6,0,std::max(0,count-13));
        if(!count)txt(119,166,"NO .THEME FILES",muted);
        for(int n=0;n<13&&first+n<count;++n) {
            const int index=first+n,y=153+n*34;
            const auto name=themeSaving&&index==0?"NEW THEME...":themeFiles[std::size_t(index-(themeSaving?1:0))].filename().u8string();
            if(index==themeFileIndex){rect(102,y-7,820,31,panel2);rect(102,y-7,focusBarWidth,31,amber);}
            txt(119,y,ui::fit(name,65),index==themeFileIndex?amber:ink);
        }
        drawDialogFooter(bounds,"LEFT/RIGHT PAGE / A SELECT / B CANCEL","FOLDER: THEMES / BESIDE THE EXECUTABLE");
    }

    void drawThemeColors() {
        // The editor stays readable even when the draft or saved colors match.
        // Only its preview uses the draft; the document and playback stay live.
        const auto applied=palette;
        palette=defaultPalette;
        const char* hint=themeField>=4?"LEFT/RIGHT BUTTON / A SELECT / B CANCEL":
            themeField==0?"A+DPAD COLOR / B CANCEL":"A+LEFT/RIGHT 1 / A+UP/DOWN 16 / B CANCEL";
        const auto bounds=dialogForContent({60,80,904,0},515+controlHeight,hint,
            "PREVIEW ONLY UNTIL APPLY");
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);
        outline(bounds.x,bounds.y,bounds.w,bounds.h,dim);
        txt(84,104,"THEME COLORS",cyan,3);
        txt(84,173,"RGB 0-255",muted);
        const auto color=themeDraft[std::size_t(themeRole)];
        labelValue(84,207,"COLOR",themeLabels[std::size_t(themeRole)],themeField==0,474);
        const std::array<const char*,3> names{{"RED","GREEN","BLUE"}};
        const std::array<int,3> values{{color.r,color.g,color.b}};
        for(int n=0;n<3;++n)labelValue(84,256+n*49,names[std::size_t(n)],number(values[std::size_t(n)]),themeField==n+1,474);
        rect(84,411,56,26,color);outline(84,411,56,26,dim);
        txt(154,417,"#"+hex((int(color.r)<<16)|(int(color.g)<<8)|int(color.b),6),ink);
        txt(394,417,number(themeRole+1)+" / "+number(themeDraft.size()),muted);

        const auto& c=themeDraft;
        txt(586,173,"PREVIEW",muted);
        rect(582,207,358,230,c[0]);outline(582,207,358,230,dim);
        rect(592,217,338,28,c[1]);txt(602,224,"PATTERN 00",c[6]);
        txt(602,260,"00  C-4 01 40 000",c[3]);
        rect(592,284,338,26,c[2]);
        txt(602,290,"01",c[4]);rect(646,285,40,24,c[7]);txt(646,290,"D-4",c[0]);
        txt(702,290,"01",c[6]);txt(746,290,"30",c[8]);txt(790,290,"447",c[7]);
        txt(602,320,"02  --- .. .. 000",c[5]);
        rect(592,344,338,26,c[10]);txt(602,350,"03  E-4 01 20 000",c[3]);
        rect(592,381,156,42,c[11]);
        for(int n=0;n<15;++n)line(596+n*10,402,606+n*10,n%2?414:390,c[6]);
        txt(770,388,"PLAY",c[8]);txt(770,410,"MUTE",c[9]);
        // Make the overlay's tint visible without obscuring the editor controls.
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        rect(888,217,42,210,c[12]);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);

        labelValue(84,466,"LOAD THEME","",themeField==4&&themeFileAction==0,421);
        labelValue(519,466,"SAVE THEME","",themeField==4&&themeFileAction==1,421);
        const std::array<const char*,3> buttons{{"RESET COLOR","RESET ALL","APPLY"}};
        for(int n=0;n<3;++n)labelValue(84+n*290,515,buttons[std::size_t(n)],"",themeField==5&&themeAction==n,276);
        drawDialogFooter(bounds,hint,"PREVIEW ONLY UNTIL APPLY");
        palette=applied;
    }
