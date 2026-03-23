# maintenance-state-store

A library for **safe persistent management of maintenance state** in a vehicle OTA safety system.

Design document: Prevent OTA updates during vehicle operation - Developer Design Document

---

## Background

To prevent OTA (Over-The-Air) updates from being accidentally applied to a vehicle while it is driving,
the system must guarantee that "driving state" and "OTA execution state" cannot coexist.

This library serves as the **Source of Truth for the Fail-Prevent layer**, safely persisting maintenance state
to the filesystem with atomic writes and corruption detection.

| State                                 | Driving | OTA     |
| ------------------------------------- | ------- | ------- |
| `OFF` (normal operation)              | Allowed | Blocked |
| `ON` (maintenance mode)               | Blocked | Allowed |
| `UNKNOWN` (corrupted / uninitialized) | Blocked | Blocked |

---

## Repository Structure

C++ and Python are intentionally kept in separate directories.

```text
include/maintenance_state_store/
  maintenance_state_store.hpp         C++ public API
src/
  maintenance_state_store.cpp         C++ implementation
python/
  maintenance_state_store/
    __init__.py                       Python package entry point
    _store.py                         Python implementation
    py.typed                          PEP 561 marker
tests/
  test_store.cpp                      C++ unit tests (GoogleTest, 20 cases)
  test_store.py                       Python unit tests (pytest, 27 cases)
  mss_cli.cpp                         CLI tool for cross-language tests
  fixtures/                           Shared JSON fixtures (ON/OFF)
CMakeLists.txt                        C++ build — produces .a and .so
pyproject.toml                        Python package config (hatchling)
cmake/
  maintenance_state_store-config.cmake.in
.github/
  workflows/
    test.yml                          GHA: cpp / python (matrix) / compat jobs
    release.yml                       GHA: release / publish jobs
    claude.yml                        Claude Code action
```

---

## C++ Build

Dependencies are fetched automatically via CMake FetchContent.

```bash
cmake -B build
cmake --build build
```

Produces both a static library and a shared library:

```text
build/libmaintenance_state_store.a
build/libmaintenance_state_store.so
```

### Install

```bash
cmake --install build --prefix /path/to/prefix
```

Installs `.a`, `.so*`, the public header, and CMake config files.

### Options

| Option          | Default | Description                 |
| --------------- | ------- | --------------------------- |
| `BUILD_TESTING` | ON      | Build GoogleTest unit tests |

```bash
cmake -B build -DBUILD_TESTING=OFF
```

---

## Python Installation

```bash
pip install -e .
```

No build step required. Works with any Python >= 3.10.

---

## Testing

### C++ (GoogleTest, 20 cases)

```bash
cmake -B build && cmake --build build
cd build && ctest --output-on-failure
```

### Python (pytest, 27 cases)

```bash
pip install -e .
pytest tests/test_store.py -v
```

### Cross-language compatibility (4 cases)

Requires the C++ build to be present. Runs automatically in the `compat` CI job.

```bash
cmake -B build && cmake --build build
pip install -e .
pytest tests/test_store.py -v -k "cpp_writes or python_writes"
```

Tests:

- `test_cpp_writes_python_reads_on/off` — C++ writes a file, Python reads it
- `test_python_writes_cpp_reads_on/off` — Python writes a file, C++ reads it

If `build/tests/mss_cli` is not found, these four tests skip automatically.

---

## C++ API

```cpp
#include "maintenance_state_store/maintenance_state_store.hpp"

using maintenance::State;
using maintenance::Store;

// Default path: /var/lib/maintenance_state_store/state.json
Store store;

// Custom path
Store store("/path/to/state.json");

// Read: returns OFF / ON / UNKNOWN
State state = store.read();
if (state == State::UNKNOWN) {
    // Block both driving and OTA
}

// Write (atomic)
bool ok = store.write(State::ON);   // Enter maintenance mode
bool ok = store.write(State::OFF);  // Exit maintenance mode

// For CLI / debug tooling only — do not call from application logic
bool ok = store.force_write(State::OFF);
```

When using via CMake:

```cmake
find_package(maintenance_state_store REQUIRED)
target_link_libraries(your_target PRIVATE maintenance::maintenance_state_store)
```

---

## Python API

```python
import maintenance_state_store as mss

store = mss.Store()                         # Default path
store = mss.Store("/path/to/state.json")    # Custom path (str or pathlib.Path)

state = store.read()   # mss.State.OFF / mss.State.ON / mss.State.UNKNOWN
ok = store.write(mss.State.ON)
ok = store.write(mss.State.OFF)
ok = store.force_write(mss.State.OFF)       # CLI / debug only

default = mss.Store.default_path()         # pathlib.Path
```

Install the package first with `pip install -e .`, then import as shown above.

---

## File Format (JSON)

Default path: `/var/lib/maintenance_state_store/state.json`

```json
{
  "version": 1,
  "state": "ON",
  "timestamp": 1760000000,
  "checksum": "a13fb4c2"
}
```

| Field       | Type     | Description                            |
| ----------- | -------- | -------------------------------------- |
| `version`   | `int`    | Format version (currently `1`)         |
| `state`     | `string` | `"ON"` or `"OFF"`                      |
| `timestamp` | `int64`  | Unix timestamp (seconds) at write time |
| `checksum`  | `string` | CRC32 as 8-digit hex (see note below)  |

> **Checksum**: CRC32 computed over the canonical string `"{version}|{state}|{timestamp}"`.
> Example: `"1|ON|1760000000"`

---

## Safety Design

### Atomic Write

Write operations guarantee power-failure resilience via the following sequence:

1. Write to a temporary file (`.tmp`) in the same directory
2. `fsync()` the temporary file to flush to disk
3. `rename()` to atomically replace the target file
4. `fsync()` the parent directory to persist the directory entry

### Corruption Detection

`read()` returns `UNKNOWN` for any of the following conditions:

- File does not exist
- JSON parse error
- Missing required fields
- `version != 1`
- `state` is not `"ON"` or `"OFF"`
- Checksum mismatch

The `UNKNOWN` state blocks both driving and OTA as a fail-safe.

### Recovery from Corruption

If the state file becomes corrupted, recovery must be performed manually
(e.g., via an Ansible initialization script) with the vehicle in a safe, stopped state.

---

## CI

Three jobs run on every push and pull request to `main`.

| Job              | What it does                                                    |
| ---------------- | --------------------------------------------------------------- |
| `cpp`            | Build, ctest, `cmake --install` → upload C++ artifact           |
| `python`         | pytest × Python 3.10–3.14 matrix → upload wheel artifact (3.12) |
| `compat`         | Build C++, run 4 cross-language subprocess tests                |

### Artifacts

| Artifact                               | Contents                                  |
| -------------------------------------- | ----------------------------------------- |
| `maintenance_state_store-cpp-<sha>`    | `.a` + `.so*` + `.hpp` + CMake config     |
| `maintenance_state_store-python-<sha>` | `.whl` + `.tar.gz` (built on Python 3.12) |
