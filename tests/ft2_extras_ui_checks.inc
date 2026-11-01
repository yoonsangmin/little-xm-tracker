        {
            // Editing-tool input regressions, compiled now and executed only on request.
            playback->stop();doc=makeDemoDocument();page=Page::Pattern;mode=Mode::Normal;
            selectedPattern=order=row=channel=field=0;instrument=1;sample=0;solo=-1;
            held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
            selectPending=selectUsed=channelModifier=false;channelChord=0;
            clearSelection();copyMask={};pasteMask={};copiedMask={};
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            playback->setSolo(-1);playback->setMuteMask(0);
            auto extrasSelect=[&](Button code){buttonInput(Button::Select,true);tap(code);buttonInput(Button::Select,false);};
            auto extrasOption=[&](Button code){buttonInput(Button::B,true);tap(code);buttonInput(Button::B,false);};
            const auto unedited=io::encodeXm(doc);
            clipboard=Cell{60,1,0x30,4,0x47};
            extrasOption(Button::Select);
            require(playback->muteMask()==1,"B first, then Select toggles the cursor channel mute");
            extrasOption(Button::Start);
            require(mode==Mode::Normal&&clipboard&&clipboard->note==60,"channel shortcuts preserve the pattern clipboard");
            const auto listeningChannels=(std::uint32_t{1}<<doc.channels)-1;
            require(playback->muteMask()==(listeningChannels&~std::uint32_t{1})&&solo==0,
                    "solo replaces manual mutes and makes a previously muted target the only audible channel");
            playback->playPattern(0);std::vector<std::int16_t> soloPcm(2048*2);playback->readFrames(soloPcm.data(),2048);
            require(std::any_of(soloPcm.begin(),soloPcm.end(),[](std::int16_t value){return value!=0;}),
                    "a previously muted solo target produces actual PCM");
            playback->stop();
            extrasOption(Button::Start);
            require(playback->muteMask()==0&&solo==-1,"turning the same solo off unmutes all channels");
            extrasOption(Button::Start);channel=1;extrasOption(Button::Select);
            require(solo==-1&&playback->muteMask()==(listeningChannels&~std::uint32_t{3}),
                    "unmuting a channel during solo keeps the effective listening set and exits solo mode");
            channel=0;extrasOption(Button::Select);
            require(solo==-1&&playback->muteMask()==(listeningChannels&~std::uint32_t{2}),
                    "manual mute after solo changes only the selected channel in the current listening set");
            extrasOption(Button::Start);extrasOption(Button::Start);
            require(playback->muteMask()==0&&solo==-1,"solo on then off clears a mixed listening set");
            playback->setMuteMask(0);playback->setSolo(-1);solo=-1;
            require(io::encodeXm(doc)==unedited,"mute and solo do not change the XM document");

            menu("COPY / PASTE MASKS");tap(Button::A);
            require(copyMask.note,"A alone does not toggle a mask value");
            chord({Button::Left});tap(Button::B);
            require(!copyMask.note&&io::encodeXm(doc)==unedited,"copy mask adjustment does not change the song");
            menu("COPY / PASTE MASKS");tap(Button::Right);tap(Button::Down);
            require(maskRow==1&&maskColumn==1,"an unvisited mask row initially keeps the current column");
            tap(Button::Left);tap(Button::Up);
            require(maskRow==0&&maskColumn==1,"mask rows remember their independently selected copy/paste column");
            for(int n=0;n<4;++n)tap(Button::Down);
            tap(Button::Left);tap(Button::Down);
            require(maskRow==5&&maskColumn==1,"SKIP EMPTY selects its only available paste column");
            tap(Button::Down);tap(Button::Left);tap(Button::Right);tap(Button::Down);
            require(maskRow==6&&maskColumn==1,"RESET MASKS ignores horizontal input and clamps at the bottom");
            tap(Button::Up);tap(Button::Up);
            require(maskRow==4&&maskColumn==0,"leaving SKIP EMPTY restores the preceding row's last column");
            tap(Button::B);
            copyMask={false,false,false,false,true,false};pasteMask={};pasteMask.skipEmpty=true;
            doc.patterns[0].at(0,0,doc.channels)={49,1,0x30,4,0};
            playback->load(doc);row=0;field=4;extrasSelect(Button::B);tap(Button::B);
            row=1;const Cell destination{60,1,0x40,2,0x34};editCell(destination);extrasSelect(Button::A);
            require(cell().note==60&&cell().volume==0x40&&cell().effect==2&&cell().param==0,
                    "parameter-only transparent paste retains the source effect-memory zero");
            undo();require(same(cell(),destination),"masked paste remains undoable");
            copyMask={};pasteMask={};copiedMask={};
            const auto beforeExpand=io::encodeXm(doc);const int extraOriginalRows=pattern().rows;row=0;
            menu("EXPAND PATTERN");require(pattern().rows==extraOriginalRows*2,"expand doubles the actual pattern row count");
            menu("SHRINK PATTERN");require(io::encodeXm(doc)==beforeExpand,"expand then shrink recovers every source cell");
            menu("SHRINK PATTERN");require(mode==Mode::Confirm,"shrinking nonempty odd rows requires confirmation");
            tap(Button::B);require(io::encodeXm(doc)==beforeExpand,"cancel shrink preserves the complete document");

            changePage(Page::Instrument);menu("COPY INSTRUMENT");
            require(instrumentClipboard&&instrumentClipboard->samples.size()==doc.instruments[0].samples.size(),"whole instrument clipboard includes samples");
            tap(Button::B); // Retain the snapshot and use the direct PASTE action below.
            doc.instruments.emplace_back();instrument=int(doc.instruments.size());normalize();playback->load(doc);
            const auto beforeReplace=io::encodeXm(doc);menu("PASTE INSTRUMENT");
            require(mode==Mode::Confirm,"instrument replacement asks before overwriting a slot");tap(Button::B);
            require(io::encodeXm(doc)==beforeReplace,"cancel instrument paste keeps the destination");
            menu("PASTE INSTRUMENT");tap(Button::A);
            require(currentInstrument()->samples[0].pcm==instrumentClipboard->samples[0].pcm,"confirmed paste includes full PCM");
            require(currentInstrument()->samples[0].pcm.data()!=instrumentClipboard->samples[0].pcm.data(),"pasted PCM owns independent storage");

            changePage(Page::Sample);sample=0;selectionStart=selectionEnd=0;
            selectionStart=2;selectionEnd=9;browserDirectory=browserDefaultFolder(2);
            const auto beforeSampleExport=io::encodeXm(doc);
            const auto sourceExportPcm=currentSample()->pcm;
            const auto exportUndoCount=undoStack.size(),exportRedoCount=redoStack.size();
            const auto exportRecoverySerial=recoverySerial;const bool exportDirty=dirty;
            const auto exportSuffix=std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            auto checkExportUnchanged=[&]{
                require(io::encodeXm(doc)==beforeSampleExport&&selectionStart==2&&selectionEnd==9&&
                        dirty==exportDirty&&undoStack.size()==exportUndoCount&&redoStack.size()==exportRedoCount&&
                        recoverySerial==exportRecoverySerial,
                        "sample WAV export keeps PCM, loop metadata, selection and edit history unchanged");
            };
            auto saveSampleExport=[&](bool range,const std::string& stem){
                menu("EXPORT SAMPLE WAV");
                require(mode==Mode::Menu&&menuTitle=="EXPORT SAMPLE WAV"&&selectionStart==2&&selectionEnd==9,
                        "selected sample export is reachable through PROCESS without clearing the range");
                if(range)tap(Button::Down);
                tap(Button::A);
                require(mode==Mode::Browser&&fileAction==(range?FileAction::ExportWavRange:FileAction::ExportWav),
                        "sample export choice opens the matching WAV save browser");
                tap(Button::Start);require(mode==Mode::Name,"sample export opens the filename editor");
                filename=stem;tap(Button::Start);if(mode==Mode::Confirm)tap(Button::A);
                require(mode==Mode::Normal,"sample export saves and returns to the editor");
                checkExportUnchanged();
                return io::decodeWav(io::readFile(browserDefaultFolder(2)/(stem+".wav")));
            };
            const auto selectedExport=saveSampleExport(true,"sample-range-"+exportSuffix);
            require(selectedExport.pcm==std::vector<std::int16_t>(sourceExportPcm.begin()+2,sourceExportPcm.begin()+9),
                    "selected sample WAV contains exactly the half-open PCM range");
            const auto wholeExport=saveSampleExport(false,"sample-whole-"+exportSuffix);
            require(wholeExport.pcm==sourceExportPcm,"whole sample WAV ignores an active partial selection");
            menu("EXPORT SAMPLE WAV");tap(Button::Down);tap(Button::A);tap(Button::B);
            require(mode==Mode::Normal,"canceling sample export closes the browser");checkExportUnchanged();
            selectionStart=selectionEnd=0;
#include "draw_workflow_ui_checks.inc"
            mode=Mode::Normal;dirty=false;copyMask={};pasteMask={};copiedMask={};clearSelection();
            playback->setSolo(-1);playback->setMuteMask(0);solo=-1;
        }
