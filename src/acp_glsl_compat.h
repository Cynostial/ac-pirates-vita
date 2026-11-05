#ifndef ACP_GLSL_COMPAT_H
#define ACP_GLSL_COMPAT_H
#include <stddef.h>
/* Rename the GLES2 identifier that conflicts with Cg's texture keyword.
 * The replacement has equal length and matches VitaGL's uniform-name mapping. */
unsigned acp_glsl_identifiers(char *source,size_t bytes);
#endif
