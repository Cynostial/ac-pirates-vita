#pragma once
#include <stdint.h>
/* mode 0 instructions, 1 progress, 2 ready, 3 missing prerequisite. */
void acp_setup_screen_load(const char *png_path);
void acp_setup_screen(uint32_t *pixels,const char *message,unsigned percent,int mode);
