        {
            // View density is an application preference, never a song edit.
            playback->stop();doc=makeDemoDocument();edit::resizeChannels(doc,32);
            doc.patterns[0].at(0,0,doc.channels).effect=4;doc.patterns[0].at(0,0,doc.channels).param=0x47;
            for(int c=1;c<8;++c)for(int r=0;r<doc.patterns[0].rows;r+=4) {
                auto value=doc.patterns[0].at(r,0,doc.channels);
                value.note=std::uint8_t(std::clamp(int(value.note)+(c%4)*3-(c>3?12:0),1,96));
                value.volume=std::uint8_t(0x30+c);value.effect=4;value.param=std::uint8_t(0x41+c);
                doc.patterns[0].at(r,c,doc.channels)=value;
            }
            page=Page::Pattern;mode=Mode::Normal;selectedPattern=order=0;row=7;channel=3;field=4;
            patternViewChannels=4;followPlayback=false;blockSelected=false;dirty=false;
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);saveEditorSettings();
            auto pageJump=[&](Button direction){buttonInput(Button::B,true);tap(direction);buttonInput(Button::B,false);};
            auto selectKey=[&](Button code){buttonInput(Button::Select,true);tap(code);buttonInput(Button::Select,false);};
            auto togglePatternView=[&] {
                menu("SETTINGS");
                auto setting=std::find_if(actions.begin(),actions.end(),[](const Action& a){return a.label.find("PATTERN VIEW: ")==0;});
                require(setting!=actions.end(),"Settings exposes the pattern view density");
                const int target=int(std::distance(actions.begin(),setting));
                while(menuIndex<target)tap(Button::Down);
                while(menuIndex>target)tap(Button::Up);
                const int before=patternViewChannels;tap(Button::A);
                require(patternViewChannels==before,"A alone does not toggle pattern density");
                chord({before==4?Button::Right:Button::Left});
                require(mode==Mode::Menu&&menuTitle=="SETTINGS"&&menuIndex==target,"A+direction changes density on the same Settings item");
            };
            const auto preferenceSong=io::encodeXm(doc);const auto preferenceRecovery=recoverySerial;
            const auto choosePreference=[&](const std::string& prefix,Button direction){
                menu("SETTINGS");
                const auto entry=std::find_if(actions.begin(),actions.end(),[&](const Action& a){return a.label.find(prefix)==0;});
                require(entry!=actions.end(),"display preference exists in Settings");
                menuIndex=int(std::distance(actions.begin(),entry));
                chord({direction});tap(Button::B);tap(Button::B);
            };
            choosePreference("PATTERN ROWS: ",Button::Left);
            choosePreference("ROW NUMBERS: ",Button::Left);
            require(patternViewRows==16&&!rowNumbersHex&&patternRowLabel(15)=="015"&&patternRowLabel(255)=="255",
                    "A+directions select sixteen rows and three-digit decimal row labels");
            row=15;require(patternTopRow()==0,"sixteen-row page includes row fifteen");
            row=16;require(patternTopRow()==16,"sixteen-row page advances at sixteen");
            row=255;require(patternTopRow()==240,"last sixteen-row page starts at 240");
            patternViewRows=32;rowNumbersHex=true;loadEditorSettings();
            require(patternViewRows==16&&!rowNumbersHex,"row density and radix survive settings reload");
            const std::string invalidRows="pattern_view_rows 0\npattern_view_rows 17\nrow_numbers_hex 2\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(invalidRows.begin(),invalidRows.end()));
            loadEditorSettings();require(patternViewRows==16&&!rowNumbersHex,"invalid row preferences do not override valid settings");
            require(!dirty&&recoverySerial==preferenceRecovery&&io::encodeXm(doc)==preferenceSong,
                    "row density and radix affect neither the XM nor recovery history");
            choosePreference("PATTERN ROWS: ",Button::Right);
            choosePreference("ROW NUMBERS: ",Button::Right);
            row=7;require(patternViewRows==32&&rowNumbersHex&&patternRowLabel(255)=="FF","32-row hex defaults can be restored");
            const auto songBefore=io::encodeXm(doc);const auto recoveryBefore=recoverySerial;
            togglePatternView();
            require(patternViewChannels==8&&page==Page::Pattern&&selectedPattern==0&&order==0&&row==7&&channel==3&&field==4,
                    "eight-channel view preserves the selected pattern, order, row, channel and field");
            require(!dirty&&recoverySerial==recoveryBefore&&io::encodeXm(doc)==songBefore,
                    "changing the view does not edit XM or schedule song recovery");
            noticeUntil=0;draw();screenshot(home/"pattern-view-settings.bmp");SDL_RenderPresent(renderer);tap(Button::B);tap(Button::B);
            patternViewChannels=4;loadEditorSettings();
            require(patternViewChannels==8,"eight-channel preference survives settings reload");
            const std::string badSettings="pattern_view_channels 6\npattern_view_channels 0\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(badSettings.begin(),badSettings.end()));
            loadEditorSettings();require(patternViewChannels==8,"invalid view densities cannot replace the active four-or-eight setting");
            saveEditorSettings();

            row=0;channel=0;field=4;noticeUntil=0;draw();screenshot(home/"pattern-view-8.bmp");SDL_RenderPresent(renderer);
            togglePatternView();tap(Button::B);tap(Button::B);
            require(patternViewChannels==4,"Settings toggles back to four channels");
            patternViewChannels=8;loadEditorSettings();require(patternViewChannels==4,"four-channel preference survives settings reload");
            noticeUntil=0;draw();screenshot(home/"pattern-view-4.bmp");SDL_RenderPresent(renderer);
            channel=3;pageJump(Button::Right);require(channel==7&&patternFirstChannel()==4,"B+Right jumps a whole four-channel page");
            pageJump(Button::Left);require(channel==3&&patternFirstChannel()==0,"B+Left returns a whole four-channel page");

            tap(Button::Start);const auto playingBefore=playback->status();togglePatternView();tap(Button::B);tap(Button::B);
            require(patternViewChannels==8&&playback->status().playing&&playingPattern&&playback->status().pattern==playingBefore.pattern,
                    "density can change during playback without stopping or changing the pattern scope");
            tap(Button::Start);
            channel=7;pageJump(Button::Right);require(channel==15&&patternFirstChannel()==8,"B+Right jumps a whole eight-channel page");
            pageJump(Button::Left);require(channel==7&&patternFirstChannel()==0,"B+Left returns a whole eight-channel page");
            channel=31;pageJump(Button::Right);require(channel==31&&patternFirstChannel()==24,"last eight-channel page reaches channel thirty-two without exceeding the song");
            row=0;noticeUntil=0;draw();screenshot(home/"pattern-view-32.bmp");SDL_RenderPresent(renderer);

            // Selection and copy/paste span the horizontal page boundary.
            doc.patterns[0].at(0,8,doc.channels)={61,1,0x3f,4,0x47};
            doc.patterns[0].at(1,7,doc.channels)={64,1,0x30,0x0a,0x01};
            doc.patterns[0].at(1,8,doc.channels)={67,1,0x35,0x0c,0x20};
            playback->load(doc);row=0;channel=7;field=0;blockSelected=false;
            selectKey(Button::B);for(int n=0;n<9;++n)tap(Button::Right);tap(Button::Down);tap(Button::B);
            require(blockClipboard&&blockClipboard->rows==2&&blockClipboard->channels==2&&patternFirstChannel()==8,
                    "a block selection crosses the eight-channel page boundary");
            row=8;channel=14;selectKey(Button::A);
            bool copiedAll=true;
            for(int r=0;r<2;++r)for(int c=0;c<2;++c)
                copiedAll=copiedAll&&same(doc.patterns[0].at(8+r,14+c,doc.channels),doc.patterns[0].at(r,7+c,doc.channels));
            require(copiedAll,"page-crossing copy and paste retain all five XM fields");

            doc=makeDemoDocument();edit::resizeChannels(doc,5);selectedPattern=order=row=0;channel=4;field=4;
            blockSelected=false;normalize();playback->load(doc);const auto sparseSong=io::encodeXm(doc);
            noticeUntil=0;draw();screenshot(home/"pattern-view-5-of-8.bmp");SDL_RenderPresent(renderer);
            pageJump(Button::Right);
            require(patternViewChannels==8&&doc.channels==5&&channel==4&&io::encodeXm(doc)==sparseSong,
                    "eight-channel view does not add channels to a smaller song or navigate into nonexistent channels");
            patternViewChannels=4;saveEditorSettings();mode=Mode::Normal;
        }
