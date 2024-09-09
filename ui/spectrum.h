#ifndef SPECTRUM_DRAW_H
#define SPECTRUM_DRAW_H

#include "../misc.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int16_t min;
  int16_t max;
} DBmRange;

void SP_AddPoint(Loot *msm);
void SP_ResetHistory();
void SP_ResetRender();
void SP_Init(uint32_t steps, uint8_t width);
void SP_Begin();
void SP_Next();
void SP_Render(FRange *p, uint8_t x, uint8_t y, uint8_t h);
void SP_RenderRssi(uint16_t rssi, char *text, bool top, uint8_t sx, uint8_t sy,
                   uint8_t sh);
void SP_RenderArrow(FRange *p, uint32_t f, uint8_t sx, uint8_t sy, uint8_t sh);

DBmRange SP_GetGradientRange();
bool SP_UpdateGradientMin(bool inc);
bool SP_UpdateGradientMax(bool inc);
uint16_t SP_GetNoiseFloor();
uint16_t SP_GetNoiseMax();

#endif /* end of include guard: SPECTRUM_DRAW_H */
