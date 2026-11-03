#include "ui/TrackerApp.hpp"
#include "ui/BitmapFont.hpp"
#include "text/Utf8.hpp"
#include "audio/Playback.hpp"
#include "audio/Capture.hpp"
#include "io/Codec.hpp"
#include "io/AudioImport.hpp"
#include "io/Recovery.hpp"
#include "io/Theme.hpp"
#include "model/Document.hpp"
#include "model/EditOperations.hpp"
#include "model/Arrangement.hpp"
#include "model/Cleanup.hpp"
#include "model/Trim.hpp"
#include "model/TrackerExtras.hpp"
#include "model/SampleTools.hpp"
#include "model/WaveSynth.hpp"
#include "model/SampleAnalysis.hpp"
#include "model/SampleChop.hpp"
#include "model/EnvelopePresets.hpp"
#include "model/Minimise.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace brick {
namespace {
constexpr int W = 1024, H = 768;
constexpr std::array<SDL_Color,13> colorPalette(const io::Theme& rgb) {
    std::array<SDL_Color,13> result{};
    for(std::size_t n=0;n<result.size();++n)
        result[n]={Uint8(rgb[n]>>16),Uint8(rgb[n]>>8),Uint8(rgb[n]),Uint8(n==12?225:255)};
    return result;
}
constexpr auto defaultPalette=colorPalette(io::defaultTheme);
constexpr auto themeKeys=io::themeColorKeys;
constexpr std::array<const char*,13> themeLabels{{"BACKGROUND","PANEL","HIGHLIGHT","TEXT","SECONDARY TEXT","DIM TEXT",
    "ACCENT","CURSOR","VOLUME / PLAY","WARNING / MUTE","PATTERN SELECTION","SAMPLE SELECTION","OVERLAY"}};
enum class Page { Song, Pattern, Instrument, Sample };
enum class Button { Up, Down, Left, Right, A, B, Start, Select, Count };
enum class Mode { Normal, Menu, Browser, Name, Confirm, Help, Envelope, EnvelopePresets, Number, RenderSettings, Busy, Recording, ImportOptions, PasteMasks, WaveDraw, EchoSettings, PitchResult, ChopSettings, SlotList, ThemeColors, ThemeFiles, TrimSettings };
enum class NameTarget { File, Song, Instrument, Sample, Theme };
enum class FileAction { OpenXm, SaveXm, ImportXi, ExportXi, ImportAudio, RenderWav, ExportWav, ExportWavRange };
std::string hex(int value, int width = 2) {
    char result[24]; std::snprintf(result, sizeof(result), "%0*X", width, value); return result;
}
template<class T> std::string number(T value) { return std::to_string(value); }
std::string channelNumber(int zeroBased) {
    const auto value = number(zeroBased + 1);
    return value.size() == 1 ? "0" + value : value;
}
std::string noteName(int note) {
    if (note == 0) return "---";
    if (note == 97) return "OFF";
    static const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    if (note < 1 || note > 96) return "???";
    return std::string(names[(note - 1) % 12]) + char('0' + (note - 1) / 12);
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return value;
}
std::optional<Button> controller(Uint8 key) {
    switch (key) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return Button::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Button::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Button::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Button::Right;
    case SDL_CONTROLLER_BUTTON_A: return Button::A;
    case SDL_CONTROLLER_BUTTON_B: return Button::B;
    case SDL_CONTROLLER_BUTTON_START: return Button::Start;
    case SDL_CONTROLLER_BUTTON_BACK: return Button::Select;
    default: return {};
    }
}
}

struct TrackerApp::Impl {
    AppOptions options;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GameController* gamepad = nullptr;
    std::unique_ptr<Playback> playback;
    Document doc;
    Pattern blankPattern;
    Page page = Page::Pattern;
    Mode mode = Mode::Normal;
    bool running = true, dirty = false, audioReady = false, aUsed = false;
    bool aPreviewActive = false, aPreviewBlocked = false;
    struct PendingNote { Cell previous; std::size_t patternCount; };
    std::optional<PendingNote> pendingNoteBefore;
    int pendingNotePattern=0,pendingNoteRow=0,pendingNoteChannel=0;
    bool modalBackHeld = false;
    bool playingPattern = false, followPlayback = false;
    int patternPlaybackOrder = -1;
    bool selectPending = false, selectUsed = false;
    int repeatDelay = 320, repeatInterval = 85;
    int patternViewChannels = 4, patternViewRows = 32;
    bool rowNumbersHex = true;
    bool patternRowAudition = false;
    AudioSettings audioSettings, audioSettingsDraft;
    std::array<SDL_Color,13> palette=defaultPalette, themeDraft=defaultPalette;
    // References keep drawing code readable; palettes belong to this app instance.
    const SDL_Color &bg=palette[0], &panel=palette[1], &panel2=palette[2],
        &ink=palette[3], &muted=palette[4], &dim=palette[5], &cyan=palette[6],
        &amber=palette[7], &green=palette[8], &red=palette[9],
        &selectionColor=palette[10], &sampleSelectionColor=palette[11], &overlayColor=palette[12];
    int themeRole=0, themeField=0, themeAction=0, themeFileAction=0, themeFileIndex=0;
    bool themeSaving=false, themeEditing=false;
    std::filesystem::path themeDirectory;
    std::vector<std::filesystem::path> themeFiles;
    std::filesystem::path themeLoadPath, themeSavePath;
    int themeLoadIndex=0, themeSaveIndex=0;
    std::optional<std::array<SDL_Color,13>> themeFilePreview;
    std::string themeFilePreviewError;
    Uint32 noticeUntil = 0, lastRecoveryAt = 0;
    std::uint64_t recoverySerial = 1, savedRecoverySerial = 0, pendingRecoverySerial = 0;
    std::future<void> recoveryWorker;
    std::optional<io::SourceFingerprint> sourceFingerprint;
    std::array<bool, static_cast<std::size_t>(Button::Count)> held{};
    std::array<Uint32, 4> repeatAt{};
    std::array<Uint32, 4> directionPressedAt{};
    Uint32 editPressedAt=0;
    int order = 0, row = 0, channel = 0, field = 0, instrument = 1, sample = 0;
    int selectedPattern = 0;
    int songOrderColumn = 0;
    bool songAppendOrder = false;
    std::optional<int> orderClipboard;
    int instrumentField = 0, sampleField = 0, songField = 0, keymapNote = 49;
    std::array<int,5> songRowFocus{{6,-1,-1,4,-1}};
    std::array<int,6> sampleRowFocus{{-1,-1,-1,-1,-1,-1}};
    int selectionStart = 0, selectionEnd = 0, solo = -1;
    std::uint64_t documentRevision=1,waveformRevision=0;
    int waveformInstrument=-1,waveformSample=-1;
    std::size_t waveformFirst=0,waveformLast=0;
    bool waveformZoom=false;
    std::array<std::pair<std::int16_t,std::int16_t>,964> waveform{};
    Cell lastCell{49,1,0,0,0};
    std::optional<Cell> clipboard;
    std::optional<edit::PatternBlock> blockClipboard;
    std::optional<Pattern> patternSlotClipboard;
    int patternSlotClipboardChannels=0;
    std::optional<edit::PcmClip> pcmClipboard;
    edit::PasteMask copyMask, pasteMask, copiedMask;
    std::optional<Instrument> instrumentClipboard;
    std::optional<Sample> sampleClipboard, drawingSample;
    int maskRow=0,maskColumn=0,drawFrame=0;
    int echoField=0,echoDelay=512,echoRepeats=3,echoDecay=50;
    bool echoTail=true;
    bool blockSelected=false;
    int blockRow=0,blockChannel=0,blockField=0,blockPattern=-1;
    std::filesystem::path currentFile, home, contentDirectory, browserDirectory;
    std::array<std::filesystem::path,3> browserFolders{};
    int browserFolderPicker=-1;
    std::string message;
    bool errorMessage = false;
    std::shared_ptr<Document> gestureBefore;
    std::string gestureLabel;
    struct Change {
        std::function<void(Document&)> undo, redo;
        bool rebuild = false;
        std::size_t bytes = 0;
        std::string label;
    };
    std::deque<Change> undoStack, redoStack;
    std::size_t undoBytes = 0;
    struct Action { std::string label, description; std::function<void()> run; std::function<void(int,bool)> adjust{}; };
    std::vector<Action> actions;
    int menuIndex = 0;
    struct MenuFrame { std::vector<Action> actions; int index; std::string title; };
    std::vector<MenuFrame> menuParents;
    std::string menuTitle="ACTIONS";
    struct Entry { std::filesystem::path path; bool directory; std::string name; };
    std::vector<Entry> entries;
    int browserIndex = 0;
    FileAction fileAction = FileAction::OpenXm;
    std::string filename;
    int nameCursor = 0;
    std::array<int,4> nameRowColumn{{-1,-1,-1,-1}};
    NameTarget nameTarget=NameTarget::File;
    bool envelopePan=false;
    int envelopeField=0,envelopePoint=0,mapFirst=1,mapLast=96;
    std::string numberTitle;
    int numberValue=0,numberMin=0,numberMax=0,numberStep=16,numberField=0;
    std::function<void(int)> numberCommit;
    RenderOptions renderOptions;
    int renderField=0;
    std::future<void> worker;
    std::function<void()> workerCompletion;
    std::atomic<bool> cancelWorker{false};
    std::mutex workerMutex;
    std::string workerMessage;
    double workerSeconds=0;
    std::string busyTitle;
    Capture capture;
    std::vector<CaptureDevice> captureDevices;
    CaptureStatus captureStatus;
    int captureDevice=0,captureLimit=60,captureField=0;
    bool captureStarted=false;
    std::optional<Sample> pendingRecording;
    std::string confirmTitle, confirmDetail;
    std::function<void()> confirmed;
    Mode confirmCancelMode=Mode::Normal;
    PlaybackStatus transport;

    explicit Impl(AppOptions o) : options(std::move(o)) {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0)
            throw std::runtime_error(SDL_GetError());
        char* executableDirectory = SDL_GetBasePath();
        if (!executableDirectory) throw std::runtime_error("Cannot locate application fonts");
        const auto executablePath = std::filesystem::absolute(std::filesystem::u8path(executableDirectory)).lexically_normal();
        const auto unicodeFont = executablePath / "assets/fonts/unifont-18.0.01.hex";
        SDL_free(executableDirectory);
        ui::loadUnicodeFont(unicodeFont);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        window = SDL_CreateWindow("Little XM Tracker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                  (options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!window) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, W, H);
        SDL_ShowCursor(SDL_DISABLE);
        home = options.dataDirectory.empty()?executablePath/"userdata":std::filesystem::absolute(options.dataDirectory).lexically_normal();
        std::error_code ec;
        std::filesystem::create_directories(home, ec);
        if (ec) throw std::runtime_error("Cannot create userdata folder: "+ec.message());
        // The shipped Brick launcher passes this same userdata path explicitly.
        // Other --data-dir roots keep tests and custom installations isolated.
        bool portableData=home==executablePath/"userdata";
        if(!portableData)portableData=std::filesystem::equivalent(home,executablePath/"userdata",ec)&&!ec;
        contentDirectory=portableData?executablePath:home;
        themeDirectory=contentDirectory/"themes";
        initializeUserFiles(executablePath/"assets");
        const auto mappings = contentDirectory / "gamecontrollerdb.txt";
        if (std::filesystem::exists(mappings) && SDL_GameControllerAddMappingsFromFile(mappings.u8string().c_str()) < 0)
            notify("COULD NOT READ gamecontrollerdb.txt", true);
        for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) {
            gamepad = SDL_GameControllerOpen(i); if (gamepad) break;
        }
        loadEditorSettings();
        browserDirectory = browserDefaultFolder(0);
        loadEnvelopePresetBank();
        doc = makeDemoDocument();
        if (!options.openPath.empty()) {
            io::XmImportInfo importInfo;
            doc = io::decodeXm(io::readFile(options.openPath), &importInfo);
            currentFile = std::filesystem::absolute(options.openPath);
            sourceFingerprint = io::fingerprintFile(currentFile);
            browserDirectory = currentFile.parent_path();
            if (importInfo.ignoredOpenMptExtensions) notifyXmExtras();
        } else if (!options.demo && !options.uiTest && !options.smoke) {
            restoreLastWork();
        }
        selectedPattern = doc.orders.front();
        playback = std::make_unique<Playback>(audioSettings);
        playback->load(doc);
        if (!options.noAudio) {
            try { playback->open(); audioReady = true; }
            catch (const std::exception& e) { notify(std::string("AUDIO UNAVAILABLE: ") + e.what(), true); }
        }
        normalize();
        lastRecoveryAt = SDL_GetTicks();
    }
    ~Impl() {
        if (recoveryWorker.valid()) recoveryWorker.wait();
        cancelWorker=true;if(worker.valid())worker.wait();capture.cancel();
        playback.reset();
        if (gamepad) SDL_GameControllerClose(gamepad);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
    void notify(std::string value, bool error = false) { message = std::move(value); errorMessage = error; noticeUntil = SDL_GetTicks() + (error ? 8000u : 3000u); }
    void notifyXmExtras() {
        notify("OPENED / OPENMPT EXTRAS NOT APPLIED OR SAVED", true);
    }
    int patternIndex() const { return selectedPattern; }
    Pattern& pattern() { const auto p=std::size_t(patternIndex());return p<doc.patterns.size()?doc.patterns[p]:blankPattern; }
    const Pattern& pattern() const { const auto p=std::size_t(patternIndex());return p<doc.patterns.size()?doc.patterns[p]:blankPattern; }
    Cell& cell() { return pattern().at(row, channel, doc.channels); }
    Instrument* currentInstrument() {
        if (instrument < 1 || instrument > int(doc.instruments.size())) return nullptr;
        return &doc.instruments[instrument - 1];
    }
    Sample* currentSample() {
        auto* i = currentInstrument();
        if (!i || sample < 0 || sample >= int(i->samples.size())) return nullptr;
        return &i->samples[sample];
    }
    void normalize() {
        if(doc.orders.empty())throw std::runtime_error("A SONG NEEDS AT LEAST ONE ORDER");
        // XM may omit trailing empty patterns. Keep that representation intact
        // until the user actually writes a cell into one of those patterns.
        if(blankPattern.cells.size()!=std::size_t(doc.channels)*64){blankPattern.rows=64;blankPattern.cells.assign(std::size_t(doc.channels)*64,{});}
        order = std::clamp(order, 0, int(doc.orders.size()) - 1);
        if(doc.orders.size()>=256)songAppendOrder=false;
        orderBlockStart = std::clamp(orderBlockStart, 0, int(doc.orders.size()) - 1);
        selectedPattern = std::clamp(selectedPattern, 0, 255);
        channel = std::clamp(channel, 0, int(doc.channels) - 1);
        row = std::clamp(row, 0, int(pattern().rows) - 1);
        if(blockSelected&&blockPattern==patternIndex()) {
            blockRow=std::clamp(blockRow,0,int(pattern().rows)-1);
            blockChannel=std::clamp(blockChannel,0,int(doc.channels)-1);
            blockField=std::clamp(blockField,0,4);
        }
        instrument = std::clamp(instrument, 1, 128);
        sample = std::clamp(sample, 0, 15);
        if (auto* s = currentSample()) {
            selectionStart = std::clamp(selectionStart, 0, int(s->pcm.size()));
            selectionEnd = std::clamp(selectionEnd, selectionStart, int(s->pcm.size()));
        } else selectionStart = selectionEnd = 0;
    }
    void loadEngine() {
        // Every valid XM pattern ID remains browsable, including a pattern
        // that becomes an implicit empty slot after undo or cleanup.
        const bool refreshPreview=playback->previewPlaying()&&
            ((mode==Mode::Normal&&(page==Page::Instrument||page==Page::Sample))||mode==Mode::Envelope);
        const auto result=playback->updateDocument(doc);
        if(refreshPreview){playback->stopPreview();startEditorPreview();}
        if(result==DocumentUpdateResult::Restarted)notify("EDIT APPLIED / PLAYBACK POSITION RESTARTED");
    }
    const char* contextEditLabel() const {
        const char* labels[]={"SONG EDIT","PATTERN EDIT","INSTRUMENT EDIT","SAMPLE EDIT"};
        return labels[int(page)];
    }
    void addChange(Change change) {
        if(change.label.empty())change.label=contextEditLabel();
        followPlayback = false;
        ++recoverySerial;
        redoStack.clear();
        undoBytes += change.bytes;
        undoStack.push_back(std::move(change));
        while (undoStack.size() > 128 || (undoBytes > 32u * 1024u * 1024u && undoStack.size() > 1)) {
            undoBytes -= undoStack.front().bytes; undoStack.pop_front();
        }
        dirty = true;
    }
    void documentChange(std::shared_ptr<Document> before,std::string label={}) {
        // Validate and prepare playback before committing history. Failed
        // metadata/import operations leave the previous editable song intact.
        edit::DocumentDelta delta;
        const auto previousMutes=playback->muteMask();
        try { (void)io::encodeXm(doc);delta=edit::DocumentDelta::between(*before,doc);normalize();if(!delta.empty())loadEngine(); }
        catch (...) {
            doc=*before;normalize();
            try { loadEngine();if(solo>=0)playback->setSolo(solo);else playback->setMuteMask(previousMutes); } catch (...) {}
            throw;
        }
        if(!delta.empty()){addChange({[delta](Document& d){delta.undo(d);}, [delta](Document& d){delta.redo(d);}, true, delta.bytes(), std::move(label)});++documentRevision;}
    }
    void editCell(Cell value,std::string label={}) {
        const Cell old = cell();
        if (old.note == value.note && old.instrument == value.instrument && old.volume == value.volume &&
            old.effect == value.effect && old.param == value.param) return;
        int p = patternIndex(), r = row, c = channel;
        if(std::size_t(p)>=doc.patterns.size()) {
            auto before=std::make_shared<Document>(doc);
            while(doc.patterns.size()<=std::size_t(p)){Pattern empty;empty.cells.resize(std::size_t(doc.channels)*64);doc.patterns.push_back(std::move(empty));}
            doc.patterns[p].at(r,c,doc.channels)=value;
            documentChange(std::move(before),std::move(label));lastCell=value;if(value.instrument)instrument=std::clamp(int(value.instrument),1,128);return;
        }
        auto assign = [p,r,c](Document& d, Cell v) { d.patterns.at(p).at(r,c,d.channels) = v; };
        playback->setCell(p, r, c, value);
        addChange({[assign,old](Document& d){assign(d,old);}, [assign,value](Document& d){assign(d,value);}, false, sizeof(Cell)*2, std::move(label)});
        cell() = value;
        lastCell = value;
        if (value.instrument) instrument = std::clamp(int(value.instrument), 1, 128);
    }
    void cancelPendingNote() {
        if(!pendingNoteBefore)return;
        const auto draft=*pendingNoteBefore;pendingNoteBefore.reset();
        if(std::size_t(pendingNotePattern)<draft.patternCount) {
            doc.patterns[std::size_t(pendingNotePattern)].at(pendingNoteRow,pendingNoteChannel,doc.channels)=draft.previous;
            playback->setCell(pendingNotePattern,pendingNoteRow,pendingNoteChannel,draft.previous);
        } else {doc.patterns.resize(draft.patternCount);loadEngine();}
    }
    void commitPendingNote() {
        if(!pendingNoteBefore)return;
        const auto value=cell();
        cancelPendingNote();editCell(value);
    }
    void insertHeldNote() {
        if(cell().note||hasSelection())return;
        pendingNoteBefore=PendingNote{cell(),doc.patterns.size()};
        pendingNotePattern=patternIndex();pendingNoteRow=row;pendingNoteChannel=channel;
        auto& value=editablePattern().at(row,channel,doc.channels);
        value.note=lastCell.note>=1&&lastCell.note<=96?lastCell.note:49;
        value.instrument=std::uint8_t(instrument);
        if(std::size_t(pendingNotePattern)<pendingNoteBefore->patternCount)
            playback->setCell(pendingNotePattern,row,channel,value);
        else loadEngine();
    }
    void undo(bool redo = false) {
        auto& from = redo ? redoStack : undoStack;
        auto& to = redo ? undoStack : redoStack;
        if (from.empty()) { notify(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
        const auto before=doc;const int previousSolo=solo;const auto previousMutes=playback->muteMask();
        try{if(redo)from.back().redo(doc);else from.back().undo(doc);normalize();if(solo>=doc.channels)solo=-1;loadEngine();}
        catch(...){doc=before;solo=previousSolo;normalize();try{loadEngine();if(solo>=0)playback->setSolo(solo);else playback->setMuteMask(previousMutes);}catch(...){}throw;}
        auto change = std::move(from.back()); from.pop_back();
        if (redo) undoBytes += change.bytes;else undoBytes -= change.bytes;
        to.push_back(std::move(change));
        ++documentRevision;++recoverySerial;dirty = true;followPlayback=false;
        notify(std::string(redo ? "REDO / " : "UNDO / ")+to.back().label);
    }
    void beginGesture(std::string label={}) {
        if(!gestureBefore){
            gestureBefore=std::make_shared<Document>(doc);
            gestureLabel=label.empty()?contextEditLabel():std::move(label);
        }
    }
    void finishGesture() {
        if(gestureBefore){
            auto before=std::move(gestureBefore);auto label=std::move(gestureLabel);
            gestureLabel.clear();documentChange(std::move(before),std::move(label));
        }
    }
    void auditionNote() {
        int n = page == Page::Pattern ? cell().note : keymapNote;
        if (page == Page::Pattern && n == 97) { playback->stopPreview(); return; }
        int ins = page == Page::Pattern && cell().note && cell().instrument ? cell().instrument : instrument;
        if (n < 1 || n > 96) n = lastCell.note >= 1 && lastCell.note <= 96 ? lastCell.note : 49;
        if (ins >= 1 && ins <= int(doc.instruments.size())) playback->previewNote(ins, n);
    }
    void auditionPatternNote() {
        if (!held[std::size_t(Button::A)] || aPreviewBlocked) return;
        const auto state = playback->status();
        // Editing during song/pattern playback never adds a second audition.
        if (state.playing && !state.rowAudition) return;
        aPreviewActive = true;
        if (patternRowAudition) {
            playback->stopPreview();
            followPlayback = false;
            if(!cell().note){
                // Audition the pending A-tap entry without editing the song:
                // a subsequent A+B / Select chord must still see the empty cell.
                auto candidate=cell();
                candidate.note=lastCell.note>=1&&lastCell.note<=96?lastCell.note:49;
                candidate.instrument=std::uint8_t(instrument);
                playback->previewRow(patternIndex(),row,channel,&candidate);
            }else playback->previewRow(patternIndex(), row);
        } else {
            if (state.rowAudition) playback->stopPreview();
            auditionNote();
        }
    }
    void stopHeldAudition(bool blockUntilRelease = false) {
        if (blockUntilRelease && held[std::size_t(Button::A)]) aPreviewBlocked = true;
        if (!aPreviewActive) return;
        aPreviewActive = false;
        playback->stopPreview();
    }
    void startEditorPreview() {
        if (page == Page::Instrument || mode == Mode::Envelope) auditionNote();
        else if (currentSample() && !currentSample()->pcm.empty()) {
            const bool range = selectionEnd > selectionStart;
            playback->previewSample(instrument, sample, keymapNote,
                range ? selectionStart : 0, range ? selectionEnd : -1);
        } else notify("EMPTY SAMPLE");
    }
    void toggleEditorPreview() {
        finishGesture();
        if (playback->previewPlaying()) playback->stopPreview();
        else startEditorPreview();
    }
    void playToggle(bool songFromBeginning = false) {
        finishGesture();
        const auto state = playback->status();
        if (state.playing) {
            playback->stop(); followPlayback = false;
        } else {
            if (!audioReady && !options.uiTest) { notify("AUDIO IS OFF. RESTART WITH AN AVAILABLE AUDIO DEVICE.", true); return; }
            if(songFromBeginning) {
                const bool keepSelection=hasSelection();
                playingPattern=false;patternPlaybackOrder=-1;songAppendOrder=false;
                playback->playSong(0,0);
                // Starting the whole song does not move selection endpoints.
                // With no selection, the editor follows the new transport.
                followPlayback=!keepSelection;
                if(!keepSelection){order=0;selectedPattern=doc.orders.front();row=0;}
                return;
            }
            if(page==Page::Song&&songAppendOrder){notify("ENTER A PATTERN IN THE NEW ORDER FIRST");return;}
            playingPattern = page != Page::Song;
            patternPlaybackOrder=playingPattern&&doc.orders[std::size_t(order)]==patternIndex()?order:-1;
            if(page==Page::Pattern&&hasSelection()) {
                const auto range=selectedBlock();
                playback->playPattern(patternIndex(),int(range.firstRow),false,int(range.lastRow),int(range.firstRow));
                // Keep the selection endpoints stable so A+directions can edit
                // immediately; playback owns a separate snapshot of the loop.
                followPlayback=false;
                notify("LOOP "+patternRowLabel(int(range.firstRow))+"-"+patternRowLabel(int(range.lastRow)-1)+" / ALL CHANNELS");
            } else {
                row = 0;
                if (playingPattern) playback->playPattern(patternIndex(), 0);
                else {selectedPattern=doc.orders[order];playback->playSong(order, 0);}
                followPlayback = !hasSelection();
            }
        }
    }
    void confirm(std::string title, std::string detail, std::function<void()> action) {
        confirmTitle = std::move(title); confirmDetail = std::move(detail); confirmed = std::move(action);confirmCancelMode=Mode::Normal;mode = Mode::Confirm;
    }
    void destructive(std::function<void()> action) {
        cancelPendingNote();if(held[std::size_t(Button::A)])aUsed=true;
        finishGesture();
        if (dirty) confirm("UNSAVED CHANGES", "CONTINUE AND DISCARD THE CURRENT CHANGES?", std::move(action));
        else action();
    }
    void changePage(Page value) {
        cancelPendingNote();finishGesture();stopHeldAudition(true);
        keymapSelected=false;keymapSelectStage=0;keymapSelectionOwner=0;
        if(held[std::size_t(Button::A)])aUsed=true;
        page = value; mode = Mode::Normal; stopWavePreview();
    }
    void navigatePage(Page value) {
        finishGesture();
        if(page==Page::Song&&value==Page::Pattern) {
            // Opening the arrangement's pattern and B+Up/Down use the same
            // order position; never search by ID because patterns can repeat.
            songAppendOrder=false;
            const int target=doc.orders[std::size_t(order)];
            if(target!=selectedPattern){selectedPattern=target;row=0;clearSelection();followPlayback=false;}
            normalize();
        }
        if(page==Page::Pattern&&value==Page::Instrument) {
            const int target=cell().instrument;
            if(target>=1&&target<=128&&target!=instrument) {
                instrument=target;sample=0;
                selectionStart=selectionEnd=0;waveformZoom=false;
            }
        }
        // Import/create actions choose their own destination via changePage().
        changePage(value);
    }
    void openMenu(bool contextual = false) {
        finishGesture(); playback->stopPreview(); actions.clear(); menuIndex = 0;
        auto add = [&](std::string label, std::string description, std::function<void()> fn) {
            actions.push_back({std::move(label),std::move(description),std::move(fn)});
        };
        add("SONG", "ARRANGE PATTERNS / BPM / SPEED", [this]{navigatePage(Page::Song);});
        add("PATTERN", "NOTES / INSTRUMENTS / VOLUME / EFFECTS", [this]{navigatePage(Page::Pattern);});
        add("INSTRUMENT", "MULTISAMPLES / KEY MAP / INSTRUMENT SETTINGS", [this]{navigatePage(Page::Instrument);});
        add("SAMPLE", "WAVEFORM / LOOP / SELECTION / AUDITION", [this]{navigatePage(Page::Sample);});
        add("CLEAR FIELD", "CLEAR THE SELECTED PATTERN FIELD", [this]{Cell v=cell(); switch(field){case 0:v.note=0;break;case 1:v.instrument=0;break;case 2:v.volume=0;break;case 3:v.effect=0;break;default:v.param=0;} editCell(v); notify("FIELD CLEARED");});
        add("UNDO", "", [this]{undo();});
        add("REDO", "", [this]{undo(true);});
        add("SAVE XM", "SAVE THE CURRENT SONG AS STANDARD XM 1.04", [this]{if(currentFile.empty())openBrowser(FileAction::SaveXm);else saveTo(currentFile,FileAction::SaveXm);});
        add("SAVE XM AS", "CHOOSE A FOLDER AND ENTER A FILENAME", [this]{openBrowser(FileAction::SaveXm);});
        add("OPEN XM", "OPEN AN XM SONG", [this]{destructive([this]{openBrowser(FileAction::OpenXm);});});
        add("IMPORT XI", "REPLACE THE CURRENT INSTRUMENT FROM XI", [this]{openBrowser(FileAction::ImportXi);});
        add("EXPORT XI", "SAVE THE CURRENT MULTISAMPLE INSTRUMENT", [this]{if(!currentInstrument())throw std::runtime_error("NO INSTRUMENT SELECTED");openBrowser(FileAction::ExportXi);});
        add("IMPORT AUDIO", "WAV / FLAC / MP3 / OGG; CHOOSE MONO MIX AND BIT DEPTH", [this]{openBrowser(FileAction::ImportAudio);});
        add("RENDER WAV", "FULL SONG / ORDER RANGE / TAIL / REPEAT", [this]{openRenderSettings();});
        add("INSERT ORDER", "INSERT BEFORE CURSOR / REUSE THIS PATTERN", [this]{insertSongOrder();});
        add("DUPLICATE PATTERN", "COPY THIS PATTERN; ONLY THIS ORDER USES THE NEW COPY", [this]{createOrderPattern(true);});
        add("NEW EMPTY PATTERN", "CREATE A NEW BLANK PATTERN FOR THIS ORDER", [this]{createOrderPattern(false);});
        add("MOVE ORDER UP", "", [this]{moveSelectedOrder(-1);});
        add("MOVE ORDER DOWN", "", [this]{moveSelectedOrder(1);});
        add("CLEAN UP SONG", "CHOOSE UNUSED DATA / LOOP TAILS / 8-BIT CONVERSION", [this]{openTrim();});
        add("TRIM SAMPLE TO SELECTION", "KEEP ONLY THE SELECTED FRAMES", [this]{trimSample();});
        add("NEW SONG", "EMPTY SONG / KEEP DEMO SAMPLE INSTRUMENTS", [this]{destructive([this]{newDocument();});});
        add("CONTROLS", "GAMEPAD SHORTCUTS FOR THIS SCREEN", [this]{mode=Mode::Help;});
        add("EXIT", "KEEP WORK IN RECOVERY AND RETURN TO THE LAUNCHER", [this]{requestExit();});
        buildToolMenus();mode = Mode::Menu;
        (void)contextual; // Every screen opens its own root action panel.

    }
    bool saving() const { return fileAction==FileAction::SaveXm||fileAction==FileAction::ExportXi||fileAction==FileAction::RenderWav||fileAction==FileAction::ExportWav||fileAction==FileAction::ExportWavRange; }
    std::string extension() const {
        if(fileAction==FileAction::ImportXi||fileAction==FileAction::ExportXi)return ".xi";
        if(fileAction==FileAction::RenderWav||fileAction==FileAction::ExportWav||fileAction==FileAction::ExportWavRange)return ".wav";
        return ".xm";
    }
    std::string browserTitle() const {
        if(browserFolderPicker>=0) {
            static const char* titles[]={"SONG FOLDER","INSTRUMENT FOLDER","SAMPLE FOLDER"};
            return titles[browserFolderPicker];
        }
        switch(fileAction){case FileAction::OpenXm:return "OPEN XM";case FileAction::SaveXm:return "SAVE XM AS";case FileAction::ImportXi:return "IMPORT XI";case FileAction::ExportXi:return "EXPORT XI";case FileAction::ImportAudio:return "IMPORT AUDIO";case FileAction::RenderWav:return "RENDER WAV";case FileAction::ExportWav:return "EXPORT SAMPLE WAV";case FileAction::ExportWavRange:return "EXPORT SELECTED RANGE WAV";}
        return "FILES";
    }
    void refreshBrowser() {
        browserIndex=0;
        std::error_code ec;
        // A folder may disappear after it was opened. Retry its category folder
        // once, without retaining partial entries or recreating the old path.
        for(int attempt=0;attempt<2;++attempt) {
            entries.clear();ec.clear();
            if(browserDirectory.has_parent_path() && browserDirectory.parent_path()!=browserDirectory)
                entries.push_back({browserDirectory.parent_path(),true,"../"});
            std::vector<Entry> found;
            for(std::filesystem::directory_iterator it(browserDirectory,ec),end;!ec&&it!=end;it.increment(ec)) {
                std::error_code itemError;bool isDir=it->is_directory(itemError);
                if(itemError)continue;
                if(!isDir) {
                    if(browserFolderPicker>=0)continue;
                    const auto suffix=lower(it->path().extension().u8string());
                    if(fileAction==FileAction::ImportAudio?!io::isAudioExtension(suffix):suffix!=extension())continue;
                }
                found.push_back({it->path(),isDir,it->path().filename().u8string()});
            }
            if(!ec) {
                std::sort(found.begin(),found.end(),[](const Entry& a,const Entry& b){if(a.directory!=b.directory)return a.directory;return lower(a.name)<lower(b.name);});
                entries.insert(entries.end(),found.begin(),found.end());
                return;
            }
            const auto fallback=browserDefaultFolder(browserFolderPicker>=0?std::size_t(browserFolderPicker):browserFolderCategory(fileAction));
            if(browserDirectory==fallback)break;
            browserDirectory=fallback;
        }
        if(ec)notify("FOLDER ERROR: "+ec.message(),true);
    }
    void openBrowser(FileAction action) {
        browserFolderPicker=-1;
        pendingAudio.reset();playback->stopPreview(); fileAction=action;
        browserDirectory=browserDefaultFolder(browserFolderCategory(action));
        refreshBrowser(); mode=Mode::Browser;
    }
    void startFilename() {
        nameTarget=NameTarget::File;
        filename = text::xmDisplayText(fileAction==FileAction::ExportXi&&currentInstrument()?currentInstrument()->name:doc.name);
        if((fileAction==FileAction::ExportWav||fileAction==FileAction::ExportWavRange)&&currentSample())filename=text::xmDisplayText(currentSample()->name);
        if(fileAction==FileAction::SaveXm&&!currentFile.empty())filename=currentFile.stem().u8string();
        for(char& c:filename)if(static_cast<unsigned char>(c)<32||std::string_view("\\/:*?\"<>|").find(c)!=std::string_view::npos)c='_';
        if(filename.empty())filename="untitled";
        filename=text::truncateBytes(filename,120);
        nameCursor=0;nameRowColumn.fill(-1);mode=Mode::Name;
    }
    void saveTo(const std::filesystem::path& path, FileAction action) {
        if(action==FileAction::SaveXm) {
            finishGesture();io::saveXmWithBackup(path,doc);currentFile=std::filesystem::absolute(path);dirty=false;
            sourceFingerprint=io::fingerprintFile(currentFile);
            ++recoverySerial;flushRecovery();
        } else if(action==FileAction::ExportXi) {
            if(!currentInstrument())throw std::runtime_error("NO INSTRUMENT");
            io::writeFileAtomic(path,io::encodeXi(*currentInstrument()));
        } else if(action==FileAction::RenderWav) {
            startRender(path);return;
        } else if(action==FileAction::ExportWav) {
            io::writeFileAtomic(path,io::encodeWavMono(*currentSampleChecked()));
        } else if(action==FileAction::ExportWavRange) {
            const auto* s=currentSampleChecked();
            if(selectionEnd<=selectionStart)throw std::runtime_error("SELECT A PCM RANGE FIRST");
            io::writeFileAtomic(path,io::encodeWavMono(*s,std::size_t(selectionStart),std::size_t(selectionEnd)));
        }
        mode=Mode::Normal;notify("SAVED " + path.u8string());
    }
    void commitFilename() {
        if(nameTarget==NameTarget::Theme){commitThemeName();return;}
        if(nameTarget!=NameTarget::File){
            const auto name=text::truncateBytes(filename,nameTarget==NameTarget::Song?20:22);
            const auto oldName=nameTarget==NameTarget::Song?doc.name:nameTarget==NameTarget::Instrument?
                (currentInstrument()?currentInstrument()->name:""):(currentSample()?currentSample()->name:"");
            if(name!=oldName)mutate([&]{if(nameTarget==NameTarget::Song)doc.name=name;else if(nameTarget==NameTarget::Instrument)editableInstrument().name=name;else editableSample().name=name;});
            mode=Mode::Normal;notify("NAME UPDATED");return;
        }
        if(filename.empty())throw std::runtime_error("ENTER A FILENAME");
        const auto path=browserDirectory/std::filesystem::u8path(filename+extension());
        const auto action=fileAction;
        if(std::filesystem::exists(path))confirm("REPLACE EXISTING FILE?",path.filename().u8string(),[this,path,action]{saveTo(path,action);});
        else saveTo(path,action);
    }
    void chooseEntry() {
        if(entries.empty())return;
        const Entry entry=entries.at(browserIndex);
        if(entry.directory){enterBrowserDirectory(entry.path);return;}
        if(browserFolderPicker>=0)return;
        if(saving()){
            nameTarget=NameTarget::File;filename=entry.path.stem().u8string();nameCursor=0;nameRowColumn.fill(-1);mode=Mode::Name;return;
        }
        if(fileAction==FileAction::OpenXm){
            openDocument(entry.path);
        }else if(fileAction==FileAction::ImportXi){
            auto i=io::decodeXi(io::readFile(entry.path));auto before=std::make_shared<Document>(doc);
            editableInstrument()=std::move(i);
            sample=0;documentChange(before,"IMPORT");changePage(Page::Instrument);notify("XI IMPORTED "+entry.name);
        }else if(fileAction==FileAction::ImportAudio){
            openAudioImport(entry.path);
        }
    }
    void openDocument(const std::filesystem::path& path) {
        cancelPendingNote();
        io::XmImportInfo importInfo;
        auto next=io::decodeXm(io::readFile(path), &importInfo);
        (void)io::encodeXm(next);
        playback->load(next);playingPattern=false;playback->setSolo(-1);playback->setMuteMask(0);
        pendingAudio.reset();
        songAppendOrder=false;songOrderColumn=0;
        doc=std::move(next);currentFile=std::filesystem::absolute(path);sourceFingerprint=io::fingerprintFile(currentFile);order=row=channel=0;selectedPattern=doc.orders.front();orderClipboard.reset();orderBlockClipboard.clear();clearSelection();instrument=1;sample=0;solo=-1;
        ++documentRevision;
        ++recoverySerial;followPlayback=false;lastRecoveryAt=SDL_GetTicks()-30000u;
        blockSelected=false;
        held.fill(false);selectPending=false;aUsed=true;gestureBefore.reset();
        undoStack.clear();redoStack.clear();undoBytes=0;dirty=false;normalize();mode=Mode::Normal;
        browserDirectory=currentFile.parent_path();
        if(importInfo.ignoredOpenMptExtensions)notifyXmExtras();
        else notify("OPENED "+path.filename().u8string());
    }
    void trimSample() {
        auto* s=currentSample();if(!s)throw std::runtime_error("NO SAMPLE");
        if(selectionEnd<=selectionStart)throw std::runtime_error("SET SELECTION START AND END FIRST");
        const auto range=selectedFrames(false);mutate([&]{edit::trimPcm(*currentSample(),range);});selectionStart=0;selectionEnd=int(currentSample()->pcm.size());notify("SAMPLE TRIMMED / UNDO AVAILABLE");
    }

#include "ui/TrackerTools.inc"
#include "ui/TrackerSession.inc"
#include "ui/TrackerTheme.inc"
#include "ui/TrackerBrowserFolders.inc"
#include "ui/TrackerArrangement.inc"
#include "ui/TrackerImport.inc"
#include "ui/TrackerTrim.inc"
#include "ui/TrackerExtras.inc"
#include "ui/TrackerAnalysis.inc"
#include "ui/TrackerEnvelopePresets.inc"
#include "ui/TrackerMinimise.inc"
#include "ui/TrackerControls.inc"
#include "ui/TrackerInstrument.inc"
#include "ui/TrackerSlots.inc"

    static int songFormRow(int value) {
        return value==6?0:value==1||value==2?1:value==3||value==5?2:value==4?3:value>=7&&value<=10?4:-1;
    }
    void navigateSongForm(Button b) {
        const int currentRow=songFormRow(songField);
        if(currentRow>=0)songRowFocus[std::size_t(currentRow)]=songField;
        if(b==Button::Up||b==Button::Down) {
            if(currentRow<0)return;
            const int targetRow=std::clamp(currentRow+(b==Button::Down?1:-1),0,4);
            const bool right=songField==2||songField==3;
            const std::array<int,5> defaults{{6,right?2:1,right?3:5,4,7}};
            songField=songRowFocus[std::size_t(targetRow)]>=0?songRowFocus[std::size_t(targetRow)]:defaults[std::size_t(targetRow)];
        } else if(currentRow==4) {
            songField=std::clamp(songField+(b==Button::Right?1:-1),7,10);
        } else {
            static constexpr int horizontal[7][2]={{0,6},{0,2},{1,2},{5,3},{0,4},{0,3},{0,6}};
            songField=horizontal[std::clamp(songField,0,6)][b==Button::Right?1:0];
        }
        const int nextRow=songFormRow(songField);
        if(nextRow>=0)songRowFocus[std::size_t(nextRow)]=songField;
    }
    static constexpr std::array<std::array<int,4>,5> sampleFormRows{{
        {9,10,11,-1},{0,1,-1,-1},{2,3,-1,-1},{5,6,7,8},{4,12,14,-1}
    }};
    static int sampleFormRow(int value) {
        for(std::size_t r=0;r<sampleFormRows.size();++r)
            for(int item:sampleFormRows[r])if(item==value&&item>=0)return int(r);
        return -1;
    }
    void rememberSampleFocus() {
        const int group=sampleField>=9&&sampleField<=11?0:sampleField<=1?1:
            sampleField<=3?2:sampleField==5||sampleField==6?3:sampleField==7||sampleField==8?4:
            sampleField==12||sampleField==14?5:-1;
        if(group>=0)sampleRowFocus[std::size_t(group)]=sampleField;
    }
    void navigateSampleForm(Button b) {
        if(sampleFormRow(sampleField)<0)sampleField=9;
        const int currentRow=sampleFormRow(sampleField);
        rememberSampleFocus();
        if(b==Button::Up||b==Button::Down) {
            const bool down=b==Button::Down;
            switch(currentRow) {
                case 0:if(down)sampleField=sampleRowFocus[1]>=0?sampleRowFocus[1]:sampleField==9?0:1;break;
                case 1:sampleField=down?(sampleField==0?2:3):(sampleRowFocus[0]>=0?sampleRowFocus[0]:9);break;
                case 2:if(!down)sampleField=sampleField==2?0:1;
                    else {const int group=sampleField==2?3:4;sampleField=sampleRowFocus[group]>=0?sampleRowFocus[group]:group==3?5:7;}break;
                case 3:sampleField=down?(sampleField<=6?4:sampleRowFocus[5]>=0?sampleRowFocus[5]:sampleField==7?12:14):(sampleField<=6?2:3);break;
                case 4:if(!down){const int group=sampleField==4?3:4;sampleField=sampleRowFocus[group]>=0?sampleRowFocus[group]:sampleField==4?5:sampleField==12?7:8;}break;
            }
        } else {
            const auto& fields=sampleFormRows[std::size_t(currentRow)];
            const auto found=std::find(fields.begin(),fields.end(),sampleField);
            const int count=int(std::count_if(fields.begin(),fields.end(),[](int item){return item>=0;}));
            const int column=int(found-fields.begin());
            sampleField=fields[std::size_t(std::clamp(column+(b==Button::Right?1:-1),0,count-1))];
        }
        rememberSampleFocus();
    }
    void normalDirection(Button b) {
        const bool edit=held[static_cast<std::size_t>(Button::A)];
        const int sign=(b==Button::Up||b==Button::Right)?1:-1;
        const bool vertical=b==Button::Up||b==Button::Down;
        if(page!=Page::Pattern||edit||vertical||hasSelection())followPlayback=false;
        if(page==Page::Instrument&&instrumentPanelDirection(b,edit))return;
        if(edit){
            aUsed=true;
            if(page==Page::Pattern){
                if(hasSelection()) {
                    const auto range=selectedBlock();
                    const auto cursorVolume=cell().volume;
                    mutatePattern([&](Pattern& p){edit::adjustBlock(p,doc.channels,range,sign,vertical,std::size_t(field),cursorVolume);});
                    if(field==0)auditionPatternNote();
                    return;
                }
                Cell v=cell();
                if(field==0) {
                    // OFF sits below C-0 for single-cell entry only. Octave
                    // edits reach C-0 first; another downward edit enters OFF.
                    if(v.note==97) { if(sign>0)v.note=1; }
                    else if(v.note==1&&sign<0)v.note=97;
                    else {
                        const int n=v.note>=1&&v.note<=96?v.note:(lastCell.note>=1&&lastCell.note<=96?lastCell.note:49);
                        v.note=std::uint8_t(std::clamp(n+sign*(vertical?12:1),1,96));
                        if(!v.instrument)v.instrument=std::uint8_t(instrument);
                    }
                }
                else if(field==1)v.instrument=std::uint8_t(std::clamp(int(v.instrument)+sign*(vertical?16:1),0,128));
                else if(field==2)v.volume=adjustVolumeColumn(v.volume,sign,vertical);
                else if(field==3)v.effect=std::uint8_t(std::clamp(int(v.effect)+sign,0,35));
                else v.param=std::uint8_t(std::clamp(int(v.param)+sign*(vertical?16:1),0,255));
                editCell(v);
                if(field==0)auditionPatternNote();
            }else if(page==Page::Song){
                if(songField==0){
                    const int delta=sign*(vertical?16:1);
                    if(songOrderColumn==1&&!songAppendOrder)editSongPatternRows(delta);
                    else if(songAppendOrder)appendSongOrder(nextSongOrderPattern(delta));
                    else {beginGesture();doc.orders[order]=std::uint8_t(std::clamp(int(doc.orders[order])+delta,0,255));}
                    return;
                }
                if(songField==4){beginGesture();doc.linear=sign>0;return;}
                if(songField==5){resizeDocumentChannels(std::clamp(int(doc.channels)+sign*(vertical?4:1),1,32));return;}
                if(songField>5)return;
                beginGesture();int delta=sign*(vertical?16:1);

                if(songField==1)doc.bpm=std::uint16_t(std::clamp(int(doc.bpm)+delta,32,255));
                if(songField==2)doc.speed=std::uint16_t(std::clamp(int(doc.speed)+sign,1,31));
                if(songField==3)doc.restart=std::uint16_t(std::clamp(int(doc.restart)+sign,0,int(doc.orders.size())-1));
                normalize();
            }else if(page==Page::Instrument){
                if(instrumentField>7)return;
                if(instrumentField==0){sample=std::clamp(sample+sign,0,15);selectionStart=selectionEnd=0;return;}
                if(instrumentField==1){
                    const int next=std::clamp(keymapNote+sign*(vertical?12:1),1,96);
                    if(next!=keymapNote){keymapNote=next;if(playback->previewPlaying())auditionNote();}
                    return;
                }
                const auto* existing=currentInstrument();
                const int oldValue=!existing?0:instrumentField==2?existing->noteToSample[keymapNote-1]:instrumentField==3?existing->fadeout:
                    instrumentField==4?existing->vibratoDepth:instrumentField==5?existing->vibratoRate:instrumentField==6?existing->vibratoType:existing->vibratoSweep;
                const int maximum=instrumentField==2?15:instrumentField==3?65535:instrumentField==4?15:instrumentField==5?63:instrumentField==6?3:255;
                const int amount=instrumentField==3?(vertical?256:16):instrumentField==7?(vertical?16:1):1;
                const int value=std::clamp(oldValue+sign*amount,0,maximum);
                if(value==oldValue)return;
                beginGesture();auto& i=editableInstrument(true);
                if(instrumentField==2){if(i.samples.size()<=std::size_t(value))i.samples.resize(std::size_t(value+1));i.noteToSample[keymapNote-1]=std::uint8_t(value);}
                if(instrumentField==3)i.fadeout=std::uint16_t(value);
                if(instrumentField==4)i.vibratoDepth=std::uint8_t(value);
                if(instrumentField==5)i.vibratoRate=std::uint8_t(value);
                if(instrumentField==6)i.vibratoType=std::uint8_t(value);
                if(instrumentField==7)i.vibratoSweep=std::uint8_t(value);
            }else{
                if(sampleField>8)return;
                Sample empty;auto* s=currentSample();
                if(!s){
                    if(sampleField>=4)return; // No PCM, so no loop or selection can be changed.
                    const int base=sampleField==0?empty.volume:sampleField==1?empty.panning:0;
                    const int lo=sampleField==0||sampleField==1?0:sampleField==2?-128:-96;
                    const int hi=sampleField==0?64:sampleField==1?255:sampleField==2?127:95;
                    const int delta=sign*(vertical?(sampleField==3?12:16):1);
                    if(std::clamp(base+delta,lo,hi)==base)return;
                    beginGesture();s=&editableSample();
                }
                int delta=sign*(vertical?16:1);
                const int length=int(s->pcm.size());
                const Uint32 now=SDL_GetTicks();
                const Uint32 heldMs=std::min(now-editPressedAt,now-directionPressedAt[std::size_t(b)]);
                const int frameDelta=sign*sampleRangeStep(length,vertical,heldMs);
                if(sampleField>=4&&!length)return;
                const auto movedFrame=[&](int value,int low,int high){return int(std::clamp<std::int64_t>(std::int64_t(value)+frameDelta,low,high));};
                if(sampleField==7||sampleField==8){
                    const bool selected=selectionEnd>selectionStart;
                    selectionStart=std::clamp(selectionStart,0,length);
                    selectionEnd=std::clamp(selectionEnd,selectionStart,length);
                    if(sampleField==7){
                        selectionStart=movedFrame(selectionStart,0,selected?selectionEnd:length);
                        // A collapsed selection is an insertion point. START
                        // moves it; END opens a range without changing START.
                        if(!selected)selectionEnd=selectionStart;
                    } else selectionEnd=movedFrame(selectionEnd,selectionStart,length);
                    if(selectionStart==selectionEnd){pcmSelecting=false;pcmSelectStage=0;}
                    rememberSampleFocus();return;
                }
                beginGesture();
                if(sampleField==0)s->volume=std::uint8_t(std::clamp(int(s->volume)+delta,0,64));
                if(sampleField==1)s->panning=std::uint8_t(std::clamp(int(s->panning)+delta,0,255));
                if(sampleField==2)s->finetune=std::int8_t(std::clamp(int(s->finetune)+delta,-128,127));
                if(sampleField==3)s->relativeNote=std::int8_t(std::clamp(int(s->relativeNote)+sign*(vertical?12:1),-96,95));
                if(sampleField==4){
                    s->loopType=std::uint8_t(std::clamp(int(s->loopType)+sign,0,2));
                    if(s->loopType){
                        if(!s->loopLength){s->loopStart=0;s->loopLength=std::uint32_t(length);}
                        else {
                            const auto end=std::uint64_t(s->loopStart)+s->loopLength;
                            s->loopStart=std::min(s->loopStart,std::uint32_t(length-1));
                            s->loopLength=std::uint32_t(std::clamp(end,std::uint64_t(s->loopStart)+1,std::uint64_t(length))-s->loopStart);
                        }
                    }
                }
                if(sampleField==5||sampleField==6){
                    const bool hasLoop=s->loopLength>0;
                    // Disabled XM loop markers may lie outside PCM. Preserve
                    // them until edited, then normalize before signed arithmetic.
                    int first=int(std::min(s->loopStart,std::uint32_t(length-1)));
                    int end=int(std::clamp(std::uint64_t(s->loopStart)+s->loopLength,
                        std::uint64_t(first),std::uint64_t(length)));
                    if(sampleField==5){
                        first=movedFrame(first,0,hasLoop?end-1:length-1);
                        if(!hasLoop)end=first+1;
                    } else end=movedFrame(end,first+1,length);
                    s->loopStart=std::uint32_t(first);s->loopLength=std::uint32_t(end-first);
                    rememberSampleFocus();
                }
            }
        }else{
            if(page==Page::Pattern){
                if(b==Button::Up)row=std::max(0,row-1);
                if(b==Button::Down)row=std::min(int(pattern().rows)-1,row+1);
                if(b==Button::Left){if(field>0)--field;else if(channel>0){--channel;field=4;}}
                if(b==Button::Right){if(field<4)++field;else if(channel+1<doc.channels){++channel;field=0;}}
            }else if(page==Page::Song){
                if(songField==12)navigateSongMonitor(b);
                else if(b==Button::Down&&((songField>=7&&songField<=10)||
                    (songField==0&&songOrderCursor()==int(doc.orders.size())-(doc.orders.size()==256?1:0))))enterSongMonitor();
                else if(songField==0&&vertical)moveSongOrderCursor(b==Button::Down?1:-1);
                else if(songField==0&&b==Button::Right&&!songAppendOrder&&songOrderColumn==0)songOrderColumn=1;
                else if(songField==0&&b==Button::Left&&songOrderColumn==1)songOrderColumn=0;
                else navigateSongForm(b);
            }else navigateSampleForm(b);
        }
    }
    void buttonDown(Button b, bool repeating=false) {
        // Show a new note immediately, but let an A-first chord replace that
        // provisional insertion without leaving an extra edit or undo entry.
        if(b!=Button::A)cancelPendingNote();
        if(!repeating){held[std::size_t(b)]=true;if(int(b)<4){directionPressedAt[int(b)]=SDL_GetTicks();repeatAt[int(b)]=directionPressedAt[int(b)]+Uint32(repeatDelay);}}
        if(b==Button::A&&!repeating){aUsed=false;aPreviewBlocked=false;editPressedAt=SDL_GetTicks();}
        if(b==Button::B||b==Button::Select||b==Button::Start)stopHeldAudition(true);
        if(b==Button::Start&&held[std::size_t(Button::A)])aUsed=true;
        if(b==Button::B&&!repeating){
            bUsed=mode!=Mode::Normal;modalBackHeld=bUsed;
            if(modalBackHeld)aUsed=true;
            channelModifier=mode==Mode::Normal&&channelControlContext()&&
                !held[std::size_t(Button::Select)]&&!held[std::size_t(Button::Start)]&&!held[std::size_t(Button::A)];
        }
        if(b==Button::Select&&!repeating){
            selectUsed=false;
            for(std::size_t n=0;n<held.size();++n)if(n!=std::size_t(Button::Select)&&held[n])selectUsed=true;
        }
        if(b!=Button::Select&&held[std::size_t(Button::Select)])selectUsed=true;
        // A modal may close on B-down. Until B is released, neither A+B
        // nor a remaining direction may act on the screen underneath it.
        if(mode==Mode::Normal&&modalBackHeld&&held[std::size_t(Button::B)]){
            if(b==Button::A)aUsed=true;
            return;
        }
        if(channelChordDown(b,repeating))return;
        if(b==Button::Start&&!repeating){
            const auto state=playback->status();
            if(state.playing){
                // Releasing a held A after Stop must not start an audition.
                if(held[std::size_t(Button::A)])aUsed=true;
                playback->stop();followPlayback=false;return;
            }
        }
        if(trimButton(b)||themeButton(b)||slotListButton(b)||importAudioButton(b,repeating)||analysisButton(b,repeating)||extrasButton(b,repeating)||toolButton(b,repeating))return;
        if(mode==Mode::Confirm){
            if(b==Button::A&&!repeating){aUsed=true;mode=Mode::Normal;auto fn=std::move(confirmed);if(fn)fn();}
            else if(b==Button::B){mode=confirmCancelMode;confirmed={};}
            return;
        }
        if(mode==Mode::Help){if(b==Button::B||b==Button::Select)mode=Mode::Normal;return;}
        if(mode==Mode::Menu){
            if(int(b)<4&&held[std::size_t(Button::A)]){
                aUsed=true;auto adjust=actions.at(menuIndex).adjust;
                if(adjust)adjust((b==Button::Right||b==Button::Up)?1:-1,b==Button::Up||b==Button::Down);
            }else{
                if(b==Button::Up)menuIndex=std::max(0,menuIndex-1);
                if(b==Button::Down)menuIndex=std::min(int(actions.size())-1,menuIndex+1);
            }
            if(b==Button::B)backMenu();
            return;
        }
        if(handleBrowser(b,repeating))return;
        if(mode==Mode::Name){
            if(int(b)<4) {
                const int currentRow=nameCursor/12,column=nameCursor%12;
                nameRowColumn[std::size_t(currentRow)]=column;
                if(b==Button::Left||b==Button::Right)
                    nameCursor=currentRow*12+std::clamp(column+(b==Button::Right?1:-1),0,11);
                else {
                    const int targetRow=std::clamp(currentRow+(b==Button::Down?1:-1),0,3);
                    const int previous=nameRowColumn[std::size_t(targetRow)];
                    nameCursor=targetRow*12+(previous>=0?previous:column);
                }
                nameRowColumn[std::size_t(nameCursor/12)]=nameCursor%12;
            }
            if(b==Button::A&&!repeating){aUsed=true;const auto& keys=nameKeys();const std::size_t maxName=nameTarget==NameTarget::File?120:nameTarget==NameTarget::Theme?48:nameTarget==NameTarget::Song?20:22;if(nameCursor<44){if(filename.size()<maxName)filename+=keys[nameCursor];}else if(nameCursor==44){text::popBackCharacter(filename);}else if(nameCursor==45)filename.clear();else if(nameCursor==46)commitFilename();else cancelName();}
            if(b==Button::Start)commitFilename();
            if(b==Button::B)cancelName();
            return;
        }
        if((b==Button::A||b==Button::B)&&held[std::size_t(Button::A)]&&held[std::size_t(Button::B)]){
            aUsed=bUsed=true;
            if(!cutChordUsed&&!repeating){cutChordUsed=true;cutCurrent();}
            return;
        }
        if(b==Button::Select&&!repeating){finishGesture();selectPending=true;}
        if(held[std::size_t(Button::Select)]){
            if(b!=Button::Select)selectUsed=true;
            if(b==Button::A||(b==Button::Select&&held[std::size_t(Button::A)])){
                aUsed=true;selectUsed=true;if(!repeating&&page!=Page::Sample)pasteCurrent();
            }else if(b==Button::B||(b==Button::Select&&held[std::size_t(Button::B)])){
                bUsed=true;selectUsed=true;if(!repeating)markSelection();
            }else if(b==Button::Left||b==Button::Right){
                clearSelection();navigatePage(static_cast<Page>(std::clamp(int(page)+(b==Button::Right?1:-1),0,3)));
            }else if(b==Button::Start)playToggle(true);
            return;
        }
        if(int(b)<4&&held[std::size_t(Button::B)]){navigateWithB(b);return;}
        if(b==Button::A){
            followPlayback=false;
            if(!repeating){
                if(page==Page::Pattern&&field==0){insertHeldNote();auditionPatternNote();}
            }
        }
        if(int(b)<4){normalDirection(b);return;}
        if(b==Button::Start){
            if(repeating)return;
            if(page==Page::Instrument||page==Page::Sample)toggleEditorPreview();
            else playToggle();
            return;
        }
        // Empty notes appear on A-down. Other taps wait for release so
        // an A-first chord cannot accidentally activate a button.
    }
    void buttonUp(Button b) {
        held[std::size_t(b)]=false;
        if(b==Button::A){stopHeldAudition();aPreviewBlocked=false;if(pendingNoteBefore){commitPendingNote();aUsed=true;}}
        if(b==Button::B){modalBackHeld=false;channelModifier=false;}
        if(!held[std::size_t(Button::A)]&&!held[std::size_t(Button::B)])cutChordUsed=false;
        if(channelChord) {
            aUsed=bUsed=selectUsed=true;selectPending=false;
            // A held B can begin another chord once every secondary key is up.
            // If B is released first, keep consuming the remaining key releases.
            if(!held[std::size_t(Button::Select)]&&!held[std::size_t(Button::Start)]) {
                channelChord=0;
                for(int n=0;n<4;++n)held[std::size_t(n)]=false;
            }
            return;
        }
        if(b==Button::Select){
            const bool openActions=selectPending&&!selectUsed&&mode==Mode::Normal;
            if(selectUsed)for(int n=0;n<4;++n)held[std::size_t(n)]=false;
            selectPending=false;
            if(openActions){aUsed=true;if(hasSelection())cancelSelection();else openMenu(true);}
            return;
        }
        if(slotListRelease(b))return;
        if(analysisRelease(b)||extrasRelease(b)){if(b==Button::B)for(int n=0;n<4;++n)held[std::size_t(n)]=false;return;}
        if(b==Button::B&&toolRelease(b)){for(int n=0;n<4;++n)held[std::size_t(n)]=false;return;}
        if(b==Button::B){
            if(bUsed)for(int n=0;n<4;++n)held[std::size_t(n)]=false;
            if(mode==Mode::Normal&&!bUsed){
                playback->stopPreview();
                if(hasSelection())copyCurrent();
                else if(page==Page::Instrument&&instrumentMapView)toggleInstrumentMap();
                else resumePatternFollow();
            }
            return;
        }
        if(b!=Button::A)return;
        finishGesture();
        if(trimRelease(b)||themeRelease(b)||importAudioRelease(b)||toolRelease(b))return;
        if(mode==Mode::Menu){
            if(!aUsed&&!actions.at(menuIndex).adjust){aUsed=true;auto fn=actions.at(menuIndex).run;mode=Mode::Normal;if(fn)fn();}
            return;
        }
        if(mode==Mode::Envelope)return;
        if(mode!=Mode::Normal||aUsed)return;
        if(page==Page::Pattern){
            Cell v=cell();
            if(field==0){if(!v.note){v.note=lastCell.note>=1&&lastCell.note<=96?lastCell.note:49;v.instrument=std::uint8_t(instrument);editCell(v);}}
            else if(field==1){v.instrument=std::uint8_t(instrument);editCell(v);}
            else if(field==2){v.volume=lastCell.volume?lastCell.volume:0x50;editCell(v);}
            else{v.effect=lastCell.effect;v.param=lastCell.param;editCell(v);}
        }else if(page==Page::Song)activateSongField();
        else if(page==Page::Instrument){if(instrumentField>=8)activateInstrumentField();}
        else if(sampleField>=9)activateSampleField();
    }
    static const std::string& nameKeys() { static const std::string keys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- .!()+";return keys; }
    void cancelName() {mode=nameTarget==NameTarget::Theme?Mode::ThemeFiles:nameTarget==NameTarget::File?Mode::Browser:Mode::Normal;}

    void rect(int x,int y,int w,int h,SDL_Color c) {SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_Rect r{x,y,w,h};SDL_RenderFillRect(renderer,&r);}
    void outline(int x,int y,int w,int h,SDL_Color c) {SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_Rect r{x,y,w,h};SDL_RenderDrawRect(renderer,&r);}
    void line(int x1,int y1,int x2,int y2,SDL_Color c) {SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderDrawLine(renderer,x1,y1,x2,y2);}
    void txt(int x,int y,std::string_view s,SDL_Color c,int scale=2) {ui::text(renderer,x,y,s,c,scale);}
    void txt(int x,int y,std::string_view s) {txt(x,y,s,ink);}
    static std::vector<std::string> dialogLines(std::string_view value,int width) {
        if(value.find(" / ")!=std::string_view::npos) {
            std::vector<std::string> result;
            std::string current;
            for(std::size_t first=0;first<value.size();) {
                const auto separator=value.find(" / ",first);
                const auto end=separator==std::string_view::npos?value.size():separator;
                const auto part=value.substr(first,end-first);
                const auto next=current.empty()?std::string(part):current+" / "+std::string(part);
                if(ui::textWidth(next)<=width)current=next;
                else {
                    if(!current.empty())result.push_back(current);
                    auto wrapped=dialogLines(part,width);
                    current=wrapped.empty()?"":wrapped.back();
                    if(!wrapped.empty())wrapped.pop_back();
                    result.insert(result.end(),wrapped.begin(),wrapped.end());
                }
                first=separator==std::string_view::npos?value.size():separator+3;
            }
            if(!current.empty())result.push_back(current);
            return result;
        }
        std::istringstream words{std::string(value)};
        std::vector<std::string> result;
        std::string word,current;
        while(words>>word) {
            const auto next=current.empty()?word:current+" "+word;
            if(ui::textWidth(next)<=width){current=next;continue;}
            if(!current.empty()){result.push_back(current);current.clear();}
            // Long filenames have no spaces. Split at Unicode scalars rather
            // than letting a name run outside a confirmation dialog.
            for(const auto codepoint:text::decodeUtf8(word)) {
                std::string glyph;text::appendUtf8(glyph,codepoint);
                if(!current.empty()&&ui::textWidth(current+glyph)>width){result.push_back(current);current.clear();}
                current+=glyph;
            }
        }
        if(!current.empty())result.push_back(current);
        return result;
    }
    void drawDialogHints(int x,int y,int width,std::string_view value,SDL_Color color) {
        const auto rows=dialogLines(value,width);
        for(std::size_t n=0;n<rows.size();++n)txt(x,y+int(n)*22,rows[n],color);
    }
    void drawDialogHints(int x,int y,int width,std::string_view value) {drawDialogHints(x,y,width,value,cyan);}
    // All modal footers share an inset, baseline spacing and bottom padding.
    // Wrapped hints grow upward; details stay above the controls.
    static constexpr int dialogInset=24,dialogBottomPadding=20,dialogLineHeight=22,dialogGlyphHeight=14,dialogDetailGap=12,dialogContentGap=20;
    // Form fields and buttons share one height; dense data grids keep their row pitch.
    static constexpr int controlHeight=38,controlTextY=(controlHeight-dialogGlyphHeight)/2,focusBarWidth=3;
    static int dialogTextHeight(std::size_t lines) {
        return lines?dialogGlyphHeight+(int(lines)-1)*dialogLineHeight:0;
    }
    SDL_Rect dialogForContent(SDL_Rect bounds,int contentBottom,std::string_view hints,std::string_view detail={},int minHintLines=1) {
        const int width=bounds.w-2*dialogInset;
        const auto details=dialogLines(detail,width);
        const int controlLines=std::max(minHintLines,int(dialogLines(hints,width).size()));
        const int detailHeight=details.empty()?0:dialogTextHeight(details.size())+dialogDetailGap;
        bounds.h=contentBottom-bounds.y+dialogContentGap+detailHeight+
            dialogTextHeight(std::size_t(controlLines))+dialogBottomPadding;
        return bounds;
    }
    void drawDialogFooter(SDL_Rect bounds,std::string_view hints,std::string_view detail={}) {
        const int width=bounds.w-2*dialogInset,x=bounds.x+dialogInset;
        const auto controls=dialogLines(hints,width),details=dialogLines(detail,width);
        const int controlsY=bounds.y+bounds.h-dialogBottomPadding-dialogTextHeight(controls.size());
        const int detailsY=controlsY-dialogDetailGap-dialogTextHeight(details.size());
        for(std::size_t n=0;n<details.size();++n)txt(x,detailsY+int(n)*dialogLineHeight,details[n],muted);
        for(std::size_t n=0;n<controls.size();++n)txt(x,controlsY+int(n)*dialogLineHeight,controls[n],cyan);
    }
    void labelValue(int x,int y,std::string label,std::string value,bool selected,int width=390) {
        rect(x,y,width,controlHeight,selected?panel2:panel);
        if(selected){rect(x,y,focusBarWidth,controlHeight,amber);}
        txt(x+14,y+controlTextY,label,selected?ink:muted);
        txt(x+width-ui::textWidth(value)-14,y+controlTextY,value,selected?amber:cyan);
    }
    void drawButton(int x,int y,int width,std::string_view label,bool selected) {
        rect(x,y,width,controlHeight,selected?panel2:bg);
        outline(x,y,width,controlHeight,selected?amber:dim);
        txt(x+(width-ui::textWidth(label))/2,y+controlTextY,label,selected?amber:cyan);
    }
#include "ui/TrackerViews.inc"

    void rangeBoundary(int x,int top,int bottom,bool focused,bool selected) {
        const auto color=focused?(selected?amber:ink):dim;
        line(x,top,x,bottom,color);
        if(focused)rect(x-2,top,5,5,color);
    }
    void envelope(int x,int y,int width,int height,const Envelope& e,int playbackTick=-1,int loopField=-1) {
        rect(x,y,width,height,bg);line(x,y+height-1,x+width,y+height-1,dim);
        if(!e.count){txt(x+12,y+height/2-7,"NO ENVELOPE",dim);return;}
        const int maxX=std::max(1,int(e.points[e.count-1].x));
        const bool loopFocus=loopField>=6&&loopField<=8;
        if(((e.flags&4)||loopFocus)&&e.loopStart<e.count&&e.loopEnd<e.count&&e.loopStart<=e.loopEnd) {
            const std::array<int,2> points{{e.loopStart,e.loopEnd}};
            for(int i=0;i<2;++i) {
                // A selected endpoint stays visible even when both points have
                // the same tick, or map to the same pixel in a small preview.
                const int n=loopField==7?1-i:i;
                const int px=x+int(e.points[std::size_t(points[n])].x)*(width-8)/maxX;
                rangeBoundary(px,y,y+height-1,loopFocus,loopField==7+n);
            }
        }
        for(int i=0;i<e.count;++i){int px=x+int(e.points[i].x)*(width-8)/maxX,py=y+height-6-int(e.points[i].y)*(height-10)/64;
            if(i){int ax=x+int(e.points[i-1].x)*(width-8)/maxX,ay=y+height-6-int(e.points[i-1].y)*(height-10)/64;line(ax,ay,px,py,cyan);}
            rect(px-2,py-2,5,5,i==e.sustain?amber:cyan);
        }
        if((e.flags&1)&&playbackTick>=0) {
            const int px=x+std::clamp(playbackTick,0,int(e.points[e.count-1].x))*(width-8)/maxX;
            line(px,y,px,y+height-1,ink);rect(px-2,y+height-5,5,5,ink);
        }
    }
    void drawSample() {
        auto* s=currentSample();
        rect(20,52,732,controlHeight,sampleField==9?panel2:bg);
        if(sampleField==9)rect(20,52,focusBarWidth,controlHeight,amber);
        txt(34,52+controlTextY,"NAME",sampleField==9?amber:muted);
        txt(112,52+controlTextY,s?ui::fit(text::xmDisplayText(s->name),52):"EMPTY SAMPLE",sampleField==9?amber:ink);
        drawButton(760,52,118,"LOAD",sampleField==10);
        drawButton(886,52,118,"GENERATE",sampleField==11);
        Sample empty;if(!s)s=&empty;
        const int x=30,y=103,ww=964,hh=284;
        const int rangeTop=96,rangeHeight=302,rangeBottom=rangeTop+rangeHeight-1;
        rect(20,rangeTop,984,rangeHeight,panel);
        line(x,y+hh/2,x+ww,y+hh/2,dim);
        if(!s->pcm.empty()){
            const std::size_t firstFrame=waveformZoom&&selectionEnd>selectionStart?std::size_t(selectionStart):0;
            const std::size_t lastFrame=waveformZoom&&selectionEnd>selectionStart?std::size_t(selectionEnd):s->pcm.size();
            const auto visibleFrames=lastFrame-firstFrame;
            auto frameX=[&](std::size_t f){const auto clipped=std::clamp(f,firstFrame,lastFrame);return x+int((clipped-firstFrame)*std::size_t(ww)/visibleFrames);};
            const bool loopFocus=sampleField>=4&&sampleField<=6;
            const bool selectionFocus=sampleField==7||sampleField==8||sampleField==12||sampleField==14;
            const bool showLoop=s->loopLength&&(s->loopType||loopFocus);
            const auto preview=playback->samplePreviewStatus();
            const bool showPreview=preview.active&&preview.instrument==instrument&&preview.sample==sample&&
                preview.frame>=firstFrame&&preview.frame<lastFrame;
            if(selectionEnd>selectionStart) {
                int left=frameX(selectionStart),right=std::max(left+1,frameX(selectionEnd));
                auto visibleMarker=[&](std::size_t frame,bool shown){
                    return shown&&frame>=firstFrame&&frame<=lastFrame?frameX(frame):-1;
                };
                const std::array<int,5> bars{{
                    visibleMarker(s->loopStart,showLoop),
                    visibleMarker(std::size_t(s->loopStart)+s->loopLength,showLoop),
                    visibleMarker(selectionStart,selectionFocus),visibleMarker(selectionEnd,selectionFocus),
                    visibleMarker(preview.frame,showPreview)}};
                auto hasBackgroundColumn=[&]{
                    if(right-left>int(bars.size()))return true;
                    for(int px=left;px<right;++px)
                        if(std::find(bars.begin(),bars.end(),px)==bars.end())return true;
                    return false;
                };
                // Subpixel selections must retain one background column even
                // under coincident bars. Expand only the display, within the
                // plot; the selected PCM frames and boundary positions stay put.
                while(!hasBackgroundColumn()) {
                    if(right<x+ww)++right;
                    else if(left>x)--left;
                    else break;
                }
                rect(left,rangeTop,right-left,rangeHeight,sampleSelectionColor);
            }
            auto drawWaveform=[&](SDL_Color color){
                if(visibleFrames<=std::size_t(ww)) {
                    // At this zoom, connect actual PCM points on the same time axis
                    // as selection/loop boundaries; isolated min/max pixels look dashed.
                    const auto frameY=[&](std::size_t frame){return y+hh/2-int(s->pcm[frame])*(hh/2-10)/32768;};
                    for(std::size_t frame=firstFrame;frame+1<lastFrame;++frame)
                        line(frameX(frame),frameY(frame),frameX(frame+1),frameY(frame+1),color);
                    // Hold the final sample to the end boundary, also covering a
                    // one-frame view. Do not invent a transition across the loop.
                    line(frameX(lastFrame-1),frameY(lastFrame-1),frameX(lastFrame),frameY(lastFrame-1),color);
                } else {
                    if(waveformRevision!=documentRevision||waveformInstrument!=instrument||waveformSample!=sample||waveformFirst!=firstFrame||waveformLast!=lastFrame){
                        for(int px=0;px<ww;++px){const auto a=firstFrame+std::size_t(px)*visibleFrames/std::size_t(ww);const auto b=std::min(lastFrame,std::max(a+1,firstFrame+std::size_t(px+1)*visibleFrames/std::size_t(ww)));const auto mm=std::minmax_element(s->pcm.begin()+a,s->pcm.begin()+b);waveform[std::size_t(px)]={*mm.first,*mm.second};}
                        waveformRevision=documentRevision;waveformInstrument=instrument;waveformSample=sample;waveformFirst=firstFrame;waveformLast=lastFrame;
                    }
                    for(int px=0;px<ww;++px){const auto mm=waveform[std::size_t(px)];
                        line(x+px,y+hh/2-int(mm.second)*(hh/2-10)/32768,x+px,y+hh/2-int(mm.first)*(hh/2-10)/32768,color);
                    }
                }
            };
            drawWaveform(cyan);
            if(selectionEnd>selectionStart) {
                // Recolor only the selected time span, preserving the same PCM
                // curve and using the theme's text color without blending.
                const int left=frameX(selectionStart),right=std::max(left+1,frameX(selectionEnd));
                SDL_Rect selectedClip{left,rangeTop,right-left,rangeHeight},previousClip{};
                const bool hadClip=SDL_RenderIsClipEnabled(renderer)==SDL_TRUE;
                SDL_RenderGetClipRect(renderer,&previousClip);
                if(!hadClip||SDL_IntersectRect(&selectedClip,&previousClip,&selectedClip)) {
                    SDL_RenderSetClipRect(renderer,&selectedClip);
                    drawWaveform(ink);
                }
                SDL_RenderSetClipRect(renderer,hadClip?&previousClip:nullptr);
            }
            auto markers=[&](std::size_t first,std::size_t last,bool focused,int firstField,int lastField){
                const std::array<std::size_t,2> frames{{first,last}};
                const std::array<int,2> fields{{firstField,lastField}};
                for(int i=0;i<2;++i){
                    const int n=focused&&sampleField==firstField?1-i:i;
                    if(frames[n]>=firstFrame&&frames[n]<=lastFrame)
                        rangeBoundary(frameX(frames[n]),rangeTop,rangeBottom,focused,sampleField==fields[n]);
                }
            };
            auto loopMarkers=[&]{if(showLoop)markers(s->loopStart,std::uint64_t(s->loopStart)+s->loopLength,loopFocus,5,6);};
            auto selectionMarkers=[&]{
                if(!selectionFocus)return;
                if(selectionEnd>selectionStart)markers(selectionStart,selectionEnd,true,7,8);
                else rangeBoundary(frameX(selectionStart),rangeTop,rangeBottom,true,true);
            };
            // Keep the range currently being edited visible when boundaries overlap.
            if(loopFocus){selectionMarkers();loopMarkers();}else{loopMarkers();selectionMarkers();}
            if(showPreview) {
                const int cursorX=frameX(preview.frame);
                line(cursorX,rangeTop,cursorX,rangeBottom,ink);
                rect(cursorX-2,rangeBottom-4,5,5,ink);
            }
        }
        txt(30,410,number(int(s->pcm.size()))+" FRAMES  /  "+(s->sixteenBit?"16-BIT":"8-BIT")+" MONO",muted);
        txt(800,410,waveformZoom&&selectionEnd>selectionStart?"SELECTION":"FULL SAMPLE",muted);
        const char* loopNames[]={"OFF","FORWARD","PING-PONG"};
        labelValue(20,440,"VOLUME",hex(s->volume),sampleField==0,487);
        labelValue(517,440,"PANNING",hex(s->panning),sampleField==1,487);
        labelValue(20,494,"FINETUNE",number(s->finetune),sampleField==2,487);
        labelValue(517,494,"RELATIVE NOTE",number(s->relativeNote),sampleField==3,487);
        for(int side=0;side<2;++side){
            const int left=20+side*497;
            rect(left,548,487,146,panel);outline(left,548,487,146,panel2);
            txt(left+14,560,side?"SELECTION":"LOOP",cyan);
        }
        auto rangeField=[&](int left,int top,int width,const char* label,std::string value,int fieldId){
            const bool active=sampleField==fieldId;
            rect(left,top,width,controlHeight,active?panel2:bg);
            if(active)rect(left,top,focusBarWidth,controlHeight,amber);
            txt(left+14,top+controlTextY,label,active?ink:muted);
            txt(left+width-14-ui::textWidth(value),top+controlTextY,value,active?amber:cyan);
        };
        rangeField(32,590,225,"START",number(s->loopStart),5);
        rangeField(269,590,226,"END",number(std::uint64_t(s->loopStart)+s->loopLength),6);
        rangeField(32,644,463,"TYPE",loopNames[std::min<int>(2,s->loopType)],4);
        rangeField(529,590,225,"START",number(selectionStart),7);
        rangeField(766,590,226,"END",number(selectionEnd),8);
        drawButton(529,644,225,"DRAW",sampleField==12);
        drawButton(766,644,226,"PROCESS",sampleField==14);
    }
    void overlay() {SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);rect(0,0,W,H,overlayColor);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);}
    void drawMenu() {
        const auto hint=actions[menuIndex].adjust?"A+DPAD EDIT / B BACK":"A SELECT / B BACK";
        const auto detail=ui::fit(actions[menuIndex].description,59);
        const int visibleRows=std::min(13,int(actions.size()));
        // Keep the footer stationary when an obvious action needs no explanation.
        auto layoutDetail=detail;
        if(layoutDetail.empty()) {
            const auto explained=std::find_if(actions.begin(),actions.end(),[](const Action& action){return !action.description.empty();});
            if(explained!=actions.end())layoutDetail=ui::fit(explained->description,59);
        }
        const auto bounds=dialogForContent({129,77,766,0},177+(visibleRows-1)*34,hint,layoutDetail);
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);outline(bounds.x,bounds.y,bounds.w,bounds.h,dim);txt(154,102,menuTitle,cyan,3);
        txt(709,112,number(menuIndex+1)+" / "+number(int(actions.size())),muted);
        int first=std::clamp(menuIndex-6,0,std::max(0,int(actions.size())-13));
        for(int n=0;n<13&&first+n<int(actions.size());++n){int idx=first+n,y=153+n*34;bool active=idx==menuIndex;
            if(active){rect(146,y-7,732,31,panel2);rect(146,y-7,focusBarWidth,31,amber);}
            txt(163,y,actions[idx].label,active?amber:ink);
        }
        drawDialogFooter(bounds,hint,detail);
    }
    void drawBrowser() {
        overlay();rect(40,48,944,666,panel);outline(40,48,944,666,dim);txt(64,74,browserTitle(),cyan,3);
        txt(64,119,ui::fit(browserDirectory.u8string(),75),muted);
        if(entries.empty())txt(64,180,browserFolderPicker>=0?"NO SUBFOLDERS / START TO USE THIS FOLDER":"NO MATCHING FILES IN THIS FOLDER",muted);
        int first=std::clamp(browserIndex-6,0,std::max(0,int(entries.size())-browserPageSize));
        for(int n=0;n<browserPageSize&&first+n<int(entries.size());++n){int idx=first+n,y=163+n*31;
            if(idx==browserIndex){rect(56,y-6,910,29,panel2);rect(56,y-6,focusBarWidth,29,amber);}
            txt(69,y,entries[idx].directory?">":" ",entries[idx].directory?cyan:muted);
            txt(96,y,ui::fit(entries[idx].name,68),idx==browserIndex?amber:ink);
        }
        drawDialogFooter({40,48,944,666},browserFolderPicker>=0?"LEFT/RIGHT PAGE / A OPEN / START USE FOLDER / B CANCEL":
            saving()?"LEFT/RIGHT PAGE / A OPEN / START SAVE / B CANCEL":
            "LEFT/RIGHT PAGE / A OPEN / B CANCEL",number(int(entries.size()))+" ENTRIES");
    }
    void drawName() {
        const char* hint="A TYPE / START SAVE / B CANCEL";
        constexpr int keysY=278,keyPitch=50;
        const auto bounds=dialogForContent({86,121,852,0},keysY+3*keyPitch+controlHeight,hint);
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);outline(bounds.x,bounds.y,bounds.w,bounds.h,dim);txt(112,147,nameTarget==NameTarget::Theme?"THEME FILENAME":nameTarget==NameTarget::File?"FILENAME":nameTarget==NameTarget::Song?"SONG TITLE":nameTarget==NameTarget::Instrument?"INSTRUMENT NAME":"SAMPLE NAME",cyan,3);
        const auto visibleName=nameTarget==NameTarget::File?filename:text::xmDisplayText(filename);
        rect(110,194,804,controlHeight,bg);txt(124,194+controlTextY,ui::fit(visibleName+"_"+(nameTarget==NameTarget::Theme?".theme":nameTarget==NameTarget::File?extension():""),63),amber);
        txt(112,244,nameTarget==NameTarget::Theme?"SAVED IN THEMES / EXTENSION ADDED AUTOMATICALLY.":nameTarget==NameTarget::File?"EXTENSION IS ADDED AUTOMATICALLY.":"ASCII INPUT / UNICODE DISPLAY.",muted);
        const auto& keys=nameKeys();
        for(int n=0;n<48;++n){int x=113+(n%12)*66,y=keysY+(n/12)*keyPitch;bool selected=n==nameCursor;
            std::string label=n<44?std::string(1,keys[n]):n==44?"DEL":n==45?"CLR":n==46?"OK":"BACK";
            drawButton(x,y,59,label,selected);
        }
        drawDialogFooter(bounds,hint);
    }
    void drawConfirm() {
        const auto detailLines=dialogLines(confirmDetail,758);
        constexpr int minimumBodyLines=3,maximumBodyLines=12;
        const int bodyLines=std::clamp(int(detailLines.size()),minimumBodyLines,maximumBodyLines);
        const int height=76+dialogTextHeight(std::size_t(bodyLines))+dialogContentGap+
            dialogGlyphHeight+dialogBottomPadding;
        const SDL_Rect bounds{107,(H-height)/2,810,height};
        const std::string controls="A CONFIRM / B CANCEL";
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);
        outline(bounds.x,bounds.y,bounds.w,bounds.h,amber);
        txt(bounds.x+26,bounds.y+25,ui::fit(confirmTitle,40),amber,3);
        for(std::size_t n=0;n<detailLines.size();++n)
            txt(bounds.x+26,bounds.y+76+int(n)*dialogLineHeight,detailLines[n],ink);
        drawDialogFooter(bounds,controls);
    }
    void drawHelp() {
        const char* names[]={"SONG","PATTERN","INSTRUMENT","SAMPLE"};
        std::vector<std::string> rows={
            "DPAD                  MOVE / EXTEND SELECTION",
            "A / A+DPAD            ENTER / EDIT VALUE",
            "A+B                   CUT / RESET",
            "SELECT+LEFT/RIGHT     CHANGE SCREEN",
            "TAP SELECT            CANCEL SELECTION / ACTIONS",
            "HOLD SELECT, TAP B    SELECT / EXPAND / CANCEL",
            "B ON SELECTION        COPY; SELECT+A: PASTE",
            "START                 PLAY / STOP",
            "SELECT+START          SONG FROM ORDER 00 / STOP"
        };
        if(page==Page::Pattern) {
            rows[1]="HOLD A / RELEASE      LISTEN / STOP; A+DPAD: EDIT";
            rows[7]="START                 PLAY / STOP; SELECTION: LOOP";
            rows.push_back("SINGLE NOTE: C-0 TO OFF: A+LEFT/DOWN; BACK: A+UP/RIGHT");
            rows.push_back("TAP B, NO SELECTION   RESUME FOLLOW WHILE PLAYING");
            rows.push_back("B+UP/DOWN             PAGE, THEN PREVIOUS/NEXT ORDER");
            rows.push_back("LEFT/RIGHT: FIELD; B+LEFT/RIGHT: PAGE / KEEP FOLLOW");
            rows.push_back("VOLUME COMMAND: A+LEFT/RIGHT VALUE / A+UP/DOWN TYPE");
        } else if(page==Page::Song) {
            rows[3]="SELECT+RIGHT          OPEN CURRENT ORDER PATTERN";
            rows.push_back("ORDERS: B+UP/DOWN     PREVIOUS/NEXT PAGE");
            rows.push_back("A+DPAD ON PATTERN     CHANGE ORDER PATTERN");
            rows.push_back("A+DPAD ON ROWS        CHANGE PATTERN LENGTH");
            rows.push_back("DOWN BELOW BUTTONS    ENTER CHANNEL MONITOR");
            rows.push_back("MONITOR: DPAD         CHANNEL; UP AT TOP: RETURN");
        } else if(page==Page::Instrument) {
            rows[7]="START                 LISTEN / STOP";
            if(instrumentMapView) {
                rows[5]="HOLD SELECT, TAP B    RANGE / PAGE / ALL / CANCEL";
                rows[6]="B ON SELECTION        COPY MAP; SELECT+A: PASTE";
                rows.push_back("KEY MAP: B+UP/DOWN    MOVE 24 NOTES / EXTEND SELECTION");
                rows.push_back("TAP B, NO SELECTION   RETURN TO INSTRUMENT SETTINGS");
                rows.push_back("KEY MAP: A+DPAD       EDIT CURRENT / SELECTED MAPPINGS");
                rows.push_back("KEY MAP: A+B          CUT MAPPINGS / RESET TO SAMPLE 00");
            } else {
                rows[5]="ACTIONS > INSTRUMENT MANAGEMENT";
                rows[6]="COPY / PASTE INSTRUMENT (WHOLE SLOT)";
                rows.push_back("B+UP/DOWN             INSTRUMENT NUMBER");
            }
            rows.push_back("B+LEFT/RIGHT          SAMPLE SLOT");
            rows.push_back("A ON NAME / LOAD      RENAME / IMPORT XI");
            rows.push_back("A ON ENVELOPE         OPEN ENVELOPE EDITOR");
            if(!instrumentMapView)rows.push_back("A ON KEY MAP          EDIT NOTE-TO-SAMPLE ASSIGNMENTS");
            rows.push_back("SAMPLE LIST: A OPEN / A+B DELETE SLOT");
        } else {
            rows[7]="START                 LISTEN TO SAMPLE / SELECTION; STOP";
            rows[2]="A+B                   ZERO CURRENT VALUE / ENDPOINT";
            rows[6]="B ON SELECTION        COPY PCM; PROCESS: PASTE PCM";
            rows.push_back("B+UP/DOWN             SAMPLE SLOT");
            rows.push_back("B+LEFT/RIGHT          OWNER INSTRUMENT");
            rows[5]="HOLD SELECT, TAP B    SELECT / EXPAND / CANCEL PCM";
            rows.push_back("A ON PROCESS          PROCESS SELECTED PCM");
            rows.push_back("A ON DRAW             OPEN DRAW");
            rows.push_back("PCM RANGE: DPAD FOCUS / A+DPAD EDIT START OR END");
            rows.push_back("A+LEFT/RIGHT: FINE / A+UP/DOWN: COARSE / HOLD: FASTER");
            rows.push_back("PROCESS > CUT PCM    CUT THE SELECTED AUDIO");
        }
        if(page==Page::Pattern||page==Page::Song) {
            rows.push_back("HOLD B, TAP SELECT    MUTE / UNMUTE CHANNEL");
            rows.push_back("HOLD B, TAP START     SOLO / UNSOLO CHANNEL");
            if(page==Page::Song)rows.push_back("MUTE/SOLO: CHANNEL MONITOR ONLY");
            rows.push_back("SAME SOLO AGAIN       UNMUTE ALL CHANNELS");
        }
        const char* hint="B OR SELECT CLOSE";
        const auto bounds=dialogForContent({75,70,874,0},156+(int(rows.size())-1)*27+dialogGlyphHeight,hint);
        overlay();rect(bounds.x,bounds.y,bounds.w,bounds.h,panel);txt(101,101,std::string(names[int(page)])+" CONTROLS",cyan,3);
        for(std::size_t n=0;n<rows.size();++n)txt(103,156+int(n)*27,rows[n],muted);
        drawDialogFooter(bounds,hint);
    }
    void draw() {
        transport=playback->status();
        syncPlaybackView();
        rect(0,0,W,H,bg);drawHeader();
        switch(page){case Page::Song:drawSong();break;case Page::Pattern:drawPattern();break;case Page::Instrument:drawInstrument();break;case Page::Sample:drawSample();break;}
        drawFooter();
        const auto appliedColors=palette;
        if(themeEditing)palette=defaultPalette;
        switch(mode){case Mode::Menu:drawMenu();break;case Mode::Browser:drawBrowser();break;case Mode::Name:drawName();break;case Mode::Confirm:drawConfirm();break;case Mode::Help:drawHelp();break;case Mode::SlotList:drawSlotList();break;default:break;}
        drawToolModes();
        drawExtrasModes();
        drawAnalysisModes();
        if(mode==Mode::ImportOptions)drawAudioImport();
        if(mode==Mode::TrimSettings)drawTrim();
        if(mode==Mode::ThemeColors)drawThemeColors();
        if(mode==Mode::ThemeFiles)drawThemeFiles();
        drawNotice();
        palette=appliedColors;
    }
    void screenshot(const std::filesystem::path& path) {
        int ww,hh;SDL_GetRendererOutputSize(renderer,&ww,&hh);
        SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,ww,hh,32,SDL_PIXELFORMAT_ARGB8888);
        if(!image)throw std::runtime_error(SDL_GetError());
        if(SDL_RenderReadPixels(renderer,nullptr,image->format->format,image->pixels,image->pitch)!=0){SDL_FreeSurface(image);throw std::runtime_error(SDL_GetError());}
        const int result=SDL_SaveBMP(image,path.u8string().c_str());SDL_FreeSurface(image);
        if(result!=0)throw std::runtime_error(SDL_GetError());
    }
    void event(const SDL_Event& e) {
        // Repeated window-close requests must not replace a pending discard or
        // recording confirmation and release the only copy of captured PCM.
        if(e.type==SDL_QUIT&&mode==Mode::Confirm)return;
        if(e.type==SDL_QUIT&&mode==Mode::Busy){cancelWorker=true;return;}
        if(e.type==SDL_QUIT&&mode==Mode::Recording){
            if(captureStarted){pendingRecording=capture.finish();captureStarted=false;captureStatus.recording=false;}
            if(pendingRecording){confirm("DISCARD RECORDING AND EXIT?","B RETURNS TO THE RECORDING SO YOU CAN KEEP IT.",[this]{pendingRecording.reset();requestExit();});confirmCancelMode=Mode::Recording;}
            else requestExit();
            return;
        }
        if(e.type==SDL_QUIT){requestExit();return;}
        if(e.type==SDL_CONTROLLERDEVICEADDED&&!gamepad&&SDL_IsGameController(e.cdevice.which)){gamepad=SDL_GameControllerOpen(e.cdevice.which);return;}
        if(e.type==SDL_CONTROLLERDEVICEREMOVED&&gamepad){
            if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad))==e.cdevice.which){
                cancelPendingNote();SDL_GameControllerClose(gamepad);gamepad=nullptr;held.fill(false);
                aPreviewActive=false;aPreviewBlocked=false;selectPending=false;aUsed=bUsed=true;
                cutChordUsed=false;channelModifier=false;channelChord=0;modalBackHeld=false;
                finishGesture();stopWavePreview();notify("CONTROLLER DISCONNECTED");
            }return;
        }
        if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){cancelPendingNote();held.fill(false);aPreviewActive=false;aPreviewBlocked=false;selectPending=false;aUsed=bUsed=true;cutChordUsed=false;channelModifier=false;channelChord=0;finishGesture();stopWavePreview();return;}
        if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP){if(auto b=controller(e.cbutton.button)){if(e.type==SDL_CONTROLLERBUTTONDOWN)buttonDown(*b);else buttonUp(*b);}return;}
    }
    int runUiTest() {
        // Existing flow fixtures deliberately share the isolated QA folder.
        // Dedicated browser checks below exercise the portable category defaults.
        browserDirectory=browserDefaultFolder(0);
        int assertions=0;
        auto require=[&](bool condition,const char* what){
            ++assertions;
            if(!condition)throw std::runtime_error(std::string("UI TEST: ")+what);
        };
        constexpr std::array<Uint8,8> controllerButtons{{
            SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_BACK}};
        auto buttonInput=[&](Button code,bool down){
            SDL_Event input{};input.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;
            input.cbutton.button=controllerButtons.at(std::size_t(code));
            input.cbutton.state=down?SDL_PRESSED:SDL_RELEASED;
            if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
            SDL_Event queued;while(SDL_PollEvent(&queued))event(queued);
        };
        auto tap=[&](Button code){buttonInput(code,true);buttonInput(code,false);};
        auto chord=[&](std::initializer_list<Button> directions){buttonInput(Button::A,true);for(auto code:directions)tap(code);buttonInput(Button::A,false);};
        auto selectChord=[&](Button code){buttonInput(Button::Select,true);tap(code);buttonInput(Button::Select,false);};
        auto optionChord=[&](Button code){buttonInput(Button::B,true);tap(code);buttonInput(Button::B,false);};
        auto menu=[&](const std::string& label){
            try {
            // Older scenario names exercise the same individual operations
            // through the unified cleanup dialog, without shipping aliases.
            const int cleanupOption=label=="CLEAN UP PATTERNS"?0:label=="CLEAN UP INSTRUMENTS"?1:label=="MINIMISE ALL INSTRUMENTS"?4:-1;
            const std::string actionLabel=cleanupOption>=0?"CLEAN UP SONG":label;
            if(mode==Mode::TrimSettings){tap(Button::B);while(mode==Mode::Menu)tap(Button::B);}
            // Follow the real context menus. Basic actions now live directly
            // on their screens or use the four edit buttons.
            if(label=="SONG"){navigatePage(Page::Song);return;}
            if(label=="PATTERN"){navigatePage(Page::Pattern);return;}
            if(label=="INSTRUMENT"){navigatePage(Page::Instrument);return;}
            if(label=="SAMPLE"){navigatePage(Page::Sample);return;}
            if(label=="SAVE XM"||label=="SAVE XM AS"||label=="OPEN XM"||label=="NEW SONG"||label=="RENAME SONG"){
                changePage(Page::Song);songField=label=="SAVE XM"?7:label=="SAVE XM AS"?8:label=="OPEN XM"?9:label=="NEW SONG"?10:6;tap(Button::A);return;
            }
            if(label=="RENAME INSTRUMENT"||label=="IMPORT XI"||label=="VOLUME ENVELOPE"||label=="PANNING ENVELOPE"){
                changePage(Page::Instrument);instrumentField=label=="RENAME INSTRUMENT"?8:label=="IMPORT XI"?9:label=="VOLUME ENVELOPE"?10:11;tap(Button::A);return;
            }
            if(label=="IMPORT AUDIO"||label=="DRAW WAVEFORM"||label=="RENAME SAMPLE"){
                changePage(Page::Sample);sampleField=label=="IMPORT AUDIO"?10:label=="DRAW WAVEFORM"?12:9;tap(Button::A);return;
            }
            if(label=="COPY PCM"){changePage(Page::Sample);selectChord(Button::B);tap(Button::B);return;}
            if(label=="PASTE PCM")changePage(Page::Sample);
            if(label=="CANCEL BLOCK SELECTION"){while(blockSelected)selectChord(Button::B);return;}
            if(label=="RENDER WAV"||actionLabel=="CLEAN UP SONG"||label=="INSERT ORDER"||label=="DUPLICATE PATTERN"||label=="NEW EMPTY PATTERN"||label=="MOVE ORDER UP"||label=="MOVE ORDER DOWN")changePage(Page::Song);
            if(label=="SWAP INSTRUMENT"||label=="MINIMISE INSTRUMENT")changePage(Page::Instrument);
            if(label=="MINIMISE SAMPLE")changePage(Page::Sample);
            if(page==Page::Sample&&hasSelection()) {
                pcmSelecting=false;sampleField=14;tap(Button::A);
            } else {
                if(hasSelection())tap(Button::Select);
                tap(Button::Select);
            }
            require(mode==Mode::Menu,"context actions are reachable with the current selection state");
            // Export choices validate the current sample on entry. Open them
            // only when EXPORT SAMPLE WAV itself is requested, never while
            // searching for an unrelated instrument action on an empty slot.
            const std::vector<std::string> groups={"ORDER MANAGEMENT","PROJECT","PATTERN TOOLS","ROWS","TRANSPOSE","INSTRUMENT MANAGEMENT","KEY MAP RANGE","SAMPLE MANAGEMENT","PCM PROCESSING","AMPLITUDE","ADVANCED PCM"};
            std::function<bool()> findAction=[&]{
                const auto item=std::find_if(actions.begin(),actions.end(),[&](const Action& a){return a.label==actionLabel;});
                if(item!=actions.end()){
                    const int target=int(std::distance(actions.begin(),item));
                    while(menuIndex<target)tap(Button::Down);
                    while(menuIndex>target)tap(Button::Up);
                    tap(Button::A);return true;
                }
                const auto candidates=actions;
                for(const auto& candidate:candidates){
                    if(std::find(groups.begin(),groups.end(),candidate.label)==groups.end())continue;
                    const auto group=std::find_if(actions.begin(),actions.end(),[&](const Action& a){return a.label==candidate.label;});
                    const int target=int(std::distance(actions.begin(),group));
                    while(menuIndex<target)tap(Button::Down);
                    while(menuIndex>target)tap(Button::Up);
                    tap(Button::A);
                    if(findAction())return true;
                    tap(Button::B);require(mode==Mode::Menu,"submenu B returns one level");
                }
                return false;
            };
            bool found=findAction();
            if(!found&&menuTitle=="SAMPLE PROCESS") {
                tap(Button::B);
                if(hasSelection())tap(Button::Select);
                tap(Button::Select);found=findAction();
            }
            require(found,"requested context action exists");
            if(cleanupOption>=0){
                require(mode==Mode::TrimSettings,"individual cleanup enters the unified options");
                for(int n=0;n<6;++n){
                    while(trimField<n)tap(Button::Down);
                    while(trimField>n)tap(Button::Up);
                    chord({n==cleanupOption?Button::Right:Button::Left});
                }
                tap(Button::Down);tap(Button::A);
                if(mode==Mode::TrimSettings){tap(Button::B);while(mode==Mode::Menu)tap(Button::B);}
            }
            } catch(const std::exception& error) {
                throw std::runtime_error("UI ACTION "+label+" / INSTRUMENT "+number(instrument)+
                    " / SAMPLE "+number(sample)+": "+error.what());
            }
        };
        auto same=[](const Cell& a,const Cell& b){return a.note==b.note&&a.instrument==b.instrument&&a.volume==b.volume&&a.effect==b.effect&&a.param==b.param;};
        doc=makeDemoDocument();page=Page::Pattern;mode=Mode::Normal;
        order=row=channel=field=0;instrument=1;sample=0;solo=-1;dirty=false;
        normalize();loadEngine();
        const int oldNote=cell().note?cell().note:49;
        chord({Button::Right,Button::Right});
        require(cell().note==std::min(96,oldNote+2),"A+direction edits note");
        tap(Button::Right);tap(Button::Right);
        require(field==2,"volume column navigation");
        chord({Button::Up,Button::Right});
        require(cell().volume==0x50,"volume column edit clamps at maximum volume before the reserved XM range");
        const auto volume=cell().volume;
        tap(Button::Right);chord({Button::Right,Button::Right,Button::Right,Button::Right});
        tap(Button::Right);chord({Button::Up,Button::Right,Button::Right,Button::Right});
        require(cell().effect!=0&&cell().param!=0&&cell().volume==volume,"volume and effect coexist");
        const Cell edited=cell();
        SDL_Event pad{};pad.type=SDL_CONTROLLERBUTTONDOWN;pad.cbutton.button=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;event(pad);
        pad.type=SDL_CONTROLLERBUTTONUP;event(pad);require(channel==0,"unused shoulder button does nothing");
        selectChord(Button::B);for(int n=0;n<4;++n)tap(Button::Left);
        tap(Button::B);tap(Button::Down);const Cell beforePaste=cell();selectChord(Button::A);
        require(same(cell(),edited),"copy and paste preserve both columns");
        menu("UNDO");require(same(cell(),beforePaste),"undo restores cell");
        menu("REDO");require(same(cell(),edited),"redo restores pasted cell");
        buttonInput(Button::B,true);tap(Button::Start);buttonInput(Button::B,false);require(solo==0,"solo shortcut");
        buttonInput(Button::B,true);tap(Button::Start);buttonInput(Button::B,false);require(solo==-1,"unsolo shortcut");
        field=0;patternRowAudition=true;buttonInput(Button::A,true);
        std::vector<std::int16_t> pcm(4096*2);playback->readFrames(pcm.data(),4096);
        require(std::any_of(pcm.begin(),pcm.end(),[](std::int16_t s){return s!=0;}),"held row audition produces PCM");
        buttonInput(Button::A,false);patternRowAudition=false;
        menu("SAVE XM AS");require(mode==Mode::Browser,"save browser opened");
        tap(Button::Start);require(mode==Mode::Name,"gamepad filename editor opened");
        // Clear the suggested name using the grid, then type UI using its keys.
        for(int n=0;n<3;++n)tap(Button::Down);
        for(int n=0;n<9;++n)tap(Button::Right);
        require(nameCursor==45,"filename clear key reached");tap(Button::A);
        tap(Button::Left);tap(Button::Up);tap(Button::Up);
        for(int n=0;n<8;++n)tap(Button::Right);
        tap(Button::A); // U; each visited name-keypad row restores its own column.
        tap(Button::Up);for(int n=0;n<8;++n)tap(Button::Right);tap(Button::A); // I
        require(filename=="UI","gamepad filename text entry");
        tap(Button::Start);if(mode==Mode::Confirm)tap(Button::A);
        require(mode==Mode::Normal&&!dirty,"Save As commits and clears dirty flag");
        const auto saved=io::readFile(browserDefaultFolder(0)/"UI.xm");
        const auto reloaded=io::decodeXm(saved);
        require(same(reloaded.patterns.at(0).at(1,0,reloaded.channels),edited),"saved XM retains edited note and both columns");
        io::writeFileAtomic(home/"ui-test.xm",saved);

        // A valid empty XM can omit every pattern, even one referenced later.
        Document empty=doc;empty.patterns.clear();empty.orders={0,7};
        io::writeFileAtomic(home/"omitted-patterns.xm",io::encodeXm(empty));
        changePage(Page::Pattern);buttonInput(Button::B,true);tap(Button::Start);buttonInput(Button::B,false);
        openDocument(home/"omitted-patterns.xm");
        require(doc.patterns.empty()&&doc.orders.size()==2,"opening omitted patterns preserves original representation");
        require(solo==-1,"opening a new song resets solo");
        changePage(Page::Song);songField=0;draw();tap(Button::Down);require(order==1&&pattern().rows==64,"later omitted pattern is a virtual 64-row view");
        selectChord(Button::Right);draw();field=0;chord({Button::Right});
        require(doc.patterns.size()==8&&cell().note!=0,"first edit materializes the referenced blank pattern");
        menu("UNDO");require(doc.patterns.empty(),"undo restores omitted-pattern representation");

        // Finishing a held metadata edit must happen before an unsaved check.
        openDocument(home/"ui-test.xm");changePage(Page::Sample);sampleField=0;
        const auto oldSampleVolume=currentSample()->volume;
        buttonInput(Button::A,true);tap(Button::Left);
        require(bool(gestureBefore),"metadata gesture is pending before A release");
        SDL_Event quit{};quit.type=SDL_QUIT;event(quit);
        require(!running&&dirty&&!gestureBefore,"quit finalizes metadata and preserves work without forcing an XM save");
        const auto recovery=io::RecoveryStore(home).latest();
        require(recovery&&recovery->document.instruments[0].samples[0].volume==currentSample()->volume,"quit checkpoint contains the committed metadata");
        running=true;buttonInput(Button::A,false);
        require(currentSample()->volume<=oldSampleVolume,"exit checkpoint preserves metadata");

        // The UI must not produce an active zero-length/out-of-range loop.
        auto before=std::make_shared<Document>(doc);
        auto* s=currentSample();s->loopType=1;s->loopStart=std::uint32_t(s->pcm.size()-1);s->loopLength=1;documentChange(before);
        sampleField=5;chord({Button::Right});
        require(currentSample()->loopStart<currentSample()->pcm.size(),"active loop start stays inside sample");
        sampleField=6;chord({Button::Left});
        require(currentSample()->loopLength>=1,"active loop length stays positive");
        (void)io::encodeXm(doc);
        const auto speed=doc.speed;const auto historySize=undoStack.size();
        beginGesture();doc.speed=0;bool rejected=false;
        try{finishGesture();}catch(const std::exception&){rejected=true;}
        require(rejected&&doc.speed==speed&&undoStack.size()==historySize,"invalid metadata rolls back before history commit");

        // Extended editor regressions are compiled now and run only on request.
        openDocument(home/"ui-test.xm");changePage(Page::Pattern);order=row=channel=field=0;
        selectChord(Button::B);tap(Button::Down);for(int n=0;n<9;++n)tap(Button::Right);
        require(blockSelected&&selectedBlock().lastRow==2&&selectedBlock().lastChannel==2,"Select+B marks a rectangular block");
        tap(Button::B);require(blockClipboard&&blockClipboard->rows==2&&blockClipboard->channels==2,"B copies the selected block");
        row=8;channel=2;selectChord(Button::A);
        require(same(pattern().at(8,2,doc.channels),blockClipboard->cells.front()),"Select+A pastes rectangular block at cursor");
        menu("UNDO");menu("REDO");
        require(same(pattern().at(8,2,doc.channels),blockClipboard->cells.front()),"block redo restores copied cells");
        menu("CANCEL BLOCK SELECTION");require(!blockSelected,"block selection can be cancelled");
        const auto originalRows=pattern().rows;
        changePage(Page::Song);songField=0;songOrderColumn=1;songAppendOrder=false;chord({Button::Right});
        require(pattern().rows==originalRows+1,"Song ROWS grows the referenced pattern directly");
        menu("UNDO");require(pattern().rows==originalRows,"pattern length undo");
        changePage(Page::Instrument);menu("VOLUME ENVELOPE");require(mode==Mode::Envelope,"volume envelope editor is reachable");
        const auto originalPoints=currentEnvelope().count;envelopeField=9;tap(Button::A);require(currentEnvelope().count==originalPoints+1,"Insert button adds envelope point");
        envelopeField=1;chord({Button::Right});tap(Button::Down);chord({Button::Left});
        const auto editedEnvelope=currentEnvelope();tap(Button::B);menu("UNDO");require(currentInstrument()->volumeEnvelope.points[std::size_t(envelopePoint)].y!=editedEnvelope.points[std::size_t(envelopePoint)].y,"envelope value undo retains points");
        menu("REDO");require(currentInstrument()->volumeEnvelope.points[std::size_t(envelopePoint)].y==editedEnvelope.points[std::size_t(envelopePoint)].y,"envelope redo restores value");
        menu("RENAME INSTRUMENT");require(mode==Mode::Name&&nameTarget==NameTarget::Instrument,"instrument name uses gamepad text editor");filename="UI ENVELOPE";tap(Button::Start);require(currentInstrument()->name=="UI ENVELOPE","instrument name commits to document");
        changePage(Page::Sample);selectionStart=0;selectionEnd=std::min(32,int(currentSample()->pcm.size()));
        const auto originalPcm=currentSample()->pcm;menu("REVERSE");
        require(currentSample()->pcm.front()==originalPcm[std::size_t(selectionEnd-1)],"sample reverse applies selection only");
        menu("UNDO");require(currentSample()->pcm==originalPcm,"compact sample delta restores PCM");
        const auto copiedPcmFrames=selectionEnd-selectionStart;
        menu("COPY PCM");require(pcmClipboard&&pcmClipboard->pcm.size()==std::size_t(copiedPcmFrames)&&!hasSelection(),"sample clipboard owns selected PCM and selection ends");
        menu("CONVERT TO 8-BIT");require(!currentSample()->sixteenBit,"sample depth conversion is reachable");menu("UNDO");
        menu("RENDER WAV");require(mode==Mode::RenderSettings,"render settings open without starting a render");
        tap(Button::Down);tap(Button::Down);chord({Button::Right});require(renderOptions.tailSeconds==3,"render tail setting");tap(Button::B);
        // Microphone device opening is deliberately excluded from UI automation.
        // A stopped recording can be exercised without touching an input device.
        pendingRecording=*currentSample();captureStarted=false;mode=Mode::Recording;dirty=false;
        SDL_Event repeatedQuit{};repeatedQuit.type=SDL_QUIT;event(repeatedQuit);
        require(mode==Mode::Confirm&&bool(pendingRecording),"quit retains pending recording for review");
        event(repeatedQuit);
        require(running&&mode==Mode::Confirm&&bool(pendingRecording),"repeated quit cannot bypass recording discard confirmation");
        tap(Button::B);require(mode==Mode::Recording&&bool(pendingRecording),"cancel quit returns to the retained recording");
        tap(Button::B);require(!pendingRecording&&mode==Mode::Normal,"explicit discard clears pending recording");
        (void)io::encodeXm(doc);

        openDocument(home/"ui-test.xm");
        noticeUntil=0;
        const std::array<std::pair<Page,const char*>,4> views={{{Page::Song,"song"},{Page::Pattern,"pattern"},{Page::Instrument,"instrument"},{Page::Sample,"sample"}}};
        for(const auto& view:views){changePage(view.first);draw();screenshot(home/(std::string("qa-")+view.second+".bmp"));SDL_RenderPresent(renderer);}
#include "../../tests/session_ui_checks.inc"
#include "../../tests/arrangement_ui_checks.inc"
#include "../../tests/pattern_view_ui_checks.inc"
#include "../../tests/import_cleanup_ui_checks.inc"
#include "../../tests/trim_ui_checks.inc"
#include "../../tests/ft2_extras_ui_checks.inc"
#include "../../tests/eight_button_ui_checks.inc"
#include "../../tests/lazy_slots_ui_checks.inc"
#include "../../tests/song_grid_ui_checks.inc"
#include "../../tests/render_range_ui_checks.inc"
#include "../../tests/audio_settings_ui_checks.inc"
#include "../../tests/theme_ui_checks.inc"
#include "../../tests/theme_files_ui_checks.inc"
#include "../../tests/note_audition_ui_checks.inc"
#include "../../tests/envelope_playhead_ui_checks.inc"
#include "../../tests/song_time_ui_checks.inc"
#include "../../tests/sample_analysis_ui_checks.inc"
#include "../../tests/envelope_presets_ui_checks.inc"
#include "../../tests/minimise_ui_checks.inc"
#include "../../tests/instrument_slots_ui_checks.inc"
#include "../../tests/whole_slot_clipboard_ui_checks.inc"
#include "../../tests/browser_folders_ui_checks.inc"
#include "../../tests/undo_labels_ui_checks.inc"
#include "../../tests/main_navigation_ui_checks.inc"
#include "../../tests/sample_layout_ui_checks.inc"
#include "../../tests/instrument_layout_ui_checks.inc"
#include "../../tests/keymap_edit_ui_checks.inc"
#include "../../tests/brick_feedback_ui_checks.inc"
#include "../../tests/waveform_view_ui_checks.inc"
#include "../../tests/range_marker_ui_checks.inc"
#include "../../tests/wave_preview_ui_checks.inc"
#include "../../tests/wave_synth_ui_checks.inc"
        changePage(Page::Pattern);draw();
        if(!options.screenshotPath.empty())screenshot(options.screenshotPath);
        std::cout<<"UI TEST PASS: "<<assertions<<" assertions; XM and application screenshots in "<<home.u8string()<<'\n';
        return 0;
    }
    int run() {
        if(options.uiTest)return runUiTest();
        int frames=0;bool captured=false;
        while(running){
            try {
                SDL_Event e;while(SDL_PollEvent(&e))event(e);
                pollTools();
                pollRecovery();
                const Uint32 now=SDL_GetTicks();
                for(int i=0;i<4;++i)if(held[i]&&Sint32(now-repeatAt[i])>=0){buttonDown(static_cast<Button>(i),true);repeatAt[i]=now+Uint32(repeatInterval);}
                draw();
                if(!options.screenshotPath.empty()&&!captured&&frames>=2){screenshot(options.screenshotPath);captured=true;}
                SDL_RenderPresent(renderer);
            }catch(const std::exception& e){if(options.smoke)throw;stopHeldAudition(true);notify(e.what(),true);gestureBefore.reset();aUsed=true;}
            ++frames;if(options.smoke&&frames>=6)break;
            SDL_Delay(8);
        }
        return 0;
    }
};

TrackerApp::TrackerApp(AppOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
TrackerApp::~TrackerApp() = default;
int TrackerApp::run() { return impl_->run(); }
} // namespace brick
