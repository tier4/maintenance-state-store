#pragma once

#include <filesystem>

namespace maintenance
{

enum class State { OFF, ON, UNKNOWN };

/**
 * @brief Manages a maintenance state file for a vehicle OTA safety system.
 *
 * The state determines whether driving or OTA updates are allowed:
 *   - OFF:     Normal operation (driving allowed, OTA not allowed)
 *   - ON:      Maintenance mode (OTA allowed, driving not allowed)
 *   - UNKNOWN: Error/corrupted state (neither driving nor OTA allowed)
 */
class Store
{
  public:
    /**
     * @brief Constructs a Store with the given file path.
     * @param file_path Path to the JSON state file. Defaults to default_path().
     */
    explicit Store(std::filesystem::path file_path = default_path());

    /**
     * @brief Reads the current maintenance state from the file.
     * @return The stored State, or State::UNKNOWN on any error (missing file,
     *         parse error, checksum mismatch, unknown state string, bad version).
     */
    [[nodiscard]] State read() const;

    /**
     * @brief Atomically writes the given state to the file.
     *
     * Creates parent directories if they don't exist.
     * Uses a temp file + rename() for atomicity, plus fsync() for durability.
     *
     * @param state The state to write. UNKNOWN is not a valid value to write;
     *              pass only OFF or ON.
     * @return true on success, false on any failure (existing file is never
     *         corrupted on failure).
     */
    [[nodiscard]] bool write(State state);

    /**
     * @brief Identical to write(). Intended for CLI/debug use only.
     *
     * Bypasses any higher-level safety checks that a caller might impose.
     */
    [[nodiscard]] bool force_write(State state);

    /**
     * @brief Returns the default path for the state file.
     */
    static std::filesystem::path default_path();

  private:
    std::filesystem::path file_path_;
};

} // namespace maintenance
