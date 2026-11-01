        // Page navigation retains its offset, crosses repeated order references,
        // and clamps partial pages without allocating implicit XM patterns.
        for(const int viewRows:{16,32}) {
            playback->stop();playback->stopPreview();held.fill(false);
            aUsed=bUsed=true;cutChordUsed=modalBackHeld=selectPending=selectUsed=channelModifier=false;channelChord=0;
            doc=makeDemoDocument();doc.patterns.push_back(doc.patterns[0]);
            edit::resizePattern(doc.patterns[0],doc.channels,std::uint16_t(viewRows*2+3),true);
            edit::resizePattern(doc.patterns[1],doc.channels,std::uint16_t(viewRows+5),true);
            doc.orders={0,0,1};doc.restart=1;
            page=Page::Pattern;mode=Mode::Normal;selectedPattern=order=channel=field=0;row=2;
            patternViewRows=viewRows;songAppendOrder=false;songOrderColumn=0;
            clearSelection();followPlayback=false;dirty=false;undoStack.clear();redoStack.clear();undoBytes=0;
            normalize();playback->load(doc);clipboard=Cell{75,1,0x30,4,0x32};
            const auto beforePageNavigation=io::encodeXm(doc);
            optionChord(Button::Down);
            require(order==0&&selectedPattern==0&&row==viewRows+2,"B+Down moves one visible page with the same relative row");
            optionChord(Button::Down);
            require(order==0&&row==viewRows*2+2,"B+Down enters a partial final page when the offset fits");
            optionChord(Button::Down);
            require(order==1&&selectedPattern==0&&row==2,"B+Down crosses to a shared pattern's next order only after its last page");
            optionChord(Button::Up);
            require(order==0&&selectedPattern==0&&row==viewRows*2+2,"B+Up enters the preceding shared pattern's last page at the same offset");
            optionChord(Button::Up);optionChord(Button::Up);optionChord(Button::Up);
            require(order==0&&row==2,"B+Up stops on the first order's first page without resetting the row");
            row=viewRows-1;optionChord(Button::Down);optionChord(Button::Down);
            require(order==0&&row==viewRows*2+2,"A short last page clamps an out-of-range row offset to its final row");
            order=2;selectedPattern=1;row=1;optionChord(Button::Up);
            require(order==1&&selectedPattern==0&&row==viewRows*2+1,"Previous-order navigation uses the prior pattern's own last page");
            order=1;row=viewRows*2+1;optionChord(Button::Down);
            require(order==2&&selectedPattern==1&&row==1,"Next-order navigation opens a different pattern at its first-page offset");
            row=viewRows+4;optionChord(Button::Down);
            require(order==2&&selectedPattern==1&&row==viewRows+4,"The last order's last page is stable at the forward boundary");
            optionChord(Button::Up);
            require(order==2&&row==4,"B+Up still moves within the final order before crossing its boundary");
            require(!dirty&&undoStack.empty()&&clipboard->note==75&&io::encodeXm(doc)==beforePageNavigation,
                    "Page and order navigation leave XM, history and clipboard unchanged in both row densities");
            doc.orders={0,255};order=0;selectedPattern=0;row=viewRows*2+1;playback->load(doc);
            const auto beforeImplicitPage=io::encodeXm(doc);const auto patternCount=doc.patterns.size();
            optionChord(Button::Down);
            require(order==1&&selectedPattern==255&&row==1&&pattern().rows==64,"A page boundary can enter an implicit empty pattern without materializing it");
            for(int n=0;n<6;++n)optionChord(Button::Down);
            require(row==64-viewRows+1&&doc.patterns.size()==patternCount&&io::encodeXm(doc)==beforeImplicitPage,
                    "Implicit patterns use 64-row page limits without allocating data");
        }

        // Arrange references in Song; Pattern navigation follows their positions.
        playback->stop();doc=makeDemoDocument();doc.patterns.push_back(doc.patterns[0]);
        doc.patterns[1].at(0,0,doc.channels).note=61;doc.orders={0,0,1};doc.restart=1;
        page=Page::Pattern;mode=Mode::Normal;selectedPattern=order=channel=field=0;row=10;
        songAppendOrder=false;songOrderColumn=0;patternViewRows=32;
        followPlayback=false;dirty=false;undoStack.clear();redoStack.clear();undoBytes=0;
        normalize();playback->load(doc);
        auto navigate=[&](Button direction){buttonInput(Button::Select,true);tap(direction);buttonInput(Button::Select,false);};
        const auto navigationBytes=io::encodeXm(doc);
        clearSelection();
        optionChord(Button::Down);
        require(order==1&&selectedPattern==0&&row==10,"B+Down crosses a one-page pattern into its repeated order while retaining the row offset");
        optionChord(Button::Down);
        require(order==2&&selectedPattern==1,"B+Down then opens the next distinct order pattern");
        row=7;optionChord(Button::Down);
        require(order==2&&selectedPattern==1&&row==7,"The last order clamps without resetting the edit row");
        optionChord(Button::Up);
        require(order==1&&selectedPattern==0,"B+Up retains the second occurrence of a shared pattern");
        optionChord(Button::Up);optionChord(Button::Up);
        require(order==0&&selectedPattern==0&&io::encodeXm(doc)==navigationBytes&&!dirty,
                "Order navigation clamps at the beginning and never edits XM references");
        changePage(Page::Song);order=1;tap(Button::Start);draw();navigate(Button::Right);
        require(order==1&&selectedPattern==0&&followPlayback,"Song follow and Pattern navigation share the current repeated order position");
        optionChord(Button::Down);
        require(order==2&&selectedPattern==1&&!followPlayback&&playback->status().playing&&playback->status().order==1,
                "Manual Pattern navigation starts at the followed order and preserves ongoing song playback");
        tap(Button::Start);order=0;selectedPattern=0;row=10;
        navigate(Button::Right);require(page==Page::Instrument&&mode==Mode::Normal&&row==10,"Select+Right changes view without opening actions or losing the cursor");
        navigate(Button::Left);require(page==Page::Pattern&&selectedPattern==0&&row==10,"returning to Pattern restores its independent editing position");
        buttonInput(Button::Select,true);buttonInput(Button::Right,true);buttonInput(Button::Select,false);
        require(page==Page::Instrument&&!held[std::size_t(Button::Right)],"releasing the view modifier consumes its held direction");
        buttonInput(Button::Right,false);navigate(Button::Left);
        tap(Button::Select);require(mode==Mode::Menu&&menuParents.empty(),"Select release in Pattern opens contextual actions without a hidden parent");
        require(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.label=="PATTERN LIBRARY";}),"main actions have no pattern library");
        tap(Button::B);require(mode==Mode::Normal,"B closes the contextual root menu");
        tap(Button::Select);
        require(mode==Mode::Menu&&selectedPattern==0&&row==10&&io::encodeXm(doc)==navigationBytes,"Select alone opens Actions without changing the edit cursor or music");
        tap(Button::B);
        navigate(Button::Down);
        require(mode==Mode::Normal&&io::encodeXm(doc)==navigationBytes,"unused Select+Down is consumed without opening Actions or editing music");
        tap(Button::Select);
        require(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.label=="PATTERN LIBRARY"||a.label=="NEW PATTERN"||a.label=="DUPLICATE PATTERN"||a.label=="INSERT PATTERN IN SONG";}),"Pattern tools do not expose a second arrangement workflow");
        tap(Button::B);

        changePage(Page::Song);order=2;songField=0;navigate(Button::Right);
        require(selectedPattern==1&&row==0&&order==2,"opening Pattern from Song selects the current order's pattern");
        changePage(Page::Song);tap(Button::Select);
        require(mode==Mode::Menu&&menuParents.empty(),"Song contextual actions open without a hidden main menu");
        tap(Button::B);require(mode==Mode::Normal,"B from Song actions returns directly to the song");
        chord({Button::Left});
        require(page==Page::Song&&doc.orders[2]==0&&selectedPattern==1&&row==0&&doc.patterns.size()==2,"A+Left reuses a pattern without opening it or copying its data");
        chord({Button::Right});require(doc.orders[2]==1,"A+Right changes the selected order pattern by one");
        chord({Button::Up});require(doc.orders[2]==17&&doc.patterns.size()==2,"A+Up selects a virtual empty pattern without materializing data");
        chord({Button::Down});require(doc.orders[2]==1&&doc.patterns.size()==2,"A+Down changes the reference by sixteen");
        const auto beforeOrderTap=io::encodeXm(doc);const auto beforeOrderTapUndo=undoStack.size();
        tap(Button::A);
        require(page==Page::Song&&selectedPattern==1&&row==0&&undoStack.size()==beforeOrderTapUndo&&io::encodeXm(doc)==beforeOrderTap,
                "A alone on an existing order keeps the screen, edit cursor and document unchanged");
        navigate(Button::Right);
        require(page==Page::Pattern&&selectedPattern==1&&row==0&&cell().note==61,"Select+Right opens the current order without another note input");

        changePage(Page::Song);order=0;songField=0;
        menu("DUPLICATE PATTERN");const int independent=doc.orders[0];
        require(page==Page::Song&&songField==0&&order==0&&selectedPattern==1&&independent==2&&doc.orders[1]==0&&doc.orders[2]==1,"duplicate replaces only the selected order and keeps the Song arrangement visible");
        require(same(doc.patterns[independent].at(0,0,doc.channels),doc.patterns[0].at(0,0,doc.channels)),"order duplication preserves all five XM fields");
        navigate(Button::Right);require(page==Page::Pattern&&selectedPattern==independent,"Select+Right enters the newly duplicated pattern");
        auto variant=cell();variant.note=65;variant.volume=0x3f;variant.effect=4;variant.param=0x47;editCell(variant);
        require(doc.patterns[0].at(0,0,doc.channels).note==49,"editing the duplicate preserves the shared original");
        changePage(Page::Song);order=2;chord({Button::Right});
        require(doc.orders[2]==independent&&edit::patternUseCount(doc,independent)==2&&doc.patterns.size()==3,"Song can reuse a newly duplicated pattern without another copy");
        navigate(Button::Right);require(same(cell(),variant),"reused pattern opens the same notes and simultaneous volume and effect columns");
        changePage(Page::Song);order=2;chord({Button::Left});order=0;navigate(Button::B);tap(Button::B);order=2;navigate(Button::A);
        require(doc.orders[2]==independent&&doc.patterns.size()==3,"Song selection copy and Select+A paste retain references without duplicating music");

        menu("NEW EMPTY PATTERN");const int emptyPattern=doc.orders[2];
        require(page==Page::Song&&selectedPattern==independent&&emptyPattern==3&&doc.patterns[emptyPattern].rows==32&&doc.orders[0]==independent,"new blank pattern replaces this order and preserves source length and other uses");
        navigate(Button::Right);require(selectedPattern==emptyPattern&&cell().note==0,"opening a newly blank pattern does not accidentally insert a note");
        menu("UNDO");require(doc.patterns.size()==3&&doc.orders[2]==independent&&selectedPattern==emptyPattern,"undo creation restores the reference and leaves the cursor on its empty slot");
        menu("REDO");require(doc.patterns.size()==4&&doc.orders[2]==emptyPattern,"redo restores the new pattern and its order reference");
        changePage(Page::Song);order=int(doc.orders.size())-1;songField=0;tap(Button::Down);
        require(songAppendOrder&&songOrderCursor()==int(doc.orders.size()),"Down from the last order selects its append row");
        tap(Button::A);
        require(doc.orders[3]==emptyPattern+1,"A on the new order advances from the preceding pattern number");
        chord({Button::Left,Button::Left});
        require(!songAppendOrder&&order==3&&doc.orders[3]==independent&&doc.restart==1&&doc.patterns.size()==4,
                "A appends a reference and A+directions changes it without copying pattern data or moving restart");
        menu("MOVE ORDER UP");menu("MOVE ORDER UP");
        require(page==Page::Song&&songField==0&&order==1&&doc.orders[1]==independent&&doc.patterns.size()==4&&doc.restart==2,"moving the appended reference into the arrangement preserves restart identity");
        menu("MOVE ORDER DOWN");require(order==2&&doc.orders[2]==independent&&doc.restart==1,"moving an order preserves restart identity");
        menu("UNDO");require(doc.orders[1]==independent&&doc.restart==2,"order movement undo restores sequence and restart");
        menu("REDO");require(doc.orders[2]==independent&&doc.restart==1,"order movement redo restores both values");
        const auto storedPatterns=doc.patterns.size();
        buttonInput(Button::A,true);buttonInput(Button::B,true);buttonInput(Button::B,false);buttonInput(Button::A,false);
        require(doc.patterns.size()==storedPatterns&&doc.orders.size()==3&&doc.orders[0]==independent&&mode==Mode::Normal,
                "A+B deletes an order reference and leaves its pattern data intact without an extra release action");
        order=0;chord({Button::Left,Button::Left});
        require(edit::patternUseCount(doc,independent)==0&&same(doc.patterns[independent].at(0,0,doc.channels),variant),"reassigning the last use keeps the now-unused pattern intact");

        // Unused XM data remains accessible through an order reference, without a library.
        order=2;chord({Button::Left});navigate(Button::Right);
        require(selectedPattern==independent&&same(cell(),variant),"a retained unused pattern is reopened by assigning its number in Song");
        changePage(Page::Song);chord({Button::Left,Button::Left});changePage(Page::Pattern);order=0;
        tap(Button::Start);require(playingPattern&&playback->status().pattern==independent,"Pattern Start still plays an edited pattern after its last order reference is changed");
        navigate(Button::Right);navigate(Button::Right);navigate(Button::Left);navigate(Button::Left);
        require(playback->status().playing&&playingPattern&&mode==Mode::Normal,"quick view navigation preserves playback scope");
        tap(Button::Start);

        const auto arranged=io::encodeXm(doc);const auto arrangedPath=home/"arrangement-qa.xm";
        saveTo(arrangedPath,FileAction::SaveXm);openDocument(arrangedPath);
        require(io::encodeXm(doc)==arranged&&doc.patterns.size()==storedPatterns,"XM save and reopen retain unused patterns and arrangement");
        selectPattern(independent);require(same(cell(),variant),"unused pattern reopens with both XM effect columns intact");
        changePage(Page::Song);order=2;songField=0;menu("NEW EMPTY PATTERN");const auto removedTarget=doc.orders[order];
        navigate(Button::Right);tap(Button::Start);
        require(playback->status().playing&&playback->status().pattern==removedTarget,"a new order pattern starts looping from Pattern");
        menu("UNDO");require(playback->status().playing&&playback->status().pattern==removedTarget&&selectedPattern==removedTarget,"undoing creation keeps the same pattern ID playing as an empty slot");

        changePage(Page::Song);songField=0;order=2;chord({Button::Right,Button::Right});
        order=0;noticeUntil=0;draw();screenshot(home/"minimal-order-song.bmp");SDL_RenderPresent(renderer);
        tap(Button::Select);noticeUntil=0;draw();screenshot(home/"minimal-order-menu.bmp");SDL_RenderPresent(renderer);tap(Button::B);
        order=2;navigate(Button::Right);field=4;noticeUntil=0;draw();screenshot(home/"minimal-order-pattern.bmp");SDL_RenderPresent(renderer);
