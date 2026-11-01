// An order stores only a pattern reference. Pattern navigation follows these
// positions, including repeated IDs; variations explicitly create a new pattern.
    int songOrderCursor() const { return songAppendOrder?int(doc.orders.size()):order; }
    void moveSongOrderCursor(int delta) {
        const int last=int(doc.orders.size())-(doc.orders.size()==256||orderBlockSelected?1:0);
        const int next=std::clamp(songOrderCursor()+delta,0,last);
        songAppendOrder=next==int(doc.orders.size());
        order=std::min(next,int(doc.orders.size())-1);
        if(songAppendOrder)songOrderColumn=0;
    }
    int nextSongOrderPattern(int delta=1) const {
        return std::clamp(int(doc.orders.back())+delta,0,255);
    }
    void appendSongOrder(int patternId=-1) {
        if(doc.orders.size()>=256)throw std::runtime_error("MAXIMUM 256 ORDERS");
        if(patternId<0)patternId=nextSongOrderPattern();
        mutate([&]{const int next=int(doc.orders.size());edit::insertOrder(doc,next,patternId);order=next;songAppendOrder=false;});
        songOrderColumn=0;clearSelection();notify("ORDER ADDED");
    }
    void insertSongOrder() {
        if(songAppendOrder){appendSongOrder();return;}
        mutate([&]{edit::insertOrder(doc,order,doc.orders[std::size_t(order)]);});
        songField=0;songOrderColumn=0;clearSelection();changePage(Page::Song);notify("ORDER INSERTED / PATTERN SHARED");
    }
    void editSongPatternRows(int delta) {
        if(songAppendOrder)return;
        const int id=doc.orders[std::size_t(order)];
        const int oldRows=std::size_t(id)<doc.patterns.size()?doc.patterns[std::size_t(id)].rows:64;
        const int nextRows=std::clamp(oldRows+delta,1,256);
        if(nextRows==oldRows)return;
        auto resize=[this,id,nextRows]{
            while(doc.patterns.size()<=std::size_t(id)){Pattern empty;empty.cells.resize(std::size_t(doc.channels)*64);doc.patterns.push_back(std::move(empty));}
            edit::resizePattern(doc.patterns[std::size_t(id)],doc.channels,std::uint16_t(nextRows),true);
            clearSelection();normalize();
        };
        if(nextRows<oldRows){
            finishGesture();
            const int uses=edit::patternUseCount(doc,id);
            confirm("SHORTEN PATTERN "+hex(id)+"?",number(oldRows)+" -> "+number(nextRows)+" ROWS / "+number(uses)+" ORDER(S) SHARE THIS PATTERN",
                [this,resize]{mutate(resize);notify("PATTERN SHORTENED / END ROWS REMOVED");});
        }else{beginGesture();resize();}
    }

    void selectPattern(int id) {
        finishGesture();
        if(id<0||id>255)throw std::out_of_range("Pattern selection");
        selectedPattern=id;row=0;blockSelected=false;followPlayback=false;
        normalize();
    }

    void movePatternOrder(int direction) {
        const int next=std::clamp(order+direction,0,int(doc.orders.size())-1);
        songAppendOrder=false;
        if(next==order&&patternIndex()==doc.orders[std::size_t(next)])return;
        order=next;
        selectPattern(doc.orders[std::size_t(order)]);
        notify("ORDER "+hex(order)+" / PATTERN "+hex(patternIndex()));
    }

    void movePatternPage(int direction) {
        const int pageRows=patternViewRows;
        const int offset=row%pageRows;
        const int nextPage=(row/pageRows+(direction>0?1:-1))*pageRows;
        if(nextPage>=0&&nextPage<pattern().rows) {
            row=std::min(nextPage+offset,int(pattern().rows)-1);
            return;
        }
        const int nextOrder=std::clamp(order+(direction>0?1:-1),0,int(doc.orders.size())-1);
        if(nextOrder==order)return;
        order=nextOrder;songAppendOrder=false;
        selectPattern(doc.orders[std::size_t(order)]);
        const int first=direction>0?0:((int(pattern().rows)-1)/pageRows)*pageRows;
        row=std::min(first+offset,int(pattern().rows)-1);
        notify("ORDER "+hex(order)+" / PATTERN "+hex(patternIndex()));
    }

    void assignPatternToOrder(int id) {
        if(id<0||id>255)throw std::out_of_range("Order pattern");
        if(songAppendOrder){appendSongOrder(id);return;}
        mutate([&]{doc.orders.at(std::size_t(order))=std::uint8_t(id);});
        followPlayback=false;
        notify("ORDER "+hex(order)+" USES PATTERN "+hex(id));
    }

    void pasteOrder() {
        if(!orderClipboard){notify("COPY AN ORDER FIRST");return;}
        assignPatternToOrder(*orderClipboard);
    }

    void moveSelectedOrder(int delta) {
        if(songAppendOrder)throw std::runtime_error("SELECT AN EXISTING ORDER FIRST");
        const int destination=std::clamp(order+delta,0,int(doc.orders.size())-1);
        if(destination==order)return;
        mutate([&]{edit::moveOrder(doc,order,destination);order=destination;});
        songField=0;changePage(Page::Song);
        notify("ORDER MOVED");
    }

    void createOrderPattern(bool duplicate) {
        if(songAppendOrder)throw std::runtime_error("SELECT AN EXISTING ORDER FIRST");
        const int source=doc.orders.at(std::size_t(order));
        const int rows=source<int(doc.patterns.size())?doc.patterns[std::size_t(source)].rows:64;
        int created=0;
        mutate([&]{
            created=duplicate?edit::duplicatePattern(doc,source):edit::createPattern(doc,rows);
            doc.orders[std::size_t(order)]=std::uint8_t(created);
        });
        songField=0;changePage(Page::Song);followPlayback=false;
        notify("ORDER "+hex(order)+" / PATTERN "+hex(source)+" -> "+hex(created)+(duplicate?" / COPIED":" / EMPTY"));
    }
