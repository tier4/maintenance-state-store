#include "maintenance_state_store/maintenance_state_store.hpp"

#include <array>
#include <cerrno>
#include <charconv> // std::to_chars, std::from_chars (C++17)
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unistd.h>

namespace maintenance
{

namespace
{

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

/// Returns a string_view into a string literal — no heap allocation.
constexpr std::string_view state_to_string(State state)
{
    switch (state) {
    case State::OFF:
        return "OFF";
    case State::ON:
        return "ON";
    case State::UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// CRC32 — compile-time lookup table (C++17 constexpr + inline variable)
// ---------------------------------------------------------------------------

constexpr uint32_t kCrc32Polynomial = 0xEDB88320u;

constexpr std::array<uint32_t, 256> make_crc32_table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1u) ? (crc >> 1) ^ kCrc32Polynomial : crc >> 1;
        }
        table[i] = crc;
    }
    return table;
}

/// Computed at compile time; no runtime initialisation cost.
inline constexpr auto kCrc32Table = make_crc32_table();

uint32_t crc32(std::string_view data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = kCrc32Table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline constexpr int kVersion = 1;

/// Build the canonical string over which the checksum is computed.
/// Format: "{version}|{state}|{timestamp}"
std::string canonical_string(int version, std::string_view state_str, int64_t timestamp)
{
    return std::to_string(version) + "|" + std::string(state_str) + "|" + std::to_string(timestamp);
}

/// Format a uint32_t as a lowercase hex string (8 digits, zero-padded).
/// Uses std::to_chars (C++17): no locale, no heap, no exceptions.
std::string uint32_to_hex(uint32_t v)
{
    std::array<char, 8> buf{};
    auto [ptr, ec]        = std::to_chars(buf.data(), buf.data() + buf.size(), v, 16);
    const std::size_t len = static_cast<std::size_t>(ptr - buf.data());
    std::string       result(8 - len, '0'); // zero-pad to 8 digits
    result.append(buf.data(), len);
    return result;
}

/// Parse a hex string to uint32_t.
/// Uses std::from_chars (C++17): no locale, no exceptions.
bool hex_to_uint32(std::string_view hex, uint32_t &out)
{
    if (hex.empty() || hex.size() > 8) {
        return false;
    }
    auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out, 16);
    return ec == std::errc{} && ptr == hex.data() + hex.size();
}

/// Get the current Unix timestamp (seconds since epoch).
int64_t current_timestamp() { return static_cast<int64_t>(std::time(nullptr)); }

/// fsync a path (file or directory) by file descriptor.
/// Returns true on success. Silently ignores EINVAL (some FSes don't support
/// fsync on directories).
bool fsync_path(const std::filesystem::path &p)
{
    int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    int ret = ::fsync(fd);
    ::close(fd);
    return ret == 0 || errno == EINVAL;
}

} // namespace

// ---------------------------------------------------------------------------
// Store implementation
// ---------------------------------------------------------------------------

std::filesystem::path Store::default_path()
{
    return "/var/lib/maintenance_state_store/state.json";
}

Store::Store(std::filesystem::path file_path) : file_path_(std::move(file_path)) {}

State Store::read() const
{
    try {
        if (!std::filesystem::exists(file_path_)) {
            return State::UNKNOWN;
        }

        std::ifstream ifs(file_path_);
        if (!ifs.is_open()) {
            return State::UNKNOWN;
        }

        nlohmann::json j;
        try {
            ifs >> j;
        } catch (const nlohmann::json::parse_error &) {
            return State::UNKNOWN;
        }

        if (!j.contains("version") || !j.contains("state") || !j.contains("timestamp") ||
            !j.contains("checksum")) {
            return State::UNKNOWN;
        }

        if (!j["version"].is_number_integer() || j["version"].get<int>() != kVersion) {
            return State::UNKNOWN;
        }

        if (!j["state"].is_string()) {
            return State::UNKNOWN;
        }
        const std::string state_str = j["state"].get<std::string>();

        if (!j["timestamp"].is_number_integer()) {
            return State::UNKNOWN;
        }
        const int64_t timestamp = j["timestamp"].get<int64_t>();

        if (!j["checksum"].is_string()) {
            return State::UNKNOWN;
        }
        uint32_t stored_checksum = 0;
        if (!hex_to_uint32(j["checksum"].get<std::string>(), stored_checksum)) {
            return State::UNKNOWN;
        }

        if (crc32(canonical_string(kVersion, state_str, timestamp)) != stored_checksum) {
            return State::UNKNOWN;
        }

        if (state_str == "OFF")
            return State::OFF;
        if (state_str == "ON")
            return State::ON;
        return State::UNKNOWN;

    } catch (...) {
        return State::UNKNOWN;
    }
}

bool Store::write(State state)
{
    try {
        if (state == State::UNKNOWN) {
            return false;
        }

        const std::string_view state_sv  = state_to_string(state);
        const int64_t          timestamp = current_timestamp();
        const std::string      canon     = canonical_string(kVersion, state_sv, timestamp);

        nlohmann::json j;
        j["version"]   = kVersion;
        j["state"]     = state_sv;
        j["timestamp"] = timestamp;
        j["checksum"]  = uint32_to_hex(crc32(canon));

        const std::string json_str = j.dump(2) + "\n";

        const std::filesystem::path dir      = file_path_.parent_path();
        const std::filesystem::path tmp_path = std::filesystem::path(file_path_) += ".tmp";

        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }

        {
            int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd < 0) {
                return false;
            }

            const char *data      = json_str.data();
            std::size_t remaining = json_str.size();
            while (remaining > 0) {
                ssize_t written = ::write(fd, data, remaining);
                if (written < 0) {
                    ::close(fd);
                    std::error_code ec;
                    std::filesystem::remove(tmp_path, ec);
                    return false;
                }
                data += written;
                remaining -= static_cast<std::size_t>(written);
            }

            if (::fsync(fd) != 0) {
                ::close(fd);
                std::error_code ec;
                std::filesystem::remove(tmp_path, ec);
                return false;
            }
            ::close(fd);
        }

        if (std::error_code ec; std::filesystem::rename(tmp_path, file_path_, ec), ec) {
            std::error_code remove_ec;
            std::filesystem::remove(tmp_path, remove_ec);
            return false;
        }

        if (!dir.empty()) {
            fsync_path(dir);
        }

        return true;

    } catch (...) {
        return false;
    }
}

bool Store::force_write(State state) { return write(state); }

} // namespace maintenance
