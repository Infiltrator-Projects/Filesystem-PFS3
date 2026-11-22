/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real PFS3 direct-index/anode/dirblock disposable-media read qualification.
 * The test image follows PFS3 on-disk records and is never mounted by Linux.
 */
#include "pfs3_reader_v2.h"
#include "pfs3_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { SECTOR=512u, MEDIA=1024u*1024u, FILE_BYTES=4096u };
static unsigned char disk[MEDIA],before[MEDIA],expected[FILE_BYTES];
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"PFS3 native reader test %d: %s\n",__LINE__,#x); \
    return 1; } } while(0)
static void be16(unsigned off,uint16_t value)
{ disk[off]=(uint8_t)(value>>8);disk[off+1]=(uint8_t)value; }
static void be32(unsigned off,uint32_t value)
{
    disk[off]=(uint8_t)(value>>24);
    disk[off+1]=(uint8_t)(value>>16);
    disk[off+2]=(uint8_t)(value>>8);
    disk[off+3]=(uint8_t)value;
}
static int read_media(void *ctx,uint64_t pos,void *dst,uint32_t n)
{
    (void)ctx;
    if(!n || pos%SECTOR || n%SECTOR || n>SECTOR ||
       pos>MEDIA || n>MEDIA-pos)return -1;
    memcpy(dst,disk+(size_t)pos,n);
    return 0;
}
static void *allocate(void *ctx,size_t n)
{ (void)ctx;return malloc(n); }
static void release(void *ctx,void *p)
{ (void)ctx;free(p); }
static void fixture(void)
{
    /* Root at physical sector 2, with 99 direct-index pointers beginning
     * at byte 116. The first index points to an anode block at sector 6. */
    const unsigned root=2u*SECTOR;
    be32(root+0u,IFS_PFS3_DISK_PFS2);
    be32(root+4u,IFS_PFS3_MODE_HARDDISK|
                   IFS_PFS3_MODE_SPLITTED_ANODES|
                   IFS_PFS3_MODE_SIZEFIELD);
    disk[root+20u]=4u;
    memcpy(disk+root+21u,"TEST",4u);
    be32(root+52u,15u);be32(root+56u,4u);
    be32(root+60u,3u);
    be16(root+64u,1024u);be16(root+66u,2u);
    be32(root+68u,150u);be32(root+72u,5u);
    be32(root+84u,MEDIA/SECTOR);
    be32(root+116u,4u);

    /* Index block number zero -> anode block at sector 6. */
    const unsigned index=4u*SECTOR;
    be16(index+0u,0x4942u);
    be32(index+8u,0u);
    be32(index+12u,6u);

    /* Anode 5 is the root directory, and 6 the kernel image. */
    const unsigned anodes=6u*SECTOR;
    be16(anodes+0u,IFS_PFS3_ANODEBLOCK_ID);
    be32(anodes+8u,0u);
    be32(anodes+16u+5u*12u,1u);
    be32(anodes+16u+5u*12u+4u,8u);
    be32(anodes+16u+6u*12u,8u);
    be32(anodes+16u+6u*12u+4u,20u);

    /* A genuine PFS3 directory block with its native variable record. */
    const unsigned dir=8u*SECTOR;
    be16(dir,IFS_PFS3_DIRBLOCK_ID);
    be32(dir+12u,5u);
    be32(dir+16u,5u);
    const unsigned e=dir+IFS_PFS3_DIRBLOCK_HEADER_BYTES;
    disk[e]=30u;
    disk[e+1u]=(uint8_t)-3;
    be32(e+2u,6u);
    be32(e+6u,FILE_BYTES);
    disk[e+17u]=10u;
    memcpy(disk+e+IFS_PFS3_DIRENTRY_NAME_OFFSET,"kernel.img",10u);
    disk[e+28u]=0u;
    disk[e+30u]=0u;

    for(unsigned i=0u;i<FILE_BYTES;++i) {
        expected[i]=(uint8_t)((i*29u+17u)&255u);
        disk[20u*SECTOR+i]=expected[i];
    }
}
int main(void)
{
    fixture();
    memcpy(before,disk,MEDIA);
    IfsFsHostRuntimeV2 host={0};
    host.abi_version=IFS_FS_HOST_ABI_V2;
    host.structure_bytes=sizeof(host);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.volume_bytes=MEDIA;
    host.logical_sector_bytes=SECTOR;
    host.physical_sector_bytes=SECTOR;
    host.maximum_transfer_bytes=SECTOR;
    host.context=disk;
    host.read_at=read_media;
    host.allocate=allocate;host.release=release;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(ifs_pfs3_v2_reader_validate(&host)==IFS_FS_OK);
    char filename[128]={0};
    IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),
                        .name=filename,.name_capacity=sizeof(filename)};
    CHECK(ifs_pfs3_v2_reader_stat(&host,"/",&entry)==IFS_FS_OK);
    CHECK(entry.type==IFS_FS_ENTRY_DIRECTORY);
    CHECK(ifs_pfs3_v2_reader_stat(&host,"/kernel.img",&entry)==IFS_FS_OK);
    CHECK(entry.type==IFS_FS_ENTRY_FILE && entry.size_bytes==FILE_BYTES);
    CHECK(ifs_pfs3_v2_reader_stat(&host,"/KERNEL.IMG",&entry)==IFS_FS_OK);
    CHECK(ifs_pfs3_v2_reader_list(&host,"/",0u,&entry)==IFS_FS_OK);
    CHECK(strcmp(filename,"kernel.img")==0 && entry.size_bytes==FILE_BYTES);
    CHECK(ifs_pfs3_v2_reader_list(&host,"/",1u,&entry)==IFS_FS_END);
    unsigned char out[FILE_BYTES]={0};
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",0u,out,
                                  FILE_BYTES)==IFS_FS_OK);
    CHECK(memcmp(out,expected,FILE_BYTES)==0);
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",511u,out,1000u)
          ==IFS_FS_OK);
    CHECK(memcmp(out,expected+511u,1000u)==0);
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",FILE_BYTES,out,1u)
          ==IFS_FS_INVALID);
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",FILE_BYTES,out,0u)
          ==IFS_FS_OK);
    CHECK(ifs_pfs3_v2_reader_stat(&host,"/missing",&entry)==IFS_FS_NOT_FOUND);
    CHECK(ifs_pfs3_v2_reader_stat(&host,"/../kernel.img",&entry)==IFS_FS_INVALID);
    CHECK(memcmp(disk,before,MEDIA)==0);

    /* A native PFS3 catalogue can contain two individually valid directory
     * entries with the same case-folded name. They must not be interpreted
     * as the first record encountered by V2 STAT or READ. */
    {
        const unsigned dir=8u*SECTOR;
        const unsigned first=dir+IFS_PFS3_DIRBLOCK_HEADER_BYTES;
        const unsigned duplicate=first+30u;
        disk[duplicate]=30u;
        disk[duplicate+1u]=(uint8_t)-3;
        be32(duplicate+2u,6u);
        be32(duplicate+6u,FILE_BYTES);
        disk[duplicate+17u]=10u;
        memcpy(disk+duplicate+IFS_PFS3_DIRENTRY_NAME_OFFSET,
               "KERNEL.IMG",10u);
        disk[duplicate+30u]=0u;
        CHECK(ifs_pfs3_v2_reader_stat(&host,"/kernel.img",&entry)
              ==IFS_FS_CORRUPT);
        CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",0u,out,1u)
              ==IFS_FS_CORRUPT);
        memcpy(disk+duplicate,before+duplicate,31u);
        CHECK(ifs_pfs3_v2_reader_stat(&host,"/kernel.img",&entry)
              ==IFS_FS_OK);
        CHECK(memcmp(disk,before,MEDIA)==0);
    }

    /* An on-disk anode index with the wrong PFS3 format marker is corruption,
     * not an empty directory or a successful no-op file read. */
    be16(4u*SECTOR,0x0000u);
    CHECK(ifs_pfs3_v2_reader_validate(&host)==IFS_FS_CORRUPT);
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",0u,out,128u)
          ==IFS_FS_CORRUPT);
    be16(4u*SECTOR,0x4942u);

    /* Malformed block extents must not allow reading reserved metadata. */
    be32(6u*SECTOR+16u+6u*12u+4u,4u);
    CHECK(ifs_pfs3_v2_reader_read(&host,"/kernel.img",0u,out,128u)
          ==IFS_FS_CORRUPT);
    be32(6u*SECTOR+16u+6u*12u+4u,20u);
    CHECK(ifs_pfs3_v2_reader_validate(&host)==IFS_FS_OK);
    CHECK(memcmp(disk,before,MEDIA)==0);
    puts("PFS3_DIRECT_INDEX_REAL_COMMON_V2_NAMESPACE_PASS");
    return 0;
}
