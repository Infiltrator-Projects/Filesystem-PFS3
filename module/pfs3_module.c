/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strict Common V2 PFS1/PFS2 read-only probe, mount and volume query.
 * PFS1 and PFS2 are on-media variants, never ABI generations.
 * On-disk metadata and file reads share the native hosted and firmware core.
 * Defragmentation, writes and repair are not yet qualified V2 services.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <infiltratr/filesystem_ro_sessions_v2.h>
#include "pfs3_core.h"
#include "pfs3_reader_v2.h"
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
#include <stddef.h>

static int examine(const IfsFsHostRuntimeV2 *host,uint32_t *variant,
                   IfsFsVolumeInfoV2 *info)
{
    unsigned char raw[512];
    IfsPfs3RootRecord root;
    IfsPfs3Format kind;
    uint32_t count, sector_bytes;
    if(!variant)return IFS_FS_INVALID;
    *variant=0u;
    if(!ifs_fs_host_v2_valid(host))return IFS_FS_INVALID;
    int rc=ifs_pfs3_v2_read_root(host,raw,&root,&sector_bytes,&count);
    if (rc) return rc;
    kind=ifs_pfs3_classify_disk_type(root.disk_type);
    /* The optional PFS root extension is real on-media metadata. Reject
     * damaged extension blocks and outstanding deferred operations before
     * declaring the volume safe for a read-only V2 session. */
    uint32_t filename_bytes=IFS_PFS3_DEFAULT_FILENAME_SIZE;
    if (root.options & IFS_PFS3_MODE_EXTENSION) {
        unsigned char extension_bytes[IFS_PFS3_EXTENSION_MIN_BYTES];
        IfsPfs3ExtensionRecord ext;
        if (root.extension >= count ||
            (uint64_t)root.extension > UINT64_MAX / sector_bytes)
            return IFS_FS_CORRUPT;
        const uint64_t offset=(uint64_t)root.extension*
                              sector_bytes;
        int rc=ifs_fs_host_read_bytes_v2(host,offset,extension_bytes,
                                         (uint32_t)sizeof(extension_bytes));
        if (rc!=IFS_FS_OK) return rc;
        const uint32_t sectors_per_reserved=
            root.reserved_block_size/sector_bytes;
        const uint32_t reserved=
            (root.last_reserved-root.first_reserved+1u)/sectors_per_reserved;
        uint16_t max_name=0u;
        if (ifs_pfs3_decode_extension(extension_bytes,sizeof(extension_bytes),
                                      &ext)!=0 ||
            ifs_pfs3_validate_extension(&ext,reserved,&max_name)!=0)
            return IFS_FS_CORRUPT;
        if (ext.postponed_operation!=IFS_PFS3_POSTPONED_NONE)
            return IFS_FS_UNSUPPORTED;
        filename_bytes=max_name;
    }
    *variant=(uint32_t)kind;
    if(info) {
        info->structure_bytes=sizeof(*info);
        info->variant=*variant;
        info->max_name_bytes=(root.options & IFS_PFS3_MODE_LONGFN)?
            filename_bytes:IFS_PFS3_DEFAULT_FILENAME_SIZE;
        info->block_bytes=sector_bytes;
        info->total_bytes=(uint64_t)count*sector_bytes;
        info->free_bytes=(uint64_t)root.blocks_free*
                         sector_bytes;
    }
    return IFS_FS_OK;
}
static int probe(const IfsFsHostRuntimeV2 *host,uint32_t *variant)
{
    return examine(host,variant,NULL);
}
/* Common V2 does not allow a process-wide one-volume singleton. Track
 * independent bounded read-only metadata sessions with no reused handles.
 * This does not advertise any unimplemented directory/file operation. */
static IfsFsReadOnlySessionsV2 sessions = IFS_FS_RO_SESSIONS_INIT_V2;
/* Associate the opaque Common V2 cookie with a size-versioned host snapshot.
 * The existing Common registry owns lifecycle and rejects stale cookies. */
typedef struct {
    uintptr_t cookie;
    unsigned active;
    IfsFsHostRuntimeV2 host;
} PfsSession;
static PfsSession readers[IFS_FS_RO_MAX_SESSIONS_V2];

static PfsSession *acquire(void *volume)
{
    uintptr_t token=(uintptr_t)volume;
    PfsSession *result=NULL;
    if (!token) return NULL;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for (unsigned i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i) {
        if (readers[i].cookie==token &&
            sessions.slots[i].cookie==token &&
            readers[i].active!=UINT_MAX) {
            ++readers[i].active;
            result=&readers[i];
            break;
        }
    }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return result;
}
static int finish(PfsSession *s,int rc)
{
    ifs_fs_ro_sessions_lock_v2(&sessions);
    --s->active;
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return rc;
}
static int mount_volume_impl(const IfsFsHostRuntimeV2 *host,void **volume,
                        IfsFsVolumeInfoV2 *info)
{
    IfsFsVolumeInfoV2 measured={0};
    uint32_t variant=0u;
    int rc;
    if (!volume || !info || info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *volume=NULL;
    if (!ifs_fs_host_v2_valid(host)) return IFS_FS_INVALID;
    if (!(host->capabilities & IFS_FS_HOST_READ_ONLY)) return IFS_FS_ACCESS;
    rc=examine(host,&variant,&measured);
    if (rc!=IFS_FS_OK) return rc;
    rc=ifs_pfs3_v2_reader_validate(host);
    if (rc!=IFS_FS_OK) return rc;
    void *token=NULL;
    rc=ifs_fs_ro_session_open_v2(&sessions,&measured,&token,info);
    if (rc!=IFS_FS_OK)return rc;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for (uint32_t i=0u;i<IFS_FS_RO_MAX_SESSIONS_V2;++i) {
        if (sessions.slots[i].cookie==(uintptr_t)token) {
            size_t bytes=host->structure_bytes<sizeof(readers[i].host)?
                         host->structure_bytes:sizeof(readers[i].host);
            memset(&readers[i].host,0,sizeof(readers[i].host));
            memcpy(&readers[i].host,host,bytes);
            readers[i].host.structure_bytes=(uint32_t)bytes;
            readers[i].cookie=(uintptr_t)token;
            *volume=token;
            break;
        }
    }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    if (!*volume) {
        /* Never publish success without the corresponding native reader. */
        (void)ifs_fs_ro_session_close_v2(&sessions,token);
        return IFS_FS_INVALID;
    }
    return IFS_FS_OK;
}
static int unmount_volume(void *volume)
{
    if (!volume) return IFS_FS_INVALID;
    int rc=IFS_FS_INVALID;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for (unsigned i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i) {
        if (readers[i].cookie!=(uintptr_t)volume ||
            sessions.slots[i].cookie!=(uintptr_t)volume) continue;
        if (readers[i].active) rc=IFS_FS_BUSY;
        else {
            sessions.slots[i]=(IfsFsReadOnlySessionSlotV2){0};
            readers[i]=(PfsSession){0};
            rc=IFS_FS_OK;
        }
        break;
    }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return rc;
}
static int query_volume(void *volume,IfsFsVolumeInfoV2 *info)
{
    return ifs_fs_ro_session_query_v2(&sessions,volume,info);
}
static int stat_path(void *volume,const char *path,IfsFsEntryV2 *entry)
{
    PfsSession *session=acquire(volume);
    return session?finish(session,ifs_pfs3_v2_reader_stat(&session->host,path,entry)):IFS_FS_INVALID;
}
static int list_path(void *volume,const char *path,uint32_t index,
                     IfsFsEntryV2 *entry)
{
    PfsSession *session=acquire(volume);
    return session?finish(session,ifs_pfs3_v2_reader_list(&session->host,path,index,entry)):IFS_FS_INVALID;
}
static int read_file(void *volume,const char *path,uint64_t offset,
                     void *out,uint32_t length)
{
    PfsSession *session=acquire(volume);
    return session?finish(session,ifs_pfs3_v2_reader_read(&session->host,path,offset,out,length)):IFS_FS_INVALID;
}
#ifndef IFS_PFS3_FIRMWARE_BUILD
/* Classic PFS1 on-media layout as used by PFS3aio 19.2. Reference:
 * original/pfs3aio/format.c and the retained AmiPart format description.
 * The initial profile supports direct indexes, 512-byte filesystem sectors
 * and 1024-byte reserved blocks. Creation uses a single clean initial
 * generation; no old copy-on-write generations are needed on new media.
 * All writes use the exclusive Common V2 lease, never an OS filesystem. */
enum { FMT_SECTOR=512u,FMT_RESERVED=1024u,FMT_BITS=8096u,
       FMT_POINTERS=253u,FMT_MAX_INDEX=5u };
static atomic_flag format_gate=ATOMIC_FLAG_INIT;
static void fmt16(uint8_t *p,uint32_t x)
{p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void fmt32(uint8_t *p,uint32_t x)
{fmt16(p,x>>16);fmt16(p+2,x);}
static void fmt_header(uint8_t *p,uint32_t id,uint32_t seq)
{
    memset(p,0,FMT_RESERVED);
    fmt16(p,id);fmt32(p+4,1u);fmt32(p+8,seq);
}
static int fmt_write(const IfsFsHostRuntimeV2 *h,uint32_t sector,
                     const void *p,uint32_t bytes)
{
    if(h->cancelled && h->cancelled(h->context))return IFS_FS_BUSY;
    return ifs_fs_host_write_bytes_v2(h,(uint64_t)sector*FMT_SECTOR,p,bytes);
}
static void fmt_set(uint8_t *p,uint32_t bit)
{p[bit/8u]|=(uint8_t)(0x80u>>(bit%8u));}
static uint32_t fmt_reserved_count(uint32_t sectors)
{
    /* Historical graduated reserved-area sizing, rounded to 32 slots. */
    static const uint32_t span[]={20480u,51200u,512000u,1048567u,10000000u};
    static const uint32_t divisor[]={20u,30u,40u,50u,70u};
    uint32_t remaining=sectors/2u,result=10u;
    for(unsigned i=0;i<5u;++i) {
        const uint32_t used=remaining<span[i]?remaining:span[i];
        result+=used/divisor[i];remaining-=used;
    }
    return (result+remaining/80u+31u)&~31u;
}
static int format_impl(const IfsFsHostRuntimeV2 *h,const char *label)
{
    const uint32_t total=(uint32_t)(h->volume_bytes/FMT_SECTOR);
    const uint32_t reserved=fmt_reserved_count(total);
    const uint32_t cluster=(524u+reserved/8u+FMT_RESERVED-1u)/FMT_RESERVED;
    const uint32_t first_data=2u+reserved*2u;
    if(first_data+64u>=total || cluster*2u>IFS_PFS3_MAX_ROOT_CLUSTER)
        return IFS_FS_UNSUPPORTED;
    const uint32_t free_data=total-first_data;
    const uint32_t maps=(free_data+FMT_BITS-1u)/FMT_BITS;
    const uint32_t indices=(maps+FMT_POINTERS-1u)/FMT_POINTERS;
    const uint32_t used=cluster+1u+indices+maps+3u+2u;
    if(!indices || indices>FMT_MAX_INDEX || used>=reserved)
        return IFS_FS_UNSUPPORTED;
    const uint32_t rootbytes=cluster*FMT_RESERVED;
    uint8_t *root=h->allocate(h->context,rootbytes);
    if(!root)return IFS_FS_NO_MEMORY;
    memset(root,0,rootbytes);
    uint8_t block[FMT_RESERVED]={0},index[FMT_RESERVED];
    uint32_t day=0u,minute=0u,tick=0u;
    if(h->realtime_ns) {
        uint64_t sec=h->realtime_ns(h->context)/1000000000ull;
        if(sec>=252460800ull && (sec-252460800ull)/86400ull<=UINT16_MAX) {
            sec-=252460800ull;
            day=(uint32_t)(sec/86400ull);
            minute=(uint32_t)(sec%86400ull/60ull);
            tick=(uint32_t)(sec%60ull)*50u;
        }
    }
    int rc=IFS_FS_OK;
    /* Probe recognizes the root without requiring a boot signature.
     * Remove candidate root signatures at every supported sector size
     * before writing any metadata, so interrupted formatting fails closed. */
    for(uint32_t i=0;i<=16u;i+=2u) {
        rc=fmt_write(h,i,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    }
    if(h->flush(h->context)!=0){rc=IFS_FS_IO_ERROR;goto done;}
    uint32_t next=2u+cluster*2u;
    const uint32_t extension=next;next+=2u;
    uint32_t mapseq=0u;
    for(uint32_t group=0;group<indices;++group) {
        const uint32_t mi=next;next+=2u;
        fmt32(root+96u+group*4u,mi);
        fmt_header(index,0x4d49u,group);
        for(uint32_t i=0;i<FMT_POINTERS && mapseq<maps;++i,++mapseq) {
            const uint32_t bm=next;next+=2u;
            fmt32(index+12u+4u*i,bm);
            fmt_header(block,0x424du,mapseq);
            /* PFS uses MSB-first free bits relative to first data sector.
             * Bits beyond the physical volume remain unavailable. */
            for(uint32_t bit=0;bit<FMT_BITS;++bit)
                if((uint64_t)mapseq*FMT_BITS+bit<free_data)
                    fmt_set(block+12u,bit);
            rc=fmt_write(h,bm,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
        }
        rc=fmt_write(h,mi,index,sizeof(index));if(rc!=IFS_FS_OK)goto done;
    }
    const uint32_t ib=next,ab=next+2u,db=next+4u;
    const uint32_t dd0=next+6u,dd1=next+8u;next+=10u;
    if(next!=2u+used*2u){rc=IFS_FS_CORRUPT;goto done;}
    fmt_header(block,0x4942u,0u);fmt32(block+12,ab);
    rc=fmt_write(h,ib,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    fmt_header(block,0x4142u,0u);
    for(uint32_t i=0;i<5u;++i)fmt32(block+20u+i*12u,UINT32_MAX);
    fmt32(block+76,1u);fmt32(block+80,db);
    rc=fmt_write(h,ab,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    fmt_header(block,0x4442u,0u);fmt32(block+12,5u);
    rc=fmt_write(h,db,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    for(uint32_t i=0;i<2u;++i) {
        fmt_header(block,0x4444u,i);fmt32(block+22,5u);
        fmt16(block+26,day);fmt16(block+28,minute);fmt16(block+30,tick);
        rc=fmt_write(h,i?dd1:dd0,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    }
    memset(block,0,sizeof(block));fmt16(block,0x4558u);
    fmt32(block+8,1u);fmt32(block+12,0x00130002u);
    fmt16(block+16,day);fmt16(block+18,minute);fmt16(block+20,tick);
    fmt16(block+22,day);fmt16(block+24,minute);fmt16(block+26,tick);
    fmt32(block+44,used-1u);fmt16(block+54,2u);fmt16(block+56,107u);
    fmt32(block+144,dd0);fmt32(block+148,dd1);
    rc=fmt_write(h,extension,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    fmt32(root,IFS_PFS3_DISK_PFS1);
    fmt32(root+4,IFS_PFS3_MODE_HARDDISK|IFS_PFS3_MODE_SPLITTED_ANODES|
                 IFS_PFS3_MODE_DIR_EXTENSION|IFS_PFS3_MODE_SIZEFIELD|
                 IFS_PFS3_MODE_EXTENSION|IFS_PFS3_MODE_DATESTAMP|
                 IFS_PFS3_MODE_EXTROVING|IFS_PFS3_MODE_LONGFN|
                 IFS_PFS3_MODE_DELDIR|IFS_PFS3_MODE_SUPERDELDIR);
    fmt32(root+8,1u);fmt16(root+12,day);fmt16(root+14,minute);
    fmt16(root+16,tick);fmt16(root+18,0xf0u);
    root[20]=(uint8_t)strlen(label);memcpy(root+21,label,root[20]);
    fmt32(root+52,first_data-1u);fmt32(root+56,2u);
    fmt32(root+60,reserved-used);fmt16(root+64,FMT_RESERVED);
    fmt16(root+66,cluster*2u);fmt32(root+68,free_data);
    fmt32(root+72,free_data/20u);fmt32(root+84,total);
    fmt32(root+88,extension);fmt32(root+116,ib);
    fmt16(root+512,0x424du);
    for(uint32_t i=used;i<reserved;++i)fmt_set(root+524,i);
    /* The primary root signature is the commit point: flush every
     * supporting record, including the reserved bitmap and boot block. */
    rc=fmt_write(h,3u,root+512,rootbytes-512u);if(rc!=IFS_FS_OK)goto done;
    memset(block,0,sizeof(block));fmt32(block,IFS_PFS3_DISK_PFS1);
    rc=fmt_write(h,0u,block,sizeof(block));if(rc!=IFS_FS_OK)goto done;
    if(h->flush(h->context)!=0){rc=IFS_FS_IO_ERROR;goto done;}
    rc=fmt_write(h,2u,root,512u);if(rc!=IFS_FS_OK)goto done;
    if(h->flush(h->context)!=0) {
        memset(block,0,sizeof(block));
        (void)ifs_fs_host_write_bytes_v2(h,1024u,block,512u);
        (void)h->flush(h->context);rc=IFS_FS_IO_ERROR;goto done;
    }
    /* Exercise the same production reader used for subsequent mounts. */
    IfsFsHostRuntimeV2 ro={0};
    const size_t n=h->structure_bytes<sizeof(ro)?h->structure_bytes:sizeof(ro);
    memcpy(&ro,h,n);ro.structure_bytes=(uint32_t)n;
    ro.capabilities=IFS_FS_HOST_READ_ONLY;ro.write_at=NULL;ro.flush=NULL;
    rc=ifs_pfs3_v2_reader_validate(&ro);
done:
    h->release(h->context,root);return rc;
}
static int format_volume(const IfsFsHostRuntimeV2 *h,uint32_t variant,
                          const char *label,uint32_t flags)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY) ||
       !(h->capabilities&IFS_FS_HOST_EXCLUSIVE) ||
       !(h->capabilities&IFS_FS_HOST_DURABLE))return IFS_FS_ACCESS;
    if(variant!=IFS_PFS3_FORMAT_PFS1 || flags ||
       h->logical_sector_bytes>4096u || h->volume_bytes%4096u ||
       h->volume_bytes<16ull*1024u*1024u ||
       h->volume_bytes>2ull*1024u*1024u*1024u)return IFS_FS_UNSUPPORTED;
    if(!label || !*label)label="Untitled";
    const size_t n=strlen(label);
    if(n>31u)return IFS_FS_INVALID;
    for(size_t i=0;i<n;++i)
        if((unsigned char)label[i]<32u || (unsigned char)label[i]>126u ||
           label[i]==':' || label[i]=='/')return IFS_FS_INVALID;
    if(atomic_flag_test_and_set_explicit(&format_gate,memory_order_acquire))
        return IFS_FS_BUSY;
    int rc=IFS_FS_OK;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for(uint32_t i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i)
        if(sessions.slots[i].cookie){rc=IFS_FS_BUSY;break;}
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    if(rc==IFS_FS_OK)rc=format_impl(h,label);
    atomic_flag_clear_explicit(&format_gate,memory_order_release);
    return rc;
}
#endif
static int mount_volume(const IfsFsHostRuntimeV2 *h,void **volume,IfsFsVolumeInfoV2 *info)
{
#ifndef IFS_PFS3_FIRMWARE_BUILD
    if(atomic_flag_test_and_set_explicit(&format_gate,memory_order_acquire)) {
        if(volume)*volume=NULL;
        return IFS_FS_BUSY;
    }
#endif
    int rc=mount_volume_impl(h,volume,info);
#ifndef IFS_PFS3_FIRMWARE_BUILD
    atomic_flag_clear_explicit(&format_gate,memory_order_release);
#endif
    return rc;
}

static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
             IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
    .probe=probe,.mount=mount_volume,.unmount=unmount_volume,
    .stat=stat_path,.directory_entry=list_path,.read=read_file
};
static const IfsFsVolumeOpsV2 volume_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_QUERY
#ifndef IFS_PFS3_FIRMWARE_BUILD
             |IFS_FS_VOLUME_FORMAT
#endif
    },
    .query=query_volume
#ifndef IFS_PFS3_FIRMWARE_BUILD
    ,.format=format_volume
#endif
};
static const IfsFsGroupHeaderV2 *query(
    uint32_t id,uint32_t minimum,uint32_t maximum)
{
    if(minimum>IFS_FS_GROUP_ABI_V2 || maximum<IFS_FS_GROUP_ABI_V2)
        return NULL;
    if(id==IFS_FS_GROUP_CORE)return &core.header;
    if(id==IFS_FS_GROUP_VOLUME)return &volume_ops.header;
    return NULL;
}
__attribute__((used,retain,section(".ifsmeta")))
static const char identity[]="IFS-MODULE:pfs3:common2";
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="pfs3",
#ifdef IFS_PFS3_FIRMWARE_BUILD
    .runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void) {return &api;}
