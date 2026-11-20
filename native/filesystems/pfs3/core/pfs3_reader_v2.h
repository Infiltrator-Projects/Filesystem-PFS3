/* SPDX-License-Identifier: GPL-3.0-or-later
 * Source-owned Common V2 PFS3 direct-index read-only engine.
 */
#ifndef IFS_PFS3_READER_V2_H
#define IFS_PFS3_READER_V2_H
#include <infiltratr/filesystem_module_v2.h>
int ifs_pfs3_v2_reader_validate(const IfsFsHostRuntimeV2 *host);
int ifs_pfs3_v2_reader_stat(const IfsFsHostRuntimeV2 *host,
                            const char *path, IfsFsEntryV2 *entry);
int ifs_pfs3_v2_reader_list(const IfsFsHostRuntimeV2 *host,
                            const char *path, uint32_t index,
                            IfsFsEntryV2 *entry);
int ifs_pfs3_v2_reader_read(const IfsFsHostRuntimeV2 *host,
                            const char *path, uint64_t offset,
                            void *destination, uint32_t count);
#endif
