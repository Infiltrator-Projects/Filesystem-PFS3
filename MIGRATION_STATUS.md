# PFS3 Common V2 migration status — 20 November 2026

The canonical, independently maintained PFS3 native disk decoder remains the
source of on-disk format semantics in `native/filesystems/pfs3/core/`.
Production PFS3.so now links only that core and its Common V2 adapter, exports
only `infiltrator_filesystem_api_v2`, and never bundles the historical V1
service/fixture worker or a Linux .ko.

## Verified scope and remaining gates

- V2 geometry/root decode and feature checks: implemented
- V2 read-only mount/volume query: implemented (metadata-only), with
  independent, non-reused mount/session handles and stale-handle rejection
- V2 STAT/LIST/READ of root/anode/directory/file data: **missing**
- Freestanding reader and embedded EFI-compatible ELF: **missing**
- Actual BOOT validate-module acceptance and OVMF kernel/initramfs reads:
  **not qualified**, no release publication from hosted-only workflows

Historical native workers, host callback interfaces and original Linux source
remain in the repository for regression/source provenance. They are compiled
only into non-installed test support, not the provider. The source-embedded
RUN and portable Debian builds are qualification artifacts only until BOOT
checks genuinely pass.
