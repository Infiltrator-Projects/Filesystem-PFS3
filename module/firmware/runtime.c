/* SPDX-License-Identifier: GPL-3.0-or-later
 * Freestanding primitives for the same native PFS3 reader and Common V2
 * adapter used by the installed hosted SO. No libc/OS/EFI imports.
 */
#include <stddef.h>
#include <stdint.h>
void *memcpy(void *restrict dst,const void *restrict src,size_t bytes)
{
    unsigned char *d=dst;
    const unsigned char *s=src;
    for(size_t i=0u;i<bytes;++i)d[i]=s[i];
    return dst;
}
void *memset(void *dst,int value,size_t bytes)
{
    unsigned char *d=dst;
    for(size_t i=0u;i<bytes;++i)d[i]=(unsigned char)value;
    return dst;
}
int memcmp(const void *lhs,const void *rhs,size_t bytes)
{
    const unsigned char *a=lhs,*b=rhs;
    for(size_t i=0u;i<bytes;++i)
        if(a[i]!=b[i])return (int)a[i]-(int)b[i];
    return 0;
}
