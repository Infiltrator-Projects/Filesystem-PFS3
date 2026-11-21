#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
reader=$(realpath "${1:?Common V2 media exerciser}")
module=$(realpath "${2:?installed provider}")
image=$(realpath "${3:?independent PFS image}")
files=$(realpath "${4:?fixture tree containing boot/vmlinuz and boot/initramfs}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
sha256sum "$image" > before.sha256
objcopy --dump-section .infiltrator_boot=reader.so "$module" carrier-copy.so
test -s reader.so
cp --reflink=auto --sparse=always "$image" damaged.img
python3 - <<'PY'
from pathlib import Path
with Path('damaged.img').open('r+b') as disk:
    disk.seek(1024); root=disk.read(512)
    assert root[:4] in (b'PFS\x01',b'PFS\x02')
    block=int.from_bytes(root[116:120],'big')
    assert block>2
    disk.seek(block*512); assert disk.read(2)==b'IB'
    disk.seek(block*512); disk.write(b'XX')
PY
sha256sum damaged.img >> before.sha256
for binary in "$module" "$work/reader.so"; do
    for sector in 512 4096; do
        "$reader" "$binary" "$image" "$files" "$sector" clean
        "$reader" "$binary" damaged.img "$files" "$sector" bad-tree
    done
done
sha256sum -c before.sha256
