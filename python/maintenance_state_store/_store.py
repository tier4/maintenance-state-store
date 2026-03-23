"""
Core implementation of the maintenance state store.

The file format is JSON with the following fields:
  - version   (int)    : format version, currently 1
  - state     (str)    : "ON" or "OFF"
  - timestamp (int)    : Unix timestamp (seconds) at write time
  - checksum  (str)    : CRC32 as 8-digit hex, computed over
                         "{version}|{state}|{timestamp}"

Writes are atomic: the data is written to a .tmp file, fsync'd, then
rename()'d into place, followed by an fsync of the parent directory.
"""

from __future__ import annotations

import binascii
import json
import os
import time
from enum import Enum
from pathlib import Path
from typing import Final

_VERSION: Final[int] = 1
_DEFAULT_PATH: Final[Path] = Path("/var/lib/maintenance_state_store/state.json")


class State(Enum):
    OFF = "OFF"
    ON = "ON"
    UNKNOWN = "UNKNOWN"


def _canonical(version: int, state_str: str, timestamp: int) -> str:
    return f"{version}|{state_str}|{timestamp}"


def _checksum(canonical: str) -> str:
    crc = binascii.crc32(canonical.encode("ascii")) & 0xFFFFFFFF
    return f"{crc:08x}"


class Store:
    """
    Manages a maintenance state file for a vehicle OTA safety system.

    The state determines whether driving or OTA updates are allowed:
      - OFF:     Normal operation (driving allowed, OTA not allowed)
      - ON:      Maintenance mode (OTA allowed, driving not allowed)
      - UNKNOWN: Error/corrupted state (neither driving nor OTA allowed)
    """

    def __init__(self, file_path: Path | str = _DEFAULT_PATH) -> None:
        self._path = Path(file_path)

    def read(self) -> State:
        """
        Read the current maintenance state from the file.

        Returns UNKNOWN on any error: missing file, parse error,
        checksum mismatch, unknown state string, or bad version.
        """
        try:
            if not self._path.exists():
                return State.UNKNOWN

            with self._path.open("r", encoding="utf-8") as f:
                data = json.load(f)

            for field in ("version", "state", "timestamp", "checksum"):
                if field not in data:
                    return State.UNKNOWN

            if not isinstance(data["version"], int) or data["version"] != _VERSION:
                return State.UNKNOWN

            if not isinstance(data["state"], str):
                return State.UNKNOWN
            state_str: str = data["state"]

            if not isinstance(data["timestamp"], int):
                return State.UNKNOWN
            timestamp: int = data["timestamp"]

            if not isinstance(data["checksum"], str):
                return State.UNKNOWN

            canon = _canonical(_VERSION, state_str, timestamp)
            if data["checksum"] != _checksum(canon):
                return State.UNKNOWN

            if state_str == "OFF":
                return State.OFF
            if state_str == "ON":
                return State.ON
            return State.UNKNOWN

        except Exception:
            return State.UNKNOWN

    def write(self, state: State) -> bool:
        """
        Atomically write the given state to the file.

        Creates parent directories if they don't exist. Uses a .tmp file +
        rename() for atomicity and fsync() for durability.

        Writing State.UNKNOWN is rejected and returns False.
        """
        if state is State.UNKNOWN:
            return False
        return self._atomic_write(state)

    def force_write(self, state: State) -> bool:
        """
        Identical to write(). For CLI / maintenance tooling only.

        Do not call from application logic.
        """
        return self.write(state)

    @staticmethod
    def default_path() -> Path:
        """Return the default path for the state file."""
        return _DEFAULT_PATH

    def _atomic_write(self, state: State) -> bool:
        tmp_path = Path(str(self._path) + ".tmp")
        try:
            state_str = state.value
            timestamp = int(time.time())
            canon = _canonical(_VERSION, state_str, timestamp)
            data = {
                "version": _VERSION,
                "state": state_str,
                "timestamp": timestamp,
                "checksum": _checksum(canon),
            }
            json_str = json.dumps(data, indent=2) + "\n"

            self._path.parent.mkdir(parents=True, exist_ok=True)

            with tmp_path.open("w", encoding="utf-8") as f:
                f.write(json_str)
                f.flush()
                os.fsync(f.fileno())

            os.rename(tmp_path, self._path)

            # fsync parent directory to persist the directory entry
            dir_fd = os.open(str(self._path.parent), os.O_RDONLY)
            try:
                os.fsync(dir_fd)
            except OSError:
                pass  # Some filesystems don't support fsync on directories
            finally:
                os.close(dir_fd)

            return True

        except Exception:
            try:
                if tmp_path.exists():
                    tmp_path.unlink()
            except Exception:
                pass
            return False
