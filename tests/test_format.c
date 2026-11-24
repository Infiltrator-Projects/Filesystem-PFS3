/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <infiltratr/filesystem_module_v2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/random.h>
#include <time.h>
typedef struct {int fd;uint32_t sector;uint64_t bytes;unsigned writes;} Media;
static int rd(void *ctx,uint64_t at,void *p,uint32_t n)
{
    Media *m=ctx;
    if(at%m->sector || n%m->sector || n>m->sector || at>m->bytes || n>m->bytes-at)return -1;
    return pread(m->fd,p,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int wr(void *ctx,uint64_t at,const void *p,uint32_t n)
{
    Media *m=ctx;
    if(at%m->sector || n%m->sector || n>m->sector || at>m->bytes || n>m->bytes-at)return -1;
    ++m->writes;
    return pwrite(m->fd,p,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int flush(void *ctx){return fsync(((Media*)ctx)->fd);}
static void *allocate(void *ctx,size_t n){(void)ctx;return malloc(n);}
static void release(void *ctx,void *p){(void)ctx;free(p);}
static int entropy(void *ctx,void *p,uint32_t n)
{(void)ctx;return getrandom(p,n,0)==(ssize_t)n?0:-1;}
static uint32_t word(const unsigned char *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t clock_ns(clockid_t id)
{
    struct timespec ts;
    if(clock_gettime(id,&ts)!=0)return 0u;
    return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
}
static uint64_t real_ns(void *ctx){(void)ctx;return clock_ns(CLOCK_REALTIME);}
static uint64_t mono_ns(void *ctx){(void)ctx;return clock_ns(CLOCK_MONOTONIC);}
static int cancelled(void *ctx){(void)ctx;return 0;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"PFS3 format line %d: %s\n",__LINE__,#x);goto done;}}while(0)
int main(int argc,char **argv)
{
    if(argc!=5)return 2;
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"%s\n",dlerror());return 2;}
    IfsFsModuleApiEntryV2 get=NULL;
    void *symbol=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);
    if(!symbol || sizeof(get)!=sizeof(symbol))return 2;
    memcpy(&get,&symbol,sizeof(get));
    const IfsFsModuleApiV2 *api=get();
    if(!ifs_fs_api_v2_valid(api,"pfs3"))return 2;
    const IfsFsCoreOpsV2 *core=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *vol=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    if(!core || !vol || !vol->format)return 2;
    Media m={.sector=(uint32_t)strtoul(argv[4],NULL,10),.bytes=128ull*1024u*1024u};
    uint32_t variant=(uint32_t)strtoul(argv[3],NULL,10);
    m.fd=open(argv[2],O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
    if(m.fd<0)return 2;
    int result=1;
    CHECK(ftruncate(m.fd,(off_t)m.bytes)==0);
    IfsFsHostRuntimeV2 h={
        .abi_version=IFS_FS_HOST_ABI_V2,.structure_bytes=sizeof(h),
        .capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE|
                      IFS_FS_HOST_CLOCK|IFS_FS_HOST_CANCEL,
        .volume_bytes=m.bytes,.logical_sector_bytes=m.sector,
        .physical_sector_bytes=m.sector,.maximum_transfer_bytes=m.sector,
        .context=&m,.read_at=rd,.write_at=wr,.flush=flush,
        .allocate=allocate,.release=release,.random_bytes=entropy,
        .monotonic_ns=mono_ns,.realtime_ns=real_ns,.cancelled=cancelled
    },ro=h;
    ro.capabilities=IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_CLOCK|IFS_FS_HOST_CANCEL;
    ro.write_at=NULL;ro.flush=NULL;
    CHECK(ifs_fs_host_v2_valid(&h) && ifs_fs_host_v2_valid(&ro));
    CHECK(vol->format(&ro,variant,"NativePFS",0u)==IFS_FS_ACCESS);
    CHECK(vol->format(&h,2u,"NativePFS",0u)==IFS_FS_UNSUPPORTED);
    CHECK(vol->format(&h,variant,"Invalid:Name",0u)==IFS_FS_INVALID);
    CHECK(!m.writes);
    CHECK(vol->format(&h,variant,"NativePFS",0u)==IFS_FS_OK);
    uint32_t detected=0;void *mount=NULL;
    IfsFsVolumeInfoV2 info={.structure_bytes=sizeof(info)};
    char name[128];
    IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),.name=name,.name_capacity=sizeof(name)};
    CHECK(core->probe(&ro,&detected)==IFS_FS_OK && detected==variant);
    CHECK(core->mount(&ro,&mount,&info)==IFS_FS_OK && mount);
    CHECK(info.variant==variant && info.total_bytes==m.bytes &&
          info.free_bytes>0u && info.free_bytes<m.bytes);
    char label[64];
    uint32_t label_bytes=UINT32_MAX;
    CHECK(vol->label && !vol->set_label &&
          vol->label(mount,label,sizeof(label),&label_bytes)==IFS_FS_OK &&
          label_bytes==9u && !strcmp(label,"NativePFS"));
    CHECK(core->stat(mount,"/",&entry)==IFS_FS_OK && entry.type==IFS_FS_ENTRY_DIRECTORY);
    CHECK(core->directory_entry(mount,"/",0u,&entry)==IFS_FS_END);
    CHECK(vol->format(&h,variant,"NativePFS",0u)==IFS_FS_BUSY);
    CHECK(core->unmount(mount)==IFS_FS_OK);
    CHECK(core->mount(&h,&mount,&info)==IFS_FS_ACCESS && !mount);
    unsigned char root[512],boot[512];
    CHECK(pread(m.fd,root,sizeof(root),1024)==(ssize_t)sizeof(root));
    CHECK(pread(m.fd,boot,sizeof(boot),0)==(ssize_t)sizeof(boot));
    CHECK(!memcmp(root,"PFS\001",4u) && !memcmp(root,boot,4u));
    /* Independently count the reserved and general allocation bitmaps
     * using on-disk geometry, not the provider's cached free summary. */
    uint32_t reserved=(word(root+52)-word(root+56)+1u)/2u;
    uint32_t free_data=word(root+68),first_data=word(root+52)+1u;
    CHECK(first_data+free_data==m.bytes/512u);
    unsigned char *bitmap=malloc((reserved+7u)/8u);
    CHECK(bitmap);
    CHECK(pread(m.fd,bitmap,(reserved+7u)/8u,1548)==(ssize_t)((reserved+7u)/8u));
    uint32_t free_reserved=0;
    for(uint32_t i=0;i<reserved;++i)free_reserved+=(bitmap[i/8u]>>(7u-i%8u))&1u;
    free(bitmap);
    CHECK(free_reserved==word(root+60));
    uint32_t counted=0u;
    for(uint32_t group=0u;group<5u;++group) {
        uint32_t ptr=word(root+96u+4u*group);
        if(!ptr)break;
        unsigned char index[1024],map[1024];
        CHECK(pread(m.fd,index,sizeof(index),(off_t)ptr*512)==(ssize_t)sizeof(index));
        CHECK(index[0]=='M' && index[1]=='I');
        for(uint32_t j=0;j<253u;++j) {
            uint32_t bm=word(index+12u+4u*j);
            if(!bm)break;
            CHECK(pread(m.fd,map,sizeof(map),(off_t)bm*512)==(ssize_t)sizeof(map));
            CHECK(map[0]=='B' && map[1]=='M');
            for(uint32_t bit=0;bit<8096u;++bit)
                counted+=(map[12u+bit/8u]>>(7u-bit%8u))&1u;
        }
    }
    CHECK(counted==free_data && info.free_bytes==(uint64_t)counted*512u);
    puts("PFS3 native PFS1 format, root directory and allocation maps PASS");
    result=0;
done:
    close(m.fd);dlclose(lib);
    if(result)unlink(argv[2]);
    return result;
}
