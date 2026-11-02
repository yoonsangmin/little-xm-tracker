            {
                // Render actual modals at native size. Their final control line
                // must share horizontal bounds and leave the same bottom gutter,
                // including dialogs whose control text wraps to a second line.
                auto dialogShot=[&](const char* file,SDL_Rect bounds=SDL_Rect{}) {
                    noticeUntil=0;draw();
                    if(!bounds.w)bounds=toolDialogBounds;
                    if(feedbackWidth==W&&feedbackHeight==H) {
                        const SDL_Rect footer{bounds.x+1,bounds.y+bounds.h-56,bounds.w-2,55};
                        std::vector<std::uint32_t> pixels(std::size_t(footer.w)*footer.h);
                        require(SDL_RenderReadPixels(renderer,&footer,SDL_PIXELFORMAT_ARGB8888,pixels.data(),footer.w*int(sizeof(std::uint32_t)))==0,
                                "modal footer pixels are readable from the production renderer");
                        int last=-1,left=footer.w,right=-1;
                        const auto controlColor=(std::uint32_t(cyan.r)<<16)|(std::uint32_t(cyan.g)<<8)|cyan.b;
                        for(int y=0;y<footer.h;++y)for(int x=0;x<footer.w;++x) {
                            if((pixels[std::size_t(y)*footer.w+x]&0x00ffffffu)!=controlColor)continue;
                            last=std::max(last,y);left=std::min(left,x);right=std::max(right,x);
                        }
                        require(last==35&&left>=23&&right<footer.w-23,
                                "every modal keeps control text inside 24-pixel side insets and a 20-pixel bottom gutter");
                    }
                    screenshot(home/file);SDL_RenderPresent(renderer);
                };
                clearSelection();mode=Mode::Normal;held.fill(false);menuParents.clear();
                openBrowser(FileAction::OpenXm);
                dialogShot("dialog-load.bmp",{40,48,944,666});tap(Button::B);
                openBrowser(FileAction::SaveXm);
                dialogShot("dialog-save.bmp",{40,48,944,666});tap(Button::B);
                menu("SETTINGS");
                dialogShot("dialog-settings.bmp",{129,77,766,486});
                openThemeColors();dialogShot("dialog-theme.bmp",{60,80,904,553});
                openThemeFiles(false);dialogShot("dialog-theme-files.bmp",{85,77,854,588});
                tap(Button::B);tap(Button::B);tap(Button::B);tap(Button::B);
                openSlotList(false);slotIndex=15;
                dialogShot("dialog-pattern-list.bmp",{75,70,874,602});tap(Button::B);
                openSlotList(true);slotIndex=15;slotFocus=1;
                dialogShot("dialog-instrument-list.bmp",{75,70,874,602});tap(Button::B);
                openCopyDestination(SlotKind::Sample);
                dialogShot("dialog-sample-destination.bmp",{75,70,874,630});tap(Button::B);
                rename(NameTarget::Song);
                dialogShot("dialog-name.bmp",{86,121,852,399});tap(Button::B);
                confirm("DELETE PATTERN","CLEAR THE PATTERN; KEEP ITS NUMBER.",[]{});
                dialogShot("dialog-confirm.bmp",{107,290,810,188});tap(Button::B);
                std::string longFilename;
                for(int n=0;n<251;++n)longFilename+=n%22==21?" ":u8"\uac00";
                longFilename+=".xm";
                confirm("REPLACE EXISTING FILE?",longFilename,[]{});
                dialogShot("dialog-confirm-long.bmp",{107,202,810,364});tap(Button::B);
                openEnvelope(false);
                dialogShot("dialog-envelope.bmp");tap(Button::B);
                openEnvelope(true);
                dialogShot("dialog-panning-envelope.bmp");tap(Button::B);
                envelopePan=false;
                openEnvelopePresets();
                dialogShot("dialog-envelope-presets.bmp");tap(Button::B);tap(Button::B);
                openNumber("PATTERN ROWS",64,1,256,[](int){});
                dialogShot("dialog-number.bmp");tap(Button::B);
                openRenderSettings();
                dialogShot("dialog-render.bmp");tap(Button::B);
                openWaveDraw();
                dialogShot("dialog-draw.bmp");
                openWavePresets();
                dialogShot("dialog-wave-presets.bmp");tap(Button::B);tap(Button::B);
                mode=Mode::PasteMasks;
                dialogShot("dialog-paste-masks.bmp");tap(Button::B);
                mode=Mode::EchoSettings;
                dialogShot("dialog-echo.bmp");mode=Mode::Normal;
                mode=Mode::Recording;
                dialogShot("dialog-recording.bmp");
                const bool savedLimitReached=captureStatus.limitReached;
                captureStatus.limitReached=true;
                dialogShot("dialog-recording-limit.bmp");
                captureStatus.limitReached=savedLimitReached;mode=Mode::Normal;
                mode=Mode::Busy;busyTitle="WAV RENDER";
                dialogShot("dialog-busy.bmp");mode=Mode::Normal;
                page=Page::Pattern;mode=Mode::Help;
                dialogShot("dialog-controls.bmp",{75,70,874,586});tap(Button::B);
                held.fill(false);mode=Mode::Normal;menuParents.clear();
            }
