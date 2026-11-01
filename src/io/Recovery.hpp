#pragma once

#include "model/Document.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>

namespace brick::io {

struct SourceFingerprint {
    std::uint64_t size = 0;
    std::uint32_t checksum = 0;
    bool operator==(const SourceFingerprint& other) const noexcept {
        return size == other.size && checksum == other.checksum;
    }
    bool operator!=(const SourceFingerprint& other) const noexcept { return !(*this == other); }
};

// Hash the exact source-file bytes, not a canonical re-encoding. Empty/missing
// paths return no value; unreadable/non-file/oversized inputs throw.
std::optional<SourceFingerprint> fingerprintFile(const std::filesystem::path& path);

struct RecoverySnapshot {
    Document document;
    std::filesystem::path sourcePath;
    std::uint64_t generation = 0;
    bool dirty = true;
    // Source bytes at the most recent explicit load/save, before later edits.
    // Missing means unknown, never evidence that overwriting a source is safe.
    std::optional<SourceFingerprint> sourceFingerprint;
};

// Two fixed, checksummed slots in dataDirectory: recovery-0.brx and
// recovery-1.brx. An interrupted write never replaces the newest valid slot.
// One owner must serialize saves/clear; this is not a multi-process store.
class RecoveryStore {
public:
    explicit RecoveryStore(std::filesystem::path dataDirectory);
    void save(const Document& document, const std::filesystem::path& sourcePath = {},
              bool dirty = true, std::optional<SourceFingerprint> baseline = {});
    // Corrupt/truncated slots are ignored; returns no value when neither is
    // valid. Other operational failures (for example access denied) throw.
    std::optional<RecoverySnapshot> latest() const;
    void clear();

private:
    std::filesystem::path directory_;
};

// Before replacing an existing XM, validate it and atomically preserve its
// exact bytes in sibling Backup/<filename>. Keep one previous manual version.
// Invalid existing XM or backup failure aborts without changing the original.
// New destinations are saved directly and do not create an empty backup.
void saveXmWithBackup(const std::filesystem::path& path, const Document& document);

} // namespace brick::io
