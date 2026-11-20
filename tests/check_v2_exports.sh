#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
module=${1:?PFS3.so required}
test -s "$module"
test "$(nm -D --defined-only "$module" | awk '$3=="infiltrator_filesystem_api_v2"{n++}END{print n+0}')" -eq 1
unexpected="$(nm -D --defined-only "$module" | awk '{print $3}' |
  sed -E 's/@@?.*$//' |
  grep -v '^infiltrator_filesystem_api_v2$' || true)"
test -z "$unexpected" || { printf 'PFS3 unexpected runtime symbols: %s\n' "$unexpected";exit 1; }
