        {
            playback->stop();playback->stopPreview();cancelPendingNote();gestureBefore.reset();
            doc=makeDemoDocument();
            const auto ownedPcm=doc.instruments[0].samples[0];
            auto& owner=doc.instruments[0];owner.name="FIRST OWNER";owner.samples.assign(16,Sample{});
            owner.samples[2].name="NAME WITHOUT PCM";
            owner.samples[5]=ownedPcm;owner.samples[5].name="OWNED FIFTH";
            owner.noteToSample.fill(5);
            doc.instruments.push_back(owner);doc.instruments[1].name="SECOND OWNER";
            doc.instruments[1].samples[5].name="OTHER OWNER";
            doc.instruments[1].samples[0]=ownedPcm;
            page=Page::Instrument;mode=Mode::Normal;instrument=1;sample=0;keymapNote=49;
            instrumentField=8;instrumentMapView=false;instrumentTopFocus=8;instrumentEnvelopeFocus=10;instrumentMappingFocus=2;
            selectedPattern=order=row=channel=field=0;selectionStart=selectionEnd=0;
            held.fill(false);aUsed=bUsed=true;aPreviewActive=aPreviewBlocked=false;
            modalBackHeld=cutChordUsed=channelModifier=selectPending=selectUsed=false;channelChord=0;
            clearSelection();followPlayback=dirty=false;undoStack.clear();redoStack.clear();undoBytes=0;
            normalize();playback->load(doc);
            const auto layoutDocument=io::encodeXm(doc);

            tap(Button::Left);tap(Button::Up);
            require(instrumentField==8,"Instrument NAME stays within the top row at its left and upper edges");
            tap(Button::Right);require(instrumentField==9,"NAME moves right to LOAD");
            tap(Button::Right);tap(Button::Right);
            require(instrumentField==9,"LOAD is the final top-row control and right never wraps");
            tap(Button::Down);
            require(instrumentField==0,"LOAD enters the first right-pane parameter");
            tap(Button::Up);require(instrumentField==9,"Parameter row returns to the last top-row control");
            tap(Button::Left);tap(Button::Down);
            require(instrumentField==13&&sample==0,"NAME enters the owned sample list at the selected slot");
            tap(Button::Down);tap(Button::Down);
            require(sample==2&&instrumentSampleName(2)=="NAME WITHOUT PCM"&&instrumentSampleName(1)=="--",
                    "Owned sample list keeps unnamed empty slots and metadata-only names at their original numbers");
            tap(Button::Down);tap(Button::Down);tap(Button::Down);
            require(sample==5&&instrumentSampleName(5)=="OWNED FIFTH"&&instrumentSampleName(4)=="--",
                    "Traversing empty slots reaches the sample at 05 without compressing the list");
            tap(Button::Right);tap(Button::Up);tap(Button::Down);
            require(instrumentField==13&&sample==5,"Returning through NAME restores the selected owned sample slot");
            for(int n=0;n<20;++n)tap(Button::Down);
            require(sample==15&&instrumentField==13,"The owned sample list ends at slot 0F without wrapping");
            for(int n=0;n<15;++n)tap(Button::Up);
            require(sample==0&&instrumentField==13,"All sixteen numbered sample slots remain directly navigable");
            tap(Button::Up);require(instrumentField==8,"Up from slot 00 reaches NAME in the row above the list");

            tap(Button::Right);tap(Button::Down);tap(Button::Down);tap(Button::Down);
            require(instrumentField==2,"The mapping row is reached vertically from LOAD through MAP NOTE");
            tap(Button::Right);tap(Button::Right);
            require(instrumentField==12,"KEY MAP sits beside MAPPED SAMPLE and clamps at its right edge");
            tap(Button::Up);require(instrumentField==1,"KEY MAP up goes directly to MAP NOTE");
            tap(Button::Down);require(instrumentField==12,"The mapping row remembers KEY MAP when returning from above");
            tap(Button::Down);require(instrumentField==3,"KEY MAP down goes directly to FADEOUT");
            tap(Button::Up);require(instrumentField==12,"The mapping row remembers KEY MAP when returning from below");
            tap(Button::Left);require(instrumentField==2,"KEY MAP left selects its adjacent MAPPED SAMPLE field");
            tap(Button::Up);tap(Button::Down);
            require(instrumentField==2,"Returning to the mapping row also remembers MAPPED SAMPLE");
            tap(Button::Right);tap(Button::A);
            require(instrumentMapView&&instrumentField==14&&keymapNote==49,
                    "The contextual KEY MAP button opens the existing map at the current note");
            keymapNote=1;tap(Button::Up);
            require(instrumentField==9,"The first map note returns to the remembered LOAD control");
            tap(Button::B);require(!instrumentMapView&&instrumentField==12&&instrumentMappingFocus==12,
                    "B returns to the contextual KEY MAP button without a separate SETTINGS toggle");
            instrumentField=8;tap(Button::A);
            require(mode==Mode::Name&&nameTarget==NameTarget::Instrument&&filename=="FIRST OWNER",
                    "The shared top NAME row edits the current instrument name");
            tap(Button::B);instrumentField=9;tap(Button::A);
            require(mode==Mode::Browser&&fileAction==FileAction::ImportXi,"Top LOAD retains XI instrument import");
            tap(Button::B);

            int outputWidth=0,outputHeight=0;SDL_GetRendererOutputSize(renderer,&outputWidth,&outputHeight);
            auto renderedText=[&](SDL_Rect bounds,std::string_view expected,SDL_Color foreground,SDL_Color background) {
                if(outputWidth!=W||outputHeight!=H)return;
                std::vector<std::uint32_t> actual(std::size_t(bounds.w)*bounds.h),reference(actual.size());
                require(SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,actual.data(),bounds.w*int(sizeof(std::uint32_t)))==0,
                        "Instrument text is readable from the production renderer");
                rect(bounds.x,bounds.y,bounds.w,bounds.h,background);txt(bounds.x,bounds.y,expected,foreground);
                require(SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,reference.data(),bounds.w*int(sizeof(std::uint32_t)))==0,
                        "Instrument text reference is readable from the production renderer");
                require(actual==reference,"Instrument renders current text at the approved position without stale headers or compressed slots");
            };
            auto drawLayout=[&]{noticeUntil=0;draw();};
            instrumentField=8;sample=0;drawLayout();
            renderedText({112,52+controlTextY,750,16},"FIRST OWNER",amber,panel2);
            renderedText({921,52+controlTextY,48,16},"LOAD",cyan,bg);
            renderedText({903,188+controlTextY,84,16},"KEY MAP",cyan,bg);
            renderedText({840,188+controlTextY,24,16},"05",cyan,panel);
            renderedText({32,696,280,16},"1/16 USED",muted,panel);
            renderedText({88,152+1*32,240,16},"--",muted,panel);
            renderedText({88,152+2*32,240,16},"NAME WITHOUT PCM",muted,panel);
            renderedText({88,152+5*32,240,16},"OWNED FIFTH",muted,panel);
            screenshot(home/"instrument-layout-parameters.bmp");SDL_RenderPresent(renderer);

            buttonInput(Button::B,true);tap(Button::Down);buttonInput(Button::B,false);
            require(instrument==2&&sample==0&&instrumentSampleName(5)=="OTHER OWNER",
                    "Switching instruments replaces the list with that instrument's owned samples");
            drawLayout();renderedText({32,696,280,16},"2/16 USED",muted,panel);
            renderedText({88,152+5*32,240,16},"OTHER OWNER",muted,panel);
            buttonInput(Button::B,true);tap(Button::Up);buttonInput(Button::B,false);
            instrumentField=12;tap(Button::A);keymapNote=49;drawLayout();
            renderedText({32,696,280,16},"1/16 USED",muted,panel);
            renderedText({374,142,84,16},"C-4",ink,bg);
            renderedText({374,142+23*23,84,16},"B-5",ink,bg);
            screenshot(home/"instrument-layout-keymap.bmp");SDL_RenderPresent(renderer);
            require(io::encodeXm(doc)==layoutDocument&&!dirty&&undoStack.empty(),
                    "Instrument layout, view changes and cancelled file/name dialogs never create or alter samples");

            doc=makeDemoDocument();instrument=1;sample=0;keymapNote=49;
            instrumentMapView=false;instrumentField=0;instrumentTopFocus=8;instrumentEnvelopeFocus=10;instrumentMappingFocus=2;
            page=Page::Pattern;mode=Mode::Normal;held.fill(false);clearSelection();noticeUntil=0;
            normalize();playback->load(doc);
        }
