/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Use the portable PFS3 core *from PFS3.so*, not a separate test copy. */
#include "pfs3_core.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "PFS3 canonical SO core failed: %s\\n", #c); return 1; } } while (0)

int main(void)
{
    const unsigned char anode[IFS_PFS3_ANODE_BYTES] = {
        0, 0, 0, 2, /* cluster length */
        0, 0, 0, 3, /* starting block */
        0, 0, 0, 0  /* next anode */
    };
    IfsPfs3AnodeRecord record = {0};

    CHECK(ifs_pfs3_decode_anode(anode, sizeof(anode), &record) == 0);
    CHECK(record.cluster_size == 2U);
    CHECK(record.block_number == 3U);
    CHECK(ifs_pfs3_validate_anode_extent(&record, 5U) == 0);
    CHECK(ifs_pfs3_validate_anode_extent(&record, 4U) != 0);
    CHECK(ifs_pfs3_decode_anode(anode, sizeof(anode) - 1U, &record) != 0);
    CHECK(ifs_pfs3_decode_anode(NULL, sizeof(anode), &record) != 0);
    return 0;
}
