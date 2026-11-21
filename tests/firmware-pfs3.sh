#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# This job accepts the production EFI binary and a validated package carrier.
# Only the PFS partition has boot payloads. All firmware variables are private
# files and the emulated NVMe disk is presented read-only.
set -euo pipefail
efi=$(realpath "${1:?rebuilt BOOT EFI image}")
module=$(realpath "${2:?installed PFS3.so}")
kernel=$(realpath "${3:?EFI-stub kernel}")
tools=$(realpath -m "${4:?temporary Hst tools directory}")
work=$(realpath -m "${5:?disposable OVMF directory}")
scripts=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"/{files,init/bin}
cd "$work"
cp "$kernel" files/vmlinuz
cp "$(command -v busybox)" init/bin/busybox
if readelf -lW init/bin/busybox | grep INTERP >/dev/null; then exit 1; fi
token=$(cat /proc/sys/kernel/random/uuid)
cat > init/init <<GUEST
#!/bin/busybox sh
set -e
b=/bin/busybox
\$b mkdir -p /dev /proc
\$b mount -t devtmpfs dev /dev
exec </dev/ttyS0 >/dev/ttyS0 2>&1
\$b mount -t proc proc /proc
\$b grep -qw pfs3_trial=$token /proc/cmdline
echo PFS3_V2_KERNEL_INITRAMFS_$token
\$b poweroff -f
GUEST
chmod 0755 init/init
(cd init && find . -print0 | cpio --null -o -H newc --quiet --owner=0:0 | gzip -1) > files/initramfs
bash "$scripts/independent-image.sh" "$tools" "$work/files" "$work/pfs.img"
sectors=$(( $(stat -c %s pfs.img) / 512 ))
start=133120
truncate -s $(( (start+sectors+4096)*512 )) boot.img
sgdisk --clear --new=1:2048:+64M --typecode=1:ef00 --new=2:"$start":$((start+sectors-1)) --typecode=2:8300 boot.img
uuid=$(sgdisk --info=2 boot.img | awk '/Partition unique GUID:/{print $4}')
[[ $uuid =~ ^[[:xdigit:]]{8}(-[[:xdigit:]]{4}){3}-[[:xdigit:]]{12}$ ]]
truncate -s 64M esp.img
mkfs.fat -F 32 esp.img >/dev/null
path=::/EFI/InfiltratorOS/BOOT
mmd -i esp.img ::/EFI ::/EFI/InfiltratorOS "$path" "$path/drivers_x64"
mcopy -i esp.img "$efi" "$path/BOOTX64.EFI"
mcopy -i esp.img "$module" "$path/drivers_x64/PFS3.so"
cat > boot.conf <<CONFIG
timeout 1
textonly true
use_nvram false
scanfor manual
menuentry "PFS3 qualification" {
    volume $uuid
    loader \\boot\\vmlinuz
    initrd \\boot\\initramfs
    ostype Linux
    options "console=ttyS0 rdinit=/init panic=1 pfs3_trial=$token"
}
CONFIG
mcopy -i esp.img boot.conf "$path/"
dd if=esp.img of=boot.img bs=1M seek=1 conv=notrunc status=none
dd if=pfs.img of=boot.img bs=512 seek="$start" conv=notrunc status=none
firmware=${OVMF_DIR:-/usr/share/OVMF}
virt-fw-vars -i "$firmware/OVMF_VARS_4M.fd" -o start.fd --append-boot-filepath '\EFI\InfiltratorOS\BOOT\BOOTX64.EFI'
cp --sparse=always --reflink=auto boot.img corrupt.img
# The two runs differ only at the first PFS index tag in the data partition.
python3 - "$start" <<'PY'
from pathlib import Path
import sys
base=int(sys.argv[1])*512
with Path('corrupt.img').open('r+b') as f:
    f.seek(base+1024+116); index=int.from_bytes(f.read(4),'big')
    assert index>2
    f.seek(base+index*512); assert f.read(2)==b'IB'
    f.seek(base+index*512); f.write(b'XX')
PY
sha256sum pfs.img boot.img corrupt.img > media.sha256
qemu=(qemu-system-x86_64)
if [[ -n ${QEMU_DATA_DIR:-} ]]; then qemu+=(-L "$QEMU_DATA_DIR"); fi
for mode in boot corrupt; do
    cp start.fd "$mode.fd"
    seconds=150
    if [[ $mode == corrupt ]]; then seconds=40; fi
    status=0
    timeout "$seconds" "${qemu[@]}" -machine q35,accel=tcg -cpu max -m 768 -display none -monitor none -net none -no-reboot \
        -serial "file:$mode.log" -drive "if=pflash,format=raw,readonly=on,file=$firmware/OVMF_CODE_4M.fd" \
        -drive "if=pflash,format=raw,file=$mode.fd" -drive "if=none,id=source,format=raw,readonly=on,file=$mode.img" \
        -device nvme,drive=source,serial=PFS3TEST || status=$?
    if [[ $mode == boot ]]; then
        test "$status" -eq 0
        grep -F "PFS3_V2_KERNEL_INITRAMFS_$token" "$mode.log"
    else
        [[ $status == 0 || $status == 124 ]]
        test -s "$mode.log"
        if grep -Fq "PFS3_V2_KERNEL_INITRAMFS_$token" "$mode.log"; then exit 1; fi
    fi
done
sha256sum -c media.sha256
