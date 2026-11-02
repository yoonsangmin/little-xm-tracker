// File dialogs start in explicitly configured folders, or packaged defaults.
// Browsing never changes the preference or creates a missing content folder.
    static constexpr int browserPageSize=15;

    static std::size_t browserFolderCategory(FileAction action) {
        switch(action) {
        case FileAction::ImportXi:case FileAction::ExportXi:return 1;
        case FileAction::ImportAudio:case FileAction::ExportWav:case FileAction::ExportWavRange:return 2;
        default:return 0;
        }
    }
    std::filesystem::path browserConfiguredFolder(std::size_t category) const {
        static const std::array<const char*,3> names={"songs","instruments","samples"};
        const auto& configured=browserFolders.at(category);
        return configured.empty()?contentDirectory/names.at(category):configured;
    }
    std::filesystem::path browserDefaultFolder(std::size_t category) const {
        const auto directory=browserConfiguredFolder(category);
        if(browserDirectoryReadable(directory))return directory;
        // Missing drives/folders retain their preference for the next visit.
        // Do not recreate a deleted folder or replace a file blocking its path.
        return contentDirectory;
    }
    bool browserDirectoryReadable(const std::filesystem::path& directory) const {
        if(directory.empty()||!directory.is_absolute()||
           directory.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)return false;
        std::error_code error;
        if(!std::filesystem::is_directory(directory,error)||error)return false;
        const std::filesystem::directory_iterator probe(directory,error);
        (void)probe;
        return !error;
    }
    void openFolderPicker(std::size_t category) {
        playback->stopPreview();
        browserDirectory=browserDefaultFolder(category);
        browserFolderPicker=int(category);
        refreshBrowser();mode=Mode::Browser;
    }
    void acceptBrowserFolder() {
        const auto directory=browserDirectory.lexically_normal();
        const auto path=directory.u8string();
        if(!browserDirectoryReadable(directory)||path.find_first_of("\r\n")!=std::string::npos) {
            notify("CANNOT USE THIS FOLDER",true);return;
        }
        const auto category=std::size_t(browserFolderPicker);
        const auto previous=browserFolders.at(category);
        browserFolders[category]=directory;
        try {saveEditorSettings();}
        catch(...) {browserFolders[category]=previous;throw;}
        browserFolderPicker=-1;
        refreshDefaultFolderSettings();notify("DEFAULT FOLDER SAVED");
    }
    void enterBrowserDirectory(const std::filesystem::path& directory) {
        const auto previous=browserDirectory.lexically_normal();
        const bool parent=directory.lexically_normal()==previous.parent_path();
        browserDirectory=directory;
        refreshBrowser();
        if(parent) {
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& entry){
                return entry.directory&&entry.path.lexically_normal()==previous;
            });
            if(found!=entries.end())browserIndex=int(std::distance(entries.begin(),found));
        }
    }
    bool handleBrowser(Button b,bool repeating) {
        if(mode!=Mode::Browser)return false;
        if(b==Button::Select){selectUsed=true;selectPending=false;return true;}
        if(b==Button::B){
            // Consume the release as well: it must not copy a selection or
            // reopen the action menu after the dialog has already closed.
            bUsed=true;selectUsed=true;selectPending=false;
            if(browserFolderPicker>=0){browserFolderPicker=-1;refreshDefaultFolderSettings();}
            else mode=Mode::Normal;
            return true;
        }
        const int last=std::max(0,int(entries.size())-1);
        if(b==Button::Up)browserIndex=std::max(0,browserIndex-1);
        if(b==Button::Down)browserIndex=std::min(last,browserIndex+1);
        if(b==Button::Left)browserIndex=std::max(0,browserIndex-browserPageSize);
        if(b==Button::Right)browserIndex=std::min(last,browserIndex+browserPageSize);
        if(b==Button::A&&!repeating){aUsed=true;chooseEntry();}
        if(b==Button::Start&&!repeating){
            if(browserFolderPicker>=0)acceptBrowserFolder();
            else if(saving())startFilename();
        }
        return true;
    }
