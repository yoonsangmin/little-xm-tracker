        {
            // Whole-slot actions are independent of the cursor, range clipboard and masks.
            playback->stop();playback->stopPreview();held.fill(false);
            aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            doc=makeDemoDocument();doc.orders={1,1,0};doc.patterns.push_back(doc.patterns[0]);
            doc.patterns[0].at(31,doc.channels-1,doc.channels)={72,1,0x50,4,0x47};
            doc.patterns[1].rows=64;
            doc.patterns[1].cells.assign(std::size_t(64)*doc.channels,Cell{60,1,0x40,2,0x34});
            page=Page::Pattern;mode=Mode::Normal;selectedPattern=0;order=2;row=9;channel=1;field=4;
            instrument=1;sample=0;followPlayback=dirty=false;songAppendOrder=false;
            clearSelection();gestureBefore.reset();undoStack.clear();redoStack.clear();undoBytes=0;
            patternSlotClipboard.reset();patternSlotClipboardChannels=0;
            clipboard=Cell{80,1,0,0,0};blockClipboard.reset();
            copyMask={false,false,false,false,false,true};pasteMask=copyMask;copiedMask=copyMask;
            normalize();playback->load(doc);
            const auto beforeWholePaste=io::encodeXm(doc);
            menu("COPY PATTERN");
            require(mode==Mode::SlotList&&slotPaste&&slotKind==SlotKind::Pattern&&slotIndex==0&&
                    patternSlotClipboard&&patternSlotClipboard->rows==32&&
                    patternSlotClipboardChannels==doc.channels&&clipboard&&clipboard->note==80&&
                    !blockClipboard&&io::encodeXm(doc)==beforeWholePaste&&undoStack.empty(),
                    "COPY PATTERN opens its destination list with a full snapshot and separate range clipboard");
            chord({Button::Down});tap(Button::Down);
            require(mode==Mode::SlotList&&slotIndex==17&&selectedPattern==0&&row==9&&channel==1&&field==4&&order==2&&
                    io::encodeXm(doc)==beforeWholePaste&&undoStack.empty(),
                    "Pattern destination browsing reaches uncreated IDs without changing the editor or document");
            tap(Button::B);
            require(mode==Mode::Normal&&!slotPaste&&selectedPattern==0&&row==9&&channel==1&&field==4&&order==2&&
                    patternSlotClipboard&&io::encodeXm(doc)==beforeWholePaste&&undoStack.empty(),
                    "Cancelling a pattern destination preserves source cursor, snapshot and document");
            menu("COPY PATTERN");tap(Button::Down);tap(Button::A);
            require(mode==Mode::Confirm,"A on an existing pattern destination asks before overwriting");
            tap(Button::B);
            require(mode==Mode::SlotList&&slotPaste&&slotIndex==1&&selectedPattern==0&&row==9&&
                    io::encodeXm(doc)==beforeWholePaste&&undoStack.empty(),
                    "Cancelling pattern overwrite returns to the same destination row without editing");
            tap(Button::B);
            selectedPattern=1;order=0;row=63;normalize();
            menu("PASTE PATTERN");
            require(mode==Mode::Confirm,"whole pattern overwrite asks before replacing shared contents");
            tap(Button::B);
            require(mode==Mode::Normal&&io::encodeXm(doc)==beforeWholePaste&&undoStack.empty(),
                    "cancel whole pattern paste keeps all rows and history intact");
            menu("PASTE PATTERN");tap(Button::A);
            bool wholePatternMatches=doc.patterns[1].rows==doc.patterns[0].rows;
            for(std::size_t n=0;n<doc.patterns[0].cells.size()&&wholePatternMatches;++n)
                wholePatternMatches=same(doc.patterns[1].cells[n],doc.patterns[0].cells[n]);
            require(wholePatternMatches&&row==31&&selectedPattern==1&&
                    doc.orders==std::vector<std::uint8_t>({1,1,0})&&undoStack.size()==1&&
                    clipboard&&clipboard->note==80,
                    "PASTE PATTERN replaces length, empty cells and all fields despite cursor/masks, preserving references");
            const auto afterWholePaste=io::encodeXm(doc);
            menu("UNDO");require(io::encodeXm(doc)==beforeWholePaste,"one Undo restores the entire overwritten pattern");
            menu("REDO");require(io::encodeXm(doc)==afterWholePaste,"one Redo restores whole pattern replacement");
            menu("COPY PATTERN");chord({Button::Down});tap(Button::A);
            require(mode==Mode::Normal&&!slotPaste&&selectedPattern==17&&row==0&&
                    doc.patterns.size()==18&&doc.patterns[17].rows==32&&doc.patterns[16].rows==64&&
                    doc.orders==std::vector<std::uint8_t>({1,1,0}),
                    "A pastes into an uncreated pattern and opens it without adding or reassigning Orders");
            menu("UNDO");require(io::encodeXm(doc)==afterWholePaste,"Undo removes newly materialized pattern slots");
            const int copiedChannels=patternSlotClipboardChannels;
            patternSlotClipboardChannels=copiedChannels==32?31:copiedChannels+1;
            bool rejectedWholePaste=false;const auto beforeRejectedPaste=io::encodeXm(doc);
            const auto beforeRejectedHistory=undoStack.size();
            try{pastePatternSlot();}catch(const std::exception&){rejectedWholePaste=true;}
            require(rejectedWholePaste&&mode==Mode::Normal&&io::encodeXm(doc)==beforeRejectedPaste&&
                    undoStack.size()==beforeRejectedHistory,
                    "a different channel count rejects whole pattern paste without partial data or history");
            patternSlotClipboardChannels=copiedChannels;

            changePage(Page::Instrument);instrument=1;instrumentField=0;
            instrumentClipboard=Instrument{};instrumentClipboard->name="KEEP CLIPBOARD";
            const auto beforeRemovedChords=io::encodeXm(doc);
            selectChord(Button::B);selectChord(Button::A);
            require(mode==Mode::Normal&&instrumentClipboard->name=="KEEP CLIPBOARD"&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "former whole-instrument copy/paste chords do nothing and never open Actions on release");
            menu("COPY INSTRUMENT");
            require(mode==Mode::SlotList&&slotPaste&&slotKind==SlotKind::Instrument&&slotIndex==0&&
                    instrumentClipboard&&io::encodeXi(*instrumentClipboard)==io::encodeXi(*currentInstrument()),
                    "COPY INSTRUMENT opens its destination list with settings and all samples");
            const auto copiedInstrumentBytes=io::encodeXi(*instrumentClipboard);
            chord({Button::Down});
            require(slotIndex==16&&instrument==1&&sample==0&&instrumentField==0&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "Instrument destination pages include empty IDs without changing the source cursor");
            tap(Button::B);
            require(mode==Mode::Normal&&instrument==1&&sample==0&&instrumentField==0&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "Cancelling an instrument destination keeps the editor and document unchanged");
            menu("COPY INSTRUMENT");tap(Button::A);
            require(mode==Mode::Confirm,"Instrument destination asks before replacing an existing slot");
            tap(Button::B);
            require(mode==Mode::SlotList&&slotPaste&&slotIndex==0&&instrument==1&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "Cancelling instrument overwrite returns to its destination list");
            chord({Button::Down});tap(Button::A);
            require(mode==Mode::Normal&&!slotPaste&&page==Page::Instrument&&instrument==17&&sample==0&&
                    doc.instruments.size()==17&&doc.instruments[15].samples.empty()&&
                    io::encodeXi(*currentInstrument())==copiedInstrumentBytes&&
                    undoStack.size()==beforeRejectedHistory+1,
                    "A copies a whole instrument into an empty numbered slot and opens the destination");
            menu("UNDO");
            require(io::encodeXm(doc)==beforeRemovedChords,"Instrument destination paste is one undoable change");

            instrument=1;changePage(Page::Sample);sample=0;selectionStart=2;selectionEnd=9;
            pcmClipboard=edit::copyPcm(*currentSample(),{2,9});
            const auto keptPcmClip=pcmClipboard->pcm;const auto keptKeyMap=currentInstrument()->noteToSample;
            tap(Button::Select); // Leave the PCM selection before opening the general Actions menu.
            menu("COPY SAMPLE SLOT");
            require(mode==Mode::SlotList&&slotPaste&&slotKind==SlotKind::Sample&&slotOwner==1&&slotIndex==0,
                    "COPY SAMPLE SLOT opens a destination list within the source instrument");
            buttonInput(Button::B,true);buttonInput(Button::Right,true);buttonInput(Button::B,false);
            require(mode==Mode::SlotList&&slotOwner==2&&!held[std::size_t(Button::Right)]&&
                    page==Page::Sample&&instrument==1&&sample==0,
                    "Releasing B first after B+Right changes destination owner without cancelling or leaking movement");
            buttonInput(Button::Right,false);chord({Button::Down});
            require(slotIndex==15&&io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "Sample destination paging reaches empty slot 0F without creating its missing owner");
            tap(Button::B);
            require(mode==Mode::Normal&&!slotPaste&&page==Page::Sample&&instrument==1&&sample==0&&
                    sampleClipboard&&pcmClipboard->pcm==keptPcmClip&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "B alone cancels sample destination selection while preserving source and both clipboards");
            menu("COPY SAMPLE SLOT");tap(Button::A);
            require(mode==Mode::Confirm,"A on an existing sample destination asks before replacing its contents");
            tap(Button::B);
            require(mode==Mode::SlotList&&slotPaste&&slotOwner==1&&slotIndex==0&&
                    io::encodeXm(doc)==beforeRemovedChords&&undoStack.size()==beforeRejectedHistory,
                    "Cancelling sample overwrite returns to the same list without a second B-release cancellation");
            optionChord(Button::Right);tap(Button::Down);tap(Button::A);
            require(mode==Mode::Normal&&!slotPaste&&page==Page::Sample&&instrument==2&&sample==1&&
                    doc.instruments.size()==2&&currentSample()->pcm==sampleClipboard->pcm&&
                    currentInstrument()->noteToSample==Instrument{}.noteToSample&&
                    doc.instruments[0].noteToSample==keptKeyMap&&pcmClipboard->pcm==keptPcmClip,
                    "Cross-instrument destination paste creates empty gaps and retains the SAMPLE editor");
            menu("UNDO");
            require(io::encodeXm(doc)==beforeRemovedChords,"Undo removes the newly created sample owner and slots");
            instrument=1;sample=0;changePage(Page::Instrument);menu("COPY SAMPLE SLOT");
            tap(Button::Down);tap(Button::A);
            require(mode==Mode::Normal&&page==Page::Instrument&&instrument==1&&sample==1&&
                    currentSample()->pcm==sampleClipboard->pcm&&currentInstrument()->noteToSample==keptKeyMap,
                    "Sample destination paste started from INSTRUMENT stays in INSTRUMENT and selects the target sample");
            menu("UNDO");
            require(io::encodeXm(doc)==beforeRemovedChords,"Instrument-screen sample paste is independently undoable");
            sample=0;changePage(Page::Sample);menu("COPY SAMPLE SLOT");tap(Button::B);
            sample=1;selectionStart=selectionEnd=0;normalize();menu("PASTE SAMPLE SLOT");
            require(mode==Mode::Normal&&currentSample()->pcm==sampleClipboard->pcm&&
                    currentSample()->loopLength==sampleClipboard->loopLength&&
                    currentInstrument()->noteToSample==keptKeyMap&&pcmClipboard->pcm==keptPcmClip,
                    "whole sample actions preserve the separate PCM range clipboard and destination key mapping");
            menu("UNDO");
            require(io::encodeXm(doc)==beforeRemovedChords,"sample slot paste is independently undoable");
            sample=0;selectedPattern=0;order=row=channel=field=0;
            copyMask={};pasteMask={};copiedMask={};clearSelection();mode=Mode::Normal;normalize();held.fill(false);
        }
