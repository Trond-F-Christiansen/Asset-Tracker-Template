#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Pad every segment of a hex file with 0xFF to 16-byte (MRAM line) boundaries.

On nRF92, J-Link with the generic CORTEX-M33 device writes MRAM as plain
memory. The bytes of a partially written 16-byte line are lost, the download
verify fails and the rest of the file is skipped. Padding every segment to
whole lines avoids this. Applied to the sysbuild merged_<board_target>.hex
files when SB_CONFIG_ATT_MERGED_HEX_PAD=y.

Usage:
    python3 pad_hex.py <in.hex> <out.hex>   (in and out may be the same file)
"""

import sys

from intelhex import IntelHex

ALIGN = 16

if len(sys.argv) != 3:
    sys.exit(__doc__)
src, dst = sys.argv[1], sys.argv[2]

ih = IntelHex(src)
for start, end in ih.segments():
    first = start & ~(ALIGN - 1)
    last = (end + ALIGN - 1) & ~(ALIGN - 1)
    for a in list(range(first, start)) + list(range(end, last)):
        ih[a] = 0xFF
    print(f"0x{start:08X}-0x{end:08X} -> 0x{first:08X}-0x{last:08X}")
ih.write_hex_file(dst)
print(f"wrote {dst}")
