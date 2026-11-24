#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$1" "$2" "$work/pfs512.img" 1 512
"$1" "$2" "$work/pfs4096.img" 1 4096
