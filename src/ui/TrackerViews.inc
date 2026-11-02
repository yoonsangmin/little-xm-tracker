// Included inside TrackerApp::Impl. Compact main views share the editor's
// existing state and controls; transport markers never move the edit cursor.
    PlaybackMonitor songMonitorSnapshot;
    Uint32 songMonitorUpdated = 0;
    bool songMonitorValid = false;
    std::array<float,2> outputPeakDisplay{};
    std::array<Uint32,2> outputClipUntil{};
    Uint32 outputMeterUpdated = 0;

    void drawHeader() {
        rect(0,0,W,44,panel);
        rect(0,43,W,1,panel2);
        const char* tabs[]={"SONG","PATTERN","INSTRUMENT","SAMPLE"};
        int tabX=16;
        for(int i=0;i<4;++i){
            const bool selected=int(page)==i;
            std::string title=tabs[i];
            if(selected&&page!=Page::Song)title+=" "+hex(page==Page::Pattern?selectedPattern:page==Page::Instrument?instrument:sample);
            // Only the active tab grows. Song reserves the same number space
            // without showing a number, keeping the combined tab width fixed.
            const int width=ui::textWidth(tabs[i])+24+(selected?36:0);
            if(selected)rect(tabX,6,width,32,panel2);
            txt(tabX+12,14,title,selected?cyan:muted);
            // The shared-pattern marker fits in the right padding rather than
            // changing tab geometry when an order uses a pattern twice.
            if(selected&&page==Page::Pattern&&edit::patternUseCount(doc,selectedPattern)>1)
                txt(tabX+width-12,14,"*",cyan);
            if(selected)rect(tabX+10,36,width-20,2,cyan);
            tabX+=width+6;
        }
        const bool previewPlaying=playback->previewPlaying();
        std::string state = transport.playing ? (transport.rowAudition?"PLAY ROW":transport.loopFirstRow>=0?"PLAY LOOP":playingPattern?"PLAY PATTERN":"PLAY SONG") : previewPlaying?"LISTEN":"STOP";
        if(transport.playing&&!playingPattern&&!transport.rowAudition&&transport.loopFirstRow<0){
            const auto seconds=static_cast<std::uint64_t>(std::max(0.0,transport.elapsedSeconds));
            const auto minutes=number(seconds/60),remainder=number(seconds%60);
            state+=std::string("  ")+(minutes.size()<2?"0":"")+minutes+":"+(remainder.size()<2?"0":"")+remainder;
        }
        const int stateX = 1000-ui::textWidth(state);
        const int bpmX=tabX+12;
        const int currentBpm=transport.playing?transport.bpm:doc.bpm;
        txt(bpmX,14,"BPM "+number(currentBpm),cyan);
        const auto now=SDL_GetTicks();
        const float seconds=std::min(1.f,float(Uint32(now-outputMeterUpdated))/1000.f);
        outputMeterUpdated=now;
        const auto output=playback->takeOutputMeter();
        const float decay=std::pow(10.f,-24.f*seconds/20.f);
        const int meterX=bpmX+108;
        constexpr int meterWidth=120;
        for(int side=0;side<2;++side) {
            outputPeakDisplay[side]=std::max(output.peak[side],outputPeakDisplay[side]*decay);
            if(output.clipped[side])outputClipUntil[side]=now+1000u;
            const bool clipped=Sint32(outputClipUntil[side]-now)>0;
            if(meterWidth<36)continue;
            const int y=side?25:6;
            txt(meterX,y,side?"R":"L",muted);
            const float level=outputPeakDisplay[side]>0.f?
                std::clamp((20.f*std::log10(outputPeakDisplay[side])+48.f)/48.f,0.f,1.f):0.f;
            rect(meterX+20,y+4,meterWidth,7,bg);
            rect(meterX+20,y+4,int(level*meterWidth),7,clipped?red:level>.875f?amber:green);
            rect(meterX+24+meterWidth,y+4,5,7,clipped?red:dim);
        }
        if (!transport.playing&&!previewPlaying) rect(stateX-18,17,7,7,muted);
        txt(stateX,14,state,transport.playing||previewPlaying?green:muted);
    }

    static std::string volumeColumnText(std::uint8_t volume) {
        // FT2 displays volume independently of the XM byte encoding.
        if (volume<0x10) return "..";
        if (volume<0x60) return hex(volume-0x10);
        static constexpr char commands[]="-+DUSVPLRM";
        return std::string(1,commands[(volume>>4)-6])+hex(volume&0x0F,1);
    }

    void drawPattern() {
        const auto mutedChannels=playback->muteMask();
        const bool compact = patternViewChannels==8;
        const int firstChannel = patternFirstChannel();
        const int firstRow = std::clamp(patternTopRow(),0,std::max(0,int(pattern().rows)-1));
        const int channelPitch = compact?117:234, channelWidth = compact?115:228;
        const std::array<int,5> fieldOffset = compact?std::array<int,5>{2,36,59,82,93}:
            std::array<int,5>{12,68,110,152,166};
        const std::array<int,5> fieldWidth = compact?std::array<int,5>{34,23,23,12,23}:
            std::array<int,5>{40,28,28,16,28};
        const auto cellText = [&](int x,int y,std::string_view value,SDL_Color color) {
            if(!compact){txt(x,y,value,color);return;}
            // All XM cell strings are ASCII. Keep 10x14 ink and reduce only
            // the gap between characters, retaining every editable field.
            for(std::size_t n=0;n<value.size();++n)
                txt(x+int(n)*11,y,value.substr(n,1),color);
        };
        txt(20,59,"ROW",dim);
        for (int c=0;c<patternViewChannels;++c) {
            const int ch=firstChannel+c, x=65+c*channelPitch;
            if (ch>=doc.channels) break;
            rect(x,52,channelWidth,28,panel2);
            txt(x+(compact?4:12),59,std::string(compact?"CH ":"CHANNEL ")+channelNumber(ch),ch==channel?cyan:ink);
            if ((mutedChannels>>ch)&1u) txt(x+(compact?101:199),59,"M",red);
            else if (solo==ch) txt(x+(compact?101:199),59,"S",amber);
        }
        const auto selection = selectedBlock();
        const int rowPitch=640/patternViewRows, textInset=(rowPitch-14)/2;
        for (int rr=0;rr<patternViewRows;++rr) {
            const int r=firstRow+rr, y=88+rr*rowPitch;
            if (r>=pattern().rows) break;
            const bool playing=transport.playing &&
                transport.pattern==patternIndex() && transport.row==r;
            txt(20,y+textInset,patternRowLabel(r),r==row?amber:r%4==0?muted:dim);
            if (playing) rect(8,y+1,4,rowPitch-2,green);
            for (int c=0;c<patternViewChannels;++c) {
                const int ch=firstChannel+c, x=65+c*channelPitch;
                if (ch>=doc.channels) break;
                rect(x,y,channelWidth,rowPitch-1,r==row?panel2:r%4==0?panel:bg);
                if (blockSelected && blockPattern==patternIndex() &&
                    std::size_t(r)>=selection.firstRow && std::size_t(r)<selection.lastRow &&
                    std::size_t(ch)>=selection.firstChannel && std::size_t(ch)<selection.lastChannel) {
                    const auto fields=edit::selectedFields(selection,std::size_t(ch));
                    int firstField=0,lastField=4;
                    while(firstField<5&&!(fields&(1u<<firstField)))++firstField;
                    while(lastField>=0&&!(fields&(1u<<lastField)))--lastField;
                    if(firstField<=lastField) {
                        // Connect the gaps inside the selection, but stop at
                        // the outer edges of its two endpoint cursor boxes.
                        const int left=std::size_t(ch)==selection.firstChannel?
                            x+fieldOffset[firstField]-(compact?1:4):x;
                        const int right=std::size_t(ch)+1<selection.lastChannel?x+channelPitch:
                            x+fieldOffset[lastField]-(compact?1:4)+fieldWidth[lastField];
                        rect(left,y,right-left,rowPitch,selectionColor);
                    }
                }
                if (playing) rect(x,y+rowPitch-2,channelWidth,1,green);
                const auto& value=pattern().at(r,ch,doc.channels);
                if (ch==channel && r==row)
                    rect(x+fieldOffset[field]-(compact?1:4),y+1,fieldWidth[field],rowPitch-3,amber);
                const std::array<std::string,5> values={noteName(value.note),
                    value.instrument?hex(value.instrument):"..", volumeColumnText(value.volume),
                    std::string(1,value.effect<10?char('0'+value.effect):char('A'+value.effect-10)),
                    hex(value.param)};
                const SDL_Color colors[]={value.note?ink:dim,value.instrument?cyan:dim,
                    value.volume>=0x10?green:dim,(value.effect||value.param)?amber:dim,
                    (value.effect||value.param)?amber:dim};
                for (int f=0;f<5;++f)
                    cellText(x+fieldOffset[f],y+textInset,values[f],ch==channel && r==row && field==f?bg:colors[f]);
                if ((mutedChannels>>ch)&1u)
                    rect(x+(compact?channelWidth-1:219),y+rowPitch/2-2,compact?1:3,7,dim);
            }
        }
    }

    void songSetting(int x,int y,int width,std::string_view label,
                     const std::string& value,int index) {
        const bool selected=songField==index;
        rect(x,y,width,controlHeight,selected?panel2:panel);
        if (selected) rect(x,y,focusBarWidth,controlHeight,amber);
        txt(x+14,y+controlTextY,label,selected?ink:muted);
        txt(x+width-14-ui::textWidth(value),y+controlTextY,value,selected?amber:cyan);
    }

    void songButton(int x,int y,int width,std::string_view label,int index) {
        drawButton(x,y,width,label,songField==index);
    }

    void drawSongMonitor() {
        const Uint32 now=SDL_GetTicks();
        if (!songMonitorValid || now-songMonitorUpdated>=34 || songMonitorSnapshot.channelCount!=doc.channels) {
            songMonitorSnapshot=playback->monitor();
            songMonitorUpdated=now;
            songMonitorValid=true;
        }
        txt(20,410,"CHANNEL MONITOR",muted);
        const int count=std::clamp(int(doc.channels),1,32);
        const int columns=songMonitorColumns();
        const int rows=(count+columns-1)/columns;
        const int gap=8;
        const int cardWidth=(984-gap*(columns-1))/columns;
        const int cardHeight=(288-gap*(rows-1))/rows;
        const auto muteMask=playback->muteMask();
        for (int ch=0;ch<count;++ch) {
            const int x=20+(ch%columns)*(cardWidth+gap);
            const int y=436+(ch/columns)*(cardHeight+gap);
            const auto& state=songMonitorSnapshot.channels[std::size_t(ch)];
            const bool channelMuted=(muteMask&(std::uint32_t{1}<<ch))!=0;
            const bool live=ch<songMonitorSnapshot.channelCount && state.active && !channelMuted;
            rect(x,y,cardWidth,cardHeight,panel);
            if (songField==12&&ch==channel) outline(x,y,cardWidth,cardHeight,amber);
            txt(x+8,y+10,"CH"+channelNumber(ch),channelMuted?red:songField==12&&ch==channel?amber:ink);
            if(channelMuted||solo==ch)txt(x+59,y+10,channelMuted?"M":"S",channelMuted?red:amber);
            txt(x+cardWidth-44,y+10,live?noteName(state.note):"---",live?ink:dim);
            if (cardWidth>=220 && state.active)
                txt(x+78,y+10,"I "+hex(state.instrument),state.muted?dim:cyan);
            const int left=x+8, right=x+cardWidth-9;
            const int top=y+32, bottom=y+cardHeight-9;
            const int center=(top+bottom)/2, amplitude=std::max(1,(bottom-top)/2);
            line(left,center,right,center,dim);
            if (live) {
                int lastX=left;
                int lastY=center-int(state.waveform[0])*amplitude/32768;
                for (std::size_t n=1;n<state.waveform.size();++n) {
                    const int px=left+int(n)* (right-left)/int(state.waveform.size()-1);
                    const int py=center-int(state.waveform[n])*amplitude/32768;
                    line(lastX,lastY,px,py,cyan);
                    lastX=px;lastY=py;
                }
            }
        }
    }

    void drawSong() {
        rect(20,60,356,332,panel);
        rect(392,60,612,332,panel);
        txt(40,78,"ORDER",muted);txt(160,78,"PATTERN",muted);txt(280,78,"ROWS",muted);
        const int total=int(doc.orders.size())+(doc.orders.size()<256?1:0);
        const int first=std::clamp(songOrderCursor()-songOrderPageRows/2,0,std::max(0,total-songOrderPageRows));
        for (int n=0;n<songOrderPageRows && first+n<total;++n) {
            const int index=first+n, y=110+n*26;
            const bool append=index==int(doc.orders.size());
            const bool selected=index==songOrderCursor();
            if(!append&&orderBlockSelected && index>=std::min(order,orderBlockStart) && index<=std::max(order,orderBlockStart))
                rect(28,y-5,340,24,selectionColor);
            if (selected) rect(28,y-5,340,24,songField==0?panel2:panel);
            if (selected && songField==0) {
                rect(28,y-5,3,24,amber);
                rect(songOrderColumn==0?168:284,y-3,songOrderColumn==0?32:48,20,amber);
            }
            if (!append&&transport.playing && !playingPattern && transport.order==index)
                txt(33,y,">",green);
            txt(54,y,hex(index),selected?amber:muted);
            const auto id=append?0:doc.orders[std::size_t(index)];
            txt(172,y,append?"..":hex(id),selected&&songField==0&&songOrderColumn==0?bg:append?dim:selected?cyan:ink);
            txt(290,y,append?"..":number(std::size_t(id)<doc.patterns.size()?doc.patterns[std::size_t(id)].rows:64),
                selected&&songField==0&&songOrderColumn==1?bg:append?dim:muted);
        }
        txt(40,366,number(doc.orders.size())+" ORDERS",muted);
        txt(208,366,songAppendOrder?"NEW ORDER":"PATTERN "+hex(doc.orders[order]),cyan);
        txt(404,78,"SONG SETTINGS",cyan);
        if(dirty || currentFile.empty()) txt(992-ui::textWidth("UNSAVED"),78,"UNSAVED",amber);
        songSetting(404,100,588,"NAME",ui::fit(text::xmDisplayText(doc.name.empty()?"UNTITLED":doc.name),39),6);
        songSetting(404,144,284,"BPM",number(doc.bpm),1);
        songSetting(708,144,284,"SPEED",number(doc.speed),2);
        songSetting(404,188,284,"CHANNELS",number(doc.channels),5);
        songSetting(708,188,284,"RESTART",hex(doc.restart),3);
        songSetting(404,232,588,"FREQUENCY",doc.linear?"LINEAR":"AMIGA",4);
        txt(404,289,"FILE",muted);
        txt(476,289,ui::fit(currentFile.empty()?"Unsaved":currentFile.filename().u8string(),43),ink);
        constexpr int buttonWidth=141, buttonGap=8;
        const char* buttons[]={"SAVE","SAVE AS","LOAD","NEW"};
        for(int i=0;i<4;++i)
            songButton(404+i*(buttonWidth+buttonGap),326,buttonWidth,buttons[i],7+i);
        drawSongMonitor();
    }

    static std::string effectContext(const Cell& value) {
        if (!value.effect && !value.param) return "Effect - No command";
        switch (value.effect) {
        case 0x00:return "0xy - Arpeggio (semitones above the base note)";
        case 0x01:return "1xx - Slide pitch up (speed)";
        case 0x02:return "2xx - Slide pitch down (speed)";
        case 0x03:return "3xx - Tone portamento (speed toward the target note)";
        case 0x04:return "4xy - Vibrato (speed, depth)";
        case 0x05:return "5xy - Tone portamento + volume slide (up, down)";
        case 0x06:return "6xy - Vibrato + volume slide (up, down)";
        case 0x07:return "7xy - Tremolo (speed, depth)";
        case 0x08:return "8xx - Set panning (00 left, FF right)";
        case 0x09:return "9xx - Sample offset (units of 256 samples)";
        case 0x0A:return "Axy - Volume slide (up, down)";
        case 0x0B:return "Bxx - Jump to order (hexadecimal order number)";
        case 0x0C:return "Cxx - Set volume (00 to 40)";
        case 0x0D:return "Dxx - Pattern break (decimal row in the next order)";
        case 0x0E: {
            static const char* descriptions[]={
                "E0x - Filter (ignored in FT2 mode)","E1x - Fine pitch slide up (amount)",
                "E2x - Fine pitch slide down (amount)","E3x - Glissando (0 off, 1 on)",
                "E4x - Vibrato waveform (shape, retrigger mode)","E5x - Set finetune (0 to F)",
                "E6x - Pattern loop (0 marks start, x repeats)","E7x - Tremolo waveform (shape, retrigger mode)",
                "E8x - Panning (ignored in FT2 mode)","E9x - Retrigger note (ticks)",
                "EAx - Fine volume slide up (amount)","EBx - Fine volume slide down (amount)",
                "ECx - Cut note (tick)","EDx - Delay note (tick)",
                "EEx - Pattern delay (extra row durations)","EFx - Invert loop (ignored in FT2 mode)"};
            return descriptions[value.param>>4];
        }
        case 0x0F:return value.param<0x20?"Fxx - Set speed (ticks per row, 01 to 1F)":
            "Fxx - Set BPM (20 to FF)";
        case 0x10:return "Gxx - Set global volume (00 to 40)";
        case 0x11:return "Hxy - Global volume slide (up, down)";
        case 0x14:return "Kxx - Key off (tick, releases the volume envelope)";
        case 0x15:return "Lxx - Set envelope position (tick)";
        case 0x19:return "Pxy - Panning slide (right, left)";
        case 0x1B:return "Rxy - Retrigger note (volume change, ticks)";
        case 0x1D:return "Txy - Tremor (x+1 sound ticks, y+1 silent ticks)";
        case 0x21:return (value.param>>4)==1?"X1x - Extra fine pitch slide up (amount)":
            (value.param>>4)==2?"X2x - Extra fine pitch slide down (amount)":
            "Xxx - Extra fine pitch slide (1x up, 2x down)";
        default:return "Effect - Reserved XM command";
        }
    }

    std::string patternContext() {
        const auto& value=cell();
        if (field==0) {
            if (!value.note) return "Note - No new note";
            if (value.note==97) return "OFF - Release the instrument envelope";
            const auto name=noteName(value.note);
            return name.substr(0,name[1]=='-'?1:2)+" - Octave "+number((value.note-1)/12);
        }
        if (field==1) {
            if (!value.instrument) return "Instrument - Keep the current instrument";
            if (value.instrument<=doc.instruments.size()) {
                const auto& name=doc.instruments[value.instrument-1].name;
                return "Instrument "+hex(value.instrument)+" - "+
                    (name.empty()?"Unnamed":text::xmDisplayText(name));
            }
            return "Instrument "+hex(value.instrument)+" - Empty slot";
        }
        if (field==2) {
            if (!value.volume) return "Volume column - No command";
            if (value.volume>=0x10 && value.volume<=0x50)
                return "Volume - "+volumeColumnText(value.volume)+" ("+number(value.volume-0x10)+" / 64)";
            switch (value.volume>>4) {
            case 0x6:return "-x - Volume slide down (amount)";
            case 0x7:return "+x - Volume slide up (amount)";
            case 0x8:return "Dx - Fine volume slide down (amount)";
            case 0x9:return "Ux - Fine volume slide up (amount)";
            case 0xA:return "Sx - Vibrato speed";
            case 0xB:return "Vx - Vibrato depth";
            case 0xC:return "Px - Set panning (0 left, F right)";
            case 0xD:return "Lx - Panning slide left (amount)";
            case 0xE:return "Rx - Panning slide right (amount)";
            case 0xF:return "Mx - Tone portamento speed";
            default:return "Volume column - Reserved XM value "+hex(value.volume);
            }
        }
        return effectContext(value);
    }

    void drawFooter() {
        rect(0,736,W,32,panel);
        rect(20,736,984,1,panel2);
        std::string context;
        if (page==Page::Pattern) context=patternContext();
        else if (page==Page::Song) {
            static const char* meanings[]={
                "Order - Pattern play sequence", "BPM - Initial playback tempo",
                "Speed - Ticks per row", "Restart - Return order when looping the song",
                "Frequency - Linear or Amiga pitch scale", "Channels - Number of XM channels",
                "Name - Song title", "Save - Write the current XM file",
                "Save as - Write XM under a new name", "Load - Open an XM song",
                "New - Create an empty song"};
            context=meanings[std::clamp(songField,0,10)];
            if(songField==0) {
                if(songAppendOrder)context="New order - Enter a pattern to append";
                else {
                    const int id=doc.orders[order], uses=edit::patternUseCount(doc,id);
                    context=songOrderColumn==1?"Rows - Length of pattern "+hex(id)+"; affects "+number(uses)+" order(s)":
                        "Pattern "+hex(id)+(uses>1?" - Shared by "+number(uses)+" orders; edits affect every use":" - Used by this order");
                }
            }
            if(songField==12)context="Channel "+channelNumber(channel)+" - HOLD B, TAP SELECT: MUTE / TAP START: SOLO";
        } else if (page==Page::Instrument) {
            static const char* meanings[]={
                "Sample slot - Sample within this instrument", "Map note - Key to assign a sample to",
                "Mapped sample - Sample played by this key", "Fadeout - Volume decrease after key off",
                "Vibrato depth - Automatic pitch modulation amount", "Vibrato rate - Automatic pitch modulation speed",
                "Vibrato type - Automatic vibrato waveform", "Vibrato sweep - Time to reach full depth",
                "Name - Rename this instrument", "Load XI - Replace this instrument",
                "Volume envelope - Edit points", "Panning envelope - Edit points",
                "Key map / Settings - Switch instrument view", "Sample - A opens editor / A+B deletes this slot",
                "Key map - A+DPAD changes this note's sample"};
            context=meanings[std::clamp(instrumentField,0,14)];
            if(instrumentField==14)context=hasKeymapSelection()?
                "Key map - A+DPAD edit range / B copy / A+B cut":
                noteName(keymapNote)+" - A+DPAD maps sample / B+UP/DOWN page / B settings";
        } else {
            static const char* meanings[]={
                "Volume - Default sample volume, 00 to 40", "Panning - Default position, 00 left to FF right",
                "Finetune - Pitch adjustment in 1/128 semitone", "Relative note - Sample tuning in semitones",
                "Loop type - Off, forward or ping-pong", "Loop start - First sample frame to repeat",
                "Loop end - First frame after the repeat range", "Selection start - First selected frame",
                "Selection end - First frame after the selected range", "Name - Rename this sample",
                "Load - Import audio", "New wave - Create a replacement sample; APPLY commits it",
                "Draw - Edit the selected range, or the whole sample", "",
                "Process - Open tools; keep the selected sample range"};
            context=meanings[std::clamp(sampleField,0,14)];
            if(sampleField>=5&&sampleField<=8) {
                static const char* names[]={"Loop start","Loop end","Selection start","Selection end"};
                context=std::string(names[sampleField-5])+" - A+DPAD edit / HOLD faster / A+B zero";
            }
        }
        std::string detail;
        if(page==Page::Pattern) {
            if(transport.playing&&transport.loopFirstRow>=0&&transport.pattern==patternIndex())
                detail="LOOP "+patternRowLabel(transport.loopFirstRow)+"-"+patternRowLabel(transport.loopEndRow-1);
            else detail=number(pattern().rows)+" ROWS";
        }
        else if(page==Page::Sample) detail="INSTRUMENT "+hex(instrument);
        constexpr int detailRight=1004;
        const int detailX=detailRight-ui::textWidth(detail);
        const int contextRight=detail.empty()?detailRight:detailX-24;
        const int contextChars=std::max(0,(contextRight-20)/12);
        if(mode==Mode::Normal&&channelControlContext()&&held[std::size_t(Button::B)])
            context="HOLD B, TAP SELECT: MUTE / TAP START: SOLO";
        txt(20,746,ui::fit(context,contextChars),amber);
        if(!detail.empty()) txt(detailX,746,detail,muted);
    }
