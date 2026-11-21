# Filesystem-PFS3

Canonical format parsing is owned by this repository, with the portable
PFS3 core in `native/filesystems/pfs3/core/`. The installed provider is
`/System/Filesystems/PFS3.so` and the illustrated manual is installed beside
other filesystem documentation.

The hosted module and its embedded freestanding ELF share the native reader. Both expose read-only mounts, path traversal, stat, directory enumeration and file reads for PFS direct-index volumes. Media blocks and the host transfer size are independent. Both profiles read the same PFS volume through 512-byte and 4 KiB hosts. Superindexed volumes, large-file layouts and link traversal currently return an unsupported result.

Package qualification uses Hst Imager to create a disposable partition independently, imports real kernel and initramfs files, and compares the returned bytes. It also checks rejection of damaged index metadata and image immutability. New versions require validation and OVMF kernel handoff through the rebuilt BOOT consumer.
