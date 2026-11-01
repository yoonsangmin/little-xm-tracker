        {
            // The gamepad settings and save dialog must pass the selected orders
            // to the offline renderer without changing the song or edit cursor.
            playback->stop();doc=makeDemoDocument();doc.orders={0,0,0,0};
            page=Page::Song;mode=Mode::Normal;songField=0;songAppendOrder=false;
            order=1;row=selectedPattern=channel=field=0;renderOptions={};
            clearSelection();held.fill(false);normalize();playback->load(doc);
            const auto beforeRender=io::encodeXm(doc);const bool beforeRenderDirty=dirty;
            menu("RENDER WAV");
            require(renderOptions.lastOrder==-1&&!worker.valid(),"render opens in full-song scope without doing work");
            chord({Button::Right});
            require(renderOptions.firstOrder==1&&renderOptions.lastOrder==1,"range scope starts at the selected Song order");
            tap(Button::Down);chord({Button::Up});
            require(renderOptions.firstOrder==1,"first render order cannot exceed the inclusive last order");
            tap(Button::Down);chord({Button::Right});
            require(renderOptions.lastOrder==2,"last render order is editable independently");
            noticeUntil=0;draw();screenshot(home/"render-order-range.bmp");SDL_RenderPresent(renderer);
            tap(Button::Down);tap(Button::Down);chord({Button::Left,Button::Left});
            require(renderOptions.tailSeconds==0,"range tail can be disabled independently of its endpoints");
            tap(Button::Down);tap(Button::Down);tap(Button::A);
            require(mode==Mode::Browser&&fileAction==FileAction::RenderWav,"range settings reach the WAV file browser");
            tap(Button::Start);filename="render-range-ui";tap(Button::Start);
            if(mode==Mode::Confirm)tap(Button::A);
            require(mode==Mode::Busy&&worker.valid(),"confirming a range output starts the cancellable render worker");
            const auto renderStarted=SDL_GetTicks();
            while(worker.valid()&&SDL_GetTicks()-renderStarted<10000u){pollTools();SDL_Delay(1);}
            require(!worker.valid()&&mode==Mode::Normal,"range render worker completes and returns to editing");
            const auto referencePath=home/"render-range-reference.wav";
            auto referenceOptions=renderOptions;referenceOptions.audioSettings=audioSettings;
            renderWav(doc,referencePath,referenceOptions);
            require(io::readFile(browserDefaultFolder(0)/"render-range-ui.wav")==io::readFile(referencePath),"UI export uses its exact first/last orders and tail settings");
            require(io::encodeXm(doc)==beforeRender&&dirty==beforeRenderDirty&&order==1,
                    "offline range export leaves song data, dirty state and cursor unchanged");
            menu("RENDER WAV");chord({Button::Left});tap(Button::Down);
            require(renderOptions.lastOrder==-1&&renderOptions.firstOrder==0&&renderField==3,
                    "full-song scope resets range and skips inactive order fields");
            tap(Button::B);require(!worker.valid(),"cancelling settings starts no new export");
            renderOptions.firstOrder=2;renderOptions.lastOrder=3;doc.orders={0};order=0;normalize();
            menu("RENDER WAV");
            require(renderOptions.firstOrder==0&&renderOptions.lastOrder==0,"saved render endpoints clamp after the song order list shrinks");
            tap(Button::B);renderOptions={};playback->load(doc);clearSelection();
        }
