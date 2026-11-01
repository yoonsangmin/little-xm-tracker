        {
            playback->stop();doc=makeDemoDocument();doc.orders={0,0};page=Page::Song;mode=Mode::Normal;
            selectedPattern=order=row=channel=field=songField=songOrderColumn=0;songAppendOrder=false;
            held.fill(false);aUsed=bUsed=true;cutChordUsed=modalBackHeld=false;
            clearSelection();gestureBefore.reset();followPlayback=false;dirty=false;
            undoStack.clear();redoStack.clear();undoBytes=0;normalize();playback->load(doc);
            const auto originalSongGrid=io::encodeXm(doc);const auto originalSongRecovery=recoverySerial;
            tap(Button::Down);tap(Button::Down);
            require(songAppendOrder&&songOrderCursor()==2&&doc.orders.size()==2,
                    "Song cursor reaches the one trailing empty order without extending the song");
            tap(Button::Down);draw();tap(Button::Start);
            require(songOrderCursor()==2&&!playback->status().playing&&!dirty&&recoverySerial==originalSongRecovery&&io::encodeXm(doc)==originalSongGrid,
                    "scrolling, drawing and Start on the append row create no empty gaps or playback");
            tap(Button::Up);chord({Button::Right});
            require(!songAppendOrder&&order==2&&doc.orders==std::vector<std::uint8_t>({0,0,1})&&doc.patterns.size()==1,
                    "editing the trailing order appends its pattern reference without creating pattern content");
            tap(Button::Down);tap(Button::A);
            require(order==3&&doc.orders==std::vector<std::uint8_t>({0,0,1,2})&&!songAppendOrder&&page==Page::Song,
                    "A appends the previous pattern number plus one and stays in Song");
            chord({Button::Left,Button::Left});
            require(doc.orders[3]==0,"an automatically suggested order remains an editable shared pattern reference");
            order=1;songOrderColumn=0;
            buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::A,false);buttonInput(Button::B,false);
            require(doc.orders==std::vector<std::uint8_t>({0,1,0}),"cutting a middle order compacts the sequence without a hole");
            order=0;songOrderColumn=0;tap(Button::Right);
            require(songField==0&&songOrderColumn==1,"Right moves from PATTERN to ROWS inside the same order");
            const auto sharedRows=doc.patterns[0].rows;
            chord({Button::Right});
            require(doc.patterns[0].rows==sharedRows+1&&doc.orders[0]==doc.orders[2]&&mode==Mode::Normal,
                    "growing ROWS edits the shared pattern without changing order references");
            const auto beforeShrink=io::encodeXm(doc);
            chord({Button::Left});
            require(mode==Mode::Confirm&&io::encodeXm(doc)==beforeShrink&&confirmDetail.find("2 ORDER(S)")!=std::string::npos,
                    "even an empty tail-row reduction asks confirmation and identifies shared users");
            tap(Button::B);require(mode==Mode::Normal&&io::encodeXm(doc)==beforeShrink,"canceling ROWS shrink preserves the entire song");
            chord({Button::Left});tap(Button::A);
            require(mode==Mode::Normal&&doc.patterns[0].rows==sharedRows,"confirmed ROWS reduction removes the tail");
            undo();require(io::encodeXm(doc)==beforeShrink,"one Undo restores a confirmed row reduction");
            const auto beforeInsertOrders=doc.orders;
            order=1;songOrderColumn=0;menu("INSERT ORDER");
            require(doc.orders==std::vector<std::uint8_t>({0,1,1,0})&&order==1&&songOrderColumn==0,
                    "INSERT ORDER creates a middle entry before the cursor and shifts following entries");
            undo();require(doc.orders==beforeInsertOrders,"Undo restores the sequence after middle insertion");
            order=0;songOrderColumn=1;
            // A populated tail is also retained until explicit confirmation.
            doc.patterns[0].at(sharedRows,0,doc.channels)={72,1,0,0,0};playback->load(doc);
            chord({Button::Left});require(mode==Mode::Confirm&&doc.patterns[0].at(sharedRows,0,doc.channels).note==72,
                    "shrinking a populated tail never drops it while confirmation is pending");
            tap(Button::A);require(doc.patterns[0].rows==sharedRows,"confirm removes the populated tail only after A");
            undo();require(doc.patterns[0].at(sharedRows,0,doc.channels).note==72,"Undo restores the removed tail note");

            order=1;songOrderColumn=1;const auto virtualOrderPattern=doc.orders[1];
            require(virtualOrderPattern>=doc.patterns.size(),"row-count fixture references an implicit empty pattern");
            chord({Button::Right});
            require(doc.patterns[virtualOrderPattern].rows==65,"editing ROWS creates an implicit pattern with the requested length");
            tap(Button::Left);require(songOrderColumn==0,"Left returns from ROWS to PATTERN");
            const auto beforeGridOrderTap=io::encodeXm(doc);
            tap(Button::A);require(page==Page::Song&&io::encodeXm(doc)==beforeGridOrderTap,"A alone on an existing order neither opens nor edits it");
            selectChord(Button::Right);require(page==Page::Pattern&&selectedPattern==virtualOrderPattern,"Select+Right opens the referenced pattern");
            changePage(Page::Song);songField=0;songAppendOrder=false;
            doc.orders={0x20};order=0;songOrderColumn=0;normalize();playback->load(doc);
            tap(Button::Down);chord({Button::Right});
            require(doc.orders==std::vector<std::uint8_t>({0x20,0x21}),"A+Right appends relative to the previous pattern rather than zero");
            tap(Button::Down);chord({Button::Up});
            require(doc.orders.back()==0x31,"A+Up appends the previous pattern plus sixteen");
            tap(Button::Down);chord({Button::Left});
            require(doc.orders.back()==0x30,"A+Left appends the previous pattern minus one");
            tap(Button::Down);chord({Button::Down});
            require(doc.orders.back()==0x20,"A+Down appends the previous pattern minus sixteen");
            tap(Button::Down);menu("INSERT ORDER");
            require(doc.orders.back()==0x21,"INSERT ORDER at the append row uses the same next-pattern default");
            tap(Button::Down);orderClipboard=0;pasteOrder();
            require(doc.orders.back()==0,"an explicit pasted zero pattern bypasses automatic numbering");
            tap(Button::Down);chord({Button::Left});
            require(doc.orders.back()==0,"relative append clamps at pattern zero");
            doc.orders={0xff};order=0;songAppendOrder=false;normalize();playback->load(doc);
            const auto beforeLastPatternAppend=io::encodeXm(doc);
            tap(Button::Down);tap(Button::A);
            require(doc.orders==std::vector<std::uint8_t>({0xff,0xff}),"automatic append clamps at FF without wrapping or clearing the pattern");
            undo();require(io::encodeXm(doc)==beforeLastPatternAppend,"one Undo restores the song before an automatic append");
            doc.orders.assign(256,0);order=255;normalize();playback->load(doc);
            tap(Button::Down);optionChord(Button::Down);
            require(order==255&&!songAppendOrder&&doc.orders.size()==256,"a full order list exposes no append slot beyond FF");
            doc.orders={0};order=0;normalize();playback->load(doc);songOrderColumn=0;
            page=Page::Pattern;mode=Mode::Normal;clearSelection();held.fill(false);
        }
