#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Generate nRF Cloud TLS credentials, onboard the device, and optionally load them into the modem.

Creates a P-256 key pair and a self-signed X.509 device certificate (CN = device ID), downloads
Amazon Root CA 1, POSTs the certificate to nRF Cloud (OnboardDevice), and writes CA / client cert /
private key to security tag 16842753 when --serial is given.

The positional argument is a name prefix: the script reads the modem IMEI over the AT UART and
builds the device ID as ``<name>-<imei>``. Pass a full ``<name>-<15-digit-imei>`` to skip the IMEI
read and use the value verbatim.

Credential writes use native ``AT%CMNG`` (PEM in quotes with real newlines), which only the
dedicated **AT client** firmware accepts. In name-prefix mode, when ``--serial`` is given and that
UART does not answer a plain ``AT`` (for example the application's Zephyr AT shell answers instead,
or nothing does), the script flashes the AT client with ``nrfutil`` and retries. Pass
``--at-client-fw <hex>`` to point at the image, ``--skip-at-client`` to assume it is already
present, or ``--serial-number`` to pick a board when several are attached. If the AT client is not
available (for example in full ``<name>-<imei>`` mode), install the PEMs with the **Cellular
Monitor Certificate Manager** instead.

Prerequisites:
  - openssl in PATH
  - NRF_CLOUD_API_KEY or NRFCLOUD_API_KEY for REST onboarding
  - pyserial for --serial (``pip install pyserial``)
  - nrfutil (with the ``device`` command) to flash the AT client when missing

Usage:
  python3 scripts/nrf_cloud_provision.py nrf --serial /dev/ttyACM0 --at-client-fw at_client.hex
  python3 scripts/nrf_cloud_provision.py nrf --serial /dev/ttyACM0 --skip-at-client
  python3 scripts/nrf_cloud_provision.py nrf-352656106119210 -o ./my_creds --skip-onboard
  python3 scripts/nrf_cloud_provision.py nrf-352656106119210 --serial /dev/ttyACM0

  Re-onboard after deleting the device in nRF Cloud (same PEMs, no new keypair)::

  python3 scripts/nrf_cloud_provision.py nrf-352656106119210 --reuse-existing-creds
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import re
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Literal, Optional

API_URL = "https://api.nrfcloud.com/v1"
AMAZON_ROOT_CA_URL = "https://www.amazontrust.com/repository/AmazonRootCA1.pem"
NRF_CLOUD_COAP_CA_PEM = """-----BEGIN CERTIFICATE-----
MIIBjzCCATagAwIBAgIUOEakGUS/7BfSlprkly7UK43ZAwowCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJblJGIENsb3VkMB4XDTIzMDUyNDEyMzUzMloXDTQ4MTIzMDEy
MzUzMlowFDESMBAGA1UEAwwJblJGIENsb3VkMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAEPVmJXT4TA1ljMcbPH0hxlzMDiPX73FHsdGM/6mqAwq9m2Nunr5/gTQQF
MBUZJaQ/rUycLmrT8i+NZ0f/OzoFsKNmMGQwHQYDVR0OBBYEFGusC7QaV825v0Ci
qEv2m1HhiScSMB8GA1UdIwQYMBaAFGusC7QaV825v0CiqEv2m1HhiScSMBIGA1Ud
EwEB/wQIMAYBAf8CAQAwDgYDVR0PAQH/BAQDAgGGMAoGCCqGSM49BAMCA0cAMEQC
IH/C3yf5aNFSFlm44CoP5P8L9aW/5woNrzN/kU5I+H38AiAwiHYlPclp25LgY8e2
n7e2W/H1LXJ7S3ENDBwKUF4qyw==
-----END CERTIFICATE-----
"""
DEFAULT_SEC_TAG = 16842753
CMNG_CHUNK_SIZE = 16
CMNG_CHUNK_DELAY_S = 0.1
AT_PROBE_TIMEOUT_S = 2.0
DEFAULT_BOOT_DELAY_S = 2.0
# A full nRF Cloud device ID ends in the 15-digit modem IMEI.
DEVICE_ID_RE = re.compile(r"-\d{15}$")

logging.basicConfig(
	level=logging.INFO,
	format="%(asctime)s %(levelname)s %(message)s",
	datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)

AtMode = Literal["auto", "direct", "shell"]


def nrf_cloud_api_key() -> Optional[str]:
	return os.environ.get("NRF_CLOUD_API_KEY") or os.environ.get("NRFCLOUD_API_KEY")


def run_openssl(args: list[str], cwd: Path) -> None:
	r = subprocess.run(
		["openssl", *args],
		cwd=str(cwd),
		capture_output=True,
		text=True,
		check=False,
	)
	if r.returncode != 0:
		log.error("openssl stderr: %s", r.stderr.strip())
		raise RuntimeError(f"openssl failed (exit {r.returncode})")


def generate_credentials(output_dir: Path, device_id: str) -> None:
	output_dir.mkdir(parents=True, exist_ok=True)
	od = output_dir.resolve()
	key_path = od / "device.key"
	crt_path = od / "device.crt"
	# Use PKCS#8 PEM ("BEGIN PRIVATE KEY") so CONFIG_NRF_CLOUD_JWT_SOURCE_CUSTOM
	# can parse the key from sec tag via tls_credential_get().
	run_openssl(
		[
			"genpkey",
			"-algorithm",
			"EC",
			"-pkeyopt",
			"ec_paramgen_curve:prime256v1",
			"-out",
			"device.key",
		],
		od,
	)
	run_openssl(
		[
			"req",
			"-new",
			"-x509",
			"-key",
			"device.key",
			"-out",
			"device.crt",
			"-days",
			"3650",
			"-subj",
			f"/CN={device_id}",
		],
		od,
	)
	run_openssl(["pkey", "-in", "device.key", "-pubout", "-out", "device_public.pem"], od)
	os.chmod(key_path, 0o600)
	log.info("Wrote %s and %s", key_path, crt_path)


def extract_private_key_hex(key_path: Path) -> str:
	"""Return the 32-byte P-256 private key scalar as a 64-char hex string.

	Used for CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX (bring-up JWT signing on targets
	whose modem cannot export the key it stores).
	"""
	r = subprocess.run(
		["openssl", "ec", "-in", str(key_path), "-noout", "-text"],
		capture_output=True,
		text=True,
		check=False,
	)
	if r.returncode != 0:
		raise RuntimeError(f"openssl ec failed (exit {r.returncode}): {r.stderr.strip()}")

	m = re.search(r"priv:\s*\n((?:\s+[0-9a-f:]+\n)+)", r.stdout)
	if not m:
		raise RuntimeError("Could not find private key scalar in openssl output")

	hex_str = re.sub(r"[^0-9a-f]", "", m.group(1))
	# ASN.1 pads with a leading 00 byte when the scalar's top bit is set.
	if len(hex_str) == 66 and hex_str.startswith("00"):
		hex_str = hex_str[2:]
	if len(hex_str) != 64:
		raise RuntimeError(f"Unexpected private key scalar length: {len(hex_str)} hex chars")

	return hex_str


def _urlopen(req: urllib.request.Request, timeout_s: float = 60):
	ctx = ssl.create_default_context()
	return urllib.request.urlopen(req, timeout=timeout_s, context=ctx)


def prepare_ca_files(output_dir: Path, include_coap_ca: bool) -> str:
	dest = output_dir / "AmazonRootCA1.pem"
	req = urllib.request.Request(AMAZON_ROOT_CA_URL, method="GET")
	with _urlopen(req) as resp:
		aws_ca = resp.read().decode("utf-8")
	dest.write_text(aws_ca, encoding="utf-8")
	log.info("Wrote %s (%d bytes)", dest, len(aws_ca))
	coap_path = output_dir / "nRFCloudCoAPRootCA.pem"
	coap_path.write_text(NRF_CLOUD_COAP_CA_PEM, encoding="utf-8")
	log.info("Wrote %s (%d bytes)", coap_path, len(NRF_CLOUD_COAP_CA_PEM))
	ca_chain = aws_ca
	if include_coap_ca:
		ca_chain = aws_ca.strip() + "\n" + NRF_CLOUD_COAP_CA_PEM.strip() + "\n"
	chain_path = output_dir / "ca_chain.pem"
	chain_path.write_text(ca_chain, encoding="utf-8")
	log.info("Wrote %s (%d bytes)", chain_path, len(ca_chain))
	return ca_chain


def onboard_device(api_key: str, device_id: str, cert_pem: str) -> None:
	url = f"{API_URL}/devices/{device_id}"
	payload = json.dumps({"certificate": cert_pem}).encode("utf-8")
	req = urllib.request.Request(
		url,
		data=payload,
		method="POST",
		headers={
			"Authorization": f"Bearer {api_key}",
			"Content-Type": "application/json",
			"Accept": "application/json",
		},
	)
	try:
		with _urlopen(req) as resp:
			code = resp.getcode()
	except urllib.error.HTTPError as e:
		log.error("Onboard response: %s %s", e.code, e.read().decode("utf-8", errors="replace"))
		raise
	else:
		if code not in (200, 202):
			raise RuntimeError(f"Unexpected HTTP status {code}")
		log.info("OnboardDevice accepted (HTTP %s)", code)


def _wrap_at_line(mode: str, at_body: str) -> str:
	"""Return full UART line: native AT or Zephyr ``at AT`` forwarding."""
	at_body = at_body.rstrip("\r\n")
	if mode == "shell":
		if not at_body.startswith("AT"):
			raise RuntimeError(f"Expected AT command, got: {at_body[:20]!r}")
		return "at " + at_body + "\r\n"
	return at_body + "\r\n"


def _serial_write_raw(ser, data: bytes, chunked: bool) -> None:
	if chunked:
		for i in range(0, len(data), CMNG_CHUNK_SIZE):
			ser.write(data[i : i + CMNG_CHUNK_SIZE])
			time.sleep(CMNG_CHUNK_DELAY_S)
	else:
		ser.write(data)


def _read_until_ok(ser, timeout_s: float) -> bytes:
	deadline = time.monotonic() + timeout_s
	acc = b""
	while time.monotonic() < deadline:
		acc += ser.read(4096)
		if b"+CME ERROR" in acc or b"CME ERROR:" in acc:
			raise RuntimeError(f"Modem/shell error, tail: {acc[-400:]!r}")
		if b"\r\nERROR\r\n" in acc or b"\nERROR\n" in acc:
			raise RuntimeError(f"Modem/shell error, tail: {acc[-400:]!r}")
		# Shell may emit ``OK\r\r\n`` (double CR) before the Zephyr prompt.
		if b"\r\nOK" in acc or acc.endswith(b"OK\r\n") or acc.endswith(b"OK\n"):
			return acc
		time.sleep(0.02)
	raise RuntimeError(f"No OK within {timeout_s}s, tail: {acc[-400:]!r}")


def _detect_at_mode(ser) -> str:
	ser.reset_input_buffer()
	ser.reset_output_buffer()
	time.sleep(0.05)
	_serial_write_raw(ser, b"AT\r\n", chunked=False)
	time.sleep(0.25)
	r = ser.read(8192)
	if b"OK" in r:
		log.info("AT interface: direct modem (AT -> OK)")
		return "direct"
	ser.reset_input_buffer()
	_serial_write_raw(ser, b"at AT\r\n", chunked=False)
	time.sleep(0.25)
	r2 = ser.read(8192)
	if b"OK" in r2:
		log.info("AT interface: Zephyr AT shell (at AT -> OK)")
		return "shell"
	raise RuntimeError(
		"Could not detect AT framing: no OK for 'AT' or 'at AT'. "
		"Flash the AT client (or pass --at-client-fw <hex>), "
		"or try --at-mode direct on the modem USB AT port."
	)


def _read_response(ser, timeout_s: float) -> bytes:
	"""Collect serial bytes until OK/ERROR or timeout. Never raises."""
	deadline = time.monotonic() + timeout_s
	acc = b""
	while time.monotonic() < deadline:
		acc += ser.read(4096)
		if b"OK" in acc or b"ERROR" in acc:
			break
		time.sleep(0.02)
	return acc


def _probe_at(ser) -> Optional[str]:
	"""Return AT framing ('direct'/'shell') if an AT client answers, else None."""
	for mode, probe in (("direct", b"AT\r\n"), ("shell", b"at AT\r\n")):
		ser.reset_input_buffer()
		_serial_write_raw(ser, probe, chunked=False)
		if b"OK" in _read_response(ser, AT_PROBE_TIMEOUT_S):
			return mode
	return None


def read_imei(ser, mode: str, timeout_s: float = 5.0) -> str:
	"""Return the 15-digit IMEI reported by AT+CGSN."""
	line = _wrap_at_line(mode, "AT+CGSN")
	ser.reset_input_buffer()
	_serial_write_raw(ser, line.encode("ascii", errors="strict"), chunked=False)
	resp = _read_response(ser, timeout_s)
	m = re.search(rb"(\d{15})", resp)
	if not m:
		raise RuntimeError(f"Could not parse IMEI from AT+CGSN response: {resp[-200:]!r}")
	return m.group(1).decode("ascii")


def _nrfutil_devices() -> list:
	r = subprocess.run(
		["nrfutil", "device", "list", "--json"],
		capture_output=True,
		text=True,
		check=False,
	)
	if r.returncode != 0:
		raise RuntimeError(f"'nrfutil device list' failed: {r.stdout}\n{r.stderr}")
	for ln in r.stdout.splitlines():
		if not ln.startswith("{"):
			continue
		try:
			msg = json.loads(ln)
		except json.JSONDecodeError:
			continue
		if msg.get("type") == "info" and "devices" in msg.get("data", {}):
			return msg["data"]["devices"]
	return []


def _resolve_serial_number(explicit: Optional[str]) -> str:
	if explicit:
		return explicit
	devices = _nrfutil_devices()
	if len(devices) == 1:
		return devices[0]["serialNumber"]
	if not devices:
		raise RuntimeError("No devices found by 'nrfutil device list'.")
	serials = ", ".join(d.get("serialNumber", "?") for d in devices)
	raise RuntimeError(f"Multiple devices connected; pass --serial-number. Found: {serials}")


def flash_at_client(fw_path: Optional[Path], serial_number: str, boot_delay: float) -> None:
	"""Program the AT client hex with nrfutil and reset the device."""
	if not fw_path or not fw_path.is_file():
		raise RuntimeError(
			f"AT client firmware not found: {fw_path}. Pass --at-client-fw <hex>."
		)
	log.info("Programming AT client %s onto %s...", fw_path.name, serial_number)
	prog = subprocess.run(
		[
			"nrfutil", "device", "program",
			"--firmware", str(fw_path),
			"--serial-number", serial_number,
		],
		capture_output=True,
		text=True,
		check=False,
	)
	if prog.returncode != 0:
		raise RuntimeError(f"nrfutil program failed: {prog.stdout}\n{prog.stderr}")
	rst = subprocess.run(
		["nrfutil", "device", "reset", "--serial-number", serial_number],
		capture_output=True,
		text=True,
		check=False,
	)
	if rst.returncode != 0:
		raise RuntimeError(f"nrfutil reset failed: {rst.stdout}\n{rst.stderr}")
	time.sleep(boot_delay)


def ensure_at_client_and_read_imei(
	serial_port: str,
	baud: int,
	at_mode: AtMode,
	at_client_fw: Optional[Path],
	serial_number: Optional[str],
	skip_at_client: bool,
	boot_delay: float,
) -> str:
	"""Ensure an AT client answers on serial_port, flashing it if needed, and read the IMEI."""
	try:
		import serial  # type: ignore
	except ImportError as e:
		raise RuntimeError("Install pyserial: pip install pyserial") from e

	def open_ser():
		s = serial.Serial(serial_port, baudrate=baud, timeout=0.05)
		time.sleep(0.15)
		s.reset_input_buffer()
		return s

	ser = open_ser()
	try:
		mode = _probe_at(ser)
		# Only the dedicated AT client (native "AT" -> OK, i.e. direct) can carry the
		# multi-line AT%CMNG writes later; a Zephyr AT shell does not count as present.
		need_flash = mode != "direct" and not skip_at_client
		if need_flash:
			log.info("AT client (direct AT) not detected (mode=%s); flashing it...", mode)
			ser.close()
			sn = _resolve_serial_number(serial_number)
			flash_at_client(at_client_fw, sn, boot_delay)
			ser = open_ser()
			mode = _probe_at(ser)
		if mode is None:
			raise RuntimeError(
				f"No AT client responding on {serial_port}. "
				"Flash the AT client or pass --at-client-fw <hex>."
			)
		if at_mode != "auto":
			mode = at_mode
		imei = read_imei(ser, mode)
		log.info("Device IMEI: %s", imei)
		return imei
	finally:
		ser.close()


def _send_at_line(ser, mode: str, at_line: str, timeout_s: float, chunked: bool) -> None:
	line = _wrap_at_line(mode, at_line)
	_serial_write_raw(ser, line.encode("ascii", errors="strict"), chunked=chunked)
	_read_until_ok(ser, timeout_s)


def _send_at_bytes(ser, data: bytes, timeout_s: float, chunked: bool) -> None:
	_serial_write_raw(ser, data, chunked=chunked)
	_read_until_ok(ser, timeout_s)


def _cmng_direct_bytes(sec_tag: int, cred_type: int, pem: str) -> bytes:
	text = pem.strip("\n") + "\n"
	body = f'AT%CMNG=0,{sec_tag},{cred_type},"\r\n{text}"\r\n'
	return body.encode("ascii", errors="strict")


def _shell_cmng_not_supported_help(serial_port: str) -> str:
	extra = ""
	try:
		import glob

		ports = sorted(glob.glob("/dev/cu.usbmodem*"))
		others = [p for p in ports if p != serial_port]
		if others:
			extra = f" Other ports on this host: {', '.join(others)}. Try each with --at-mode direct."
	except OSError:
		pass
	return (
		"Zephyr AT shell (``at AT``) only forwards a single line per command; AT%CMNG PEM payloads "
		"span multiple lines and cannot be sent reliably through the application UART shell."
		"\n\nUse one of:\n"
		"  1) nRF Connect for Desktop -> Cellular Monitor -> Certificate Manager "
		"(sec tag 16842753, AT+CFUN=4 first).\n"
		"  2) A dedicated modem AT USB interface (if your board exposes one) with "
		"``--at-mode direct`` on that COM/tty device."
		f"{extra}"
	)


def modem_write_credentials(
	serial_port: str,
	sec_tag: int,
	ca_pem: str,
	client_pem: str,
	key_pem: str,
	baud: int,
	at_mode: AtMode,
	clear_first: bool,
) -> None:
	try:
		import serial  # type: ignore
	except ImportError as e:
		raise RuntimeError("Install pyserial: pip install pyserial") from e

	ser = serial.Serial(serial_port, baudrate=baud, timeout=0.05)
	try:
		time.sleep(0.15)
		if ser.in_waiting:
			ser.reset_input_buffer()
		mode: str = at_mode
		if mode == "auto":
			mode = _detect_at_mode(ser)
		ser.reset_input_buffer()

		if mode == "shell":
			raise RuntimeError(_shell_cmng_not_supported_help(serial_port))

		try:
			_send_at_line(ser, mode, "AT", timeout_s=3.0, chunked=False)
		except RuntimeError as e:
			# Some builds boot in Zephyr shell and require entering AT pass-through first.
			if "command not found" in str(e):
				ser.reset_input_buffer()
				_serial_write_raw(ser, b"at at_cmd_mode start\r\n", chunked=False)
				time.sleep(0.6)
				ser.reset_input_buffer()
				_send_at_line(ser, mode, "AT", timeout_s=3.0, chunked=False)
			else:
				raise
		log.info("AT+CFUN=4 (modem offline for credential writes)...")
		_send_at_line(ser, mode, "AT+CFUN=4", timeout_s=15.0, chunked=False)
		time.sleep(0.6)

		if clear_first:
			for t in (0, 1, 2):
				log.info("Clear sec_tag %d type %d (ignore errors if empty)...", sec_tag, t)
				try:
					_send_at_line(
						ser, mode, f"AT%CMNG=3,{sec_tag},{t}", timeout_s=8.0, chunked=False
					)
				except RuntimeError:
					log.info("Clear type %d produced an error; continuing", t)

		def cmng_write(cred_type: int, pem: str) -> None:
			text = pem.strip("\n") + "\n"
			log.info("Writing CMNG type %d (%d bytes PEM)...", cred_type, len(text))
			payload = _cmng_direct_bytes(sec_tag, cred_type, pem)
			_send_at_bytes(ser, payload, timeout_s=120.0, chunked=True)

		cmng_write(0, ca_pem)
		cmng_write(1, client_pem)
		cmng_write(2, key_pem)

		log.info("AT+CFUN=1 (restore full functionality)...")
		_send_at_line(ser, mode, "AT+CFUN=1", timeout_s=25.0, chunked=False)
	finally:
		ser.close()


def write_summary(output_dir: Path, device_id: str, sec_tag: int, include_coap_ca: bool) -> None:
	meta = {
		"device_id": device_id,
		"sec_tag": sec_tag,
		"files": {
			"private_key": "device.key",
			"device_certificate": "device.crt",
			"public_key": "device_public.pem",
			"amazon_root_ca1": "AmazonRootCA1.pem",
			"coap_root_ca": "nRFCloudCoAPRootCA.pem",
			"ca_chain": "ca_chain.pem",
		},
	}
	(output_dir / "provision_summary.json").write_text(
		json.dumps(meta, indent=2) + "\n", encoding="utf-8"
	)
	hint = (
		f"Device: {device_id}\nSecurity tag: {sec_tag}\n\n"
		f"CoAP CA included in CA chain: {include_coap_ca}\n\n"
		"Install on modem (Zephyr shell UART cannot carry multi-line AT%CMNG):\n"
		"1) Open nRF Connect Cellular Monitor, connect the kit, send AT+CFUN=4.\n"
		"2) Certificate Manager -> sec tag above -> paste:\n"
		"   - CA: ca_chain.pem\n"
		"   - Client cert: device.crt\n"
		"   - Private key: device.key\n"
		"3) Update certificates, then AT+CFUN=1.\n"
	)
	(output_dir / "MODEM_INSTALL.txt").write_text(hint, encoding="utf-8")


def resolve_device_id(args) -> str:
	"""Return the nRF Cloud device ID, deriving <name>-<imei> from the modem when needed."""
	name = args.name
	if DEVICE_ID_RE.search(name):
		return name
	if not args.serial:
		log.error(
			"Deriving <name>-<imei> needs a connected device: pass --serial PORT "
			"(or give the full device id, e.g. %s-352656106119210).",
			name,
		)
		sys.exit(1)
	at_fw = Path(args.at_client_fw).expanduser().resolve() if args.at_client_fw else None
	imei = ensure_at_client_and_read_imei(
		args.serial,
		args.baud,
		args.at_mode,
		at_fw,
		args.serial_number,
		args.skip_at_client,
		args.boot_delay,
	)
	return f"{name}-{imei}"


def main() -> None:
	parser = argparse.ArgumentParser(description="Provision nRF Cloud TLS credentials.")
	parser.add_argument(
		"name",
		help=(
			"Device name prefix; the modem IMEI is appended as <name>-<imei> "
			"(e.g. 'nrf' -> nrf-352656106119210). Pass a full <name>-<imei> to skip "
			"the IMEI read."
		),
	)
	parser.add_argument(
		"-o",
		"--output-dir",
		type=Path,
		default=None,
		help="Output directory (default: credentials/nrf_cloud/<device_id> under cwd)",
	)
	parser.add_argument(
		"--sec-tag",
		type=int,
		default=DEFAULT_SEC_TAG,
		help=f"Modem security tag (default {DEFAULT_SEC_TAG})",
	)
	parser.add_argument(
		"--skip-onboard",
		action="store_true",
		help="Only generate files; do not call nRF Cloud REST API",
	)
	parser.add_argument(
		"--reuse-existing-creds",
		action="store_true",
		help=(
			"Skip openssl key/cert generation; use existing device.key and device.crt "
			"in the output dir (refresh CA chain, REST onboard, optional --serial)"
		),
	)
	parser.add_argument(
		"--no-coap-ca",
		action="store_true",
		help="Do not append nRF Cloud CoAP root CA to the CA chain",
	)
	parser.add_argument(
		"--serial",
		metavar="PORT",
		help="AT client UART for IMEI read and AT%%CMNG credential write (e.g. /dev/ttyACM0)",
	)
	parser.add_argument(
		"--at-mode",
		choices=("auto", "direct"),
		default="auto",
		help="AT UART framing: auto-detect or force native modem AT (default auto)",
	)
	parser.add_argument(
		"--baud",
		type=int,
		default=115200,
		help="Serial baud rate (default 115200)",
	)
	parser.add_argument(
		"--no-clear",
		action="store_true",
		help="Do not delete existing credentials on the sec tag before writing",
	)
	parser.add_argument(
		"--at-client-fw",
		metavar="HEX",
		help="AT client firmware hex, flashed with nrfutil when no AT client answers",
	)
	parser.add_argument(
		"--serial-number",
		help="nrfutil device serial number for flashing (auto-detected if only one is connected)",
	)
	parser.add_argument(
		"--skip-at-client",
		action="store_true",
		help="Do not flash the AT client; assume one is already present",
	)
	parser.add_argument(
		"--boot-delay",
		type=float,
		default=DEFAULT_BOOT_DELAY_S,
		help=f"Seconds to wait after flashing/reset before probing (default {DEFAULT_BOOT_DELAY_S})",
	)
	args = parser.parse_args()

	device_id = resolve_device_id(args)

	out = args.output_dir or (Path("credentials") / "nrf_cloud" / device_id)

	api_key = nrf_cloud_api_key()
	if not api_key and not args.skip_onboard:
		log.error("Set NRF_CLOUD_API_KEY or NRFCLOUD_API_KEY, or use --skip-onboard")
		sys.exit(1)

	log.info("Device ID: %s", device_id)
	log.info("Output dir: %s", out.resolve())
	include_coap_ca = not args.no_coap_ca

	if args.reuse_existing_creds:
		key_p = out / "device.key"
		crt_p = out / "device.crt"
		if not key_p.is_file() or not crt_p.is_file():
			log.error(
				"--reuse-existing-creds requires %s and %s in %s",
				key_p.name,
				crt_p.name,
				out.resolve(),
			)
			sys.exit(1)
		log.info("Reusing existing %s and %s (no new keypair)", key_p.name, crt_p.name)
	else:
		generate_credentials(out, device_id)
	ca_pem = prepare_ca_files(out, include_coap_ca=include_coap_ca)
	write_summary(out, device_id, args.sec_tag, include_coap_ca)

	cert_pem = (out / "device.crt").read_text(encoding="utf-8")
	if not args.skip_onboard:
		onboard_device(api_key, device_id, cert_pem)

	if args.serial:
		key_pem = (out / "device.key").read_text(encoding="utf-8")
		modem_write_credentials(
			args.serial,
			args.sec_tag,
			ca_pem,
			cert_pem,
			key_pem,
			args.baud,
			args.at_mode,
			clear_first=not args.no_clear,
		)
		log.info("Modem credentials written on sec_tag %s", args.sec_tag)
	else:
		log.info("Skip modem write (no --serial). Use Cellular Monitor or pass --serial PORT.")

	log.info("Done. PEM bundle: %s", out.resolve())

	key_hex = extract_private_key_hex(out / "device.key")
	log.info("CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX=%s", key_hex)
	log.info(
		"Bring-up only: pass this value to -DCONFIG_APP_CLOUD_JWT_TEST_KEY_BUILTIN=y "
		"-DCONFIG_APP_CLOUD_JWT_TEST_KEY_HEX on the west build command line. Never commit it; "
		"treat it as compromised if it is ever pushed."
	)


if __name__ == "__main__":
	main()
