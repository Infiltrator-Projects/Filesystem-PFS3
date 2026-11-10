/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strict Common V2 provider over source-owned PFS3 canonical core.
 * Disk signatures PFS1/PFS2 identify on-media variants, NOT ABI generations.
 * Existing worker/relocation/repair code remains local until V2 ported.
 */
#include <infiltratr/filesystem_module_v2.h>
#include "pfs3_core.h"
#include <stdint.h>
static int probe(const IfsFsHostRuntimeV2 *host,uint32_t *variant)
{
    unsigned char raw[512];
    IfsPfs3RootRecord root;
    IfsPfs3Format kind;
    uint64_t count;
    if(!variant || !ifs_fs_host_v2_valid(host))
        return IFS_FS_INVALID;
    *variant=0u;
    if(host->volume_bytes<1536u || host->maximum_transfer_bytes<sizeof(raw))
        return IFS_FS_NOT_FOUND;
    if(host->read_at(host->context,1024u,raw,sizeof(raw))!=0)
        return IFS_FS_IO_ERROR;
    if(ifs_pfs3_decode_root(raw,sizeof(raw),&root)!=0)
        return IFS_FS_CORRUPT;
    kind=ifs_pfs3_classify_disk_type(root.disk_type);
    if(kind==IFS_PFS3_FORMAT_INVALID)
        return IFS_FS_NOT_FOUND;
    count=host->volume_bytes/host->logical_sector_bytes;
    if(count>UINT32_MAX)
        return IFS_FS_UNSUPPORTED;
    if(ifs_pfs3_validate_root_record(&root,host->logical_sector_bytes,
                                    (uint32_t)count)!=IFS_PFS3_MEDIA_OK)
        return IFS_FS_CORRUPT;
    *variant=(uint32_t)kind;
    return IFS_FS_OK;
}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE},
    .probe=probe
};
static const IfsFsGroupHeaderV2 *query(
    uint32_t id,uint32_t min,uint32_t max)
{
    if(id!=IFS_FS_GROUP_CORE || min>IFS_FS_GROUP_ABI_V2 ||
       max<IFS_FS_GROUP_ABI_V2) return NULL;
    return &core.header;
}
__attribute__((used,section(".ifsmeta")))
static const char identity[]="IFS-MODULE:pfs3:common2";
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="pfs3",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void) {return &api;}
