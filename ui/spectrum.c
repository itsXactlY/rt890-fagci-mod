#include "spectrum.h"
#include "../helper/helper.h"
#include "../ui/gfx.h"
#include "../ui/helper.h"

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

static DBmRange dBmRange = {-150, -120};

static bool ticksRendered = false;

static const int16_t GRADIENT_PALETTE[] = {
    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,
    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,
    0x0,    0x0,    0x0,    0x0,    0x800,  0x800,  0x800,  0x1000, 0x1000,
    0x1000, 0x1800, 0x1800, 0x1800, 0x2000, 0x2000, 0x2000, 0x2800, 0x2800,
    0x2800, 0x3000, 0x3000, 0x3000, 0x3800, 0x3800, 0x3800, 0x4000, 0x4000,
    0x4800, 0x4800, 0x4800, 0x4800, 0x5000, 0x5000, 0x5000, 0x5800, 0x5800,
    0x6000, 0x6000, 0x6000, 0x6800, 0x6800, 0x6800, 0x7000, 0x7000, 0x7000,
    0x7800, 0x7800, 0x7800, 0x8000, 0x8000, 0x8000, 0x8800, 0x8800, 0x8820,
    0x9040, 0x9061, 0x9881, 0x98a1, 0xa0c1, 0xa0e2, 0xa902, 0xa922, 0xb142,
    0xb163, 0xb983, 0xb9a3, 0xb9c3, 0xc1e4, 0xc204, 0xca24, 0xca44, 0xd265,
    0xd285, 0xdaa5, 0xdac5, 0xe2e6, 0xe306, 0xeb26, 0xeb46, 0xf367, 0xf387,
    0xfb87, 0xfbc7, 0xf3c8, 0xf3e8, 0xebe9, 0xec09, 0xe42a, 0xdc2a, 0xdc4b,
    0xd44b, 0xd46c, 0xcc8c, 0xc48c, 0xc4ad, 0xbcad, 0xb4ce, 0xb4ee, 0xacef,
    0xad0f, 0xa530, 0x9d30, 0x9d51, 0x9551, 0x8d72, 0x8d92, 0x8593, 0x85b3,
    0x7dd4, 0x75d4, 0x75f5, 0x6df5, 0x6616, 0x6636, 0x5e36, 0x5e57, 0x5677,
    0x4e78, 0x4e98, 0x4699, 0x46b9, 0x3eda, 0x36da, 0x36fb, 0x2f1b, 0x271c,
    0x273c, 0x1f3d, 0x1f5d, 0x177e, 0xf7e,  0xf9f,  0x7bf,  0x79f,  0x77f,
    0x77f,  0x75f,  0x75f,  0x73f,  0x71f,  0x71f,  0x6ff,  0x6ff,  0x6df,
    0x6bf,  0x6bf,  0x69f,  0x67f,  0x67f,  0x65f,  0x65f,  0x63f,  0x61f,
    0x61f,  0x5ff,  0x5ff,  0x5df,  0x5df,  0x5bf,  0x59f,  0x59f,  0x57f,
    0x55f,  0x55f,  0x53f,  0x53f,  0x51f,  0x4ff,  0x4ff,  0x4df,  0x4df,
    0x4bf,  0x49f,  0x49f,  0x47f,  0x47f,  0x45f,  0x43f,  0x43f,  0x41f,
    0x41f,  0x3ff,  0x3df,  0x3df,  0x3bf,  0x39f,  0x39f,  0x37f,  0x37f,
    0x35f,  0x35f,  0x33f,  0x31f,  0x31f,  0x2ff,  0x2ff,  0x2df,  0x2bf,
    0x2bf,  0x29f,  0x27f,  0x27f,  0x25f,  0x25f,  0x23f,  0x21f,  0x21f,
    0x1ff,  0x1ff,  0x1df,  0x1df,  0x1bf,  0x19f,  0x19f,  0x17f,  0x15f,
    0x15f,  0x13f,  0x13f,  0x11f,  0xff,   0xff,   0xdf,   0xbf,   0xbf,
    0x9f,   0x9f,   0x7f,   0x7f,   0x5f,   0x3f,   0x3f,   0x1f,   0x319f,
    0x631f, 0x9c9f, 0xce1f, 0xffbf};

static uint16_t MapColor(uint16_t rssi) {
  rssi = ConvertDomain(Rssi2DBm(rssi), dBmRange.min, dBmRange.max, 27,
                       ARRAY_SIZE(GRADIENT_PALETTE) - 1);
  return GRADIENT_PALETTE[rssi];
}

static uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

static uint32_t ClampF(uint32_t v, uint32_t min, uint32_t max) {
  return v <= min ? min : (v >= max ? max : v);
}

static uint32_t ConvertDomainF(uint32_t aValue, uint32_t aMin, uint32_t aMax,
                               uint32_t bMin, uint32_t bMax) {
  const uint32_t aRange = aMax - aMin;
  const uint32_t bRange = bMax - bMin;
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

static void drawTicks(uint8_t x1, uint8_t x2, uint8_t y, uint32_t fs,
                      uint32_t fe, uint32_t div, uint8_t h) {
  for (uint32_t f = fs - (fs % div) + div; f < fe; f += div) {
    uint8_t x = ConvertDomainF(f, fs, fe, x1, x2);
    DISPLAY_DrawRectangle1(x, y - h + 3, h, 1, COLOR_GREY);
  }
}

static void SP_DrawTicks(uint8_t x1, uint8_t x2, uint8_t y, FRange *range) {
  uint32_t fs = range->start;
  uint32_t fe = range->end;
  uint32_t bw = fe - fs;

  if (bw > 5000000) {
    drawTicks(x1, x2, y, fs, fe, 1000000, 3);
    drawTicks(x1, x2, y, fs, fe, 500000, 2);
  } else if (bw > 1000000) {
    drawTicks(x1, x2, y, fs, fe, 500000, 3);
    drawTicks(x1, x2, y, fs, fe, 100000, 2);
  } else if (bw > 500000) {
    drawTicks(x1, x2, y, fs, fe, 500000, 3);
    drawTicks(x1, x2, y, fs, fe, 100000, 2);
  } else {
    drawTicks(x1, x2, y, fs, fe, 100000, 3);
    drawTicks(x1, x2, y, fs, fe, 50000, 2);
  }
}

void SP_Render(FRange *p, uint8_t sx, uint8_t sy, uint8_t sh) {
  const uint16_t rssiMin = Min(rssiHistory, filledPoints);
  const uint16_t rssiMax = Max(rssiHistory, filledPoints);
  const uint16_t vMin = rssiMin - 2;
  const uint16_t vMax = rssiMax + 20 + (rssiMax - rssiMin) / 2;

  dBmRange.min = Rssi2DBm(vMin);
  dBmRange.max = Rssi2DBm(vMax);

  const uint8_t S_BOTTOM = 13;
  const uint8_t G_BOTTOM = S_BOTTOM + 4;
  sh = 96 - G_BOTTOM - 13;

  if (!ticksRendered) {
    DISPLAY_DrawRectangle1(0, S_BOTTOM, 4, 160, COLOR_BACKGROUND);
    DISPLAY_DrawRectangle0(sx, G_BOTTOM - 1, historySize, 1, COLOR_GREY);
    DISPLAY_DrawRectangle0(sx, G_BOTTOM + sh, historySize, 1, COLOR_GREY);
    SP_DrawTicks(sx, sx + historySize - 1, S_BOTTOM, p);
    ticksRendered = true;
    Int2Ascii(p->start / 10, 7);
    ShiftShortStringRight(2, 7);
    gShortString[3] = '.';
    UI_DrawSmallString(2, 2, gShortString, 8);

    Int2Ascii(p->end / 10, 7);
    ShiftShortStringRight(2, 7);
    gShortString[3] = '.';
    UI_DrawSmallString(112, 2, gShortString, 8);
  }

  for (uint8_t i = 0; i < filledPoints; i += exLen) {
    if (needRedraw[i]) {
      needRedraw[i] = false;

      uint8_t yVal =
          ConvertDomain(rssiHistory[i] * 2, vMin * 2, vMax * 2, 0, sh);
      DISPLAY_DrawRectangle1Nr(i, G_BOTTOM, yVal, exLen,
                               MapColor(rssiHistory[i]));
      DISPLAY_DrawRectangle1Nr(i, yVal + G_BOTTOM, sh - yVal, exLen,
                               COLOR_BACKGROUND);
      if (markers[i]) {
        DISPLAY_DrawRectangle1Nr(i, S_BOTTOM, 2, exLen, COLOR_GREEN);
      } else {
        DISPLAY_DrawRectangle1Nr(i, S_BOTTOM, 2, exLen, COLOR_BACKGROUND);
      }
    }
  }
  DISPLAY_ResetWindow();
}

void SP_RenderArrow(FRange *p, uint32_t f, uint8_t sx, uint8_t sy, uint8_t sh) {
  uint8_t cx = ConvertDomainF(f, p->start, p->end, sx, sx + historySize - 1);
  DrawVLine(cx, sy, 4, COLOR_GREY);
  DISPLAY_DrawRectangle0(cx - 2, sy, 5, 2, COLOR_GREY);
}

void SP_RenderRssi(uint16_t rssi, char *text, bool top, uint8_t sx, uint8_t sy,
                   uint8_t sh) {
  const uint8_t S_BOTTOM = sy + sh;
  const uint16_t rssiMin = Min(rssiHistory, filledPoints);
  const uint16_t rssiMax = Max(rssiHistory, filledPoints);
  const uint16_t vMin = rssiMin - 2;
  const uint16_t vMax = rssiMax + 20 + (rssiMax - rssiMin) / 2;

  uint8_t yVal = ConvertDomainF(rssi, vMin, vMax, 0, sh);
  DrawHLine(sx, S_BOTTOM - yVal, sx + filledPoints, COLOR_GREY);
  /* PrintSmallEx(sx, S_BOTTOM - yVal + (top ? -2 : 6), POS_L, COLOR_GREY, "%s
     %u %d", text, rssi, Rssi2DBm(rssi)); */
}

DBmRange SP_GetGradientRange() { return dBmRange; }

uint16_t SP_GetNoiseFloor() { return Std(rssiHistory, filledPoints); }
uint16_t SP_GetNoiseMax() { return Max(noiseHistory, filledPoints); }
