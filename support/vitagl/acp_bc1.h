#ifndef ACP_BC1_H
#define ACP_BC1_H
#include <stddef.h>
#include <stdint.h>
size_t acp_bc1_size(unsigned w,unsigned h);
size_t acp_bc1_offset(unsigned x,unsigned y,unsigned w,unsigned h);
void acp_etc1_to_bc1(const uint8_t *src,uint8_t *dst,unsigned w,unsigned h);
int acp_bc1_read(const uint8_t *src,unsigned tw,unsigned th,int x,int y,
                 int w,int h,uint8_t *dst,unsigned bpp,unsigned alignment);
int acp_etc1_read(const uint8_t *src,unsigned tw,unsigned th,int x,int y,
                  int w,int h,uint8_t *dst,unsigned bpp,unsigned alignment);
/* Port policy: nonzero only in the local emulator, which cannot sample ETC1. */
int acp_etc1_transcode(void);
void acp_etc1_native_upload(unsigned w,unsigned h,int level);
#endif
