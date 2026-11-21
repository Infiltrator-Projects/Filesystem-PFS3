#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The Amiga binary is imported into the test disk's RDB only. Hst Imager
# independently formats the volume and populates its filesystem records.
set -euo pipefail
tools=$(realpath -m "${1:?temporary directory for image tools}")
source=$(realpath "${2:?directory containing vmlinuz and initramfs}")
out=$(realpath -m "${3:?output partition file}")
mkdir -p "$tools"
cli="$tools/hst.imager"
if [[ ! -x "$cli" ]]; then
    archive=hst-imager_v2.0.719-4056377_console_linux_x64.zip
    curl -fsSL --retry 3 "https://github.com/henrikstengaard/hst-imager/releases/download/2.0.719/$archive" -o "$tools/console.zip"
    echo "b706dc2b5e196d686eb6800852658fa00839a5573bdee2a2b2449a79eb2a1b6e  $tools/console.zip" | sha256sum -c -
    unzip -p "$tools/console.zip" hst.imager > "$cli"
    chmod 0755 "$cli"
fi
if [[ ! -f "$tools/pfs3aio.lha" ]]; then
    curl -fsSL --retry 3 https://aminet.net/disk/misc/pfs3aio.lha -o "$tools/pfs3aio.lha"
fi
echo "4837919cfebd78a4ce70206e4e5acb5430ca35d958bfbdab524ac2d14febf104  $tools/pfs3aio.lha" | sha256sum -c -
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$cli" blank disk.img 128mb
"$cli" rdb init disk.img
"$cli" rdb fs import disk.img "$tools/pfs3aio.lha" --dos-type PDS3 --name pfs3aio
"$cli" rdb part add disk.img DH0 PDS3 64mb --bootable
"$cli" rdb part format disk.img 1 BootFiles
"$cli" fs mkdir disk.img/rdb/DH0/boot
"$cli" fs copy "$source/vmlinuz" disk.img/rdb/DH0/boot
"$cli" fs copy "$source/initramfs" disk.img/rdb/DH0/boot
"$cli" rdb part export disk.img 1 "$out"
test -s "$out"
