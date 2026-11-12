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
static int fail_read;
static uint32_t logical_sector=512u,maximum_read=512u;
static int rd(void *ctx,uint64_t off,void *out,uint32_t n) {
    (void)ctx;
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
                            IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)));
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *ops=
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    CHECK(core && core->header.capabilities==(IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT));
    CHECK(core->probe && core->mount && core->unmount && !core->write);
    CHECK(ops && ops->header.capabilities==IFS_FS_VOLUME_QUERY &&
          ops->query && !ops->repair && !ops->format);
    CHECK(!ifs_fs_group_v2(api,IFS_FS_GROUP_DEFRAG));
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
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
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
    stale=mounted;
    CHECK(info.variant==2u && info.block_bytes==512u &&
          info.total_bytes==MEDIA && info.free_bytes==150u*512u &&
          info.max_name_bytes==IFS_PFS3_DEFAULT_FILENAME_SIZE);
    snapshot.structure_bytes=sizeof(snapshot);
    CHECK(ops->query(mounted,&snapshot)==IFS_FS_OK &&
          snapshot.total_bytes==info.total_bytes &&
          snapshot.free_bytes==info.free_bytes);
    {
        void *duplicate=NULL;
        CHECK(core->mount(&host,&duplicate,&snapshot)==IFS_FS_BUSY);
        CHECK(!duplicate);
    }
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    CHECK(core->unmount(stale)==IFS_FS_INVALID);
    CHECK(ops->query(stale,&snapshot)==IFS_FS_INVALID);
    be32(off,0x50465301u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==1u);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_OK &&
          mounted && mounted!=stale && info.variant==1u);
    CHECK(ops->query(stale,&snapshot)==IFS_FS_INVALID);
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    /* PFS3 media with 4K logical sectors has a 4K reserved block;
     * its 512-byte root is still located at partition offset 1024. */
    be16(off+64u,4096u);
    be32(off+84u,MEDIA/4096u);
    /* Classic PFS1 forbids 4K reserved blocks; PFS2 permits them. */
    be32(off,0x50465302u);
    host.logical_sector_bytes=4096u;
    host.physical_sector_bytes=4096u;
    host.maximum_transfer_bytes=4096u;
    logical_sector=4096u;maximum_read=4096u;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->probe(&host,&variant)==IFS_FS_OK && variant==2u);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&mounted,&info)==IFS_FS_OK &&
          info.block_bytes==4096u &&
          info.free_bytes==150u*4096u);
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
