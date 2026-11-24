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
static int mount_volume(const IfsFsHostRuntimeV2 *host,void **volume,
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
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
             IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
    .probe=probe,.mount=mount_volume,.unmount=unmount_volume,
    .stat=stat_path,.directory_entry=list_path,.read=read_file
};
static const IfsFsVolumeOpsV2 volume_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_QUERY},
    .query=query_volume
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
