        {
            // Every dialog starts from its configured category; directory browsing
            // and cancel releases must not edit music or overwrite a clipboard.
            playback->stop();playback->stopPreview();mode=Mode::Normal;held.fill(false);
            const auto savedHome=home,savedContentDirectory=contentDirectory;
            const auto savedBrowserFolders=browserFolders;
            const auto savedBrowserDirectory=browserDirectory,savedCurrentFile=currentFile;
            const auto savedPage=page;
            const auto savedClipboard=clipboard;
            const auto savedBlockClipboard=blockClipboard;
            const bool savedBlockSelected=blockSelected;
            const int savedBlockRow=blockRow,savedBlockChannel=blockChannel,savedBlockPattern=blockPattern;
            const auto beforeFolderDocument=io::encodeXm(doc);
            const auto beforeFolderUndo=undoStack.size(),beforeFolderRedo=redoStack.size();
            const auto beforeFolderSerial=recoverySerial;const bool beforeFolderDirty=dirty;
            const auto folderQa=home/("browser-folders-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            home=folderQa/"settings";contentDirectory=folderQa/"portable-app";
            browserFolders={};browserFolderPicker=-1;
            std::filesystem::create_directories(home);std::filesystem::create_directories(contentDirectory);
            for(std::size_t category=0;category<3;++category)
                require(browserDefaultFolder(category)==contentDirectory&&!std::filesystem::exists(browserConfiguredFolder(category)),
                        "Missing packaged content folders fall back to the application root without being recreated");
            // Packaging supplies these empty directories; runtime does not.
            for(const char* category:{"songs","instruments","samples"})std::filesystem::create_directories(contentDirectory/category);
            const auto defaultSong=browserDefaultFolder(0),defaultInstrument=browserDefaultFolder(1),defaultSample=browserDefaultFolder(2);
            require(defaultSong==contentDirectory/"songs"&&defaultInstrument==contentDirectory/"instruments"&&
                    defaultSample==contentDirectory/"samples"&&std::filesystem::is_directory(defaultSong)&&
                    std::filesystem::is_directory(defaultInstrument)&&std::filesystem::is_directory(defaultSample),
                    "Portable defaults use existing packaged category folders separately from settings");
            const auto historyPath=home/"browser-folders.txt";
            for(const auto action:{FileAction::OpenXm,FileAction::SaveXm,FileAction::RenderWav,
                                  FileAction::ImportXi,FileAction::ExportXi,
                                  FileAction::ImportAudio,FileAction::ExportWav,FileAction::ExportWavRange}) {
                browserDirectory=folderQa;openBrowser(action);
                const auto expected=browserDefaultFolder(browserFolderCategory(action));
                require(mode==Mode::Browser&&browserDirectory==expected,"Every dialog ignores the last visited directory and starts at its category default");
                tap(Button::Select);require(mode==Mode::Browser,"Select is unused in the file browser, including on release");
                tap(Button::B);require(mode==Mode::Normal,"B closes the browser without invoking the underlying editor on release");
            }
            require(!std::filesystem::exists(historyPath),"Browsing never creates a last-folder preference file");
            const std::vector<std::uint8_t> historySentinel={'l','e','g','a','c','y'};
            io::writeFileAtomic(historyPath,historySentinel);
            const auto nested=defaultSong/std::filesystem::u8path(u8"Nested 한글 space");
            std::filesystem::create_directories(nested);
            const auto preservedSong=folderQa/"existing-song.xm";
            io::writeFileAtomic(preservedSong,beforeFolderDocument);
            currentFile=preservedSong;
            openBrowser(FileAction::SaveXm);
            require(browserDirectory==defaultSong&&currentFile==preservedSong,"Save As starts in songs while keeping the explicit SAVE target unchanged");
            tap(Button::B);
            require(mode==Mode::Normal&&currentFile==preservedSong&&io::readFile(preservedSong)==beforeFolderDocument,
                    "Canceling Save As neither relocates nor overwrites the existing song");
            currentFile=savedCurrentFile;

            auto enterDirectory=[&](const std::filesystem::path& destination){
                const auto entry=std::find_if(entries.begin(),entries.end(),[&](const Entry& value){return value.directory&&value.path==destination;});
                require(entry!=entries.end(),"Requested folder appears in the browser");
                browserIndex=int(std::distance(entries.begin(),entry));tap(Button::A);
                require(mode==Mode::Browser&&browserDirectory==destination,"A opens the explicitly selected directory");
            };
            openBrowser(FileAction::OpenXm);enterDirectory(nested);
            require(!entries.empty()&&entries.front().name=="../","Parent navigation is an explicit first entry");
            enterDirectory(defaultSong);
            require(entries.at(browserIndex).path==nested,"Returning to a parent focuses the folder just left");
            tap(Button::A);require(browserDirectory==nested,"A reenters the focused child without searching the parent list");
            page=Page::Pattern;blockSelected=true;blockRow=row;blockChannel=channel;blockPattern=patternIndex();
            clipboard=Cell{12,0,0,0,0};blockClipboard.reset();
            buttonInput(Button::B,true);
            require(mode==Mode::Normal&&browserDirectory==nested,"B immediately cancels rather than walking up folders");
            buttonInput(Button::B,false);
            require(blockSelected&&!blockClipboard&&clipboard&&clipboard->note==12,
                    "Browser B release cannot copy the selected pattern over the clipboard");
            require(mode==Mode::Normal,"Browser cancel release stays on the original editor");
            openBrowser(FileAction::OpenXm);
            require(browserDirectory==defaultSong,"Reopening after navigating and canceling resets to songs");
            enterDirectory(nested);tap(Button::Select);
            require(mode==Mode::Browser&&browserDirectory==nested&&blockSelected&&!blockClipboard&&clipboard&&clipboard->note==12,
                    "Browser Select does nothing and cannot clear the editor selection or open its menu");
            tap(Button::B);
            openBrowser(FileAction::ImportAudio);
            require(browserDirectory==defaultSample,"Navigating song folders cannot affect the sample default");
            tap(Button::B);

            for(int n=0;n<40;++n)io::writeFileAtomic(defaultSong/("file-"+number(n)+".xm"),{'x'});
            openBrowser(FileAction::OpenXm);
            require(entries.size()>std::size_t(browserPageSize*2),"Paging fixture spans more than two visible pages");
            browserIndex=0;tap(Button::Left);require(browserIndex==0,"Page-left clamps at the first entry");
            tap(Button::Right);require(browserIndex==browserPageSize,"Page-right advances one visible page");
            tap(Button::Left);require(browserIndex==0,"Page-left returns by one visible page");
            browserIndex=int(entries.size())-2;tap(Button::Right);
            require(browserIndex==int(entries.size())-1,"Page-right clamps at the final entry");
            tap(Button::Right);require(browserIndex==int(entries.size())-1,"Repeated right paging does not overflow the list");
            tap(Button::B);
            browserDirectory=folderQa/"vanished";fileAction=FileAction::ImportAudio;refreshBrowser();
            require(browserDirectory==defaultSample&&!std::filesystem::exists(folderQa/"vanished"),
                    "A vanished folder falls back to samples without recreating a stale path");
            require(std::filesystem::remove(defaultInstrument),"Only the new empty test directory is removed");
            io::writeFileAtomic(defaultInstrument,{'k','e','e','p'});
            require(browserDefaultFolder(1)==contentDirectory&&io::readFile(defaultInstrument)==std::vector<std::uint8_t>({'k','e','e','p'}),
                    "A file blocking a default folder is preserved and the dialog falls back to the portable root");

            const auto configuredSong=folderQa/std::filesystem::u8path(u8"Songs \"mix\" 한글 space");
            const auto configuredInstrument=folderQa/"Custom instruments",configuredSample=folderQa/"Custom samples";
            browserFolders={configuredSong,configuredInstrument,configuredSample};
            for(const auto& directory:browserFolders)std::filesystem::create_directories(directory);
            const auto configuredFolders=browserFolders;
            saveEditorSettings();browserFolders={};loadEditorSettings();
            require(browserFolders==configuredFolders,
                    "Default folder preferences round-trip absolute paths with spaces, Unicode and quoted characters");
            for(const auto action:{FileAction::OpenXm,FileAction::SaveXm,FileAction::ImportXi,FileAction::ImportAudio}) {
                openBrowser(action);
                require(browserDirectory==configuredFolders[browserFolderCategory(action)],
                        "Each file category uses its own explicitly configured default");
                tap(Button::B);
            }

            const auto chosenFolder=configuredSong/"Picked subfolder";
            std::filesystem::create_directory(chosenFolder);
            io::writeFileAtomic(configuredSong/"ignored.xm",{'x'});
            const auto folderPreferencesBeforeCancel=io::readFile(home/"editor-settings.txt");
            menuParents.clear();openDefaultFolderSettings();noticeUntil=0;draw();screenshot(savedHome/"qa-default-folders.bmp");
            require(mode==Mode::Menu&&menuTitle=="DEFAULT FOLDERS"&&actions.size()==3,
                    "Default Folders exposes one setting for each file category");
            menuIndex=0;tap(Button::A);
            require(mode==Mode::Browser&&browserFolderPicker==0&&browserDirectory==configuredSong&&
                    std::all_of(entries.begin(),entries.end(),[](const Entry& entry){return entry.directory;}),
                    "A on Songs opens a directory-only picker at the configured default");
            noticeUntil=0;draw();screenshot(savedHome/"qa-default-folder-picker.bmp");
            enterDirectory(chosenFolder);tap(Button::Select);
            require(mode==Mode::Browser&&browserDirectory==chosenFolder&&browserFolders==configuredFolders,
                    "Folder-picker Select is unused and browsing does not change preferences");
            tap(Button::B);
            require(mode==Mode::Menu&&menuTitle=="DEFAULT FOLDERS"&&browserFolderPicker<0&&
                    browserFolders==configuredFolders&&io::readFile(home/"editor-settings.txt")==folderPreferencesBeforeCancel&&
                    blockSelected&&!blockClipboard&&clipboard&&clipboard->note==12,
                    "Cancel returns to Default Folders without saving or copying the underlying Pattern selection");
            tap(Button::A);enterDirectory(chosenFolder);tap(Button::Start);
            require(mode==Mode::Menu&&menuTitle=="DEFAULT FOLDERS"&&browserFolderPicker<0&&
                    browserFolders[0]==chosenFolder&&browserFolders[1]==configuredInstrument&&browserFolders[2]==configuredSample&&
                    !playback->status().playing&&blockSelected&&!blockClipboard&&clipboard&&clipboard->note==12,
                    "Picker Start saves only the chosen category without playing music or leaking a button release");
            const auto acceptedFolders=browserFolders;
            browserFolders={};loadEditorSettings();
            require(browserFolders==acceptedFolders,"The accepted directory survives preference reload");
            openBrowser(FileAction::SaveXm);
            require(browserDirectory==chosenFolder&&currentFile==savedCurrentFile,
                    "Save As uses the configured song folder without changing the current SAVE target");
            tap(Button::B);

            const auto unavailable=folderQa/"unavailable-drive"/"songs";
            browserFolders[0]=unavailable;
            require(browserDefaultFolder(0)==contentDirectory&&browserConfiguredFolder(0)==unavailable&&!std::filesystem::exists(unavailable),
                    "Unavailable configured locations retain their preference but fall back without creating a path");
            browserFolders[1]=defaultInstrument;
            require(browserDefaultFolder(1)==contentDirectory&&io::readFile(defaultInstrument)==std::vector<std::uint8_t>({'k','e','e','p'}),
                    "A configured path blocked by a file is preserved and safely falls back");
            browserFolders=acceptedFolders;
            const std::string malformedFolders="song_folder \"relative/path\"\ninstrument_folder unquoted\nsample_folder \"unterminated\n";
            io::writeFileAtomic(home/"editor-settings.txt",std::vector<std::uint8_t>(malformedFolders.begin(),malformedFolders.end()));
            loadEditorSettings();
            require(browserFolders==acceptedFolders,"Malformed, relative and unquoted folder preferences cannot replace valid defaults");
            saveEditorSettings();

            const auto workingSettingsHome=home;
            const auto blockedSettingsHome=folderQa/"blocked-settings";
            io::writeFileAtomic(blockedSettingsHome,{'k','e','e','p'});
            openDefaultFolderSettings();openFolderPicker(0);enterDirectory(configuredSong);
            home=blockedSettingsHome;bool folderSaveFailed=false;
            try {tap(Button::Start);}catch(const std::exception&){folderSaveFailed=true;buttonInput(Button::Start,false);}
            home=workingSettingsHome;
            require(folderSaveFailed&&browserFolders==acceptedFolders&&mode==Mode::Browser&&browserFolderPicker==0&&
                    io::readFile(blockedSettingsHome)==std::vector<std::uint8_t>({'k','e','e','p'}),
                    "A preference write failure preserves the prior directory and leaves the picker available");
            tap(Button::B);

            require(io::readFile(historyPath)==historySentinel,"Legacy folder history is ignored and left untouched");
            require(io::encodeXm(doc)==beforeFolderDocument&&currentFile==savedCurrentFile&&
                    undoStack.size()==beforeFolderUndo&&redoStack.size()==beforeFolderRedo&&
                    recoverySerial==beforeFolderSerial&&dirty==beforeFolderDirty,
                    "Default folders, paging, cancellation and fallback never change the song or history");
            home=savedHome;contentDirectory=savedContentDirectory;browserDirectory=savedBrowserDirectory;page=savedPage;
            browserFolders=savedBrowserFolders;browserFolderPicker=-1;
            clipboard=savedClipboard;blockClipboard=savedBlockClipboard;
            blockSelected=savedBlockSelected;blockRow=savedBlockRow;blockChannel=savedBlockChannel;blockPattern=savedBlockPattern;
            mode=Mode::Normal;held.fill(false);selectPending=false;modalBackHeld=false;menuParents.clear();
        }
