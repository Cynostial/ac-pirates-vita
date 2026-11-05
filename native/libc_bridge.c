/* Structure-free libc/libm calls for Android ARMv7. Original code, MIT.
 * Both this module and its libraries must use softfp. FILE, pthread, time,
 * errno and filesystem ABIs need their own compatibility implementations. */
#define _GNU_SOURCE
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include "acp_native.h"

_Static_assert(sizeof(int) == 4 && sizeof(long) == 4, "ARMv7 integer ABI required");
_Static_assert(sizeof(wchar_t) == 4, "Android ARMv7 uses 32-bit wchar_t");
_Static_assert(sizeof(long double) == 8, "Android ARMv7 long double ABI required");

/* RTABI memory helpers preserve ALL FP registers, unlike ordinary libc
 * calls. The assembly adapters retain volatile VFP/NEON state around Vita's
 * optimized memory functions. d8-d15 are already preserved by AAPCS. */
extern void acp_aeabi_memcpy(void *dst,const void *src,size_t count);
extern void acp_aeabi_memmove(void *dst,const void *src,size_t count);
extern void acp_aeabi_memset(void *dst,size_t count,int value);
extern int memcmp(const void *left,const void *right,size_t count);
static int acp_isfinite(double value) { return isfinite(value); }
static int acp_signbit(double value) { return signbit(value); }
static unsigned char ctype_values[257];
static int16_t lower_values[257],upper_values[257];
static const unsigned char *android_ctype=ctype_values;
static const int16_t *android_lower=lower_values;
static const int16_t *android_upper=upper_values;
void acp_libc_init(void) {
    lower_values[0]=upper_values[0]=-1;
    for (int c=0;c<256;c++) {
        unsigned value=0;
        if(c>='A'&&c<='Z')value|=1;
        if(c>='a'&&c<='z')value|=2;
        if(c>='0'&&c<='9')value|=4;
        if(c==' '||(c>=9&&c<=13))value|=8;
        if(c>=33&&c<=126&&!(value&7))value|=16;
        if(c<32||c==127)value|=32;
        if((c>='a'&&c<='f')||(c>='A'&&c<='F'))value|=64;
        if(c==' ')value|=128;
        ctype_values[c+1]=value;
        lower_values[c+1]=(c>='A'&&c<='Z')?c+32:c;
        upper_values[c+1]=(c>='a'&&c<='z')?c-32:c;
    }
}

#define ACP_BIND(name, target) {name, (uintptr_t)&target}
const AcpSymbol acp_libc_symbols[] = {
    ACP_BIND("malloc", acp_malloc),
    ACP_BIND("calloc", acp_calloc),
    ACP_BIND("realloc", acp_realloc),
    ACP_BIND("free", acp_free),
    ACP_BIND("memcmp", memcmp),
    ACP_BIND("__aeabi_memcpy", acp_aeabi_memcpy),
    ACP_BIND("__aeabi_memmove", acp_aeabi_memmove),
    ACP_BIND("__aeabi_memset", acp_aeabi_memset),
    ACP_BIND("__isfinite", acp_isfinite),
    ACP_BIND("__signbit", acp_signbit),
    ACP_BIND("_ctype_", android_ctype),
    ACP_BIND("_tolower_tab_", android_lower),
    ACP_BIND("_toupper_tab_", android_upper),
    ACP_BIND("strlcpy", strlcpy),
#include "libc_bindings.inc"
};
const size_t acp_libc_symbol_count =
    sizeof(acp_libc_symbols) / sizeof(acp_libc_symbols[0]);

