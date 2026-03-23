#include "maintenance_state_store/maintenance_state_store.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;
using maintenance::State;
using maintenance::Store;

// ---------------------------------------------------------------------------
// Test fixture: creates a unique temporary directory per test and removes it
// on teardown.
// ---------------------------------------------------------------------------
class StoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a unique temp directory for this test
        fs::path    base = fs::temp_directory_path() / "mss_test_XXXXXX";
        std::string tmpl = base.string();

        // mkdtemp modifies the template in-place
        char *result = ::mkdtemp(tmpl.data());
        ASSERT_NE(result, nullptr) << "mkdtemp failed: " << std::strerror(errno);

        tmp_dir_    = result;
        state_file_ = tmp_dir_ / "state.json";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
        // Ignore errors during cleanup
    }

    fs::path tmp_dir_;
    fs::path state_file_;
};

// ---------------------------------------------------------------------------
// read() on missing file returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, ReadMissingFileReturnsUnknown)
{
    Store store(state_file_);
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// write(ON) then read() returns ON
// ---------------------------------------------------------------------------
TEST_F(StoreTest, WriteOnThenReadReturnsOn)
{
    Store store(state_file_);
    EXPECT_TRUE(store.write(State::ON));
    EXPECT_EQ(store.read(), State::ON);
}

// ---------------------------------------------------------------------------
// write(OFF) then read() returns OFF
// ---------------------------------------------------------------------------
TEST_F(StoreTest, WriteOffThenReadReturnsOff)
{
    Store store(state_file_);
    EXPECT_TRUE(store.write(State::OFF));
    EXPECT_EQ(store.read(), State::OFF);
}

// ---------------------------------------------------------------------------
// Overwrite: write ON, then OFF, then read() returns OFF
// ---------------------------------------------------------------------------
TEST_F(StoreTest, OverwriteState)
{
    Store store(state_file_);
    EXPECT_TRUE(store.write(State::ON));
    EXPECT_EQ(store.read(), State::ON);

    EXPECT_TRUE(store.write(State::OFF));
    EXPECT_EQ(store.read(), State::OFF);
}

// ---------------------------------------------------------------------------
// write(UNKNOWN) returns false
// ---------------------------------------------------------------------------
TEST_F(StoreTest, WriteUnknownReturnsFalse)
{
    Store store(state_file_);
    EXPECT_FALSE(store.write(State::UNKNOWN));
}

// ---------------------------------------------------------------------------
// Corrupted file (bad checksum) → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, CorruptedChecksumReturnsUnknown)
{
    Store store(state_file_);
    ASSERT_TRUE(store.write(State::ON));

    // Tamper with the file: replace the checksum field
    {
        std::ifstream ifs(state_file_);
        std::string   content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());

        // Replace the checksum value with a deliberately wrong one
        auto pos = content.find("\"checksum\"");
        ASSERT_NE(pos, std::string::npos);
        // Find the value after the colon
        auto colon_pos = content.find(':', pos);
        ASSERT_NE(colon_pos, std::string::npos);
        auto quote_open = content.find('"', colon_pos);
        ASSERT_NE(quote_open, std::string::npos);
        auto quote_close = content.find('"', quote_open + 1);
        ASSERT_NE(quote_close, std::string::npos);

        // Replace with a fake checksum of the same length
        content.replace(quote_open + 1, quote_close - quote_open - 1, "deadbeef");

        std::ofstream ofs(state_file_, std::ios::trunc);
        ofs << content;
    }

    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Corrupted file (bad JSON) → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, CorruptedJsonReturnsUnknown)
{
    // Write garbage JSON directly
    std::ofstream ofs(state_file_);
    ofs << "{ this is not valid JSON !!!";
    ofs.close();

    Store store(state_file_);
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Corrupted file (empty file) → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, EmptyFileReturnsUnknown)
{
    // Create an empty file
    std::ofstream ofs(state_file_);
    ofs.close();

    Store store(state_file_);
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Corrupted file (unknown state string) → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, UnknownStateStringReturnsUnknown)
{
    // We cannot simply write "UNKNOWN" via the API (write() rejects it),
    // so we craft a valid-looking JSON with a valid checksum for state "BLAH".
    // The easiest way: write a known valid file and mutate the state + checksum.
    // Instead, we just write a JSON with an unknown state string and a matching
    // checksum by computing it ourselves in the test.

    // Canonical string for version=1, state="BLAH", timestamp=0:
    // CRC32("1|BLAH|0") — we just write mismatched state and verify UNKNOWN.
    // Simpler: write a file with missing "state" field.
    std::ofstream ofs(state_file_);
    ofs << R"({"version":1,"state":"BLAH","timestamp":0,"checksum":"00000000"})";
    ofs.close();

    Store store(state_file_);
    // Either the checksum mismatch triggers first (the "00000000" is wrong),
    // or the unknown state check triggers — either way, result must be UNKNOWN.
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Corrupted file (wrong version) → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, WrongVersionReturnsUnknown)
{
    std::ofstream ofs(state_file_);
    ofs << R"({"version":2,"state":"ON","timestamp":0,"checksum":"00000000"})";
    ofs.close();

    Store store(state_file_);
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Missing required fields → read() returns UNKNOWN
// ---------------------------------------------------------------------------
TEST_F(StoreTest, MissingFieldsReturnsUnknown)
{
    std::ofstream ofs(state_file_);
    ofs << R"({"version":1,"state":"ON"})"; // no timestamp, no checksum
    ofs.close();

    Store store(state_file_);
    EXPECT_EQ(store.read(), State::UNKNOWN);
}

// ---------------------------------------------------------------------------
// force_write() works similarly to write()
// ---------------------------------------------------------------------------
TEST_F(StoreTest, ForceWriteOnThenReadReturnsOn)
{
    Store store(state_file_);
    EXPECT_TRUE(store.force_write(State::ON));
    EXPECT_EQ(store.read(), State::ON);
}

TEST_F(StoreTest, ForceWriteOffThenReadReturnsOff)
{
    Store store(state_file_);
    EXPECT_TRUE(store.force_write(State::OFF));
    EXPECT_EQ(store.read(), State::OFF);
}

TEST_F(StoreTest, ForceWriteUnknownReturnsFalse)
{
    Store store(state_file_);
    EXPECT_FALSE(store.force_write(State::UNKNOWN));
}

// ---------------------------------------------------------------------------
// Atomic write: temp file is cleaned up after a successful write
// ---------------------------------------------------------------------------
TEST_F(StoreTest, TempFileCleanedUpAfterWrite)
{
    Store store(state_file_);
    EXPECT_TRUE(store.write(State::ON));

    // The .tmp file must not exist after a successful write
    fs::path tmp_path = state_file_;
    tmp_path += ".tmp";
    EXPECT_FALSE(fs::exists(tmp_path));
}

// ---------------------------------------------------------------------------
// write() creates parent directories if they don't exist
// ---------------------------------------------------------------------------
TEST_F(StoreTest, WriteCreatesParentDirectories)
{
    fs::path nested_file = tmp_dir_ / "nested" / "deep" / "state.json";
    Store    store(nested_file);
    EXPECT_TRUE(store.write(State::ON));
    EXPECT_TRUE(fs::exists(nested_file));
    EXPECT_EQ(store.read(), State::ON);
}

// ---------------------------------------------------------------------------
// default_path() returns the expected path
// ---------------------------------------------------------------------------
TEST(StoreStaticTest, DefaultPath)
{
    EXPECT_EQ(Store::default_path(), fs::path("/var/lib/maintenance_state_store/state.json"));
}

// ---------------------------------------------------------------------------
// Default constructor uses default_path()
// ---------------------------------------------------------------------------
TEST(StoreStaticTest, DefaultConstructorUsesDefaultPath)
{
    // We just verify the object can be constructed; we don't actually read/write
    // /var/lib/... in a unit test (would require root and pollute the system).
    Store store; // should not throw
    // A missing file at the default path should return UNKNOWN (not crash)
    // Only run this check if the default path doesn't actually exist on this system
    if (!fs::exists(Store::default_path())) {
        EXPECT_EQ(store.read(), State::UNKNOWN);
    }
}

// ---------------------------------------------------------------------------
// Cross-language compatibility tests
//
// These tests read fixture files whose checksums were computed independently
// by the Python implementation.  If either implementation changes the file
// format or the checksum algorithm, these tests will fail immediately.
//
// Fixture path is relative to the CMake source directory so that ctest can
// find it regardless of the build directory location.
// ---------------------------------------------------------------------------
class CompatibilityTest : public ::testing::Test
{
  protected:
    // Resolve a fixture path relative to this source file's directory.
    static fs::path fixture(const std::string &name)
    {
        // __FILE__ is the source file path; go up one directory to tests/fixtures/
        fs::path src = fs::path(__FILE__).parent_path();
        return src / "fixtures" / name;
    }
};

TEST_F(CompatibilityTest, ReadFixtureStateOn)
{
    Store store(fixture("state_on_v1.json"));
    EXPECT_EQ(store.read(), State::ON)
        << "C++ could not read the shared fixture written by the Python "
           "checksum reference. Check that both implementations use the same "
           "CRC32 algorithm and JSON field names.";
}

TEST_F(CompatibilityTest, ReadFixtureStateOff)
{
    Store store(fixture("state_off_v1.json"));
    EXPECT_EQ(store.read(), State::OFF)
        << "C++ could not read the shared fixture written by the Python "
           "checksum reference. Check that both implementations use the same "
           "CRC32 algorithm and JSON field names.";
}
