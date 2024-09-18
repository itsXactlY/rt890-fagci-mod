#include "spectrum.h"
#include "../driver/st7735s.h"
#include "../helper/helper.h"
#include "../ui/gfx.h"
#include "../ui/helper.h"
#include <string.h>

#define MAX_POINTS 160

static uint16_t rssiHistory[MAX_POINTS] = {0};
static uint16_t noiseHistory[MAX_POINTS] = {0};
static bool markers[MAX_POINTS] = {0};
static bool needRedraw[MAX_POINTS] = {0};
static uint8_t x;
static uint8_t historySize;
static uint8_t filledPoints;

static uint32_t stepsCount;
static uint32_t currentStep;
static uint8_t exLen;

static DBmRange dBmRange = {-150, -40};

static bool ticksRendered = false;

static uint8_t wf[45][80] = {0};
static uint8_t osy[160] = {0};

static uint8_t py[160] = {0};
static uint8_t opy[160] = {0};

static const int16_t GRADIENT_PALETTE[] = {
    0x2000, 0x3000, 0x5000, 0x9000, 0xfc44, 0xffbf, 0x7bf, 0x1b5f,
    0x1b5f, 0x1f,   0x1f,   0x18,   0x13,   0xe,    0x9,
};

static uint8_t getPalIndex(uint16_t rssi) {
  return ConvertDomain(Rssi2DBm(rssi), dBmRange.min, dBmRange.max, 0,
                       ARRAY_SIZE(GRADIENT_PALETTE) - 1);
}

static uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

static uint32_t ClampF(uint32_t v, uint32_t min, uint32_t max) {
  return v <= min ? min : (v >= max ? max : v);
}

static uint32_t ConvertDomainF(uint32_t aValue, uint32_t aMin, uint32_t aMax,
                               uint32_t bMin, uint32_t bMax) {
  const uint64_t aRange = aMax - aMin;
  const uint64_t bRange = bMax - bMin;
  aValue = ClampF(aValue, aMin, aMax);
  return ((aValue - aMin) * bRange + aRange / 2) / aRange + bMin;
}

void SP_ResetHistory(void) {
  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    rssiHistory[i] = 0;
    noiseHistory[i] = UINT16_MAX;
    markers[i] = false;
    needRedraw[i] = false;
  }
  filledPoints = 0;
  currentStep = 0;
}

void SP_ResetRender() {
  ticksRendered = false;
  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    needRedraw[i] = true;
  }
}

void SP_Begin(void) { currentStep = 0; }

void SP_Next(void) {
  if (currentStep < stepsCount - 1) {
    currentStep++;
  }
}

void SP_Init(uint32_t steps, uint8_t width) {
  stepsCount = steps;
  historySize = width;
  exLen = ceilDiv(historySize, stepsCount);
  SP_ResetHistory();
  SP_Begin();
  ticksRendered = false;
  for (uint8_t y = 0; y < ARRAY_SIZE(wf); ++y) {
    memset(wf[y], 0, ARRAY_SIZE(wf[0]));
  }
}

void SP_AddPoint(Loot *msm) {
  uint8_t ox = 255;
  for (uint8_t exIndex = 0; exIndex < exLen; ++exIndex) {
    x = historySize * currentStep / stepsCount + exIndex;
    if (ox != x) {
      rssiHistory[x] = markers[x] = 0;
      needRedraw[x] = false;
      ox = x;
    }
    if (msm->rssi > rssiHistory[x]) {
      rssiHistory[x] = msm->rssi;
      needRedraw[x] = true;
    }
    if (msm->noise < noiseHistory[x]) {
      noiseHistory[x] = msm->noise;
      needRedraw[x] = true;
    }
    if (markers[x] == false && msm->open) {
      markers[x] = msm->open;
    }
  }
  if (x > filledPoints && x < historySize) {
    filledPoints = x + 1;
  }
}

void SP_ResetPoint(void) {
  for (uint8_t exIndex = 0; exIndex < exLen; ++exIndex) {
    uint8_t lx = historySize * currentStep / stepsCount + exIndex;
    rssiHistory[lx] = 0;
    noiseHistory[lx] = UINT16_MAX;
    needRedraw[lx] = false;
    markers[lx] = false;
  }
}

static void drawTicks(uint8_t y, uint32_t fs, uint32_t fe, uint32_t div,
                      uint8_t h, uint16_t c) {
  for (uint32_t f = fs - (fs % div) + div; f < fe; f += div) {
    uint8_t x = ConvertDomainF(f, fs, fe, 0, 159);
    ST7735S_SetAddrWindow(x, y, x, y + h - 1);
    for (uint8_t yp = y; yp < y + h; ++yp) {
      ST7735S_SendU16(yp / 2 % 2 ? c : COLOR_BACKGROUND);
    }
  }
  DISPLAY_ResetWindow();
}

static void SP_DrawTicks(uint8_t y, uint8_t h, FRange *range) {
  uint32_t fs = range->start;
  uint32_t fe = range->end;
  uint32_t bw = fe - fs;
  const uint16_t c1 = COLOR_GREY;
  // const uint16_t c2 = COLOR_GREY_DARK;

  for (uint32_t p = 100000000; p >= 10; p /= 10) {
    if (bw > p) {
      // drawTicks(y, fs, fe, p / 2, h, c2);
      drawTicks(y, fs, fe, p, h, c1);
      return;
    }
  }
}

void SP_Render(FRange *p, uint8_t sy, uint8_t sh) {
  const uint16_t rssiMin = Min(rssiHistory, filledPoints);
  const uint16_t noiseFloor = SP_GetNoiseFloor();
  const uint16_t rssiMax = Max(rssiHistory, filledPoints);
  const uint16_t vMin = rssiMin - 1;
  const uint16_t vMax =
      rssiMax + Clamp((rssiMax - noiseFloor), 40, rssiMax - noiseFloor);

  dBmRange.min = Rssi2DBm(vMin);
  dBmRange.max = Rssi2DBm(vMax);

  for (uint8_t i = 0; i < filledPoints; i += exLen) {
    uint8_t yVal = ConvertDomain(rssiHistory[i] * 2, vMin * 2, vMax * 2, 0, sh);
    if (yVal > py[i]) {
      py[i] = yVal;
    }
    if (yVal < osy[i]) {
      DISPLAY_DrawRectangle1Nr(i, 62 + yVal, osy[i] - yVal, exLen,
                               COLOR_BACKGROUND);
    }
    osy[i] = yVal;
    opy[i] = py[i];
  }
  SP_DrawTicks(sy, sh, p);
  for (uint8_t i = 0; i < filledPoints; i += exLen) {
    DISPLAY_DrawRectangle1Nr(i, sy, osy[i], exLen, COLOR_FOREGROUND);
  }
  DISPLAY_ResetWindow();
}

void WF_Render(bool wfDown, uint8_t skipLastN) {
  if (wfDown) {
    for (uint8_t y = ARRAY_SIZE(wf) - 1; y > 0; --y) {
      memcpy(wf[y], wf[y - 1], ARRAY_SIZE(wf[0]));
    }

    for (uint8_t i = 0; i < filledPoints; ++i) {
      if (i % 2 == 0) {
        wf[0][i / 2] = getPalIndex(rssiHistory[i]);
      } else {
        wf[0][i / 2] |= getPalIndex(rssiHistory[i]) << 4;
      }
    }
  }

  // const uint8_t YMAX = ARRAY_SIZE(wf) - skipLastN - 1;

  ST7735S_SetAddrWindow(0, 11, 159, 11 + ARRAY_SIZE(wf) - 1);

  for (uint8_t x = 0; x < ARRAY_SIZE(wf[0]); ++x) {
    for (int8_t y = ARRAY_SIZE(wf) - 1; y >= 0; --y) {
      ST7735S_SendU16(y > ARRAY_SIZE(wf) - skipLastN
                          ? COLOR_BACKGROUND
                          : GRADIENT_PALETTE[wf[y][x] & 0xF]);
    }
    for (int8_t y = ARRAY_SIZE(wf) - 1; y >= 0; --y) {
      ST7735S_SendU16(y > ARRAY_SIZE(wf) - skipLastN
                          ? COLOR_BACKGROUND
                          : GRADIENT_PALETTE[(wf[y][x] >> 4) & 0xF]);
    }
  }
}

void SP_RenderArrow(FRange *p, uint32_t f, uint8_t sx, uint8_t sy, uint8_t sh) {
  uint8_t cx = ConvertDomainF(f, p->start, p->end, sx, sx + historySize - 1);
  DrawVLine(cx, sy, 4, COLOR_GREY);
  DISPLAY_DrawRectangle0(cx - 2, sy, 5, 2, COLOR_GREY);
}

DBmRange SP_GetGradientRange() { return dBmRange; }

uint16_t SP_GetNoiseFloor() { return Std(rssiHistory, filledPoints); }
uint16_t SP_GetNoiseMax() { return Max(noiseHistory, filledPoints); }

static uint8_t curX = 160 / 2;
static uint8_t curSbWidth = 16;
void CUR_Render(uint8_t y) {
  DISPLAY_Fill(0, 159, y, y + 6 - 1, COLOR_BACKGROUND);

  y++;
  DISPLAY_Fill(curX - curSbWidth + 1, curX + curSbWidth - 1, y, y + 4 - 1,
               COLOR_RGB(8, 16, 0));
  DISPLAY_DrawRectangle1Nr(curX - curSbWidth, y, 4, 1, COLOR_YELLOW);
  DISPLAY_DrawRectangle1Nr(curX + curSbWidth, y, 4, 1, COLOR_YELLOW);

  DISPLAY_DrawRectangle1Nr(curX, y, 4, 1, COLOR_YELLOW);
  for (uint8_t d = 1; d < 4; ++d) {
    DISPLAY_DrawRectangle1Nr(curX - d, y + d, 4 - d, 1, COLOR_YELLOW);
    DISPLAY_DrawRectangle1Nr(curX + d, y + d, 4 - d, 1, COLOR_YELLOW);
  }
  DISPLAY_ResetWindow();
}

bool CUR_Move(bool up) {
  if (up) {
    if (curX + curSbWidth < 159) {
      curX++;
      return true;
    }
  } else {
    if (curX - curSbWidth > 0) {
      curX--;
      return true;
    }
  }
  return false;
}

bool CUR_Size(bool up) {
  if (up) {
    if (curX + curSbWidth < 159 && curX - curSbWidth > 0) {
      curSbWidth++;
      return true;
    }
  } else {
    if (curSbWidth > 1) {
      curSbWidth--;
      return true;
    }
  }
  return false;
}

static uint32_t roundToStep(uint32_t f, uint32_t step) {
  uint32_t sd = f % step;
  if (sd > step / 2) {
    f += step - sd;
  } else {
    f -= sd;
  }
  return f;
}

FRange CUR_GetRange(FRange *p, uint32_t step) {
  FRange range = {
      .start = ConvertDomainF(curX - curSbWidth, 0, 159, p->start, p->end),
      .end = ConvertDomainF(curX + curSbWidth, 0, 159, p->start, p->end),
  };
  range.start = roundToStep(range.start, step);
  range.end = roundToStep(range.end, step);
  return range;
}

uint32_t CUR_GetCenterF(FRange *p, uint32_t step) {
  return roundToStep(ConvertDomainF(curX, 0, 159, p->start, p->end), step);
}

void CUR_Reset() {
  curX = 80;
  curSbWidth = 16;
}
