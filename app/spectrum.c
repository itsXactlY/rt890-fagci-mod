#include "spectrum.h"
#include "../driver/battery.h"
#include "../driver/bk4819.h"
#include "../driver/delay.h"
#include "../driver/key.h"
#include "../driver/speaker.h"
#include "../driver/st7735s.h"
#include "../helper/helper.h"
#include "../misc.h"
#include "../radio/channels.h"
#include "../radio/scheduler.h"
#include "../radio/settings.h"
#include "../ui/gfx.h"
#include "../ui/helper.h"
#include "../ui/main.h"
#include "../ui/spectrum.h"
#include "radio.h"

static const uint16_t U16_MAX = 65535;

static bool running;
static ChannelInfo_t *vfo;
static FRange range;
static uint32_t step;
static Loot msm;

static uint16_t rssiO = U16_MAX;
static uint16_t noiseO = 0;

static uint32_t lastRender = 0;
static uint8_t delayMs = 3;
static bool hard = true;
static uint16_t noiseOpenDiff = 14;

static Loot catch = {0};
static bool isListening = false;

static KEY_t LastKey;
static uint32_t lastBatRender;
static uint32_t lastStarKeyTime = 0;
static uint32_t lastCursorTime = 0;

static bool showBounds = false;

static void updateStats() {
  const uint16_t noiseFloor = SP_GetNoiseFloor();
  const uint16_t noiseMax = SP_GetNoiseMax();
  rssiO = noiseFloor;
  noiseO = noiseMax - noiseOpenDiff;
}

static bool isSquelchOpen() { return msm.rssi >= rssiO && msm.noise <= noiseO; }

static int8_t band = 0;

static inline void tuneTo(uint32_t f) {
  int8_t b = 0;
  if (f > 24000000) {
    b = 1;
  } else {
    b = -1;
  }
  if (b != band) {
    band = b;
    BK4819_SelectFilter(b > 0);
  }
  BK4819_WriteRegister(0x38, (f >> 0) & 0xFFFF);
  BK4819_WriteRegister(0x39, (f >> 16) & 0xFFFF);
  if (hard) {
    BK4819_WriteRegister(0x30, 0x0200);
  } else {
    BK4819_WriteRegister(0x30, 0xBFF1 & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }
  BK4819_WriteRegister(0x30, 0xBFF1);
  DELAY_WaitMS(delayMs);
}

static inline void measure() {
  msm.rssi = BK4819_GetRSSI();
  msm.noise = BK4819_GetNoise();
  if (isListening) {
    noiseO -= noiseOpenDiff;
    msm.open = isSquelchOpen();
    noiseO += noiseOpenDiff;
  } else {
    msm.open = isSquelchOpen();
  }
}

static void drawF(uint32_t f, uint8_t x, uint8_t y, uint16_t color) {
  Int2Ascii(f, 8);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  gColorForeground = color;
  UI_DrawSmallString(x, y, gShortString, 8);
  gColorForeground = COLOR_FOREGROUND;
}

static const uint32_t NUM_TIMEOUT = 3000;

static void renderNumbers() {
  if (gTimeSinceBoot - lastStarKeyTime < NUM_TIMEOUT) {
    // DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);
    Int2Ascii(delayMs, 2);
    UI_DrawSmallString(2, 2, gShortString, 2);

    Int2Ascii(noiseOpenDiff, 2);
    UI_DrawSmallString(160 - 11, 2, gShortString, 2);
  } else if (gTimeSinceBoot - lastCursorTime < NUM_TIMEOUT) {
    // DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);

    FRange cursorBounds = CUR_GetRange(&range, step);
    drawF(cursorBounds.start, 2, 2, COLOR_YELLOW);
    drawF(CUR_GetCenterF(&range, step), 58, 2, COLOR_YELLOW);
    drawF(cursorBounds.end, 112, 2, COLOR_YELLOW);
  } else {
    /* if (catch.f || showBounds) {
      DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);
    } */
    if (catch.f) {
      drawF(catch.f, 58, 2, COLOR_GREEN);
    }

    if (showBounds) {
      drawF(range.start, 2, 2, COLOR_FOREGROUND);
      drawF(range.end, 112, 2, COLOR_FOREGROUND);
    }
  }
}

static void render(bool wfDown) {
  bool shortWf = catch.f || showBounds ||
                 (gTimeSinceBoot - lastStarKeyTime < NUM_TIMEOUT) ||
                 (gTimeSinceBoot - lastCursorTime < NUM_TIMEOUT);
  SP_Render(&range, 62, 30);
  WF_Render(wfDown, shortWf ? 11 : 0);
  CUR_Render(56);

  renderNumbers();

  if (gTimeSinceBoot - lastBatRender > 2000) {
    lastBatRender = gTimeSinceBoot;
    gBatteryVoltage = BATTERY_GetVoltage();
    UI_DrawBatteryBar();
  }
  lastRender = gTimeSinceBoot;
}

static void init() {
  DISPLAY_FillColor(COLOR_BACKGROUND);

  catch.f = 0;

  msm.f = range.start;
  SP_Init((range.end - range.start) / step, 160);
  CUR_Reset();

  running = true;

  render(false);
}

static inline void toggleListening() {
  if (isListening != msm.open) {
    isListening = msm.open;
    if (isListening) {
      catch = msm;

      BK4819_StartAudio();
    } else {
      RADIO_EndAudio();
    }
    render(true);
  }
}

void Spectrum_Loop(void) {
  if (!isListening) {
    tuneTo(msm.f);
  }

  measure();
  SP_AddPoint(&msm);
  toggleListening();

  if (isListening) {
    return;
  }

  msm.f += step;
  SP_Next();

  if (msm.f > range.end) {
    updateStats();
    msm.f = range.start;
    render(true);
    SP_Begin();
    return;
  }

  /* if (gTimeSinceBoot - lastRender >= 500) {
    render();
  } */
}

static uint32_t keyHoldTime = 0;
static bool keyHold = false;

bool CheckKeys(void) {
  static KEY_t Key;

  Key = KEY_GetButton();
  if (Key != LastKey && Key != KEY_NONE) {
    keyHoldTime = gTimeSinceBoot;
  }

  FRange newRange;
  keyHold = Key == LastKey && gTimeSinceBoot - keyHoldTime >= 500;
  bool isNewKey = Key != LastKey;

  LastKey = Key;

  if (isNewKey || keyHold) {
    switch (Key) {
    case KEY_UP:
      return CUR_Move(true);
    case KEY_DOWN:
      return CUR_Move(false);
    case KEY_2:
      return CUR_Size(true);
    case KEY_8:
      return CUR_Size(false);
    case KEY_1:
      if (delayMs < 20) {
        delayMs++;
        lastStarKeyTime = gTimeSinceBoot;
        return true;
      }
      return false;
    case KEY_7:
      if (delayMs > 1) {
        delayMs--;
        lastStarKeyTime = gTimeSinceBoot;
        return true;
      }
      return false;
    case KEY_3:
      if (noiseOpenDiff < 40) {
        noiseOpenDiff++;
        lastStarKeyTime = gTimeSinceBoot;
        return true;
      }
      return false;
    case KEY_9:
      if (noiseOpenDiff > 1) {
        noiseOpenDiff--;
        lastStarKeyTime = gTimeSinceBoot;
        return true;
      }
      return false;
    default:
      break;
    }
  }
  if (isNewKey) {
    switch (Key) {
    case KEY_MENU:
      newRange = CUR_GetRange(&range, step);
      range = newRange;
      init();
      return true;
    case KEY_4:
      hard ^= 1;
      return true;
    case KEY_STAR:
      showBounds ^= 1;
      return true;
    case KEY_EXIT:
      running = false;
      return true;
    default:
      break;
    }
  }
  return false;
}

void StopSpectrum(void) {
  SCREEN_TurnOn();
  ST7735S_Init();

  if (gSettings.WorkMode) {
    CHANNELS_LoadChannel(gSettings.VfoChNo[!gSettings.CurrentVfo],
                         !gSettings.CurrentVfo);
    CHANNELS_LoadChannel(gSettings.VfoChNo[gSettings.CurrentVfo],
                         gSettings.CurrentVfo);
  } else {
    CHANNELS_LoadChannel(gSettings.CurrentVfo ? 999 : 1000,
                         !gSettings.CurrentVfo);
    CHANNELS_LoadChannel(gSettings.CurrentVfo ? 1000 : 999,
                         gSettings.CurrentVfo);
  }

  RADIO_Tune(gSettings.CurrentVfo);
  UI_DrawMain(false);
}

void APP_Spectrum(void) {
  RADIO_EndAudio(); // Just in case audio is open when spectrum starts
  RADIO_Tune(gSettings.CurrentVfo);
  vfo = &gVfoState[gSettings.CurrentVfo];
  uint32_t f1 = gVfoState[0].RX.Frequency;
  uint32_t f2 = gVfoState[1].RX.Frequency;

  step = FREQUENCY_GetStep(gSettings.FrequencyStep);

  if (f1 < f2) {
    range.start = f1;
    range.end = f2;
  } else {
    range.start = f2;
    range.end = f1;
  }

  init();

  while (running) {
    Spectrum_Loop();
    while (CheckKeys()) {
      if (LastKey == KEY_UP || LastKey == KEY_DOWN || LastKey == KEY_2 ||
          LastKey == KEY_8) {
        CUR_Render(56);
        renderNumbers();
        lastCursorTime = gTimeSinceBoot;
      } else {
        render(false);
      }
      DELAY_WaitMS(keyHold ? 5 : 300);
    }
  }
  StopSpectrum();
}
