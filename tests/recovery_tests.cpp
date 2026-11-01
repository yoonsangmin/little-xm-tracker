#include "io/Codec.hpp"
#include "io/Recovery.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        static std::atomic<unsigned> serial{0};
        const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("brick-recovery-tests-" + std::to_string(timestamp) + "-" + std::to_string(++serial));
        check(fs::create_directory(path), "Cannot create isolated test directory");
    }
    ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

brick::Document song(const char* name) {
    auto document = brick::makeDemoDocument();
    document.name = name;
    return document;
}

void repairChecksum(Bytes& bytes) {
    // Independent bitwise CRC32, unlike the table-driven store implementation.
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i + 4 < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    crc ^= 0xffffffffu;
    for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
}

void rotationAndUnicode() {
    TemporaryDirectory temporary;
    const auto home = temporary.path / "state";
    brick::io::RecoveryStore store(home);
    check(!store.latest(), "missing store should be empty");
    store.clear();
    const auto source = temporary.path / fs::u8path(u8"\ud55c\uae00 \u65e5\u672c\u8a9e.xm");
    auto document = song("first");
    document.patterns[0].at(1, 0, document.channels) = {60, 1, 0xf3, 0x23, 0xa7};
    store.save(document, source);
    auto recovered = store.latest();
    check(recovered && recovered->generation == 1, "first generation missing");
    check(recovered->sourcePath == source, "Unicode source path was not preserved");
    check(brick::io::encodeXm(recovered->document) == brick::io::encodeXm(document), "recovery changed XM data");
    check(!fs::exists(source), "autosave wrote the source XM");
    for (unsigned generation = 2; generation <= 7; ++generation) {
        document.name = "generation " + std::to_string(generation);
        store.save(document, source);
        recovered = store.latest();
        check(recovered && recovered->generation == generation && recovered->document.name == document.name,
              "slot rotation lost the newest generation");
    }
    std::size_t fileCount = 0;
    for (const auto& entry : fs::directory_iterator(home)) {
        check(entry.path().filename() == "recovery-0.brx" || entry.path().filename() == "recovery-1.brx",
              "autosave created an additional persistent file");
        ++fileCount;
    }
    check(fileCount == 2, "autosave should use exactly two fixed slots");
    brick::io::RecoveryStore reopened(home);
    check(reopened.latest()->generation == 7, "store depended on in-memory generation state");
    reopened.save(song("unsaved"));
    check(reopened.latest()->sourcePath.empty(), "unsaved document acquired a source path");
}

void corruptedLatestAndInterruptedWrite() {
    TemporaryDirectory temporary;
    brick::io::RecoveryStore store(temporary.path);
    store.save(song("first"));
    store.save(song("second"));
    store.save(song("third")); // Slot 0 is latest; slot 1 retains generation 2.
    const auto newestPath = temporary.path / "recovery-0.brx";
    const auto previousPath = temporary.path / "recovery-1.brx";
    const auto newest = brick::io::readFile(newestPath);
    const auto previous = brick::io::readFile(previousPath);
    const auto interrupted = temporary.path / "recovery-0.brx.tmp.999.1";
    brick::io::writeFileAtomic(interrupted, newest);
    auto damaged = newest;
    damaged[damaged.size() - 5] ^= 0x80;
    brick::io::writeFileAtomic(newestPath, damaged);
    auto recovered = store.latest();
    check(recovered && recovered->generation == 2 && recovered->document.name == "second",
          "corrupt latest did not fall back, or temporary file was recovered");
    check(brick::io::readFile(previousPath) == previous, "read recovery changed the older slot");

    damaged = newest;
    damaged[19] ^= 0x80; // Generation metadata is checksummed too.
    brick::io::writeFileAtomic(newestPath, damaged);
    check(store.latest()->generation == 2, "corrupt generation metadata outranked a valid slot");
    damaged = newest;
    damaged.resize(damaged.size() - 9);
    brick::io::writeFileAtomic(newestPath, damaged);
    check(store.latest()->generation == 2, "truncated latest did not fall back");

    store.save(song("replacement"));
    recovered = store.latest();
    check(recovered && recovered->generation == 3 && recovered->document.name == "replacement",
          "save did not replace corrupt slot using the last valid generation");
    check(brick::io::readFile(previousPath) == previous, "save overwrote the sole valid previous slot");
    auto invalid = song("invalid");
    invalid.channels = 0;
    const auto before = brick::io::readFile(newestPath);
    rejects([&] { store.save(invalid); }, "invalid document was accepted for recovery");
    check(brick::io::readFile(newestPath) == before && brick::io::readFile(previousPath) == previous,
          "failed autosave changed a recovery slot");

    const auto unrelated = temporary.path / "keep.txt";
    brick::io::writeFileAtomic(unrelated, Bytes{1, 2, 3});
    store.clear();
    check(!store.latest(), "clear left a recoverable document");
    check(fs::exists(interrupted) && fs::exists(unrelated), "clear removed unrelated or interrupted temporary files");
    store.clear();
}

void invalidContainersAreNotRecoverable() {
    TemporaryDirectory temporary;
    brick::io::RecoveryStore store(temporary.path);
    store.save(song("valid"));
    const auto path = temporary.path / "recovery-0.brx";
    const auto original = brick::io::readFile(path);
    auto damaged = original;
    damaged[48] = 'X'; // Version 2, empty source path: byte 48 begins XM.
    repairChecksum(damaged);
    brick::io::writeFileAtomic(path, damaged);
    check(!store.latest(), "valid checksum concealed an invalid XM payload");
    damaged = original;
    for (std::size_t i = 20; i < 24; ++i) damaged[i] = 0xff;
    repairChecksum(damaged);
    brick::io::writeFileAtomic(path, damaged);
    check(!store.latest(), "oversized source length was accepted");
    damaged = original;
    damaged.push_back(0);
    brick::io::writeFileAtomic(path, damaged);
    check(!store.latest(), "unexpected container suffix was accepted");
    damaged = original;
    for (std::size_t i = 12; i < 20; ++i) damaged[i] = 0xff;
    repairChecksum(damaged);
    brick::io::writeFileAtomic(path, damaged);
    rejects([&] { store.save(song("overflow")); }, "generation overflow wrapped around");
    check(brick::io::readFile(path) == damaged, "generation overflow changed the valid slot");
}

void savedStateAndSourceFingerprint() {
    TemporaryDirectory temporary;
    brick::io::RecoveryStore store(temporary.path / "state");
    const auto source = temporary.path / fs::u8path(u8"\uc6d0\ubcf8.xm");
    check(!brick::io::fingerprintFile({}) && !brick::io::fingerprintFile(source), "missing file acquired a fingerprint");
    // Standard CRC32 check vector verifies compatibility independently of the
    // recovery codec and includes raw bytes without requiring a valid XM.
    const Bytes checkVector = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    brick::io::writeFileAtomic(source, checkVector);
    const auto known = brick::io::fingerprintFile(source);
    check(known && known->size == 9 && known->checksum == 0xcbf43926u, "source fingerprint is not standard CRC32");
    const auto document = song("saved state");
    brick::io::writeFileAtomic(source, brick::io::encodeXm(document));
    const auto baseline = brick::io::fingerprintFile(source);
    store.save(document, source, false, baseline);
    auto recovered = store.latest();
    check(recovered && !recovered->dirty && recovered->sourceFingerprint == baseline,
          "clean flag or source baseline was not preserved");
    check(recovered->sourcePath == source, "fingerprinted source path changed");

    auto edited = document;
    edited.name = "edited locally";
    store.save(edited, source, true, baseline);
    recovered = store.latest();
    check(recovered && recovered->dirty && recovered->sourceFingerprint == baseline,
          "dirty snapshot lost its original source baseline");
    check(recovered->sourceFingerprint == brick::io::fingerprintFile(source), "unchanged source was reported changed");
    auto external = document;
    external.name = "external edit";
    brick::io::writeFileAtomic(source, brick::io::encodeXm(external));
    check(recovered->sourceFingerprint != brick::io::fingerprintFile(source), "same-size external modification was not detected");

    const auto latestPath = temporary.path / "state" / "recovery-1.brx";
    const auto latest = brick::io::readFile(latestPath);
    check(latest[8] == 2, "new recovery writer did not emit version 2");
    auto damaged = latest;
    damaged[32] ^= 1; // Clean/dirty classification must be protected by CRC.
    brick::io::writeFileAtomic(latestPath, damaged);
    check(store.latest()->generation == 1, "unchecked dirty flag overrode a clean snapshot");
    damaged = latest;
    damaged[44] ^= 1; // Source-fingerprint checksum is also protected.
    brick::io::writeFileAtomic(latestPath, damaged);
    check(store.latest()->generation == 1, "unchecked fingerprint metadata was accepted");
    damaged = latest;
    damaged[32] |= 4;
    repairChecksum(damaged);
    brick::io::writeFileAtomic(latestPath, damaged);
    check(store.latest()->generation == 1, "unknown metadata flags were accepted");
    fs::remove(source);
    check(!brick::io::fingerprintFile(source), "deleted source retained a fingerprint");
    rejects([&] { (void)brick::io::fingerprintFile(temporary.path); }, "directory was accepted as source file");
}

void legacyRecoveryIsDirtyWithUnknownBaseline() {
    TemporaryDirectory temporary;
    brick::io::RecoveryStore store(temporary.path);
    const auto source = temporary.path / "previous.xm";
    const auto document = song("old recovery");
    store.save(document, source, false, brick::io::SourceFingerprint{1234, 0xabcdef01});
    const auto path = temporary.path / "recovery-0.brx";
    auto legacy = brick::io::readFile(path);
    legacy[8] = 1;
    legacy.erase(legacy.begin() + 32, legacy.begin() + 48);
    repairChecksum(legacy);
    brick::io::writeFileAtomic(path, legacy);
    const auto recovered = store.latest();
    check(recovered && recovered->generation == 1 && recovered->document.name == document.name,
          "version 1 recovery is no longer readable");
    check(recovered->dirty && !recovered->sourceFingerprint && recovered->sourcePath == source,
          "legacy recovery incorrectly claimed a clean or known source state");
    store.save(document); // Existing callers remain dirty with no baseline.
    const auto defaults = store.latest();
    check(defaults && defaults->generation == 2 && defaults->dirty && !defaults->sourceFingerprint,
          "default save arguments did not preserve previous dirty-recovery behavior");
}

void manualBackupAndFailures() {
    TemporaryDirectory temporary;
    const auto path = temporary.path / fs::u8path(u8"\uace1.xm");
    const auto backup = temporary.path / "Backup" / path.filename();
    const auto first = song("first");
    const auto second = song("second");
    const auto third = song("third");
    brick::io::saveXmWithBackup(path, first);
    check(!fs::exists(backup), "first manual save created a meaningless backup");
    const auto firstBytes = brick::io::readFile(path);
    brick::io::saveXmWithBackup(path, second);
    check(brick::io::readFile(backup) == firstBytes, "backup did not retain exact previous file bytes");
    check(brick::io::decodeXm(brick::io::readFile(path)).name == "second", "manual save did not replace current XM");
    const auto secondBytes = brick::io::readFile(path);
    brick::io::saveXmWithBackup(path, third);
    check(brick::io::readFile(backup) == secondBytes, "backup did not rotate to the immediately previous manual file");

    auto invalid = third;
    invalid.name.assign(21, 'x');
    const auto thirdBytes = brick::io::readFile(path);
    rejects([&] { brick::io::saveXmWithBackup(path, invalid); }, "invalid new XM overwrote the original");
    check(brick::io::readFile(path) == thirdBytes && brick::io::readFile(backup) == secondBytes,
          "invalid new XM changed original or backup");

    fs::remove(backup);
    fs::create_directory(backup); // Cannot replace a directory with a backup file.
    rejects([&] { brick::io::saveXmWithBackup(path, first); }, "backup failure did not abort manual save");
    check(brick::io::readFile(path) == thirdBytes, "backup failure changed original XM");
    fs::remove(backup);
    brick::io::writeFileAtomic(backup, secondBytes);
    brick::io::writeFileAtomic(path, Bytes{0, 1, 2, 3});
    rejects([&] { brick::io::saveXmWithBackup(path, first); }, "invalid existing file was overwritten");
    check(brick::io::readFile(path) == Bytes({0, 1, 2, 3}) && brick::io::readFile(backup) == secondBytes,
          "invalid existing XM replaced a good backup or original");
}
} // namespace

int main() {
    try {
        rotationAndUnicode();
        corruptedLatestAndInterruptedWrite();
        invalidContainersAreNotRecoverable();
        savedStateAndSourceFingerprint();
        legacyRecoveryIsDirtyWithUnknownBaseline();
        manualBackupAndFailures();
        std::cout << "Recovery rotation, corruption fallback, container validation, and manual backups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
