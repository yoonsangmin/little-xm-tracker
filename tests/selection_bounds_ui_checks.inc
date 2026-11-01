            if(feedbackWidth==W&&feedbackHeight==H) {
                // Read the blank pixel row between tracker rows: the selection
                // must connect all interior whitespace, without leaking past
                // either endpoint's visible cursor box in either direction.
                const auto savedChannels=doc.channels;
                edit::resizeChannels(doc,16);
                const std::array<std::array<int,4>,6> endpoints={{{0,0,0,2},{0,4,0,4},
                    {0,1,2,2},{0,0,2,4},{0,4,1,0},{0,2,8,1}}};
                for(int visibleChannels:{4,8})for(int visibleRows:{16,32}) {
                    patternViewChannels=visibleChannels;patternViewRows=visibleRows;
                    const int pitch=visibleChannels==8?117:234;
                    const std::array<int,5> cursorLeft=visibleChannels==8?
                        std::array<int,5>{1,35,58,81,92}:std::array<int,5>{8,64,106,148,162};
                    const std::array<int,5> cursorWidth=visibleChannels==8?
                        std::array<int,5>{34,23,23,12,23}:std::array<int,5>{40,28,28,16,28};
                    for(const auto& ends:endpoints)for(bool backwards:{false,true}) {
                        blockSelected=true;blockPattern=selectedPattern;
                        blockRow=backwards?7:2;row=backwards?2:7;
                        blockChannel=ends[backwards?2:0];blockField=ends[backwards?3:1];
                        channel=ends[backwards?0:2];field=ends[backwards?1:3];
                        const int pageChannel=patternFirstChannel();
                        const int left=65+(ends[0]-pageChannel)*pitch+cursorLeft[ends[1]];
                        const int right=65+(ends[2]-pageChannel)*pitch+cursorLeft[ends[3]]+cursorWidth[ends[3]];
                        noticeUntil=0;draw();
                        const SDL_Rect stripe{65,88+4*(640/visibleRows)-1,visibleChannels*pitch,1};
                        std::vector<std::uint32_t> pixels(std::size_t(stripe.w));
                        require(SDL_RenderReadPixels(renderer,&stripe,SDL_PIXELFORMAT_ARGB8888,pixels.data(),
                                stripe.w*int(sizeof(std::uint32_t)))==0,"selection boundary pixels are readable");
                        bool bounded=true;
                        for(int x=0;x<stripe.w;++x) {
                            const auto selectionRgb=(std::uint32_t(selectionColor.r)<<16)|(std::uint32_t(selectionColor.g)<<8)|selectionColor.b;
                            const bool selected=(pixels[std::size_t(x)]&0x00ffffffu)==selectionRgb;
                            bounded=bounded&&selected==(stripe.x+x>=left&&stripe.x+x<right);
                        }
                        require(bounded,"selection fills only between endpoint cursor edges, including internal gaps and clipped pages");
                    }
                }
                edit::resizeChannels(doc,savedChannels);
                patternViewChannels=4;patternViewRows=32;
                blockRow=2;blockChannel=0;blockField=0;row=7;channel=0;field=2;
                feedbackShot("selection-right-native.bmp");
                blockRow=7;blockField=2;row=2;field=0;
                feedbackShot("selection-left-native.bmp");
            }
