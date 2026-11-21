#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify the exact V2 filesystem library, embedded firmware ELF, and imports.
set -euo pipefail
module=$(realpath "${1:?PFS3.so required}")
test -s "$module"
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/tests/check_v2_exports.sh" "$module"
readelf -SW "$module" | grep -F .infiltrator_boot >/dev/null
work=$(mktemp -d);trap 'rm -rf -- "$work"' EXIT
objcopy --dump-section ".infiltrator_boot=$work/firmware.so" "$module" "$work/carrier.so"
test -s "$work/firmware.so"
readelf -hW "$work/firmware.so" | grep -F 'DYN (Shared object file)' >/dev/null
! readelf -dW "$work/firmware.so" | grep -F '(NEEDED)'
! nm -u "$work/firmware.so" | grep .
nm -D --defined-only "$work/firmware.so" | grep -F infiltrator_filesystem_api_v2 >/dev/null
if readelf -Wr "$work/firmware.so" | grep 'R_X86_64_' | grep -v 'R_X86_64_RELATIVE'; then
 echo "Unsupported freestanding relocation" >&2;exit 1
fi
echo PFS3_COMMON_V2_PACKAGED_FREESTANDING_PASS
