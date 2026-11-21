/* SPDX-License-Identifier: GPL-3.0-or-later
 * Portable read-only PFS3 direct-index/anode/directory/file decoder.
 * This is the same canonical on-disk core used by the hosted provider;
 * Linux VFS, FUSE, host paths and writable I/O are not involved.
 * Superindexes, large files, symbolic links and hard links fail closed.
 */
#include "pfs3_core.h"
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { ROOT_ANODE=5u,
       INDEX_ID=0x4942u, INDEX_COUNT=99u, MAX_CHAIN=1048576u };
typedef struct {
    const IfsFsHostRuntimeV2 *host;
    IfsPfs3RootRecord root;
    uint32_t index[INDEX_COUNT];
    uint32_t media_sectors, sector_bytes, reserved_sectors;
    uint16_t max_name;
} Reader;
typedef struct {
    uint32_t anode;
    uint32_t bytes;
    int type;
    uint8_t name_length;
    char name[IFS_PFS3_MAX_FILENAME_SIZE+1u];
} File;
static uint16_t be16(const unsigned char *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|p[3];
}
static int rd(const Reader *r,uint64_t at,void *dst,uint32_t bytes)
{
    if (at>r->host->volume_bytes || bytes>r->host->volume_bytes-at)
        return IFS_FS_CORRUPT;
    return bytes?ifs_fs_host_read_bytes_v2(r->host,at,dst,bytes):IFS_FS_OK;
}
static int reserved(const Reader *r,uint32_t sector)
{
    return sector>=r->root.first_reserved &&
           sector<=r->root.last_reserved &&
           (sector-r->root.first_reserved)%r->reserved_sectors==0u &&
           sector<=r->root.last_reserved-(r->reserved_sectors-1u);
}
static int reserved_read(const Reader *r,uint32_t sector,unsigned char *out)
{
    if (!reserved(r,sector))return IFS_FS_CORRUPT;
    return rd(r,(uint64_t)sector*r->sector_bytes,out,r->root.reserved_block_size);
}
/* PFS places its root at logical disk block two. The partition block size
 * and the host BlockIo transfer size are independent. An identified PFS
 * root with invalid geometry is corruption, including a stale size field. */
int ifs_pfs3_v2_read_root(const IfsFsHostRuntimeV2 *host,uint8_t raw[512],
                          IfsPfs3RootRecord *root,uint32_t *sector_bytes,
                          uint32_t *media_sectors)
{
    if (!ifs_fs_host_v2_valid(host) || !raw || !root || !sector_bytes || !media_sectors)
        return IFS_FS_INVALID;
    for (uint32_t block=512;block<=4096;block*=2) {
        uint64_t at=(uint64_t)block*2u;
        if (at+512>host->volume_bytes) break;
        int rc=ifs_fs_host_read_bytes_v2(host,at,raw,512);
        if (rc) return rc;
        if (ifs_pfs3_classify_disk_type(be32(raw))==IFS_PFS3_FORMAT_INVALID) continue;
        uint64_t count=host->volume_bytes/block;
        if (count>UINT32_MAX) return IFS_FS_UNSUPPORTED;
        if (host->volume_bytes%block || ifs_pfs3_decode_root(raw,512,root) ||
            ifs_pfs3_validate_root_record(root,block,(uint32_t)count)!=IFS_PFS3_MEDIA_OK ||
            root->blocks_free>count) return IFS_FS_CORRUPT;
        *sector_bytes=block;
        *media_sectors=(uint32_t)count;
        return IFS_FS_OK;
    }
    return IFS_FS_NOT_FOUND;
}
static int start(const IfsFsHostRuntimeV2 *host,Reader *r)
{
    unsigned char root[512];
    if (!r || !ifs_fs_host_v2_valid(host))return IFS_FS_INVALID;
    if (!(host->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    memset(r,0,sizeof(*r));
    r->host=host;
    int rc=ifs_pfs3_v2_read_root(host,root,&r->root,&r->sector_bytes,&r->media_sectors);
    if (rc) return rc;
    if (!(r->root.options&IFS_PFS3_MODE_HARDDISK) ||
        r->root.options&(IFS_PFS3_MODE_SUPERINDEX|
                         IFS_PFS3_MODE_LARGEFILE))
        return IFS_FS_UNSUPPORTED;
    r->reserved_sectors=r->root.reserved_block_size/r->sector_bytes;
    if (!r->reserved_sectors)return IFS_FS_CORRUPT;
    r->max_name=IFS_PFS3_DEFAULT_FILENAME_SIZE;
    for (uint32_t i=0u;i<INDEX_COUNT;++i) {
        r->index[i]=be32(root+116u+i*4u);
        if (r->index[i] && !reserved(r,r->index[i]))
            return IFS_FS_CORRUPT;
    }
    if (r->root.options&IFS_PFS3_MODE_EXTENSION) {
        unsigned char block[4096];
        IfsPfs3ExtensionRecord ext;
        uint16_t name=0u;
        rc=reserved_read(r,r->root.extension,block);
        if (rc!=IFS_FS_OK)return rc;
        if (ifs_pfs3_decode_extension(block,r->root.reserved_block_size,&ext) ||
            ifs_pfs3_validate_extension(&ext,
                (r->root.last_reserved-r->root.first_reserved+1u)/
                r->reserved_sectors,&name))
            return IFS_FS_CORRUPT;
        if (ext.postponed_operation!=IFS_PFS3_POSTPONED_NONE)
            return IFS_FS_UNSUPPORTED;
        r->max_name=name;
    }
    return IFS_FS_OK;
}
static int anode(const Reader *r,uint32_t number,IfsPfs3AnodeRecord *out)
{
    unsigned char block[4096];
    const uint32_t nodes=(r->root.reserved_block_size-
                          IFS_PFS3_ANODEBLOCK_HEADER_BYTES)/IFS_PFS3_ANODE_BYTES;
    const uint32_t indices=(r->root.reserved_block_size-12u)/4u;
    if (!number || !nodes || !indices)return IFS_FS_CORRUPT;
    uint32_t sequence=(r->root.options&IFS_PFS3_MODE_SPLITTED_ANODES)?
        number>>16:number/nodes;
    uint32_t slot=(r->root.options&IFS_PFS3_MODE_SPLITTED_ANODES)?
        number&0xffffu:number%nodes;
    if (slot>=nodes)return IFS_FS_CORRUPT;
    uint32_t index_number=sequence/indices, index_offset=sequence%indices;
    if (index_number>=INDEX_COUNT || !r->index[index_number])
        return IFS_FS_CORRUPT;
    int rc=reserved_read(r,r->index[index_number],block);
    if (rc!=IFS_FS_OK)return rc;
    if (be16(block)!=INDEX_ID || be32(block+8u)!=index_number)
        return IFS_FS_CORRUPT;
    uint32_t target=be32(block+12u+index_offset*4u);
    rc=reserved_read(r,target,block);
    if (rc!=IFS_FS_OK)return rc;
    IfsPfs3AnodeBlockView view;
    if (ifs_pfs3_decode_anode_block(block,r->root.reserved_block_size,&view) ||
        view.sequence!=sequence)return IFS_FS_CORRUPT;
    uint32_t off=IFS_PFS3_ANODEBLOCK_HEADER_BYTES+
                 slot*IFS_PFS3_ANODE_BYTES;
    if (ifs_pfs3_decode_anode(block+off,
            r->root.reserved_block_size-off,out))return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
static int equal(const char *a,size_t n,const char *b,size_t m)
{
    if (n!=m)return 0;
    for(size_t i=0;i<n;++i) {
        unsigned char x=(unsigned char)a[i],y=(unsigned char)b[i];
        if(x>='a'&&x<='z')x-='a'-'A';
        if(y>='a'&&y<='z')y-='a'-'A';
        if(x!=y)return 0;
    }
    return 1;
}
/* Traverse the full chain of reserved directory blocks, not an in-memory
 * fixture namespace. Each entry is decoded by the shared PFS3 native core. */
static int lookup(const Reader *r,uint32_t directory,
                  const char *name,size_t name_len,uint32_t index,File *found)
{
    unsigned char block[4096];
    uint32_t current=directory,guard=0u;
    uint64_t ordinal=0u;
    while(current) {
        IfsPfs3AnodeRecord node;
        if(++guard>MAX_CHAIN)return IFS_FS_CORRUPT;
        int rc=anode(r,current,&node);
        if(rc!=IFS_FS_OK)return rc;
        if (!node.cluster_size || !reserved(r,node.block_number))
            return IFS_FS_CORRUPT;
        for(uint32_t i=0u;i<node.cluster_size;++i) {
            uint64_t pos=(uint64_t)node.block_number+
                          (uint64_t)i*r->reserved_sectors;
            if(pos>UINT32_MAX || !reserved(r,(uint32_t)pos))
                return IFS_FS_CORRUPT;
            rc=reserved_read(r,(uint32_t)pos,block);
            if(rc!=IFS_FS_OK)return rc;
            IfsPfs3DirBlockView view;
            uint32_t entries=0u;
            int extended=!!(r->root.options&IFS_PFS3_MODE_DIR_EXTENSION);
            if(ifs_pfs3_decode_directory_block(block,
                r->root.reserved_block_size,extended,&view,&entries) ||
                view.directory_anode!=directory)return IFS_FS_CORRUPT;
            uint32_t off=IFS_PFS3_DIRBLOCK_HEADER_BYTES;
            for(uint32_t j=0u;j<entries;++j) {
                IfsPfs3DirEntryView e;
                if(ifs_pfs3_decode_directory_entry(block+off,
                      r->root.reserved_block_size-off,extended,&e)
                        !=IFS_PFS3_DIRENTRY_OK ||
                   !e.record_bytes || e.name_length>r->max_name ||
                   e.anode<6u)return IFS_FS_CORRUPT;
                const char *disk=(const char *)(block+off+
                                  IFS_PFS3_DIRENTRY_NAME_OFFSET);
                for(uint32_t k=0u;k<e.name_length;++k)
                    if((unsigned char)disk[k]>=128u)return IFS_FS_UNSUPPORTED;
                if((name && equal(name,name_len,disk,e.name_length)) ||
                   (!name && ordinal==index)) {
                    found->anode=e.anode;
                    found->bytes=e.file_size_low;
                    found->type=e.type;
                    found->name_length=e.name_length;
                    memcpy(found->name,disk,e.name_length);
                    found->name[e.name_length]='\0';
                    return IFS_FS_OK;
                }
                ++ordinal;
                off+=e.record_bytes;
            }
        }
        current=node.next_anode;
    }
    return name?IFS_FS_NOT_FOUND:IFS_FS_END;
}
static int resolve(const Reader *r,const char *path,File *out)
{
    if(!path || path[0]!='/')return IFS_FS_INVALID;
    if(!path[1]) {
        out->anode=ROOT_ANODE;out->bytes=0u;out->type=2;
        out->name[0]='/';out->name[1]='\0';out->name_length=1u;
        return IFS_FS_OK;
    }
    const char *cursor=path+1;
    uint32_t parent=ROOT_ANODE;
    for(unsigned depth=0u;depth<128u;++depth) {
        const char *end=cursor;
        while(*end && *end!='/') {
            if((size_t)(end-cursor)>=IFS_PFS3_MAX_FILENAME_SIZE)
                return IFS_FS_INVALID;
            ++end;
        }
        size_t length=(size_t)(end-cursor);
        if(!length || (length==1u && cursor[0]=='.') ||
           (length==2u && cursor[0]=='.' && cursor[1]=='.'))
            return IFS_FS_INVALID;
        if(length>r->max_name)return IFS_FS_NOT_FOUND;
        int rc=lookup(r,parent,cursor,length,0u,out);
        if(rc!=IFS_FS_OK)return rc;
        if(!*end)return IFS_FS_OK;
        if(out->type!=2)return IFS_FS_NOT_FOUND;
        parent=out->anode;cursor=end+1;
    }
    return IFS_FS_CORRUPT;
}
static int present(const File *file,IfsFsEntryV2 *out)
{
    if(!out || out->structure_bytes<sizeof(*out) || !out->name)
        return IFS_FS_INVALID;
    if(file->type!=2 && file->type!=-3)return IFS_FS_UNSUPPORTED;
    if(file->name_length>=out->name_capacity)return IFS_FS_NO_SPACE;
    memcpy(out->name,file->name,file->name_length);
    out->name[file->name_length]='\0';
    out->name_bytes=file->name_length;
    out->type=file->type==2?IFS_FS_ENTRY_DIRECTORY:IFS_FS_ENTRY_FILE;
    out->size_bytes=file->type==2?0u:file->bytes;
    out->attributes=0u;
    return IFS_FS_OK;
}
int ifs_pfs3_v2_reader_validate(const IfsFsHostRuntimeV2 *host)
{
    Reader r;
    int rc=start(host,&r);
    if(rc!=IFS_FS_OK)return rc;
    IfsPfs3AnodeRecord node;
    rc=anode(&r,ROOT_ANODE,&node);
    if(rc!=IFS_FS_OK)return rc;
    if(!node.cluster_size || !reserved(&r,node.block_number))
        return IFS_FS_CORRUPT;
    unsigned char raw[4096];
    rc=reserved_read(&r,node.block_number,raw);
    if(rc!=IFS_FS_OK)return rc;
    IfsPfs3DirBlockView view;
    uint32_t count=0u;
    if(ifs_pfs3_decode_directory_block(raw,r.root.reserved_block_size,
       !!(r.root.options&IFS_PFS3_MODE_DIR_EXTENSION),&view,&count) ||
       view.directory_anode!=ROOT_ANODE)return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
int ifs_pfs3_v2_reader_stat(const IfsFsHostRuntimeV2 *host,
                            const char *path,IfsFsEntryV2 *entry)
{
    Reader r;
    File found;
    int rc=start(host,&r);
    if(rc!=IFS_FS_OK)return rc;
    rc=resolve(&r,path,&found);
    return rc==IFS_FS_OK?present(&found,entry):rc;
}
int ifs_pfs3_v2_reader_list(const IfsFsHostRuntimeV2 *host,
                            const char *path,uint32_t index,
                            IfsFsEntryV2 *entry)
{
    Reader r;
    File dir,found;
    int rc=start(host,&r);
    if(rc!=IFS_FS_OK)return rc;
    rc=resolve(&r,path,&dir);
    if(rc!=IFS_FS_OK)return rc;
    if(dir.type!=2)return IFS_FS_INVALID;
    rc=lookup(&r,dir.anode,NULL,0u,index,&found);
    return rc==IFS_FS_OK?present(&found,entry):rc;
}
int ifs_pfs3_v2_reader_read(const IfsFsHostRuntimeV2 *host,
                            const char *path,uint64_t offset,
                            void *destination,uint32_t count)
{
    if(!destination && count)return IFS_FS_INVALID;
    Reader r;
    File found;
    int rc=start(host,&r);
    if(rc!=IFS_FS_OK)return rc;
    rc=resolve(&r,path,&found);
    if(rc!=IFS_FS_OK)return rc;
    if(found.type!=-3)return IFS_FS_UNSUPPORTED;
    if(offset>found.bytes || count>found.bytes-offset)
        return IFS_FS_INVALID;
    if(!count)return IFS_FS_OK;
    uint32_t current=found.anode,guard=0u;
    uint64_t skip=offset/r.sector_bytes;
    uint32_t inside=(uint32_t)(offset%r.sector_bytes);
    unsigned char *out=destination;
    while(current && count) {
        IfsPfs3AnodeRecord node;
        if(++guard>MAX_CHAIN)return IFS_FS_CORRUPT;
        rc=anode(&r,current,&node);
        if(rc!=IFS_FS_OK)return rc;
        if(!node.cluster_size || node.block_number<=r.root.last_reserved ||
           ifs_pfs3_validate_anode_extent(&node,r.media_sectors))
            return IFS_FS_CORRUPT;
        if(skip>=node.cluster_size) {
            skip-=node.cluster_size;current=node.next_anode;continue;
        }
        for(uint64_t i=skip;i<node.cluster_size && count;++i) {
            uint32_t n=r.sector_bytes-inside;
            if(n>count)n=count;
            rc=rd(&r,((uint64_t)node.block_number+i)*r.sector_bytes+inside,out,n);
            if(rc!=IFS_FS_OK)return rc;
            out+=n;count-=n;inside=0u;
        }
        skip=0u;
        current=node.next_anode;
    }
    return count?IFS_FS_CORRUPT:IFS_FS_OK;
}
