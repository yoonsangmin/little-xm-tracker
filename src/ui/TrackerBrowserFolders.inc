// File dialogs always start in portable category folders. Navigation is local
// to the current dialog; no path history is read, written, or migrated.
    static constexpr int browserPageSize=15;

    static std::size_t browserFolderCategory(FileAction action) {
        switch(action) {
        case FileAction::ImportXi:case FileAction::ExportXi:return 1;
        case FileAction::ImportAudio:case FileAction::ExportWav:case FileAction::ExportWavRange:return 2;
        default:return 0;
        }
    }
    std::filesystem::path browserDefaultFolder(std::size_t category) {
        static const std::array<const char*,3> names={"songs","instruments","samples"};
        const auto directory=contentDirectory/names.at(category);
        std::error_code error;
        std::filesystem::create_directories(directory,error);
        if(!error&&browserDirectoryReadable(directory))return directory;
        // Preserve a file or protected directory blocking the default folder.
        // The portable root remains available for choosing another location.
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
            bUsed=true;selectUsed=true;selectPending=false;mode=Mode::Normal;return true;
        }
        const int last=std::max(0,int(entries.size())-1);
        if(b==Button::Up)browserIndex=std::max(0,browserIndex-1);
        if(b==Button::Down)browserIndex=std::min(last,browserIndex+1);
        if(b==Button::Left)browserIndex=std::max(0,browserIndex-browserPageSize);
        if(b==Button::Right)browserIndex=std::min(last,browserIndex+browserPageSize);
        if(b==Button::A&&!repeating){aUsed=true;chooseEntry();}
        if(b==Button::Start&&!repeating&&saving())startFilename();
        return true;
    }
