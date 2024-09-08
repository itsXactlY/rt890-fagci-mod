#include "spectrum.h"
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

static bool running;
static ChannelInfo_t *vfo;
static FRange range;
static uint32_t step;
static Loot msm;

static uint16_t tick = 0;
static uint8_t delayMs = 3;
static bool hard = true;

void Spectrum_Loop(void) {
  BK4819_SetFrequency(msm.f);
  const uint16_t reg = BK4819_ReadRegister(0x30);
  if (hard) {
    BK4819_WriteRegister(0x30, 0x0200);
  } else {
    BK4819_WriteRegister(0x30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }
  BK4819_WriteRegister(0x30, reg);
  DELAY_WaitMS(delayMs);
  msm.rssi = BK4819_GetRSSI();
  msm.noise = BK4819_GetNoise();
  msm.open = msm.noise < 50;
  SP_AddPoint(&msm);
  msm.f += step;
  SP_Next();
  if (tick >= 100) {
    SP_Render(&range, 0, 0, 128);
    tick = 0;
  }
  if (msm.f > range.end) {
    msm.f = range.start;
    SP_Render(&range, 0, 0, 128);
    SP_Begin();
  }
  tick++;
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
    case KEY_5:
      hard ^= 1;
      DELAY_WaitMS(100);
      return true;
    case KEY_1:
      SP_UpdateGradientMin(true);
      return true;
    case KEY_7:
      SP_UpdateGradientMin(false);
      return true;
    case KEY_3:
      SP_UpdateGradientMax(true);
      return true;
    case KEY_9:
      SP_UpdateGradientMax(false);
      return true;
    case KEY_2:
      if (delayMs < 20) {
        delayMs++;
        return true;
      }
      return false;
    case KEY_8:
      if (delayMs > 1) {
        delayMs--;
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
  range.start = f1 < f2 ? f1 : f2;
  range.end = f1 > f2 ? f1 : f2;

  DISPLAY_Fill(0, 159, 0, 127, COLOR_BACKGROUND);

  msm.f = range.start;
  SP_Init((range.end - range.start) / step, 160);

  running = true;

  SP_Render(&range, 0, 0, 128);
  while (running) {
    Spectrum_Loop();
    while (CheckKeys()) {
      SP_Render(&range, 0, 0, 128);
      DELAY_WaitMS(10);
    }
  }
  StopSpectrum();
}
