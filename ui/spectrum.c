#include "spectrum.h"
#include "../driver/st7735s.h"
#include "../helper/helper.h"
#include "../ui/gfx.h"
#include <string.h>

#define MAX_POINTS 160
#define WF_XN 80
#define WF_YN 45

static uint16_t rssiHistory[MAX_POINTS] = {0};
static uint16_t noiseHistory[MAX_POINTS] = {0};
static bool markers[MAX_POINTS] = {0};
static bool needRedraw[MAX_POINTS] = {0};
static uint8_t x = 255;
static uint8_t filledPoints;

static uint32_t stepsCount;
static uint32_t currentStep;
static uint32_t step;
static uint32_t bw;
static uint8_t exLen;

static FRange range;

static DBmRange dBmRange = {-150, -40};

static bool ticksRendered = false;

static uint8_t wf[WF_YN][WF_XN] = {0};
static uint16_t osy[MAX_POINTS] = {0};

static uint8_t curX = MAX_POINTS / 2;
static uint8_t curSbWidth = 16;

static const int16_t GRADIENT_PALETTE[] = {
    0x2000, 0x3000, 0x5000, 0x9000, 0xfc44, 0xffbf, 0x7bf, 0x1b5f,
    0x1b5f, 0x1f,   0x1f,   0x18,   0x13,   0xe,    0x9,
};

static uint8_t getPalIndex(uint16_t rssi) {
  return ConvertDomain(Rssi2DBm(rssi), dBmRange.min, dBmRange.max, 0,
                       ARRAY_SIZE(GRADIENT_PALETTE) - 1);
}

static uint16_t v(uint8_t x) { return rssiHistory[x]; }

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
  for (uint8_t y = 0; y < ARRAY_SIZE(wf); ++y) {
    memset(wf[y], 0, ARRAY_SIZE(wf[0]));
  }
  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    osy[i] = rssiHistory[i] = 0;
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

void SP_Init(FRange *r, uint32_t stepSize, uint32_t _bw) {
  filledPoints = 0;
  step = stepSize;
  bw = _bw;
  range = *r;
  stepsCount = (r->end - r->start) / stepSize + 1;
  exLen = ceilDiv(MAX_POINTS, stepsCount);
  // todo: limit exLen to ConvertDomain bw
  SP_ResetHistory();
  SP_Begin();
  ticksRendered = false;
}

static uint8_t f2x(uint32_t f) {
  return ConvertDomain(f, range.start, range.end, 0, MAX_POINTS - 1);
}

static uint8_t ox = 255;

void SP_AddPoint(Loot *msm) {
  x = f2x(msm->f);
  uint8_t ex = f2x(msm->f + step);
  if (ox != x) {
    rssiHistory[x] = markers[x] = 0;
    noiseHistory[x] = UINT16_MAX;
    ox = x;
  }
  if (msm->rssi > rssiHistory[x]) {
    rssiHistory[x] = msm->rssi;
  }
  if (msm->noise < noiseHistory[x]) {
    noiseHistory[x] = msm->noise;
  }
  if (markers[x] == false && msm->open) {
    markers[x] = msm->open;
  }
  for (uint8_t i = 0; i < ex - x; ++i) {
    rssiHistory[x + i] = rssiHistory[x];
    noiseHistory[x + i] = noiseHistory[x];
    markers[x + i] = markers[x];
  }
  if (x > filledPoints && x < MAX_POINTS) {
    filledPoints = x + 1;
  }
}

static void drawTicks(uint8_t y, uint32_t fs, uint32_t fe, uint32_t div,
                      uint8_t h, uint16_t c) {
  for (uint32_t f = fs - (fs % div) + div; f < fe; f += div) {
    uint8_t x = f2x(f);
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
    if (p < bw) {
      // drawTicks(y, fs, fe, p / 2, h, c2);
      drawTicks(y, fs, fe, p, h, c1);
      return;
    }
  }
}

typedef struct {
  uint8_t sx;
  uint8_t w;
  uint16_t v;
} Bar;

static Bar bar(uint16_t *data, uint8_t i) {
  uint8_t sz = f2x(range.start + step) - f2x(range.start);
  uint8_t szBw = f2x(range.start + bw) - f2x(range.start);

  if (szBw < sz) {
    sz = szBw;
  }

  uint8_t w = sz; // % 2 == 0 ? sz + 1 : sz;

  int16_t sx = i - w / 2;
  int16_t ex = i + w / 2;

  if (sx < 0) {
    w += sx;
    sx = 0;
  }

  if (ex > MAX_POINTS) {
    w -= ex - MAX_POINTS;
  }
  return (Bar){sx, w, data[i]};
}

static void renderBar(uint8_t sy, uint16_t *data, uint8_t i, bool fill) {
  Bar b = bar(data, i);
  DISPLAY_DrawRectangle1Nr(b.sx, sy, b.v, b.w,
                           fill ? COLOR_FOREGROUND : COLOR_BACKGROUND);
}

static void renderWf(uint16_t *data, uint8_t i) {
  Bar b = bar(data, i);
  for (uint8_t i = b.sx; i < b.sx + b.w; ++i) {
    if (i % 2 == 0) {
      wf[0][i / 2] = getPalIndex(b.v);
    } else {
      wf[0][i / 2] &= 0x0F;
      wf[0][i / 2] |= getPalIndex(b.v) << 4;
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

  for (uint32_t f = range.start; f <= range.end; f += step) {
    uint8_t i = f2x(f);
    uint8_t yVal = ConvertDomain(v(i) * 2, vMin * 2, vMax * 2, 0, sh);
    renderBar(sy, osy, i, false);
    osy[i] = yVal;
  }
  SP_DrawTicks(sy, sh, p);
  for (uint32_t f = range.start; f <= range.end; f += step) {
    renderBar(sy, osy, f2x(f), true);
  }
  DISPLAY_ResetWindow();
}

void WF_Render(bool wfDown) {
  const uint8_t YN = ARRAY_SIZE(wf);
  const uint8_t XN = ARRAY_SIZE(wf[0]);
  if (wfDown) {
    for (uint8_t y = YN - 1; y > 0; --y) {
      memcpy(wf[y], wf[y - 1], XN);
    }

    memset(wf[0], 0, WF_XN);

    for (uint32_t f = range.start; f <= range.end; f += step) {
      renderWf(rssiHistory, f2x(f));
    }
  }

  ST7735S_SetAddrWindow(0, 11, MAX_POINTS - 1, 11 + YN - 1);

  for (uint8_t x = 0; x < XN; ++x) {
    for (int8_t y = YN - 1; y >= 0; --y) {
      ST7735S_SendU16(GRADIENT_PALETTE[wf[y][x] & 0xF]);
    }
    for (int8_t y = YN - 1; y >= 0; --y) {
      ST7735S_SendU16(GRADIENT_PALETTE[(wf[y][x] >> 4) & 0xF]);
    }
  }
}

void SP_RenderArrow(FRange *p, uint32_t f, uint8_t sx, uint8_t sy, uint8_t sh) {
  uint8_t cx = ConvertDomainF(f, p->start, p->end, sx, sx + MAX_POINTS - 1);
  DrawVLine(cx, sy, 4, COLOR_GREY);
  DISPLAY_DrawRectangle0(cx - 2, sy, 5, 2, COLOR_GREY);
}

DBmRange SP_GetGradientRange() { return dBmRange; }

uint16_t SP_GetNoiseFloor() { return Std(rssiHistory, filledPoints); }
uint16_t SP_GetNoiseMax() { return Max(noiseHistory, filledPoints); }

void CUR_Render(uint8_t y) {
  DISPLAY_Fill(0, MAX_POINTS - 1, y, y + 6 - 1, COLOR_BACKGROUND);

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
    if (curX + curSbWidth < MAX_POINTS - 1) {
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
    if (curX + curSbWidth < MAX_POINTS - 1 && curX - curSbWidth > 0) {
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
      .start = ConvertDomainF(curX - curSbWidth, 0, MAX_POINTS - 1, p->start,
                              p->end),
      .end = ConvertDomainF(curX + curSbWidth, 0, MAX_POINTS - 1, p->start,
                            p->end),
  };
  range.start = roundToStep(range.start, step);
  range.end = roundToStep(range.end, step);
  return range;
}

uint32_t CUR_GetCenterF(FRange *p, uint32_t step) {
  return roundToStep(ConvertDomainF(curX, 0, MAX_POINTS - 1, p->start, p->end),
                     step);
}

void CUR_Reset() {
  curX = 80;
  curSbWidth = 16;
}
