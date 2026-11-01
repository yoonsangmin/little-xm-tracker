#include "io/Recovery.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace brick::io {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<std::uint8_t, 8> magic = {{'B', 'R', 'X', 'R', 'E', 'C', '0', '1'}};
constexpr std::size_t legacyHeaderSize = 32;
constexpr std::size_t headerSize = 48;
constexpr std::size_t checksumSize = 4;
constexpr std::size_t maxContainerBytes = 128u * 1024u * 1024u;
constexpr std::size_t maxPathBytes = 64u * 1024u;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::array<std::uint32_t, 256> crcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        auto value = static_cast<std::uint32_t>(i);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
        table[i] = value;
    }
    return table;
}
constexpr auto crcLookup = crcTable();

std::uint32_t crc32(const Bytes& bytes, std::size_t count) {
    std::uint32_t result = 0xffffffffu;
    for (std::size_t i = 0; i < count; ++i)
        result = crcLookup[(result ^ bytes[i]) & 0xff] ^ (result >> 8);
    return result ^ 0xffffffffu;
}

void appendInteger(Bytes& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint64_t integer(const Bytes& bytes, std::size_t offset, unsigned count) {
    require(offset <= bytes.size() && count <= bytes.size() - offset, "Truncated recovery header");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i) result |= std::uint64_t(bytes[offset + i]) << (8 * i);
    return result;
}

std::filesystem::path slotPath(const std::filesystem::path& directory, int slot) {
    return directory / (slot == 0 ? "recovery-0.brx" : "recovery-1.brx");
}

// Check filesystem failures separately from malformed container contents. An
// unreadable slot must not be mistaken for disposable corrupt data during save.
std::optional<RecoverySnapshot> readSlot(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory) return std::nullopt;
    if (error) throw std::filesystem::filesystem_error("Cannot inspect recovery slot", path, error);
    if (!std::filesystem::exists(status)) return std::nullopt;
    require(std::filesystem::is_regular_file(status), "Recovery slot is not a regular file: " + path.u8string());
    const auto size = std::filesystem::file_size(path);
    if (size < legacyHeaderSize + checksumSize || size > maxContainerBytes) return std::nullopt;
    auto bytes = readFile(path);

    try {
        require(bytes.size() >= legacyHeaderSize + checksumSize, "Truncated recovery container");
        require(std::equal(magic.begin(), magic.end(), bytes.begin()), "Unknown recovery signature");
        const auto version = integer(bytes, 8, 4);
        require(version == 1 || version == 2, "Unsupported recovery version");
        const auto actualHeaderSize = version == 1 ? legacyHeaderSize : headerSize;
        require(bytes.size() >= actualHeaderSize + checksumSize, "Truncated recovery metadata");
        const auto generation = integer(bytes, 12, 8);
        const auto pathLength = integer(bytes, 20, 4);
        const auto xmLength = integer(bytes, 24, 8);
        require(generation != 0, "Invalid recovery generation");
        require(pathLength <= maxPathBytes, "Recovery source path is too long");
        const auto available = bytes.size() - actualHeaderSize - checksumSize;
        require(pathLength <= available && xmLength == available - pathLength, "Invalid recovery payload lengths");
        require(integer(bytes, bytes.size() - checksumSize, 4) == crc32(bytes, bytes.size() - checksumSize),
                "Recovery checksum mismatch");
        bool dirty = true;
        std::optional<SourceFingerprint> baseline;
        if (version == 2) {
            const auto flags = integer(bytes, 32, 4);
            require((flags & ~std::uint64_t(3)) == 0, "Unsupported recovery flags");
            dirty = (flags & 1) != 0;
            const auto sourceSize = integer(bytes, 36, 8);
            const auto sourceChecksum = static_cast<std::uint32_t>(integer(bytes, 44, 4));
            if (flags & 2) baseline = SourceFingerprint{sourceSize, sourceChecksum};
            else require(sourceSize == 0 && sourceChecksum == 0, "Unexpected recovery fingerprint data");
        }
        const auto pathSize = static_cast<std::size_t>(pathLength);
        const std::string source(reinterpret_cast<const char*>(bytes.data() + actualHeaderSize), pathSize);
        require(source.find('\0') == std::string::npos, "Recovery source path contains NUL");
        auto sourcePath = std::filesystem::u8path(source);

        // Reuse the container allocation rather than holding a second XM byte
        // buffer alongside the decoded sample data.
        bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(actualHeaderSize + pathSize));
        bytes.resize(static_cast<std::size_t>(xmLength));
        auto document = decodeXm(bytes);
        return RecoverySnapshot{std::move(document), std::move(sourcePath), generation, dirty, baseline};
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> generationAt(const std::filesystem::path& path) {
    const auto snapshot = readSlot(path);
    if (!snapshot) return std::nullopt;
    return snapshot->generation;
}

Bytes encodeContainer(const Document& document, const std::filesystem::path& sourcePath,
                      std::uint64_t generation, bool dirty, std::optional<SourceFingerprint> baseline) {
    const auto source = sourcePath.u8string();
    require(source.size() <= maxPathBytes && source.find('\0') == std::string::npos,
            "Recovery source path is invalid or exceeds 64 KiB");
    auto bytes = encodeXm(document);
    const auto prefixSize = headerSize + source.size();
    require(bytes.size() <= maxContainerBytes - prefixSize - checksumSize,
            "Recovery container exceeds the 128 MiB input limit");
    Bytes prefix;
    prefix.reserve(prefixSize);
    prefix.insert(prefix.end(), magic.begin(), magic.end());
    appendInteger(prefix, 2, 4);
    appendInteger(prefix, generation, 8);
    appendInteger(prefix, source.size(), 4);
    appendInteger(prefix, bytes.size(), 8);
    appendInteger(prefix, (dirty ? 1u : 0u) | (baseline ? 2u : 0u), 4);
    appendInteger(prefix, baseline ? baseline->size : 0, 8);
    appendInteger(prefix, baseline ? baseline->checksum : 0, 4);
    prefix.insert(prefix.end(), source.begin(), source.end());
    bytes.insert(bytes.begin(), prefix.begin(), prefix.end());
    appendInteger(bytes, crc32(bytes, bytes.size()), 4);
    return bytes;
}
} // namespace

std::optional<SourceFingerprint> fingerprintFile(const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory) return std::nullopt;
    if (error) throw std::filesystem::filesystem_error("Cannot inspect source XM", path, error);
    if (!std::filesystem::exists(status)) return std::nullopt;
    require(std::filesystem::is_regular_file(status), "Source XM is not a regular file: " + path.u8string());
    const auto bytes = readFile(path);
    return SourceFingerprint{static_cast<std::uint64_t>(bytes.size()), crc32(bytes, bytes.size())};
}

RecoveryStore::RecoveryStore(std::filesystem::path dataDirectory) {
    require(!dataDirectory.empty(), "Recovery data directory is empty");
    directory_ = std::filesystem::absolute(std::move(dataDirectory)).lexically_normal();
}

void RecoveryStore::save(const Document& document, const std::filesystem::path& sourcePath,
                         bool dirty, std::optional<SourceFingerprint> baseline) {
    std::filesystem::create_directories(directory_);
    // Validate each slot separately so save does not retain two PCM documents.
    const auto first = generationAt(slotPath(directory_, 0));
    const auto second = generationAt(slotPath(directory_, 1));
    const bool newestIsSecond = second && (!first || *second >= *first);
    const std::uint64_t newest = newestIsSecond ? *second : first.value_or(0);
    require(newest < std::numeric_limits<std::uint64_t>::max(), "Recovery generation counter is exhausted");
    const int target = (!first && !second) || newestIsSecond ? 0 : 1;
    const auto bytes = encodeContainer(document, sourcePath, newest + 1, dirty, baseline);
    writeFileAtomic(slotPath(directory_, target), bytes);
}

std::optional<RecoverySnapshot> RecoveryStore::latest() const {
    auto first = readSlot(slotPath(directory_, 0));
    auto second = readSlot(slotPath(directory_, 1));
    if (second && (!first || second->generation >= first->generation)) return second;
    return first;
}

void RecoveryStore::clear() {
    // Only remove the two exact slot names; interrupted temporary files and
    // unrelated user files are never enumerated or removed here.
    std::error_code firstError;
    std::filesystem::path failedPath;
    for (int slot = 0; slot < 2; ++slot) {
        const auto path = slotPath(directory_, slot);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory) continue;
        if (!error && std::filesystem::is_directory(status)) error = std::make_error_code(std::errc::is_a_directory);
        if (!error) std::filesystem::remove(path, error);
        if (error && !firstError) { firstError = error; failedPath = path; }
    }
    if (firstError) throw std::filesystem::filesystem_error("Cannot clear recovery slot", failedPath, firstError);
}

void saveXmWithBackup(const std::filesystem::path& path, const Document& document) {
    require(!path.filename().empty(), "Save filename is empty");
    const auto replacement = encodeXm(document); // Validate before changing either file.
    if (std::filesystem::exists(path)) {
        require(std::filesystem::is_regular_file(path), "XM destination is not a regular file: " + path.u8string());
        const auto previous = readFile(path);
        try {
            (void)decodeXm(previous);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error(std::string("Existing XM is invalid; original preserved: ") + error.what());
        }
        const auto backupDirectory = path.parent_path() / "Backup";
        const auto backupPath = backupDirectory / path.filename();
        if (std::filesystem::exists(backupPath)) {
            require(!std::filesystem::equivalent(path, backupPath), "Backup resolves to the original XM; save cancelled");
        }
        std::filesystem::create_directories(backupDirectory);
        writeFileAtomic(backupPath, previous); // Failure here leaves the original untouched.
    }
    writeFileAtomic(path, replacement);
}

} // namespace brick::io
