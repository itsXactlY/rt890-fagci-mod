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

static uint16_t tick = 0;
static uint8_t delayMs = 3;
static bool hard = true;
static uint16_t noiseOpenDiff = 14;

static Loot catch = {0};
static bool isListening = false;

static void updateStats() {
  const uint16_t noiseFloor = SP_GetNoiseFloor();
  const uint16_t noiseMax = SP_GetNoiseMax();
  rssiO = noiseFloor;
  noiseO = noiseMax - noiseOpenDiff;
}

static bool isSquelchOpen() { return msm.rssi >= rssiO && msm.noise <= noiseO; }

static inline void tuneTo(uint32_t f) {
  BK4819_WriteRegister(0x38, (msm.f >> 0) & 0xFFFF);
  BK4819_WriteRegister(0x39, (msm.f >> 16) & 0xFFFF);
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

static void render() {
  SP_Render(&range, 0, 0, 96);
  tick = 0;
  Int2Ascii(delayMs, 2);
  UI_DrawSmallString(2, 96 - 13 + 4, gShortString, 2);

  Int2Ascii(noiseOpenDiff, 2);
  UI_DrawSmallString(160 - 46, 96 - 13 + 4, gShortString, 2);

  gBatteryVoltage = BATTERY_GetVoltage();
  UI_DrawStatusIcon(139, ICON_BATTERY, true, COLOR_FOREGROUND);
  UI_DrawBattery(false);
}

static inline void toggleListening() {
  if (isListening != msm.open) {
    isListening = msm.open;
    if (isListening) {
      catch = msm;

      Int2Ascii(catch.f, 8);
      ShiftShortStringRight(2, 7);
      gShortString[3] = '.';
      gColorForeground = COLOR_GREEN;
      UI_DrawSmallString(58, 2, gShortString, 8);
      gColorForeground = COLOR_FOREGROUND;

      BK4819_StartAudio();
    } else {
      RADIO_EndAudio();
    }
    render();
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
    render();
    SP_Begin();
    return;
  }

  tick++;
  if (tick >= 100) {
    render();
  }
}

bool CheckKeys(void) {
  static uint8_t KeyHoldTimer;
  static KEY_t Key;
  static KEY_t LastKey;

  Key = KEY_GetButton();
  if (Key == LastKey && Key != KEY_NONE) {
    KeyHoldTimer += 10;
  }

  if (Key != LastKey || KeyHoldTimer >= 50) {
    KeyHoldTimer = 0;
    switch (Key) {
    case KEY_EXIT:
      running = false;
      return true;
    case KEY_4:
      hard ^= 1;
      DELAY_WaitMS(100);
      return true;
    case KEY_1:
      if (delayMs < 20) {
        delayMs++;
        return true;
      }
      return false;
    case KEY_7:
      if (delayMs > 1) {
        delayMs--;
        return true;
      }
      return false;
    case KEY_3:
      if (noiseOpenDiff < 40) {
        noiseOpenDiff++;
        return true;
      }
      return false;
    case KEY_9:
      if (noiseOpenDiff > 1) {
        noiseOpenDiff--;
        return true;
      }
      return false;
    default:
      break;
    }
    LastKey = Key;
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

  DISPLAY_FillColor(COLOR_BACKGROUND);

  catch.f = 0;

  msm.f = range.start;
  SP_Init((range.end - range.start) / step, 160);

  running = true;

  render();

  while (running) {
    Spectrum_Loop();
    while (CheckKeys()) {
      SP_ResetRender();
      render();
      DELAY_WaitMS(100);
    }
  }
  StopSpectrum();
}
