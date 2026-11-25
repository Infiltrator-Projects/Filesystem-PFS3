# Filesystem-PFS3

## 1. Purpose and format scope

Amiga Professional File System PFS3, direct-index reader scope. This repository owns the format-specific implementation of `PFS3.so` for InfiltratorOS.

## 2. On-disk architecture and invariants

Root and directory chains, allocation nodes and native block mappings must be validated before exposing a mount. The reader distinguishes filesystem block geometry from host transfer sizes and validates the complete root directory chain.

## 3. Common V2 implementation and capabilities

Hosted and embedded freestanding reader share the native core. The descriptor declares probe, mount, stat, enumeration, read and format; consult profile masks and tests for format availability. Independently produced PFS media exercises read access on 512-byte and 4 KiB hosts. The canonical entry point is `infiltrator_filesystem_api_v2`. Consumers must negotiate the actual profile and operation masks and use bounded, partition-relative host I/O. A source-level callback does not by itself establish that every format variant or host profile is qualified.

## 4. Integrity, mutation, allocation and recovery

Superindexed volumes, large-file layouts and link traversal are outside the documented reader scope. Format availability is not proof of general namespace mutation or recovery. Production BOOT handoff is separate. For destructive operations, require the applicable exclusive writable host, durability contract and format-specific validation. Never silently substitute a kernel or FUSE driver or treat a rejected or unavailable operation as success. Preserve original disk-format semantics; relocation and recovery are separate from ordinary file I/O.

## 5. Build, installation and release

The target installation is `/System/Filesystems/PFS3.so` and `/System/Resources/Documentation/Filesystem-PFS3.pdf`. The project release policy is a portable amd64 DEB and a CPU-native source-first RUN, with GitHub's automatic source archive; do not treat this policy statement as verification of the current installer payload. The release workflow is the authoritative executable build and package specification. Historical adapters and test artifacts are not additional production filesystem authorities.

## 6. Qualification and limitations

[Recorded qualification workflow](https://github.com/Infiltrator-Projects/Filesystem-PFS3/actions/runs/45526904930) · [Current workflows](https://github.com/Infiltrator-Projects/Filesystem-PFS3/actions?query=branch%3Amain). A successful workflow establishes the tested scope only, not complete original-format parity, physical-media crash safety, or production BOOT/kernel integration. Use disposable images for destructive qualification.

## 7. Technical references and provenance

- [Provider implementation](module/pfs3_module.c)
- [Native design or source provenance](native/filesystems/pfs3/DESIGN.md)
- [Migration and source history](MIGRATION_STATUS.md)
- [Original-format reference source](docs/PFS3.pages) and [PDF](docs/PFS3.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

The original-format reference and historical migration notes describe different evidence from the current production provider. When descriptions disagree, the current exported descriptor, implementation, executable tests and exact-revision release evidence take precedence.
