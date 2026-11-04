#pragma once
#include <stdint.h>
void IogPortraitLine(int line);
void IogPortraitCapture(const uint8_t *field);
const uint32_t *IogPortraitCompose(const uint32_t *native, int width, int height,
                                  int drawable_width, int drawable_height, int *out_w, int *out_h);
void IogPortraitSetLayout(float zoom, int filter, int safe, int pad);
int IogPortraitFilter(void);
float IogPortraitDrawScale(int width,int height);

void IogPortraitAudit(const uint8_t *field,unsigned frame);
void IogPortraitResetFrame(void);
