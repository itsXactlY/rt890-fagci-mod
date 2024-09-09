#include "spectrum.h"
#include "../helper/helper.h"
#include "../ui/gfx.h"
#include "../ui/helper.h"

#define MAX_POINTS 160

static uint16_t rssiHistory[MAX_POINTS] = {0};
static uint16_t noiseHistory[MAX_POINTS] = {0};
static bool markers[MAX_POINTS] = {0};
static bool updated[MAX_POINTS] = {0};
static uint8_t x;
static uint8_t historySize;
static uint8_t filledPoints;

static uint32_t stepsCount;
static uint32_t currentStep;
static uint8_t exLen;

static DBmRange dBmRange = {-150, -60};

static bool ticksRendered = false;

static const uint32_t GRADIENT_PALETTE[] = {
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000002, 0x000005, 0x000008, 0x00000b, 0x00000e, 0x000010, 0x000013,
    0x000016, 0x000019, 0x00001c, 0x00001e, 0x000021, 0x000024, 0x000027,
    0x00002a, 0x00002c, 0x00002f, 0x000032, 0x000035, 0x000038, 0x00003a,
    0x00003d, 0x000040, 0x000043, 0x000046, 0x000048, 0x00004b, 0x00004e,
    0x000051, 0x000054, 0x000056, 0x000059, 0x00005c, 0x00005f, 0x000062,
    0x000064, 0x000067, 0x00006a, 0x00006d, 0x000070, 0x000072, 0x000075,
    0x000078, 0x00007b, 0x00007e, 0x000080, 0x000083, 0x000086, 0x000089,
    0x00008c, 0x02048f, 0x040893, 0x060c97, 0x08109b, 0x0a149f, 0x0c19a3,
    0x0e1da6, 0x1021aa, 0x1225ae, 0x1429b2, 0x162db6, 0x1832ba, 0x1a36bd,
    0x1c3ac1, 0x1e3ec5, 0x2042c9, 0x2246cd, 0x244bd1, 0x264fd4, 0x2853d8,
    0x2a57dc, 0x2c5be0, 0x2e5fe4, 0x3064e8, 0x3268eb, 0x346cef, 0x3670f3,
    0x3874f7, 0x3a78fb, 0x3c7dff, 0x3f7ffa, 0x4382f5, 0x4784f0, 0x4b87eb,
    0x4f8ae6, 0x538ce1, 0x578fdc, 0x5b91d7, 0x5f94d2, 0x6397cc, 0x6699c7,
    0x6a9cc2, 0x6e9ebd, 0x72a1b8, 0x76a4b3, 0x7aa6ae, 0x7ea9a9, 0x82aba4,
    0x86ae9f, 0x8ab199, 0x8db394, 0x91b68f, 0x95b88a, 0x99bb85, 0x9dbe80,
    0xa1c07b, 0xa5c376, 0xa9c571, 0xadc86c, 0xb1cb66, 0xb4cd61, 0xb8d05c,
    0xbcd257, 0xc0d552, 0xc4d84d, 0xc8da48, 0xccdd43, 0xd0df3e, 0xd4e239,
    0xd8e533, 0xdbe72e, 0xdfea29, 0xe3ec24, 0xe7ef1f, 0xebf21a, 0xeff415,
    0xf3f710, 0xf7f90b, 0xfbfc06, 0xffff00, 0xfffd00, 0xfffa00, 0xfff800,
    0xfff500, 0xfff300, 0xfff000, 0xffee00, 0xffeb00, 0xffe900, 0xffe600,
    0xffe300, 0xffe100, 0xffde00, 0xffdc00, 0xffd900, 0xffd700, 0xffd400,
    0xffd200, 0xffcf00, 0xffcc00, 0xffca00, 0xffc700, 0xffc500, 0xffc200,
    0xffc000, 0xffbd00, 0xffbb00, 0xffb800, 0xffb600, 0xffb300, 0xffb000,
    0xffae00, 0xffab00, 0xffa900, 0xffa600, 0xffa400, 0xffa100, 0xff9f00,
    0xff9c00, 0xff9900, 0xff9700, 0xff9400, 0xff9200, 0xff8f00, 0xff8d00,
    0xff8a00, 0xff8800, 0xff8500, 0xff8300, 0xff8000, 0xff7d00, 0xff7b00,
    0xff7800, 0xff7600, 0xff7300, 0xff7100, 0xff6e00, 0xff6c00, 0xff6900,
    0xff6600, 0xff6400, 0xff6100, 0xff5f00, 0xff5c00, 0xff5a00, 0xff5700,
    0xff5500, 0xff5200, 0xff5000, 0xff4d00, 0xff4a00, 0xff4800, 0xff4500,
    0xff4300, 0xff4000, 0xff3e00, 0xff3b00, 0xff3900, 0xff3600, 0xff3300,
    0xff3100, 0xff2e00, 0xff2c00, 0xff2900, 0xff2700, 0xff2400, 0xff2200,
    0xff1f00, 0xff1d00, 0xff1a00, 0xff1700, 0xff1500, 0xff1200, 0xff1000,
    0xff0d00, 0xff0b00, 0xff0800, 0xff0600, 0xff0300, 0xff0000, 0xff3333,
    0xff6666, 0xff9999, 0xffcccc, 0xffffff};

static uint16_t MapColor(uint16_t rssi) {
  rssi = ConvertDomain(Rssi2DBm(rssi), dBmRange.min, dBmRange.max, 35,
                       ARRAY_SIZE(GRADIENT_PALETTE) - 1);
  uint32_t p = GRADIENT_PALETTE[rssi];
  uint8_t r = (p >> 16) & 0xff;
  uint8_t g = (p >> 8) & 0xff;
  uint8_t b = (p >> 0) & 0xff;

  b = (b * 249 + 1014) >> 11;
  g = (g * 243 + 505) >> 10;
  r = (r * 249 + 1014) >> 11;

  return COLOR_RGB(r, g, b);
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
    updated[i] = false;
  }
  filledPoints = 0;
  currentStep = 0;
}

void SP_ResetRender() {
  ticksRendered = false;
  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    updated[i] = false;
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
      updated[x] = false;
      ox = x;
    }
    if (msm->rssi > rssiHistory[x]) {
      rssiHistory[x] = msm->rssi;
      updated[x] = true;
    }
    if (msm->noise < noiseHistory[x]) {
      noiseHistory[x] = msm->noise;
      updated[x] = true;
    }
    if (markers[x] == false && msm->open) {
      markers[x] = msm->open;
    }
  }
  if (x > filledPoints && x < historySize) {
    filledPoints = x;
  }
}

void SP_ResetPoint(void) {
  for (uint8_t exIndex = 0; exIndex < exLen; ++exIndex) {
    uint8_t lx = historySize * currentStep / stepsCount + exIndex;
    rssiHistory[lx] = 0;
    noiseHistory[lx] = UINT16_MAX;
    updated[lx] = false;
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

static void ShiftShortStringRight(uint8_t Start, uint8_t End) {
  for (int8_t i = End; i > Start; i--) {
    gShortString[i + 1] = gShortString[i];
  }
}

void SP_Render(FRange *p, uint8_t sx, uint8_t sy, uint8_t sh) {
  const uint8_t S_BOTTOM = 13;
  const uint16_t rssiMin = Min(rssiHistory, filledPoints);
  const uint16_t rssiMax = Max(rssiHistory, filledPoints);
  const uint16_t vMin = rssiMin - 2;
  const uint16_t vMax = rssiMax + 20 + (rssiMax - rssiMin) / 2;
  sh = 128 - S_BOTTOM;

  if (!ticksRendered) {
    DISPLAY_DrawRectangle1(0, S_BOTTOM, 4, 160, COLOR_BACKGROUND);
    SP_DrawTicks(sx, sx + historySize - 1, S_BOTTOM, p);
    ticksRendered = true;
  }

  DISPLAY_DrawRectangle0(sx, S_BOTTOM + 3, historySize, 1, COLOR_GREY);

  Int2Ascii(p->start / 10, 7);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  UI_DrawSmallString(2, 2, gShortString, 8);

  Int2Ascii(p->end / 10, 7);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  UI_DrawSmallString(112, 2, gShortString, 8);

  for (uint8_t i = 0; i < filledPoints; ++i) {
    if (updated[i]) {
      updated[i] = false;

      uint8_t yVal =
          ConvertDomain(rssiHistory[i], vMin, vMax, S_BOTTOM + 4, sh);
      DISPLAY_DrawRectangle1(i, yVal + S_BOTTOM + 4, sh - yVal, 1,
                             COLOR_BACKGROUND);
      DISPLAY_DrawRectangle1(i, S_BOTTOM + 4, yVal, 1,
                             MapColor(rssiHistory[i]));
      if (markers[i]) {
        DISPLAY_DrawRectangle1(i, S_BOTTOM, 2, 1, COLOR_GREEN);
      }
    }
  }
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

bool SP_UpdateGradientMin(bool inc) {
  if (inc) {
    if (dBmRange.min < 30 && dBmRange.min < dBmRange.max - 1) {
      dBmRange.min++;
      return true;
    }
  } else {
    if (dBmRange.min > -179) {
      dBmRange.min--;
      return true;
    }
  }
  return false;
}

bool SP_UpdateGradientMax(bool inc) {
  if (inc) {
    if (dBmRange.max < 30) {
      dBmRange.max++;
      return true;
    }
  } else {
    if (dBmRange.max > -179 && dBmRange.max > dBmRange.min + 1) {
      dBmRange.max--;
      return true;
    }
  }
  return false;
}

uint16_t SP_GetNoiseFloor() { return Std(rssiHistory, filledPoints); }
uint16_t SP_GetNoiseMax() { return Max(noiseHistory, filledPoints); }
