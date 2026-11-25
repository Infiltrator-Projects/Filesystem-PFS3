/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strict Common V2 read-only PFS1/PFS2 media signatures and validated roots.
 */
#include <infiltratr/filesystem_module_v2.h>
#include "../native/filesystems/pfs3/core/pfs3_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MEDIA (1024u*1024u)
static unsigned char disk[MEDIA];
static unsigned char initial_media[MEDIA];
static int fail_read;
static const IfsFsCoreOpsV2 *reentrant_core;
static void *reentrant_volume;
static int close_result;
static uint32_t logical_sector=512u,maximum_read=512u;
static int rd(void *ctx,uint64_t off,void *out,uint32_t n) {
    (void)ctx;
    if (reentrant_volume) {
        void *handle=reentrant_volume;
        reentrant_volume=NULL;
        close_result=reentrant_core->unmount(handle);
    }
    if(fail_read || !out || (off%logical_sector) || (n%logical_sector) ||
       n>maximum_read || off>MEDIA || n>MEDIA-off)return -1;
    memcpy(out,disk+(size_t)off,n);return 0;
}
static void *alloc(void *ctx,size_t n){(void)ctx;return malloc(n);}
static void release(void *ctx,void *p){(void)ctx;free(p);}
static void be16(unsigned off,uint16_t x){
    disk[off]=(unsigned char)(x>>8);
    disk[off+1]=(unsigned char)x;
}
static void be32(unsigned off,uint32_t x){
    disk[off]=(unsigned char)(x>>24);
    disk[off+1]=(unsigned char)(x>>16);
    disk[off+2]=(unsigned char)(x>>8);
    disk[off+3]=(unsigned char)x;
}
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"PFS3 V2 failure %s:%d\n",#x,__LINE__);return 1;}}while(0)
static int forbidden_write(void *ctx,uint64_t at,const void *src,uint32_t n)
{ (void)ctx;(void)at;(void)src;(void)n;return -1; }
static int forbidden_flush(void *ctx)
{ (void)ctx;return -1; }
int main(void){
    const IfsFsModuleApiV2 *api=infiltrator_filesystem_api_v2();
    CHECK(ifs_fs_api_v2_valid(api,"pfs3"));
    CHECK(api->runtime_profiles==IFS_FS_PROFILE_HOSTED);
    CHECK(api->group_mask==(IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                            IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)|
                            IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG)));
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *ops=
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    CHECK(core && core->header.capabilities==(IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
          IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ));
    CHECK(core->probe && core->mount && core->unmount && !core->write);
    CHECK(ops && ops->header.capabilities==(IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_FORMAT|IFS_FS_VOLUME_LABEL_GET) &&
          ops->query && ops->label && !ops->set_label && !ops->repair && ops->format);
    const IfsFsDefragOpsV2 *defrag=
        (const IfsFsDefragOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_DEFRAG);
    CHECK(defrag && defrag->header.capabilities==IFS_FS_DEFRAG_MAP &&
          defrag->map && !defrag->plan && !defrag->execute);
    IfsFsHostRuntimeV2 host={0};
    host.abi_version=IFS_FS_HOST_ABI_V2;
    host.structure_bytes=sizeof(host);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.volume_bytes=MEDIA;
    host.logical_sector_bytes=512u;
    host.physical_sector_bytes=512u;
    host.maximum_transfer_bytes=512u;
    host.read_at=rd;host.allocate=alloc;host.release=release;
    CHECK(ifs_fs_host_v2_valid(&host));
    uint32_t variant=99u;
    {
        IfsFsHostRuntimeV2 invalid_host=host;
        invalid_host.abi_version=1u;
        CHECK(core->probe(&invalid_host,&variant)==IFS_FS_INVALID && variant==0u);
    }
    variant=99u;
    CHECK(core->probe(&host,&variant)==IFS_FS_NOT_FOUND && variant==0u);
    const unsigned off=1024u;
    be32(off,0x50465302u); /* real on-disk PFS2 family identifier */
    be32(off+4u,0x13u);    /* harddisk/split anodes/size field */
    disk[off+20u]=4u;
    memcpy(disk+off+21u,"TEST",4);
    be32(off+52u,15u);be32(off+56u,4u);
    be32(off+60u,3u);
    be16(off+64u,1024u);be16(off+66u,2u);
    be32(off+68u,150u);be32(off+72u,5u);
    be32(off+84u,MEDIA/512u);
    /* Real reserved index/anode/root directory blocks are mandatory for
     * the hosted file namespace. This also proves PFS3 media isn't
     * accepted simply by drawing a synthetic superblock envelope. */
    const unsigned idx=4u*512u, ab=6u*512u, dir=8u*512u;
    be32(off+116u,4u);
    be16(idx,0x4942u);be32(idx+8u,0u);be32(idx+12u,6u);
    be16(ab,IFS_PFS3_ANODEBLOCK_ID);be32(ab+8u,0u);
    be32(ab+16u+5u*12u,1u);be32(ab+16u+5u*12u+4u,8u);
    be16(dir,IFS_PFS3_DIRBLOCK_ID);
    be32(dir+12u,5u);be32(dir+16u,5u);
    /* Canonical PFS3 file entry and two-sector anode data chain. */
    be32(ab+16u+6u*12u,2u);
    be32(ab+16u+6u*12u+4u,20u);
    {
        unsigned entry=dir+IFS_PFS3_DIRBLOCK_HEADER_BYTES;
        disk[entry]=30u;
        disk[entry+1u]=(unsigned char)-3;
        be32(entry+2u,6u);
        be32(entry+6u,1024u);
        disk[entry+17u]=10u;
        memcpy(disk+entry+IFS_PFS3_DIRENTRY_NAME_OFFSET,"kernel.img",10u);
        disk[entry+28u]=0u;
        disk[entry+30u]=0u;
        for(unsigned i=0u;i<1024u;++i)
            disk[20u*512u+i]=(unsigned char)((i*13u+31u)&255u);
    }

    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
    /* Unknown feature flags must not masquerade as a supported PFS volume. */
    be32(off+4u,0x80000013u);
    CHECK(core->probe(&host,&variant)==IFS_FS_CORRUPT);
    be32(off+4u,0x13u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
    /* Actual reserved-block extension: validate its header, filename
     * setting and uncommitted postponed operations before mounting. */
    const unsigned ext_off=10u*512u;
    be32(off+4u,0x433u); /* long names and root extension */
    be32(off+88u,10u);
    be16(ext_off,IFS_PFS3_EXTENSION_ID);
    be16(ext_off+56u,64u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
    {
        IfsFsVolumeInfoV2 ext_info={.structure_bytes=sizeof(ext_info)};
        void *ext_mount=NULL;
        CHECK(core->mount(&host,&ext_mount,&ext_info)==IFS_FS_OK && ext_mount);
        CHECK(ext_info.max_name_bytes==64u);
        CHECK(core->unmount(ext_mount)==IFS_FS_OK);
    }
    be32(ext_off+28u,IFS_PFS3_POSTPONED_FREEBLOCKS_FREE);
    CHECK(core->probe(&host,&variant)==IFS_FS_UNSUPPORTED);
    be32(ext_off+28u,IFS_PFS3_POSTPONED_NONE);
    be16(ext_off,0u);
    CHECK(core->probe(&host,&variant)==IFS_FS_CORRUPT);
    be16(ext_off,IFS_PFS3_EXTENSION_ID);
    be16(ext_off+56u,1u);
    CHECK(core->probe(&host,&variant)==IFS_FS_CORRUPT);
    be16(ext_off+56u,64u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
    be32(off+4u,0x13u);
    be32(off+88u,0u);
    IfsFsVolumeInfoV2 info={0},snapshot={0};
    void *mounted=NULL,*stale=NULL;
    info.structure_bytes=sizeof(info);
    host.capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE;
    host.write_at=forbidden_write;host.flush=forbidden_flush;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->probe(&host,&variant)==IFS_FS_OK);
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_ACCESS && !mounted);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.write_at=NULL;host.flush=NULL;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_OK && mounted);
    char label[64],tiny[2]={'X','X'};
    uint32_t label_bytes=UINT32_MAX;
    CHECK(ops->label(mounted,label,sizeof(label),&label_bytes)==IFS_FS_OK &&
          label_bytes==4u && !strcmp(label,"TEST"));
    CHECK(ops->label(mounted,tiny,sizeof(tiny),&label_bytes)==IFS_FS_NO_SPACE &&
          !label_bytes && tiny[0]=='X' && tiny[1]=='X');
    stale=mounted;
    CHECK(info.variant==2u && info.block_bytes==512u &&
          info.total_bytes==MEDIA && info.free_bytes==150u*512u &&
          info.max_name_bytes==IFS_PFS3_DEFAULT_FILENAME_SIZE);
    snapshot.structure_bytes=sizeof(snapshot);
    CHECK(ops->query(mounted,&snapshot)==IFS_FS_OK &&
          snapshot.total_bytes==info.total_bytes &&
          snapshot.free_bytes==info.free_bytes);
    /* Read through the production Common V2 ABI, not a private parser. */
    memcpy(initial_media,disk,MEDIA);
    {
        char name[128]={0},data[1024]={0};
        IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),
                            .name=name,.name_capacity=sizeof(name)};
        CHECK(core->stat(mounted,"/",&entry)==IFS_FS_OK &&
              entry.type==IFS_FS_ENTRY_DIRECTORY);
        CHECK(core->stat(mounted,"/kernel.img",&entry)==IFS_FS_OK &&
              entry.type==IFS_FS_ENTRY_FILE && entry.size_bytes==1024u);
        CHECK(core->directory_entry(mounted,"/",0u,&entry)==IFS_FS_OK &&
              strcmp(name,"kernel.img")==0);
        CHECK(core->directory_entry(mounted,"/",1u,&entry)==IFS_FS_END);
        reentrant_core=core;
        reentrant_volume=mounted;
        CHECK(core->read(mounted,"/kernel.img",0u,data,1024u)==IFS_FS_OK);
        {
            IfsFsExtentV2 extent={0};
            uint32_t count=99u;
            CHECK(defrag->map(mounted,"/kernel.img",0u,&extent,1u,&count)==IFS_FS_OK &&
                  count==1u && extent.logical_offset==0u && extent.length>0u);
            CHECK(defrag->map(mounted,"/missing",0u,&extent,1u,&count)==IFS_FS_NOT_FOUND && !count);
        }
        CHECK(!reentrant_volume && close_result==IFS_FS_BUSY);
        for (unsigned i=0u;i<1024u;++i)
            CHECK((unsigned char)data[i]==(unsigned char)((i*13u+31u)&255u));
        CHECK(core->read(mounted,"/kernel.img",511u,data,24u)==IFS_FS_OK);
        for (unsigned i=0u;i<24u;++i)
            CHECK((unsigned char)data[i]==(unsigned char)(((i+511u)*13u+31u)&255u));
        CHECK(core->stat(mounted,"/missing",&entry)==IFS_FS_NOT_FOUND);
        CHECK(core->stat(mounted,"/../kernel.img",&entry)==IFS_FS_INVALID);
        CHECK(core->read(mounted,"/kernel.img",1024u,data,1u)==IFS_FS_INVALID);
        CHECK(memcmp(initial_media,disk,MEDIA)==0);
    }
    {
        void *parallel=NULL;
        IfsFsVolumeInfoV2 other={.structure_bytes=sizeof(other)};
        CHECK(core->mount(&host,&parallel,&other)==IFS_FS_OK &&
              parallel && parallel!=mounted);
        CHECK(other.total_bytes==info.total_bytes &&
              other.free_bytes==info.free_bytes);
        other.structure_bytes=sizeof(other);
        CHECK(ops->query(parallel,&other)==IFS_FS_OK);
        CHECK(core->unmount(parallel)==IFS_FS_OK);
        other.structure_bytes=sizeof(other);
        CHECK(ops->query(parallel,&other)==IFS_FS_INVALID);
    }
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    CHECK(core->unmount(stale)==IFS_FS_INVALID);
    CHECK(ops->query(stale,&snapshot)==IFS_FS_INVALID);
    {
        char name[32]={0};
        IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),
                            .name=name,.name_capacity=sizeof(name)};
        CHECK(core->stat(stale,"/",&entry)==IFS_FS_INVALID);
    }
    be32(off,0x50465301u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==1u);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_OK &&
          mounted && mounted!=stale && info.variant==1u);
    CHECK(ops->query(stale,&snapshot)==IFS_FS_INVALID);
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    /* Repeat the actual 512-byte PFS volume through a 4Kn transport.
     * The on-media block numbers, free space and file contents stay the same. */
    host.logical_sector_bytes=4096u;
    host.physical_sector_bytes=4096u;
    host.maximum_transfer_bytes=4096u;
    logical_sector=4096u;maximum_read=4096u;
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==1u);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_OK &&
          info.block_bytes==512u && info.free_bytes==150u*512u);
    {
        char name[32];
        IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),.name=name,.name_capacity=sizeof(name)};
        CHECK(core->stat(mounted,"/kernel.img",&entry)==IFS_FS_OK && entry.size_bytes==1024);
    }
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    host.logical_sector_bytes=512u;
    host.physical_sector_bytes=512u;
    host.maximum_transfer_bytes=512u;
    logical_sector=512u;maximum_read=512u;
    be16(off+64u,1024u);
    be32(off+84u,MEDIA/512u);
    be32(off,0x444f5301u); /* foreign Amiga DOS */
    CHECK(core->probe(&host,&variant)==IFS_FS_NOT_FOUND);
    be32(off,0x50465302u);
    be32(off+84u,42u);
    CHECK(core->probe(&host,&variant)==IFS_FS_CORRUPT);
    be32(off+84u,MEDIA/512u);
    disk[off+20u]=0u;
    CHECK(core->probe(&host,&variant)==IFS_FS_CORRUPT);
    disk[off+20u]=4u;
    fail_read=1;CHECK(core->probe(&host,&variant)==IFS_FS_IO_ERROR);
    fail_read=0;host.read_at=NULL;
    CHECK(core->probe(&host,&variant)==IFS_FS_INVALID);
    puts("PFS3-STRICT-COMMON-V2-MOUNT-QUERY-PASS");
    return 0;
}
