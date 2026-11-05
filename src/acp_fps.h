#ifndef ACP_FPS_H
#define ACP_FPS_H
#include <stdint.h>
typedef struct {uint64_t started;unsigned frames,tenths;int ready,initialized;char label[16];} acp_fps;
/* Called only by the display callback, after rendering completes. No GL or IO. */
void acp_fps_draw(acp_fps *,uint32_t *pixels,unsigned width,unsigned height,unsigned stride,uint64_t now);
#endif
