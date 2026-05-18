#!/usr/bin/env python3
"""
storage_dump.py - Asset Tracker Template storage dump reader

Connects to the device over serial, sends the 'att_storage dump' shell command,
parses and decodes all records from every registered data type, and writes the
results to a JSON file.

With '--location-requests' the cell/WiFi data stored in LOCATION_CLOUD_REQUEST
records is forwarded to the nRF Cloud Ground Fix REST API
(https://api.nrfcloud.com/v1/location/ground-fix) to resolve coordinates.

Authentication:
    JWT token::
        --jwt YOUR_NRF_CLOUD_JWT

Usage:
    python3 storage_dump.py --port /dev/ttyACM0 --output dump.json
    python3 storage_dump.py --port /dev/ttyACM0 --output dump.json \\
        --location-requests --jwt YOUR_NRF_CLOUD_JWT

Requirements:
    pip install pyserial requests

Struct layout notes (ARM Cortex-M, little-endian, natural alignment):
    All decoders match the default arm-zephyr-eabi-gcc ABI. If you have changed
    CONFIG_APP_LOCATION_NEIGHBOR_CELLS_MAX or CONFIG_APP_LOCATION_WIFI_APS_MAX
    from their defaults (10), pass --ncells-max and --wifi-aps-max accordingly.
"""

import argparse
import fcntl
import json
import re
import struct
import sys
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import serial
import requests


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

GROUND_FIX_URL = "https://api.nrfcloud.com/v1/location/ground-fix"

# location_msg_type enum values
LOCATION_CLOUD_REQUEST = 3
LOCATION_GNSS_DATA = 5

# power_msg_type enum values
POWER_BATTERY_PERCENTAGE_SAMPLE_RESPONSE = 1

# environmental_msg_type enum values
ENVIRONMENTAL_SENSOR_SAMPLE_RESPONSE = 1

# ---------------------------------------------------------------------------
# Struct format strings (little-endian, '<')
#
# struct power_msg (40 bytes):
#   int32  type         (4b)
#   4x     padding      (4b, to 8-byte-align next double)
#   double percentage   (8b)
#   bool   charging     (1b, uint8)
#   7x     padding      (7b, to 8-byte-align next double)
#   double voltage      (8b)
#   int64  timestamp    (8b)
#
FMT_POWER_MSG = "<i4xd?7xdq"
SZ_POWER_MSG = struct.calcsize(FMT_POWER_MSG)  # 40

#
# struct environmental_msg (40 bytes):
#   int32  type         (4b)
#   4x     padding      (4b)
#   double temperature  (8b)
#   double humidity     (8b)
#   double pressure     (8b)
#   int64  timestamp    (8b)
#
FMT_ENV_MSG = "<i4xdddq"
SZ_ENV_MSG = struct.calcsize(FMT_ENV_MSG)  # 40

#
# struct location_cell_info (28 bytes):
#   uint32 id            (4b) - E-UTRAN cell ID (eci)
#   int32  mcc           (4b)
#   int32  mnc           (4b)
#   uint32 tac           (4b)
#   uint16 timing_advance(2b)
#   2x     padding       (2b)
#   uint32 earfcn        (4b)
#   int16  rsrp          (2b)
#   int16  rsrq          (2b)
#
FMT_CELL_INFO = "<IiiIH2xIhh"
SZ_CELL_INFO = struct.calcsize(FMT_CELL_INFO)  # 28

#
# struct location_neighbor_cell_info (16 bytes, incl. 2b trailing pad):
#   uint32 earfcn       (4b)
#   int32  time_diff    (4b)
#   uint16 phys_cell_id (2b) - pci
#   int16  rsrp         (2b)
#   int16  rsrq         (2b)
#   2x     padding      (2b, to keep array elements 4-byte aligned)
#
FMT_NEIGHBOR_CELL = "<IiHhh2x"
SZ_NEIGHBOR_CELL = struct.calcsize(FMT_NEIGHBOR_CELL)  # 16

#
# struct location_wifi_ap_info (8 bytes):
#   int8   rssi        (1b)
#   uint8  mac[6]      (6b)
#   uint8  mac_length  (1b)
#
FMT_WIFI_AP = "<b6sB"
SZ_WIFI_AP = struct.calcsize(FMT_WIFI_AP)  # 8


# ---------------------------------------------------------------------------
# Decoded record dataclass
# ---------------------------------------------------------------------------

@dataclass
class StorageRecord:
    storage_type: str          # "BATTERY", "LOCATION", "ENVIRONMENTAL", ...
    index: int                 # absolute ring-buffer index
    status: str                # "sent" or "pending"
    raw_hex: str               # full hex bytes as string
    decoded: Optional[Dict[str, Any]] = None
    location: Optional[Dict[str, Any]] = None   # ground-fix result


# ---------------------------------------------------------------------------
# Struct decoders
# ---------------------------------------------------------------------------

def _decode_cell_info(data: bytes, offset: int) -> Dict[str, Any]:
    if offset + SZ_CELL_INFO > len(data):
        return {}
    eid, mcc, mnc, tac, tadv, earfcn, rsrp, rsrq = struct.unpack_from(
        FMT_CELL_INFO, data, offset
    )
    return {
        "eci": eid,
        "mcc": mcc,
        "mnc": mnc,
        "tac": tac,
        "timing_advance": tadv,
        "earfcn": earfcn,
        "rsrp": rsrp,
        "rsrq": rsrq,
    }


def _decode_neighbor_cell(data: bytes, offset: int) -> Dict[str, Any]:
    if offset + SZ_NEIGHBOR_CELL > len(data):
        return {}
    earfcn, time_diff, pci, rsrp, rsrq = struct.unpack_from(
        FMT_NEIGHBOR_CELL, data, offset
    )
    return {
        "earfcn": earfcn,
        "time_diff": time_diff,
        "pci": pci,
        "rsrp": rsrp,
        "rsrq": rsrq,
    }


def _decode_wifi_ap(data: bytes, offset: int) -> Dict[str, Any]:
    if offset + SZ_WIFI_AP > len(data):
        return {}
    rssi, mac_bytes, mac_len = struct.unpack_from(FMT_WIFI_AP, data, offset)
    mac_str = ":".join(f"{b:02x}" for b in mac_bytes[:mac_len])
    return {
        "macAddress": mac_str,
        "signalStrength": rssi,
    }


def decode_battery(data: bytes) -> Dict[str, Any]:
    if len(data) < SZ_POWER_MSG:
        return {"error": f"too short: {len(data)} < {SZ_POWER_MSG}"}
    msg_type, pct, charging, voltage, ts = struct.unpack_from(FMT_POWER_MSG, data)
    return {
        "msg_type": (
            "POWER_BATTERY_PERCENTAGE_SAMPLE_RESPONSE"
            if msg_type == POWER_BATTERY_PERCENTAGE_SAMPLE_RESPONSE
            else f"unknown({msg_type})"
        ),
        "percentage": pct,
        "charging": bool(charging),
        "voltage": voltage,
        "timestamp_ms": ts,
    }


def decode_environmental(data: bytes) -> Dict[str, Any]:
    if len(data) < SZ_ENV_MSG:
        return {"error": f"too short: {len(data)} < {SZ_ENV_MSG}"}
    msg_type, temp, hum, pres, ts = struct.unpack_from(FMT_ENV_MSG, data)
    return {
        "msg_type": (
            "ENVIRONMENTAL_SENSOR_SAMPLE_RESPONSE"
            if msg_type == ENVIRONMENTAL_SENSOR_SAMPLE_RESPONSE
            else f"unknown({msg_type})"
        ),
        "temperature_celsius": temp,
        "humidity_percent": hum,
        "pressure_pa": pres,
        "timestamp_ms": ts,
    }


def decode_location(
    data: bytes,
    ncells_max: int = 10,
    wifi_aps_max: int = 10,
) -> Dict[str, Any]:
    """
    Decode a struct location_msg.

    The union member is selected by the first 4 bytes (enum location_msg_type).
    The union is 8-byte aligned, so there are 4 bytes padding after the type field.
    Handles LOCATION_CLOUD_REQUEST (type=3) and LOCATION_GNSS_DATA (type=5).

    Offsets for LOCATION_CLOUD_REQUEST with ncells_max=8 (CONFIG_APP_LOCATION_NEIGHBOR_CELLS_MAX),
    wifi_aps_max=10 (CONFIG_APP_LOCATION_WIFI_APS_MAX), default ABI:
      current_cell  :  +8   (28 bytes, struct location_cell_info)
      ncells_count  : +36   (uint8, +3 pad)
      neighbor_cells: +40   (ncells_max * 16 bytes = 128)
      gci_count     : +168  (uint8, +3 pad)
      gci_cells     : +172  (ncells_max * 28 bytes = 224)
      wifi_cnt      : +396  (uint16)
      wifi_aps      : +398  (wifi_aps_max * 8 bytes = 80)
    """
    if len(data) < 8:
        return {"error": f"too short: {len(data)}"}

    msg_type = struct.unpack_from("<i", data, 0)[0]

    if msg_type == LOCATION_GNSS_DATA:
        if len(data) < 8 + 20:
            return {"msg_type": "LOCATION_GNSS_DATA", "error": "data too short"}
        lat, lon, acc = struct.unpack_from("<ddf", data, 8)
        return {
            "msg_type": "LOCATION_GNSS_DATA",
            "latitude": lat,
            "longitude": lon,
            "accuracy_m": acc,
        }

    if msg_type == LOCATION_CLOUD_REQUEST:
        off_current_cell = 8
        off_ncells_count = off_current_cell + SZ_CELL_INFO       # 36
        off_neighbor_cells = off_ncells_count + 1 + 3             # 40 (uint8 + 3b pad)
        off_gci_count = off_neighbor_cells + ncells_max * SZ_NEIGHBOR_CELL
        off_gci_cells = off_gci_count + 1 + 3                     # uint8 + 3b pad
        off_wifi_cnt = off_gci_cells + ncells_max * SZ_CELL_INFO
        off_wifi_aps = off_wifi_cnt + 2                            # uint16

        current_cell = _decode_cell_info(data, off_current_cell)

        ncells_count = data[off_ncells_count] if off_ncells_count < len(data) else 0
        neighbor_cells = [
            _decode_neighbor_cell(data, off_neighbor_cells + i * SZ_NEIGHBOR_CELL)
            for i in range(min(ncells_count, ncells_max))
        ]

        gci_count = data[off_gci_count] if off_gci_count < len(data) else 0
        gci_cells = [
            _decode_cell_info(data, off_gci_cells + i * SZ_CELL_INFO)
            for i in range(min(gci_count, ncells_max))
        ]

        wifi_cnt = (
            struct.unpack_from("<H", data, off_wifi_cnt)[0]
            if off_wifi_cnt + 2 <= len(data) else 0
        )
        wifi_aps = [
            _decode_wifi_ap(data, off_wifi_aps + i * SZ_WIFI_AP)
            for i in range(min(wifi_cnt, wifi_aps_max))
        ]

        return {
            "msg_type": "LOCATION_CLOUD_REQUEST",
            "current_cell": current_cell,
            "ncells_count": ncells_count,
            "neighbor_cells": neighbor_cells,
            "gci_cells_count": gci_count,
            "gci_cells": gci_cells,
            "wifi_cnt": wifi_cnt,
            "wifi_aps": wifi_aps,
        }

    return {"msg_type": f"unknown({msg_type})", "raw": data.hex()}


def decode_record(
    storage_type: str,
    data: bytes,
    ncells_max: int,
    wifi_aps_max: int,
) -> Optional[Dict[str, Any]]:
    t = storage_type.upper()
    if t == "BATTERY":
        return decode_battery(data)
    if t == "ENVIRONMENTAL":
        return decode_environmental(data)
    if t == "LOCATION":
        return decode_location(data, ncells_max=ncells_max, wifi_aps_max=wifi_aps_max)
    return {"raw": data.hex()}


# ---------------------------------------------------------------------------
# Ground-fix helpers: Zephyr RSRP/RSRQ encoding → dBm/dB
# ---------------------------------------------------------------------------

# Zephyr stores LTE RSRP/RSRQ as encoded uint8 offsets (see lte_lc.h).
# The nRF Cloud ground-fix API expects actual dBm / dB values.
# RSRP: raw 0-97 → dBm = raw - 140   (e.g. 41 → -99 dBm)
# RSRQ: raw 0-34 → dB  = raw/2 - 19.5 (e.g. 20 → -9.5 dB)

# Zephyr sentinel for "timing advance not measured"
_LTE_TA_INVALID = 65535


def _rsrp_to_dbm(raw: int) -> Optional[float]:
    """Convert Zephyr-encoded RSRP (0-97) to dBm. Returns None if invalid."""
    if not raw or raw <= 0:
        return None
    return float(raw - 140)


def _rsrq_to_db(raw: int) -> Optional[float]:
    """Convert Zephyr-encoded RSRQ (0-34) to dB. Returns None if invalid."""
    if not raw or raw <= 0:
        return None
    return round(raw / 2.0 - 19.5, 1)


def _nmr_entry(nc: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Build a single LteNeighborMeasurement from a decoded neighbor_cell dict.
    earfcn and pci are required by the API; returns None if either is missing.
    """
    earfcn = nc.get("earfcn")
    pci = nc.get("pci")
    if not earfcn or pci is None:
        return None
    entry: Dict[str, Any] = {"earfcn": earfcn, "pci": pci}
    rsrp = _rsrp_to_dbm(nc.get("rsrp") or 0)
    rsrq = _rsrq_to_db(nc.get("rsrq") or 0)
    if rsrp is not None:
        entry["rsrp"] = rsrp
    if rsrq is not None:
        entry["rsrq"] = rsrq
    if nc.get("time_diff"):
        entry["timeDiff"] = nc["time_diff"]
    return entry


def _cell_to_lte_entry(
    cell: Dict[str, Any], nmr: Optional[List[Dict]] = None
) -> Dict[str, Any]:
    """Convert a decoded location_cell_info dict to an nRF Cloud LTE cell object.
    RSRP/RSRQ are converted from Zephyr-encoded values to dBm/dB.
    """
    entry: Dict[str, Any] = {
        "mcc": cell["mcc"],
        "mnc": cell["mnc"],
        "eci": cell["eci"],
        "tac": cell["tac"],
    }
    if cell.get("earfcn"):
        entry["earfcn"] = cell["earfcn"]
    rsrp = _rsrp_to_dbm(cell.get("rsrp") or 0)
    rsrq = _rsrq_to_db(cell.get("rsrq") or 0)
    if rsrp is not None:
        entry["rsrp"] = rsrp
    if rsrq is not None:
        entry["rsrq"] = rsrq
    tadv = cell.get("timing_advance", _LTE_TA_INVALID)
    if tadv and tadv != _LTE_TA_INVALID:
        entry["adv"] = tadv
    if nmr:
        entry["nmr"] = nmr
    return entry


# ---------------------------------------------------------------------------
# Ground-fix REST API
# ---------------------------------------------------------------------------

def call_ground_fix(decoded: Dict[str, Any], api_key: str, debug: bool = False) -> Optional[Dict[str, Any]]:
    """Resolve a LOCATION_CLOUD_REQUEST record via POST /v1/location/ground-fix."""
    if decoded.get("msg_type") != "LOCATION_CLOUD_REQUEST":
        return None

    # Neighbor cells go in nmr[] inside the serving cell entry
    nmr = [e for nc in decoded.get("neighbor_cells", []) if (e := _nmr_entry(nc))]

    lte_cells = []
    current = decoded.get("current_cell", {})
    if current.get("eci"):
        lte_cells.append(_cell_to_lte_entry(current, nmr=nmr or None))
    for cell in decoded.get("gci_cells", []):
        if cell.get("eci"):
            lte_cells.append(_cell_to_lte_entry(cell))

    wifi_aps = [
        {"macAddress": ap["macAddress"], "signalStrength": ap["signalStrength"]}
        for ap in decoded.get("wifi_aps", [])
        if ap.get("macAddress")
    ]

    body: Dict[str, Any] = {}
    if lte_cells:
        body["lte"] = lte_cells
    if wifi_aps:
        body["wifi"] = wifi_aps

    if not body:
        print("  [rest] No LTE cells or Wi-Fi APs to send, skipping.", file=sys.stderr)
        return None

    if debug:
        print(f"  [rest] POST body:\n{json.dumps(body, indent=2)}", file=sys.stderr)

    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
    }

    try:
        resp = requests.post(GROUND_FIX_URL, json=body, headers=headers, timeout=15)
        resp.raise_for_status()
        return resp.json()
    except requests.exceptions.HTTPError:
        print(f"  [rest] HTTP {resp.status_code}: {resp.text}", file=sys.stderr)
    except requests.exceptions.RequestException as e:
        print(f"  [rest] Request failed: {e}", file=sys.stderr)
    return None


# ---------------------------------------------------------------------------
# Serial / shell I/O
# ---------------------------------------------------------------------------

def _send_command(ser: serial.Serial, cmd: str, char_delay: float = 0.005) -> None:
    """Send a shell command robustly over USB CDC-ACM.

    USB CDC-ACM on Zephyr has a small TX ring buffer. Sending a burst of
    characters at once can overflow it, causing silent character drops that
    result in garbled commands (e.g. 'att_storagedup').  Sending one byte
    at a time with a short inter-character delay lets the device's USB IN
    endpoint drain between bytes, eliminating drops.

    Args:
        ser:        Open serial port.
        cmd:        Command string (without newline).
        char_delay: Seconds between each character (default: 12 ms).
    """
    for ch in cmd:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(char_delay)
    ser.write(b"\r\n")
    ser.flush()


def _send_command_verified(
    ser: serial.Serial,
    cmd: str,
    retries: int = 3,
    char_delay: float = 0.012,
) -> bool:
    """Send a command and verify the shell echoes it back intact.

    The Zephyr shell echoes each character as it is typed.  After sending
    the full command + CR, we read back the echoed line and compare it to
    the command we sent.  If there is a mismatch (dropped chars) we clear
    the input buffer and retry up to `retries` times.

    Returns True if the command was confirmed, False after all retries fail.
    """
    for attempt in range(1, retries + 1):
        ser.reset_input_buffer()
        _send_command(ser, cmd, char_delay=char_delay)

        # Collect the echo: read until we see a newline or timeout (1 s)
        echo = b""
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            try:
                ch = ser.read(1)
            except serial.SerialException:
                # Device disconnected or port error — treat as mismatch
                ch = b""
            if not ch:
                continue
            if ch in (b"\r", b"\n"):
                break
            echo += ch

        # Strip ANSI escape codes from echo before comparing
        echo_str = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", echo.decode("utf-8", errors="replace")).strip()

        if echo_str == cmd:
            return True

        print(
            f"\n  Command echo mismatch (attempt {attempt}/{retries}): "
            f"sent={cmd!r} got={echo_str!r} — retrying...",
            file=sys.stderr,
        )
        # Give the shell a moment to settle before next attempt
        time.sleep(0.2)
        ser.reset_input_buffer()

    return False


def _collect_dump_output(ser: serial.Serial, timeout_s: float = 30.0) -> List[str]:
    """
    Read lines until the dump end-marker '===================' is seen or no new
    line arrives within timeout_s seconds (idle timeout, reset on each received line).
    Returns lines from (and including) '=== Storage Dump ===' onward.
    Zephyr log lines ([HH:MM:SS.mmm,uuu] prefix) and shell prompts are filtered.

    Prints a live overwriting progress line:
        Reading BATTERY  2/5
    """
    lines: List[str] = []
    in_dump = False
    deadline = time.monotonic() + timeout_s

    # Live progress state
    current_type: str = ""
    current_total: int = 0
    current_record: int = 0
    progress_active: bool = False

    def _show_progress(label: str, n: int, total: int) -> None:
        msg = f"  Reading {label:<16} {n}/{total}"
        # Pad to overwrite any longer previous line
        print(f"\r{msg:<50}", end="", flush=True)

    def _end_progress() -> None:
        if progress_active:
            print()  # newline after last overwrite

    while time.monotonic() < deadline:
        try:
            raw = ser.readline()
        except serial.SerialException as e:
            _end_progress()
            err = str(e).lower()
            if "errno 6" in err or "device not configured" in err:
                print(
                    "\nDevice USB connection dropped mid-dump (errno 6). "
                    "This can happen when the device cannot keep up with the TX rate. "
                    "Rebuild firmware with the latest changes (k_sleep between records) "
                    "and re-run the script.",
                    file=sys.stderr,
                )
            elif "no data" in err or "disconnected" in err or "multiple access" in err:
                print("Device is busy or disconnected (port in use by another process).",
                      file=sys.stderr)
            else:
                print(f"Serial error while reading: {e}", file=sys.stderr)
            sys.exit(1)

        if not raw:
            continue

        # A line arrived — reset the idle deadline
        deadline = time.monotonic() + timeout_s

        try:
            line = raw.decode("utf-8", errors="replace")
        except Exception:
            continue

        # Strip ANSI escape codes
        line = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", line)
        line = line.rstrip("\r\n").rstrip()

        # Skip Zephyr log lines: [HH:MM:SS.mmm,uuu] <level> module: ...
        if re.match(r"^\[[\d:.,]+\]", line):
            continue
        # Skip empty lines and shell prompts
        if not line or re.match(r"^uart:~\$|^~\$", line):
            continue
        # Detect and warn about Zephyr dropped-message notices
        m_drop = re.match(r"^---\s+(\d+)\s+messages? dropped\s+---", line)
        if m_drop:
            _end_progress()
            print(f"WARNING: {m_drop.group(1)} log messages were dropped by the device "
                  "during the dump. Increase CONFIG_LOG_BUFFER_SIZE in prj.conf "
                  "if records appear truncated.", file=sys.stderr)
            progress_active = False  # progress already closed by _end_progress
            continue

        if "=== Storage Dump ===" in line:
            in_dump = True

        if in_dump:
            lines.append(line)

            # Detect type header:  "Type: BATTERY   total=5  ..."
            m = re.search(
                r"Type:\s+(\S+)\s+total=(\d+)", line
            )
            if m:
                _end_progress()
                current_type = m.group(1)
                current_total = int(m.group(2))
                current_record = 0
                progress_active = current_total > 0
                if progress_active:
                    _show_progress(current_type, 0, current_total)

            # Detect record header:  "  [5] status=sent | 40 bytes:"
            elif re.search(r"\[\s*\d+\]\s+status=", line):
                current_record += 1
                if progress_active:
                    _show_progress(current_type, current_record, current_total)

            # End marker
            elif line.strip() == "===================":
                _end_progress()
                progress_active = False
                break

    else:
        # Timeout — close out any open progress line
        _end_progress()
        print(
            f"\nWARNING: No data received for {timeout_s:.0f}s — dump timed out. "
            "Output may be incomplete. "
            "Use --timeout to increase the idle limit (e.g. --timeout 10).",
            file=sys.stderr,
        )

    return lines


# ---------------------------------------------------------------------------
# Dump output parser
# ---------------------------------------------------------------------------

# "Type: BATTERY          total=3    pending=1    read_offset=2        write_offset=3"
_RE_TYPE_HDR = re.compile(
    r"Type:\s+(\S+)\s+total=(\d+)\s+pending=(\d+)\s+read_offset=(\d+)\s+write_offset=(\d+)"
)
# "  [5] status=sent    | 40 bytes:"
_RE_RECORD_HDR = re.compile(
    r"\[\s*(\d+)\]\s+status=(\S+)\s*\|\s*(\d+)\s+bytes:"
)
# "    0000: 9a 99 39 ..."
_RE_HEX_ROW = re.compile(r"([0-9a-fA-F]{4}):\s+([0-9a-fA-F ]+)")


def parse_dump_lines(lines: List[str]) -> List[StorageRecord]:
    records: List[StorageRecord] = []
    current_type: Optional[str] = None
    current_record: Optional[StorageRecord] = None
    current_bytes: bytearray = bytearray()

    def _commit() -> None:
        nonlocal current_record, current_bytes
        if current_record is not None:
            current_record.raw_hex = current_bytes.hex()
            records.append(current_record)
        current_record = None
        current_bytes = bytearray()

    for line in lines:
        m = _RE_TYPE_HDR.search(line)
        if m:
            _commit()
            current_type = m.group(1)
            continue

        if current_type is None or "(empty)" in line:
            continue

        m = _RE_RECORD_HDR.search(line)
        if m:
            _commit()
            current_record = StorageRecord(
                storage_type=current_type,
                index=int(m.group(1)),
                status=m.group(2).strip(),
                raw_hex="",
            )
            current_bytes = bytearray()
            continue

        if current_record is not None:
            m = _RE_HEX_ROW.search(line)
            if m:
                current_bytes.extend(bytes.fromhex(m.group(2).replace(" ", "")))

    _commit()
    return records


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Dump and decode Asset Tracker Template LittleFS storage over serial shell."
    )
    parser.add_argument("--port", required=True,
                        help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    parser.add_argument("--baud", type=int, default=1000000,
                        help="Baud rate (default: 1000000)")
    parser.add_argument("--output", default="storage_dump.json",
                        help="Output JSON file (default: storage_dump.json)")
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="Idle timeout: seconds since last received line before giving up (default: 10)")
    parser.add_argument("--location-requests", action="store_true",
                        help="Resolve LOCATION_CLOUD_REQUEST records via nRF Cloud ground-fix API")
    parser.add_argument("--jwt",
                        help="nRF Cloud JWT token (required with --location-requests)")
    parser.add_argument("--debug", action="store_true",
                        help="Print the ground-fix request body for each record")
    parser.add_argument("--ncells-max", type=int, default=8,
                        help="CONFIG_APP_LOCATION_NEIGHBOR_CELLS_MAX (default: 8, matches prj.conf)")
    parser.add_argument("--wifi-aps-max", type=int, default=10,
                        help="CONFIG_APP_LOCATION_WIFI_APS_MAX (default: 10, matches prj.conf)")
    parser.add_argument("--pending-only", action="store_true",
                        help="Only include records with status=pending in the output")

    args = parser.parse_args()

    if args.location_requests and not args.jwt:
        parser.error("--location-requests requires --jwt")

    # Resolve bearer token from the provided JWT.
    bearer_token: Optional[str] = None
    if args.jwt:
        bearer_token = args.jwt

    # --- Connect ---
    print(f"Connecting to {args.port} at {args.baud} baud...")
    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=0.5,
            xonxoff=False,
            rtscts=False,
        )
    except serial.SerialException as e:
        print(f"Failed to open serial port: {e}", file=sys.stderr)
        sys.exit(1)

    # Attempt an exclusive lock on the port fd. If another program (terminal
    # emulator, nRF Connect Serial Monitor, etc.) already has the port open,
    # LOCK_EX | LOCK_NB will fail immediately with BlockingIOError so we can
    # exit cleanly before sending any data.
    try:
        fcntl.flock(ser.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except (BlockingIOError, OSError):
        ser.close()
        print(
            f"Port {args.port} is already open by another process.\n"
            "Close any terminal emulator or Serial Monitor connected to "
            "that port and try again.",
            file=sys.stderr,
        )
        sys.exit(1)

    # Check the port is actually readable before committing to the dump
    try:
        ser.reset_input_buffer()
        # A 0-byte read with a short timeout confirms the port is alive
        ser.timeout = 0.2
        ser.read(0)
        ser.timeout = 0.5
    except serial.SerialException as e:
        ser.close()
        err = str(e).lower()
        if "no data" in err or "disconnected" in err or "multiple access" in err:
            print("Device is busy or disconnected (port in use by another process).",
                  file=sys.stderr)
        else:
            print(f"Port not accessible: {e}", file=sys.stderr)
        sys.exit(1)

    # Give the shell a moment to be ready, then send the command
    time.sleep(0.3)
    ser.reset_input_buffer()

    # --- Shell readiness check ---
    # Send an empty newline and wait for a shell prompt to confirm the shell
    # is idle and not busy processing another command or flooded with log output.
    print("Checking shell readiness...", end="", flush=True)
    _send_command(ser, "")
    shell_ready = False
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        try:
            line = raw.decode("utf-8", errors="replace").strip()
        except Exception:
            continue
        # Strip ANSI escape codes before matching
        line = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", line)
        if re.search(r"uart:~\$|~\$", line):
            shell_ready = True
            break
    if not shell_ready:
        ser.close()
        print(
            "\nShell did not respond within 3s — the UART may be busy with log output "
            "or another command is still running.\n"
            "Try:\n"
            "  1. Wait for the current operation to finish.\n"
            "  2. Reduce log verbosity (e.g. set CONFIG_LOG_DEFAULT_LEVEL=0 in prj.conf).\n"
            "  3. Close any other terminal emulator connected to the same port.",
            file=sys.stderr,
        )
        sys.exit(1)
    print(" ready.")
    ser.reset_input_buffer()

    print("Sending 'att_storage dump'...")
    if not _send_command_verified(ser, "att_storage dump"):
        ser.close()
        print(
            "Failed to send 'att_storage dump' without character drops after 3 attempts.\n"
            "Try increasing --baud or reducing USB load on the host.",
            file=sys.stderr,
        )
        sys.exit(1)

    lines = _collect_dump_output(ser, timeout_s=args.timeout)
    ser.close()

    if not lines:
        print(
            "No dump output received. Check the port, baud rate, and that the "
            "device firmware has CONFIG_APP_STORAGE_SHELL_DUMP=y.",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"Received {len(lines)} lines of dump output.")

    # --- Parse ---
    records = parse_dump_lines(lines)

    if args.pending_only:
        records = [r for r in records if r.status == "pending"]

    print(f"Parsed {len(records)} record(s).")

    # --- Decode binary payloads ---
    for rec in records:
        if rec.raw_hex:
            rec.decoded = decode_record(
                rec.storage_type,
                bytes.fromhex(rec.raw_hex),
                args.ncells_max,
                args.wifi_aps_max,
            )

    # --- Location resolution ---
    if args.location_requests:
        cloud_reqs = [
            r for r in records
            if r.decoded and r.decoded.get("msg_type") == "LOCATION_CLOUD_REQUEST"
        ]
        print(f"Resolving {len(cloud_reqs)} LOCATION_CLOUD_REQUEST record(s)...")
        for rec in cloud_reqs:
            print(f"  Record [{rec.index}] ({rec.status})...", end="", flush=True)
            result = call_ground_fix(rec.decoded, bearer_token, debug=args.debug)
            if result:
                rec.location = result
                print(
                    f" lat={result.get('lat')}, lon={result.get('lon')}, "
                    f"uncertainty={result.get('uncertainty')}m, "
                    f"method={result.get('fulfilledWith')}"
                )
            else:
                print(" no location resolved.")

    # --- Write output ---
    output = []
    for rec in records:
        entry: Dict[str, Any] = {
            "storage_type": rec.storage_type,
            "index": rec.index,
            "status": rec.status,
            "raw_hex": rec.raw_hex,
        }
        if rec.decoded is not None:
            decoded = dict(rec.decoded)
            # Embed the resolved location directly into the decoded object so
            # the request data and its result are co-located in the output.
            if rec.location is not None:
                decoded["resolved_location"] = {
                    "lat": rec.location.get("lat"),
                    "lon": rec.location.get("lon"),
                    "uncertainty_m": rec.location.get("uncertainty"),
                    "method": rec.location.get("fulfilledWith"),
                }
            entry["decoded"] = decoded
        output.append(entry)

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)

    print(f"Wrote {len(output)} record(s) to '{args.output}'.")


if __name__ == "__main__":
    main()
