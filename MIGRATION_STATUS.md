# PFS3 Common V2 migration status — 21 November 2026

The production `PFS3.so` links the source-owned native PFS1/PFS2
on-disk codec (`native/filesystems/pfs3/core/pfs3_core.c`), the portable
direct-index reader (`pfs3_reader_v2.c`), and the actual Common V2 adapter.
The historical V1 services and Linux driver remain in the repository for
source provenance/test support only. Neither is linked into the installed
module; no filesystem-specific `.ko` is distributed as a fallback.

## Functional scope confirmed by hosted CI

- Common V2 PROBE, read-only MOUNT/UNMOUNT, VOLUME.QUERY, STAT, LIST
  and READ are implemented, with generation-safe independent mount tokens.
- A real reserved index/anode/block/directory decoder traverses native
  PFS on-disk objects. Disposable images test root and regular-file
  lookup, case-insensitive names, directory enumeration, full and
  unaligned reads, missing paths, corrupted anode/index/extension metadata
  and byte-for-byte read-only source-media nonmutation.
- Unknown mode bits, invalid extension geometry, unfinished postponed
  operations and invalid block pointers are rejected.
- The qualified namespace subset is 512-byte-sector direct-index PFS1/PFS2.
  The existing metadata-only hosted mount for larger logical host sectors
  returns UNSUPPORTED for STAT/LIST/READ. Superindexes, large files, links,
  non-ASCII names and other not-yet-qualified features are not represented
  as successful reads. Allocation totals are recorded, not a full
  independent free-space bitmap verification.

## Firmware and release gate

The same native C format engine builds as a no-import freestanding V2 ELF,
with no glibc, Linux VFS, FUSE or filesystem-specific kernel dependency.
The ELF is embedded byte-for-byte into the installed `PFS3.so` in its
`.infiltrator_boot` section. CI has passed the no-import freestanding
compile, exact ELF carrier comparison, production Common V2 callback tests,
and installed shared-object/PDF-only checks.

**Still unqualified for BOOT:** the released BOOT 0.1.77
`validate-module` artifact required by the existing firmware gate now
returns HTTP 404. No replacement validator or altered policy is used.
Until the unmodified production validator actually accepts the *packaged*
`PFS3.so`, and OVMF demonstrates real kernel and initramfs reads,
PFS3 must not be published as BOOT-compatible or marked complete.

The hosted Debian/native RUN builds are qualification artifacts only.
No changes to the live Zabbix VM, ESP, certificates, GRUB or boot order.
