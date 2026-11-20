/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strict Common V2 PFS1/PFS2 read-only probe, mount and volume query.
 * PFS1 and PFS2 are on-media variants, never ABI generations.
 * On-disk metadata comes from the same canonical PFS3 core as pfs3.ko.
 * Defragmentation, writes and repair are not yet qualified V2 services.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <infiltratr/filesystem_ro_sessions_v2.h>
#include "pfs3_core.h"
#include <stdint.h>
#include <stddef.h>

static int examine(const IfsFsHostRuntimeV2 *host,uint32_t *variant,
                   IfsFsVolumeInfoV2 *info)
{
    unsigned char raw[512];
    IfsPfs3RootRecord root;
    IfsPfs3Format kind;
    uint64_t count;
    if(!variant || !ifs_fs_host_v2_valid(host))
        return IFS_FS_INVALID;
    *variant=0u;
    if(host->volume_bytes<1536u)return IFS_FS_NOT_FOUND;
    {
        /* Partition-relative PFS3 roots begin at byte 1024 even on 4Kn. */
        int rc=ifs_fs_host_read_bytes_v2(host,1024u,raw,
                                        (uint32_t)sizeof(raw));
        if(rc!=IFS_FS_OK)return rc;
    }
    if(ifs_pfs3_decode_root(raw,sizeof(raw),&root)!=0)
        return IFS_FS_CORRUPT;
    kind=ifs_pfs3_classify_disk_type(root.disk_type);
    if(kind==IFS_PFS3_FORMAT_INVALID)return IFS_FS_NOT_FOUND;
    count=host->volume_bytes/host->logical_sector_bytes;
    if(count>UINT32_MAX)return IFS_FS_UNSUPPORTED;
    if(ifs_pfs3_validate_root_record(&root,host->logical_sector_bytes,
                                    (uint32_t)count)!=IFS_PFS3_MEDIA_OK ||
       root.blocks_free>count)
        return IFS_FS_CORRUPT;
    /* The optional PFS root extension is real on-media metadata. Reject
     * damaged extension blocks and outstanding deferred operations before
     * declaring the volume safe for a read-only V2 session. */
    uint32_t filename_bytes=IFS_PFS3_DEFAULT_FILENAME_SIZE;
    if (root.options & IFS_PFS3_MODE_EXTENSION) {
        unsigned char extension_bytes[IFS_PFS3_EXTENSION_MIN_BYTES];
        IfsPfs3ExtensionRecord ext;
        if (root.extension >= count ||
            (uint64_t)root.extension > UINT64_MAX / host->logical_sector_bytes)
            return IFS_FS_CORRUPT;
        const uint64_t offset=(uint64_t)root.extension*
                              host->logical_sector_bytes;
        int rc=ifs_fs_host_read_bytes_v2(host,offset,extension_bytes,
                                         (uint32_t)sizeof(extension_bytes));
        if (rc!=IFS_FS_OK) return rc;
        const uint32_t reserved=(root.last_reserved-root.first_reserved+1u)*
                                host->logical_sector_bytes/root.reserved_block_size;
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
        info->block_bytes=host->logical_sector_bytes;
        info->total_bytes=count*host->logical_sector_bytes;
        info->free_bytes=(uint64_t)root.blocks_free*
                         host->logical_sector_bytes;
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
    return ifs_fs_ro_session_open_v2(&sessions,&measured,volume,info);
}
static int unmount_volume(void *volume)
{
    return ifs_fs_ro_session_close_v2(&sessions,volume);
}
static int query_volume(void *volume,IfsFsVolumeInfoV2 *info)
{
    return ifs_fs_ro_session_query_v2(&sessions,volume,info);
}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT},
    .probe=probe,.mount=mount_volume,.unmount=unmount_volume
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
__attribute__((used,section(".ifsmeta")))
static const char identity[]="IFS-MODULE:pfs3:common2";
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="pfs3",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void) {return &api;}
