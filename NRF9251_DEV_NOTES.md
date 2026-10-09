# nRF9251 bring-up notes (temporary)

> **Temporary developer notes during the nRF9251 DK bring-up.** Not to be part of the published
> documentation.

All commands run from the repository root (`project/` in a west workspace) unless noted.

## Build

```shell
west build -p -b nrf9251dk/nrf9251/cpuapp --sysbuild -d build app
west ncs-mergehex --no-rebuild -d build
```

`west ncs-mergehex` is a separate step. Without it, `build/merged.hex` does not exist (J-Link
then reports `Failed to open file.`).

## Merged hex

### How `build/merged.hex` is produced

1. `SB_CONFIG_MERGED_HEX_FILES=y` (`app/Kconfig.sysbuild`) makes sysbuild write one
   `build/merged_<board_target>.hex` per board target, from the hex files `west flash` would
   program:
   - nRF9251 DK: `merged_nrf9251dk_0_1_0_nrf9251_cpuapp.hex` (MCUboot, UICR/PERIPHCONF, signed
     app), plus `merged_nrf9251dk_0_1_0_nrf9251_cpuppr.hex` when the PPR image is built.
   - nRF91 boards: two files, the secure target (b0, MCUboot s0/s1, provisioning) and `/ns`
     (TF-M + app).
2. `west ncs-mergehex` (NCS v3.5.0-preview2 and later) combines them into `build/merged.hex`
   following `app/mergehex.yaml`. Files the current build does not produce are skipped, so one
   entry covers all boards.

`ncs-mergehex` skips missing inputs **silently**. If a board target is renamed, `merged.hex`
can be incomplete without an error. Check that the `Generating build/merged.hex from ...` line
lists all expected files.

### nRF92 MRAM padding

There is no nRF9251 J-Link device yet, so the board uses `-device CORTEX-M33` with
`nrf9251_cpuapp.JLinkScript`. With the generic device, J-Link writes MRAM as plain memory:

- MRAM commits whole 16-byte lines. When a hex segment ends partway through a line, those last
  bytes are lost.
- J-Link's read-back verify then fails (`Writing target memory failed`) and **all remaining
  segments are skipped**. After `nrfutil device recover`, MCUboot is written, but the app,
  PERIPHCONF and UICR are not, and MCUboot reports `Unable to find bootable image`.
- Without a recover the old image is still in MRAM, which hides the problem.

`SB_CONFIG_ATT_MERGED_HEX_PAD` (default `y` on nRF92 only) pads every segment of the
`merged_<board_target>.hex` files with `0xFF` to a 16-byte boundary during the build
(`app/sysbuild/CMakeLists.txt`, `scripts/pad_hex.py`). The build log shows
`Padding merged_<board_target>.hex to 16-byte MRAM lines`. nRF91 output is unchanged.

nrfutil does not need the padding: it programs MRAM through IronSide and the MRAM controller.
The per-image hex files that `west flash` uses are not padded.

### Flashing

nrfutil (recommended):

```shell
nrfutil device recover
nrfutil device program --firmware build/merged.hex
```

J-Link Commander (works with the padded image):

```shell
nrfutil device recover
JLinkExe -device CORTEX-M33 -if SWD -speed 4000 \
  -jlinkscriptfile ../nrf/boards/nordic/nrf9251dk/support/nrf9251_cpuapp.JLinkScript \
  -CommanderScript flash.jlink
```

with `flash.jlink`:

```
r
h
erase
loadfile build/merged.hex
r
g
qc
```

Expect `Downloading file [build/merged.hex]... O.K.`. After a recover, J-Link prints many
`CPU could not be halted` / `SYSRESETREQ has confused core` messages. These are expected and do
not stop the download.

Other gotchas:

- Do not turn off J-Link's download verify to get past the error. The bytes are really lost,
  so the image would be silently corrupt.
- `nrfutil device reset --reset-kind RESET_SYSTEM` is not supported for the nRF92 application
  core. Power-cycle the DK instead.

## nRF Cloud provisioning (`scripts/nrf_cloud_provision.py`)

Bring-up helper that creates nRF Cloud credentials for the DK, onboards the device and writes
the credentials to the modem. It:

1. Reads the modem IMEI over the AT UART and builds the device ID `<name>-<imei>`, or uses a
   full `<name>-<15-digit IMEI>` given on the command line as is.
2. Generates a P-256 key pair and a self-signed certificate (CN = device ID) with `openssl`.
3. Downloads Amazon Root CA 1 and builds the CA chain (with the nRF Cloud CoAP root CA unless
   `--no-coap-ca`).
4. Onboards the device with the nRF Cloud REST API (`POST /v1/devices/<device_id>`).
5. With `--serial`, writes CA, certificate and key to security tag `16842753` with `AT%CMNG`.
6. Prints the private key scalar for `CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX` (see below).

### Prerequisites

- `openssl` in `PATH`
- `NRF_CLOUD_API_KEY` (or `NRFCLOUD_API_KEY`) set, unless `--skip-onboard`
- `pip install pyserial` (for `--serial`)
- `nrfutil` with the `device` command (to flash the AT client)
- The nRF92 **AT client** firmware hex. Multi-line `AT%CMNG` writes only work with the dedicated
  AT client, not through the ATT Zephyr AT shell.

### Typical flow

```shell
export NRF_CLOUD_API_KEY=<your API key>

# Flashes the AT client if no AT client answers on the port, reads the IMEI,
# generates and onboards credentials, and writes them to the modem.
python3 scripts/nrf_cloud_provision.py nrf --serial /dev/tty.usbmodemXXXX \
    --at-client-fw <path/to/at_client_nrf92.hex>
```

The AT client replaces the application, so flash ATT again afterwards. Not yet verified on
nRF9251: whether `nrfutil device recover` also clears the modem credentials. If the device fails
to connect after a recover, check the sec tag (`AT%CMNG=1,16842753`) and provision again.

Useful variants:

| Command | Use |
|---|---|
| `... nrf --serial PORT --skip-at-client` | The AT client is already running |
| `... nrf-<IMEI> -o ./my_creds --skip-onboard` | Generate files only, no REST call, no modem write |
| `... nrf-<IMEI> --reuse-existing-creds` | Re-onboard after deleting the device in nRF Cloud, with the same keys |
| `... --serial-number <SN>` | Several boards attached |

If the AT client is not available, install the PEMs with nRF Connect for Desktop →
**Cellular Monitor → Certificate Manager** (`AT+CFUN=4` first, sec tag `16842753`: CA =
`ca_chain.pem`, client cert = `device.crt`, key = `device.key`). The script also writes these
steps to `MODEM_INSTALL.txt`.

### Output

`credentials/nrf_cloud/<device_id>/` (relative to the current directory):
`device.key`, `device.crt`, `device_public.pem`, `AmazonRootCA1.pem`,
`nRFCloudCoAPRootCA.pem`, `ca_chain.pem`, `provision_summary.json`, `MODEM_INSTALL.txt`.

`credentials/` is in `.gitignore` because it contains private keys. Run the script from the
repository root so the output lands there.

### JWT signing key (bring-up only)

On this target the modem cannot export the key it stores, so the nRF9251 build signs the nRF
Cloud JWT in firmware (`CONFIG_NRF_CLOUD_JWT_SOURCE_CUSTOM=y`). The script prints the key
scalar as `CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX=<64 hex chars>`. Pass it on the build command
line together with the matching device:

```shell
west build -p -b nrf9251dk/nrf9251/cpuapp --sysbuild -d build app -- \
    -DCONFIG_APP_CLOUD_JWT_TEST_KEY_BUILTIN=y \
    -DCONFIG_APP_CLOUD_JWT_TEST_KEY_HEX=\"<64 hex chars>\" \
    -DCONFIG_APP_CLOUD_JWT_ISS_HW_PREFIX=\"nrf9251\"
west ncs-mergehex --no-rebuild -d build
```

> **The key is secret.** Never commit it to a `.conf` file or a script. A key that has been
> pushed is compromised: regenerate the credentials, delete the device in nRF Cloud and onboard
> it again.
