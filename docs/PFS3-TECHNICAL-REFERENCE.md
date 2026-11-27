# PFS3: technical reference

## History and scope

Professional File System is the Amiga filesystem developed by Michiel Pelt, later released as PFS3. The maintained PFS3aio code supports historical media and later extensions. This reference is based on the original PFS3aio source retained in this repository, including blocks.h, anode allocation, directory handling, allocation.c and disk update code. The two observed disk identifiers PFS followed by binary 1 or 2 (0x50465301/02) are not DOS or SFS identifiers. PFS3 uses allocation nodes (anodes) that describe runs of storage, reserved metadata blocks, allocation bitmaps and metadata commit mechanisms. A direct-index reader is not equivalent to support for every original PFS3 feature.

The historical commercial and later open-source PFS3 development aimed at hard-disk throughput, directory responsiveness and recovery. PFS3aio source release is important preservation evidence; it is not a variation of classic Amiga FFS or Unix inode structures.

## Geometry and root field inventory

The root is at disk logical block 2, with disk block bytes S and reserved metadata block bytes R separately defined. All multibyte fields are big-endian and packed according to the original 2-byte alignment. Root fields: type 0, options 4, datestamp 8, 16-bit creation day/minute/tick/protection 12/14/16/18, Pascal name 20..51, last/first reserved sector 52/56, free reserved blocks 60, reserved block bytes 64 (16-bit), root cluster sectors 66, free data blocks 68, always-free reserve 72, roving pointer 76, deleted-directory pointer 80, disk size in logical sectors 84, root extension sector 88, reserved 92, index array from 96. Validate declared size, S/R ratio and every extent. A reserved-block pointer must align relative to first_reserved; it is a sector address, not a reserved-block index.

## Mode bits and root extension

Mode bits 0..12 select hard disk, split anodes, directory extension, deleted directory, size field, root extension, datestamp, superindex, super deleted directory, extended roving, long filename, large file and stored geometry respectively. These are independent feature gates, not version labels. An EX root extension has ID at 0, options 4, datestamp 8, format version 12, root/volume dates at 16/22, postponed operation and three arguments at 28/32/36/40, reserved roving at 44, bit and current anode sequence at 48/50, deldir roving/size 52/54, max name length 56 and superindex array from 64. Postponed operation types 1,2,3 denote free blocks with/without anodes and free anode chain. Do not replay unknown operations or infer current state from a datestamp alone.

## Reserved storage and free bitmaps

The reserved area has its own bitmap directly after the root record; its bits represent R-byte reserved blocks rather than S-byte data blocks. Ordinary data allocation starts at last_reserved+1, according to the original allocation.c. Each bitmap block begins with 16-bit BM ID, reserved word, 32-bit datestamp and sequence at 4/8; bitmap words start at 12. Bits are MSB first and 1 means free. With R=1024, each normal bitmap covers (1024-12)*8=8096 sectors. Data sector z maps to bit z-(last_reserved+1); divide by bitmap coverage then select the word and bit. For first data sector 1000 and z=1100, bit 100 is word 3 mask 0x08000000. A separate reserved bit indexes (sector-first_reserved)/(R/S). Verify both maps, counts and always-free reserves before allocating.

## Bitmap and anode indexes

An index block has 16-bit ID, reserved word, datestamp at 4, sequence at 8, 32-bit pointers from 12. Capacity Q=(R-12)/4. In the small/direct profile the root holds five bitmap index pointers at 96 and 99 anode index pointers from 116. A bitmap block sequence b maps through root bitmap index floor(b/Q) and pointer b mod Q; validate all IDs, sequence and bounds. Larger superindex profiles have 104 bitmap index pointers and obtain anode index roots through the extension's superindex. Do not interpret the small root union as a superindex array. Superindex traversal and feature admission must be verified against the original handler before use. Unallocated pointer entries are not valid metadata addresses.

## Anode record and lookup example

An AB block has ID 0x4142 at 0, datestamp 4, sequence 8, reserved word 12 and 12-byte anodes from 16. Each anode holds run length in disk blocks at 0, first physical sector at 4, next anode identity at 8. At R=1024, N=(1024-16)/12=84 anodes per block and Q=253 pointers per index. For nonsplit numbering, sequence=floor(id/N), slot=id mod N; split numbering uses upper/lower 16 bits. Resolve index number floor(sequence/Q), index offset sequence mod Q, then AB slot at 16+12*slot. Example id=1000 nonsplit yields sequence 11, slot 76, record offset 928. Split ID 0x000B004C identifies the same slot. Root anode is 5; user identities start at 6; zero terminates an anode chain. Validate every sequence, reserved pointer and slot.

## File data and directory records

Anode links determine logical order. A run (physical 2000,length 3) followed by (3000,length 2) at S=512 maps logical byte 1800 to physical byte 3000*512+264=1536264. Clip to the directory entry's file size, not run capacity. Empty files may have a zero-length anode with a sentinel start. A DB directory block has ID 0x4442, datestamp 4, directory anode 12 and parent 16, variable entries from 20. Each entry: next/record length byte 0, signed type 1, anode 2, low file size 6, three 16-bit DateStamp components 10/12/14, protection 16, name length 17, name from 18 followed by one-byte comment length and comment. Zero length ends directory records; all nonzero lengths must be even and bounded. Follow the directory's anode chain through reserved metadata, not ordinary payload.

## Extended entries and deletion semantics

Directory-extension records end in a 16-bit presence mask for up to eleven extra words. Starting immediately before the mask, walk backwards through present fields in increasing bit order; omitted words are zero. The known fields include link identity, ownership, high protection bits, rollover sizes/pointer and high 16 bits of file size (field 10). Large file length combines high16 shifted 32 with low32. Verify name/comment and extra fields do not overlap and preserve unknown extensions. Native deleted-directory records may keep recoverable file references; a deleted entry is not proof its old payload is still allocated. Rollover files and soft/hard links have separate semantics and require feature-specific verification. Do not flatten a link or rollover object into an ordinary file.

## Metadata update and recovery

The original implementation distinguishes immediately allocatable space, pending releases and reserved metadata headroom. Modified metadata can be relocated, retaining old blocks until a root update publishes the new state. Deferred frees and postponed operations avoid prematurely reclaiming referenced blocks. A writer must follow the original UpdateDisk, allocation and anode lifecycle, including rollback on failed publication; a datestamp does not establish a journal commit by itself. A relocation needs reserved metadata space, data copy and verification, coherent anode and bitmap updates and durable root publication. Do not release old storage before the new reachable root is durable. Exact ordering and interrupted root/metadata recovery must be tested against original images.

## Implementation and qualification boundary

The earlier direct-index hosted and freestanding profile was documented with independently produced PFS media on 512-byte and 4 KiB hosts. Superindex, large-file and link traversal were outside that historical reader's scope; format and namespace mutation are distinct. Historical workflow 45526904930 is evidence of its tested scope only.

Common V2 is the independent provider interface: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate real operation masks and media profiles. Use bounded host I/O, exclusive unmounted ownership and durable flush for mutation; distinguish unsupported, corrupt, I/O and stale-handle results. Test Media and Defragmenter use the same engine only for supported operations. Hosted, freestanding, BOOT, kernel, recovery and package qualification are independent. Current capability and release status belongs to the repository README and exact-source qualification records; historical evidence is not a current status claim.

Historical source or test reference: https://github.com/Infiltrator-Projects/Filesystem-PFS3/actions/runs/45526904930 .

## References and acceptance

Pinned original source: https://github.com/Infiltrator-Projects/Filesystem-PFS3/tree/2a4bfebea4b31ef0ea2d6cdbf6c04a7abcc7c5dc/native/filesystems/pfs3/original/pfs3aio ; cross-check blocks.h, allocation.c, anode and directory algorithms and root update. Test actual PFS3/PFS3aio fixtures with split and nonsplit numbering, both disk IDs, reserved block sizes, direct and superindex, long names, directory extensions, large files, links, deldir, rollover and interrupted frees. Compare file hashes and all metadata ownership; native writer qualification, exact superindex and recovery semantics remain outstanding. Historical provider evidence is identified in Implementation and qualification boundary; implementation support is separate from the format's full potential.
