# Filesystem-PFS3

Canonical format parsing is owned by this repository, with the portable
PFS3 core in `native/filesystems/pfs3/core/`. The installed provider is
`/System/Filesystems/PFS3.so` and the illustrated manual is installed beside
other filesystem documentation.

Its current Common V2 CORE supports real root/geometry probing, bounded
read-only metadata mount sessions, and VOLUME query. **PFS3 is not yet
BOOT-compatible**: full PFS3 anode/directory traversal and file reads, a
shared freestanding UEFI reader, production BOOT validation and disposable
OVMF kernel/initramfs qualification are outstanding.

The prior worker/fixture/OS-specific code is retained as non-installed test
support. No `.ko` is shipped as a substitute. The hosted DEB and native RUN
qualify the V2 ELF but are not newly published as BOOT-compatible releases.
