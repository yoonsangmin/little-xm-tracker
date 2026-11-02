        {
            // Direction-only input must never execute a button or edit music.
            playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
            held.fill(false);aUsed=bUsed=true;
            aPreviewActive=aPreviewBlocked=modalBackHeld=cutChordUsed=channelModifier=false;
            selectPending=selectUsed=false;channelChord=0;clearSelection();
            doc=makeDemoDocument();selectedPattern=order=row=channel=field=0;
            instrument=1;sample=0;selectionStart=selectionEnd=0;
            followPlayback=pcmSelecting=false;mode=Mode::Normal;menuParents.clear();
            normalize();playback->load(doc);
            const auto navigationDocument=io::encodeXm(doc);

            page=Page::Song;songField=10;songRowFocus={{6,-1,-1,4,-1}};
            tap(Button::Up);require(songField==4,"Song file buttons go directly up to Frequency");
            tap(Button::Down);require(songField==10,"Song returns to the last file button, New");
            tap(Button::Right);require(songField==10,"Song file-button right edge clamps");
            for(int n=0;n<4;++n)tap(Button::Left);
            require(songField==7,"Song file-button left edge stays on Save instead of jumping to Orders");
            tap(Button::Down);require(songField==12,"Down from Song file buttons enters the monitor");
            tap(Button::Up);require(songField==7,"Monitor returns to the originating Song button");

            page=Page::Sample;sampleField=14;sampleRowFocus.fill(-1);
            tap(Button::Up);require(sampleField==8,"Sample PROCESS goes directly up to selection END before an endpoint has been focused");
            tap(Button::Down);require(sampleField==14,"Sample returns to the last button, Process");
            tap(Button::Down);tap(Button::Right);require(sampleField==14,"Sample last-row and right edges clamp");
            tap(Button::Left);require(sampleField==12,"Sample horizontal input moves from PROCESS to DRAW in the selection box");
            tap(Button::Left);require(sampleField==4,"Sample horizontal input moves to the adjacent loop type field");
            tap(Button::Left);require(sampleField==4,"Sample loop type clamps at the left edge of the bottom row");
            tap(Button::Up);tap(Button::Down);require(sampleField==4,"Sample remembers the bottom row's loop type focus");

            page=Page::Instrument;instrumentMapView=false;instrumentField=11;
            tap(Button::Up);require(instrumentField==7,"Both Instrument envelope panels return up to Vibrato Sweep");
            tap(Button::Down);require(instrumentField==11,"Instrument restores the Panning Envelope panel");
            tap(Button::Down);require(instrumentField==11,"Instrument envelope row does not wrap at its bottom");
            instrumentField=9;tap(Button::Down);require(instrumentField==0,"Load XI goes down to the instrument fields");
            tap(Button::Up);require(instrumentField==9,"Instrument top row restores Load XI rather than Name");
            tap(Button::Up);require(instrumentField==9,"Instrument top row does not wrap upward");
            instrumentMapView=true;instrumentField=14;keymapNote=1;
            tap(Button::Right);require(instrumentField==14,"Key-map right input cannot jump vertically to Settings");
            tap(Button::Up);require(instrumentField==9,"Key map restores the last top-row action");
            instrumentMapView=false;

            rename(NameTarget::Song);
            tap(Button::Up);tap(Button::Left);require(nameCursor==0,"Name keypad top-left edges clamp");
            for(int n=0;n<15;++n)tap(Button::Right);
            require(nameCursor==11,"Name keypad Right cannot wrap into the next row");
            tap(Button::Down);require(nameCursor==23,"Unvisited keypad row initially keeps the current column");
            for(int n=0;n<3;++n)tap(Button::Left);
            tap(Button::Up);require(nameCursor==11,"Keypad returns to the preceding row's last column");
            tap(Button::Down);require(nameCursor==20,"Keypad restores the new row's own last column");
            tap(Button::Down);tap(Button::Down);tap(Button::Down);
            require(nameCursor==44,"Name keypad bottom clamps without wrapping");
            for(int n=0;n<15;++n)tap(Button::Left);
            require(nameCursor==36,"Name keypad Left cannot wrap to the prior row");
            tap(Button::B);

            openSlotList(true);tap(Button::Up);tap(Button::Left);tap(Button::Right);
            require(slotFocus<0&&slotIndex==0,"Slot list top clamps and horizontal keys do not select footer actions");
            tap(Button::Down);tap(Button::A);
            require(slotIndex==1&&slotFocus==0&&mode==Mode::SlotList,"A chooses the current slot and enters Open without opening it");
            tap(Button::Right);tap(Button::Right);tap(Button::Down);
            require(slotIndex==1&&slotFocus==1,"Slot footer clamps at Delete and at its bottom, preserving the target");
            tap(Button::Up);require(slotIndex==1&&slotFocus<0,"Slot footer Up returns directly to the same target row");
            tap(Button::A);require(slotIndex==1&&slotFocus==1,"Choosing the same slot restores Delete");
            tap(Button::B);require(mode==Mode::SlotList&&slotIndex==1&&slotFocus<0,"B from slot buttons returns to the list without cancelling it");
            tap(Button::Down);tap(Button::A);
            require(slotIndex==2&&slotFocus==1,"Slot action memory also survives choosing another target");
            tap(Button::Left);tap(Button::Left);require(slotFocus==0,"Slot footer left edge clamps at Open");
            tap(Button::Up);chord({Button::Down});
            require(slotIndex==18&&slotFocus<0,"A+Down still pages the numbered list without entering its buttons");
            tap(Button::B);
            require(io::encodeXm(doc)==navigationDocument,"Row navigation and cancelled name editing never alter the XM document");
            mode=Mode::Normal;page=Page::Pattern;held.fill(false);menuParents.clear();
            instrumentField=sampleField=0;songField=0;
            songRowFocus={{6,-1,-1,4,-1}};sampleRowFocus.fill(-1);
            instrumentTopFocus=8;instrumentEnvelopeFocus=10;
        }
