"""
pytest tests for the maintenance_state_store Python package.

Install the package first:
    pip install -e .

Then run:
    pytest tests/test_store.py -v
"""

import json
import pathlib

import pytest

import maintenance_state_store as mss


@pytest.fixture
def state_file(tmp_path: pathlib.Path) -> pathlib.Path:
    """Return a path inside a fresh temporary directory for each test."""
    return tmp_path / "state.json"


# ---------------------------------------------------------------------------
# read() on missing file returns UNKNOWN
# ---------------------------------------------------------------------------
def test_read_missing_file_returns_unknown(state_file):
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# write(ON) then read() returns ON
# ---------------------------------------------------------------------------
def test_write_on_then_read_returns_on(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True
    assert store.read() == mss.State.ON


# ---------------------------------------------------------------------------
# write(OFF) then read() returns OFF
# ---------------------------------------------------------------------------
def test_write_off_then_read_returns_off(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.OFF) is True
    assert store.read() == mss.State.OFF


# ---------------------------------------------------------------------------
# Overwrite: write ON then OFF, read returns OFF
# ---------------------------------------------------------------------------
def test_overwrite_state(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True
    assert store.read() == mss.State.ON

    assert store.write(mss.State.OFF) is True
    assert store.read() == mss.State.OFF


# ---------------------------------------------------------------------------
# write(UNKNOWN) returns False
# ---------------------------------------------------------------------------
def test_write_unknown_returns_false(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.UNKNOWN) is False


# ---------------------------------------------------------------------------
# Corrupted file (bad checksum) -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_corrupted_checksum_returns_unknown(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True

    with state_file.open("r") as f:
        data = json.load(f)
    data["checksum"] = "deadbeef"
    with state_file.open("w") as f:
        json.dump(data, f)

    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Corrupted file (bad JSON) -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_corrupted_json_returns_unknown(state_file):
    state_file.write_text("{ this is not valid JSON !!!")
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Empty file -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_empty_file_returns_unknown(state_file):
    state_file.write_text("")
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Unknown state string -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_unknown_state_string_returns_unknown(state_file):
    bad = {"version": 1, "state": "BLAH", "timestamp": 0, "checksum": "206ccd8c"}
    state_file.write_text(json.dumps(bad))
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Wrong version -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_wrong_version_returns_unknown(state_file):
    bad = {"version": 2, "state": "ON", "timestamp": 0, "checksum": "00000000"}
    state_file.write_text(json.dumps(bad))
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Missing required fields -> read() returns UNKNOWN
# ---------------------------------------------------------------------------
def test_missing_fields_returns_unknown(state_file):
    state_file.write_text(json.dumps({"version": 1, "state": "ON"}))
    store = mss.Store(state_file)
    assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# force_write() behaves like write()
# ---------------------------------------------------------------------------
def test_force_write_on(state_file):
    store = mss.Store(state_file)
    assert store.force_write(mss.State.ON) is True
    assert store.read() == mss.State.ON


def test_force_write_off(state_file):
    store = mss.Store(state_file)
    assert store.force_write(mss.State.OFF) is True
    assert store.read() == mss.State.OFF


def test_force_write_unknown_returns_false(state_file):
    store = mss.Store(state_file)
    assert store.force_write(mss.State.UNKNOWN) is False


# ---------------------------------------------------------------------------
# Atomic write: .tmp file is cleaned up after a successful write
# ---------------------------------------------------------------------------
def test_tmp_file_cleaned_up_after_write(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True
    assert not pathlib.Path(str(state_file) + ".tmp").exists()


# ---------------------------------------------------------------------------
# write() creates parent directories if they don't exist
# ---------------------------------------------------------------------------
def test_write_creates_parent_directories(tmp_path):
    nested = tmp_path / "nested" / "deep" / "state.json"
    store = mss.Store(nested)
    assert store.write(mss.State.ON) is True
    assert nested.exists()
    assert store.read() == mss.State.ON


# ---------------------------------------------------------------------------
# default_path() returns the expected path
# ---------------------------------------------------------------------------
def test_default_path():
    assert mss.Store.default_path() == pathlib.Path(
        "/var/lib/maintenance_state_store/state.json"
    )


# ---------------------------------------------------------------------------
# Missing file at default path returns UNKNOWN (no crash)
# ---------------------------------------------------------------------------
def test_default_constructor_missing_file_returns_unknown():
    if not mss.Store.default_path().exists():
        store = mss.Store()
        assert store.read() == mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# JSON structure is correct after write
# ---------------------------------------------------------------------------
def test_json_file_structure_after_write(state_file):
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True

    with state_file.open("r") as f:
        data = json.load(f)

    assert data["version"] == 1
    assert data["state"] == "ON"
    assert isinstance(data["timestamp"], int)
    assert isinstance(data["checksum"], str) and len(data["checksum"]) == 8


# ---------------------------------------------------------------------------
# State enum members are distinct
# ---------------------------------------------------------------------------
def test_state_enum_members():
    assert isinstance(mss.State.OFF, mss.State)
    assert isinstance(mss.State.ON, mss.State)
    assert isinstance(mss.State.UNKNOWN, mss.State)
    assert mss.State.OFF != mss.State.ON
    assert mss.State.OFF != mss.State.UNKNOWN
    assert mss.State.ON != mss.State.UNKNOWN


# ---------------------------------------------------------------------------
# Checksum consistency: Python result matches C++ file format
# ---------------------------------------------------------------------------
def test_checksum_round_trip(state_file):
    """Write with Python, read with Python; verify checksum field is valid."""
    store = mss.Store(state_file)
    assert store.write(mss.State.ON) is True

    with state_file.open("r") as f:
        data = json.load(f)

    import binascii

    canon = f"{data['version']}|{data['state']}|{data['timestamp']}"
    expected = f"{binascii.crc32(canon.encode('ascii')) & 0xFFFFFFFF:08x}"
    assert data["checksum"] == expected


# ---------------------------------------------------------------------------
# Cross-language compatibility tests
#
# These tests read fixture files whose checksums were computed independently
# by the C++ implementation.  If either implementation changes the file
# format or the checksum algorithm, these tests will fail immediately.
# ---------------------------------------------------------------------------
_FIXTURES = pathlib.Path(__file__).parent / "fixtures"
_MSS_CLI = pathlib.Path(__file__).parent.parent / "build" / "tests" / "mss_cli"


def test_compat_read_fixture_state_on():
    """Python must be able to read the shared fixture verified by C++ tests."""
    store = mss.Store(_FIXTURES / "state_on_v1.json")
    assert store.read() == mss.State.ON, (
        "Python could not read the shared fixture. "
        "Check that both implementations use the same CRC32 algorithm and JSON field names."
    )


def test_compat_read_fixture_state_off():
    """Python must be able to read the shared fixture verified by C++ tests."""
    store = mss.Store(_FIXTURES / "state_off_v1.json")
    assert store.read() == mss.State.OFF, (
        "Python could not read the shared fixture. "
        "Check that both implementations use the same CRC32 algorithm and JSON field names."
    )


# ---------------------------------------------------------------------------
# Live cross-language tests (require the mss_cli binary from the C++ build)
#
# These tests skip automatically when the binary is not present, so a
# Python-only install still works.  In CI, the C++ build runs first so
# the binary is always available.
# ---------------------------------------------------------------------------

pytestmark_cli = pytest.mark.skipif(
    not _MSS_CLI.exists(),
    reason="mss_cli binary not found (run cmake --build build first)",
)


@pytestmark_cli
def test_cpp_writes_python_reads_on(tmp_path):
    """C++ writes ON → Python must read ON."""
    import subprocess

    out = tmp_path / "state.json"
    subprocess.run([str(_MSS_CLI), "write", "ON", str(out)], check=True)
    assert mss.Store(out).read() == mss.State.ON


@pytestmark_cli
def test_cpp_writes_python_reads_off(tmp_path):
    """C++ writes OFF → Python must read OFF."""
    import subprocess

    out = tmp_path / "state.json"
    subprocess.run([str(_MSS_CLI), "write", "OFF", str(out)], check=True)
    assert mss.Store(out).read() == mss.State.OFF


@pytestmark_cli
def test_python_writes_cpp_reads_on(tmp_path):
    """Python writes ON → C++ must read ON."""
    import subprocess

    out = tmp_path / "state.json"
    mss.Store(out).write(mss.State.ON)
    result = subprocess.run(
        [str(_MSS_CLI), "read", str(out)], capture_output=True, text=True, check=True
    )
    assert result.stdout.strip() == "ON"


@pytestmark_cli
def test_python_writes_cpp_reads_off(tmp_path):
    """Python writes OFF → C++ must read OFF."""
    import subprocess

    out = tmp_path / "state.json"
    mss.Store(out).write(mss.State.OFF)
    result = subprocess.run(
        [str(_MSS_CLI), "read", str(out)], capture_output=True, text=True, check=True
    )
    assert result.stdout.strip() == "OFF"
