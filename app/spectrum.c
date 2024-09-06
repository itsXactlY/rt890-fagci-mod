/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "app/spectrum.h"
#include "app/radio.h"
#include "driver/bk4819.h"
#include "driver/delay.h"
#include "driver/key.h"
#include "driver/pins.h"
#include "driver/speaker.h"
#include "driver/st7735s.h"
#include "helper/helper.h"
#include "helper/inputbox.h"
#include "misc.h"
#include "radio/channels.h"
#include "radio/settings.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/main.h"

#ifdef UART_DEBUG
#include "driver/uart.h"
#include "external/printf/printf.h"
#endif

#define WATERFALL_RIGHT_MARGIN 0
#define WATERFALL_LEFT_MARGIN 0
#define H_WATERFALL_WIDTH 127

#define SCROLL_LEFT_MARGIN 55
#define SCROLL_RIGHT_MARGIN 160

static uint32_t CurrentFreq;
static uint8_t CurrentFreqIndex;
static uint32_t FreqCenter;
static uint32_t FreqMin;
static uint32_t FreqMax;
static uint8_t CurrentModulation;
static uint8_t CurrentFreqStepIndex;
static uint32_t CurrentFreqStep;
static uint32_t CurrentFreqChangeStep;
static uint8_t CurrentStepCountIndex;
static uint8_t CurrentStepCount;
static uint16_t CurrentScanDelay;
static uint16_t RssiValue[160] = {0};
static uint16_t SquelchLevel;
static uint8_t bExit;
static uint8_t bRXMode;
static uint8_t bResetSquelch;
static uint8_t bRestartScan;
static uint8_t bFilterEnabled;
static uint8_t bNarrow;
static uint16_t RssiLow;
static uint16_t RssiHigh;
static uint8_t bHold;
#ifdef ENABLE_SPECTRUM_PRESETS
FreqPreset CurrentBandInfo;
uint8_t CurrentBandIndex;
uint8_t bInBand = FALSE;
#endif
static uint8_t DisplayMode;

static const uint8_t BarScale = 40;
static const uint8_t BarY = 15;

void ShiftShortStringRight(uint8_t Start, uint8_t End) {
  for (uint8_t i = End; i > Start; i--) {
    gShortString[i] = gShortString[i - 1];
  }
}

void DrawCurrentFreq(uint16_t Color) {

  gColorForeground = Color;
  Int2Ascii(CurrentFreq, 8);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';

  if (!DisplayMode) {
    UI_DrawString(30, 78, gShortString, 8);

    UI_DrawSmallString(98, 68, Mode[CurrentModulation], 2);
  } else {
    UI_DrawSmallString(2, 20, gShortString, 8);

    UI_DrawSmallString(2, 50, Mode[CurrentModulation], 2);
  }

  gColorForeground = Color;
  ConvertRssiToDbm(RssiValue[CurrentFreqIndex]);
  if (!DisplayMode) {
    UI_DrawSmallString(118, 72, gShortString, 4);
  } else {
    UI_DrawSmallString(2, 40, gShortString, 4);
  }

  gColorForeground = COLOR_RED;
  ConvertRssiToDbm(SquelchLevel);
  if (!DisplayMode) {
    UI_DrawSmallString(118, 60, gShortString, 4);
  } else {
    UI_DrawSmallString(29, 40, gShortString, 4);
  }
}

#ifdef ENABLE_SPECTRUM_PRESETS
void GetCurrentBand(void) {
  uint8_t PresetCount;

  PresetCount = sizeof(FreqPresets) / sizeof(FreqPresets[0]);

  for (uint8_t i = 0; i < PresetCount; i++) {
    if (FreqCenter >= FreqPresets[i].StartFreq &&
        FreqCenter < FreqPresets[i].EndFreq) {
      bInBand = TRUE;
      CurrentBandInfo = FreqPresets[i];
      CurrentBandIndex = i;
    } else if (FreqCenter >= FreqPresets[i].EndFreq &&
               FreqCenter >= FreqPresets[i + 1].StartFreq) {
      bInBand = FALSE;
      CurrentBandIndex = i;
    }
  }
}
#endif

void DrawLabels(void) {

  gColorForeground = COLOR_FOREGROUND;

  Int2Ascii(FreqMin / 10, 7);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  UI_DrawSmallString(2, 2, gShortString, 8);

  Int2Ascii(FreqMax / 10, 7);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  if (!DisplayMode) {
    UI_DrawSmallString(112, 2, gShortString, 8);
  } else {
    UI_DrawSmallString(2, 88, gShortString, 8);
  }

#ifdef ENABLE_SPECTRUM_PRESETS
  if (bInBand && !DisplayMode) {
    UI_DrawSmallString(2, 84, CurrentBandInfo.Name, 14);
  }
#endif

  if (!DisplayMode) {
    gShortString[2] = ' ';
    Int2Ascii(CurrentStepCount, (CurrentStepCount < 100) ? 2 : 3);
    UI_DrawSmallString(2, 72, gShortString, 3);

    UI_DrawSmallString(2, 60, StepStrings[CurrentFreqStepIndex], 5);
  } else {
    UI_DrawSmallString(2, 72, StepStrings[CurrentFreqStepIndex], 5);
  }

  Int2Ascii(CurrentScanDelay, (CurrentScanDelay < 10) ? 1 : 2);
  if (CurrentScanDelay < 10) {
    gShortString[1] = gShortString[0];
    gShortString[0] = ' ';
  }

  if (!DisplayMode) {
    UI_DrawSmallString(146, 84, gShortString, 2);

    UI_DrawSmallString(152, 72, (bFilterEnabled) ? "F" : "U", 1);

    UI_DrawSmallString(152, 60, (bNarrow) ? "N" : "W", 1);

    UI_DrawSmallString(136, 84, (bHold) ? "H" : " ", 1);
  } else {
    UI_DrawSmallString(32, 72, gShortString, 2);

    UI_DrawSmallString(2, 60, (bFilterEnabled) ? "F" : "U", 1);

    UI_DrawSmallString(15, 60, (bNarrow) ? "N" : "W", 1);

    UI_DrawSmallString(30, 60, (bHold) ? "H" : " ", 1);
  }

  gColorForeground = COLOR_GREY;

  Int2Ascii(CurrentFreqChangeStep / 10, 5);
  ShiftShortStringRight(0, 5);
  gShortString[1] = '.';
  if (!DisplayMode) {
    UI_DrawSmallString(64, 2, gShortString, 6);
  } else {
    UI_DrawSmallString(2, 30, gShortString, 6);
  }
}

void SetFreqMinMax(bool getCurrentBand) {
  CurrentFreqChangeStep = CurrentFreqStep * (CurrentStepCount >> 1);
  FreqMin = FreqCenter - CurrentFreqChangeStep;
  FreqMax = FreqCenter + CurrentFreqChangeStep;
#ifdef ENABLE_SPECTRUM_PRESETS
  if (getCurrentBand) {
    GetCurrentBand();
  }
#endif
  FREQUENCY_SelectBand(FreqCenter);
  BK4819_EnableFilter(bFilterEnabled);
  RssiValue[CurrentFreqIndex] = 0; // Force a rescan
}

void SetStepCount(void) {
  if (!DisplayMode) {
    CurrentStepCount = 160 >> CurrentStepCountIndex;
  } else {
    CurrentStepCount = 128 >> CurrentStepCountIndex;
  }
}

void IncrementStepIndex(void) {
  CurrentStepCountIndex = (CurrentStepCountIndex + 1) % STEPS_COUNT;
  SetStepCount();
  SetFreqMinMax(true);
  DrawLabels();
}

void IncrementFreqStepIndex(void) {
  CurrentFreqStepIndex = (CurrentFreqStepIndex + 1) % 10;
  CurrentFreqStep = FREQUENCY_GetStep(CurrentFreqStepIndex);
  SetFreqMinMax(true);
  DrawLabels();
}

void IncrementScanDelay(void) {
  CurrentScanDelay = (CurrentScanDelay + 2) % 12;
  DrawLabels();
}

void ChangeCenterFreq(uint8_t Up) {
  if (Up) {
    FreqCenter += CurrentFreqChangeStep;
  } else {
    FreqCenter -= CurrentFreqChangeStep;
  }
  SetFreqMinMax(true);
  DrawLabels();
}

void ChangeHoldFreq(uint8_t Up) {
  if (Up) {
    CurrentFreqIndex = (CurrentFreqIndex + 1) % CurrentStepCount;
  } else {
    CurrentFreqIndex =
        (CurrentFreqIndex + CurrentStepCount - 1) % CurrentStepCount;
  }
  CurrentFreq = FreqMin + (CurrentFreqIndex * CurrentFreqStep);
}

void ChangeSquelchLevel(uint8_t Up) {
  if (Up) {
    SquelchLevel += 2;
  } else {
    SquelchLevel -= 2;
  }
}

#ifdef ENABLE_SPECTRUM_PRESETS
void ChangeBandPreset(uint8_t Up) {
  uint8_t PresetCount;

  PresetCount = sizeof(FreqPresets) / sizeof(FreqPresets[0]);

  if (Up) {
    CurrentBandIndex = (CurrentBandIndex + 1) % PresetCount;
  } else {
    CurrentBandIndex = (CurrentBandIndex + PresetCount - 1) % PresetCount;
  }

  bInBand = TRUE;

  CurrentStepCountIndex = FreqPresets[CurrentBandIndex].StepCountIndex;
  SetStepCount();

  CurrentFreqStepIndex = FreqPresets[CurrentBandIndex].StepSizeIndex;
  CurrentFreqStep = FREQUENCY_GetStep(CurrentFreqStepIndex);
  CurrentBandInfo = FreqPresets[CurrentBandIndex];
  FreqCenter = FreqPresets[CurrentBandIndex].StartFreq +
               (CurrentFreqStep * (CurrentStepCount >> 1));
  SetFreqMinMax(false);

  CurrentModulation = FreqPresets[CurrentBandIndex].ModulationType;

  bNarrow = FreqPresets[CurrentBandIndex].bNarrow;
  BK4819_WriteRegister(0x43, (bNarrow) ? 0x4048 : 0x3028);

  bRestartScan = TRUE;
  bResetSquelch = TRUE;

  DrawCurrentFreq((bRXMode) ? COLOR_GREEN : COLOR_BLUE);
  DrawLabels();
}
#endif

void ChangeDisplayMode(void) {
  DisplayMode ^= 1;
  bRestartScan = TRUE;

  ST7735S_Init();

  CurrentStepCountIndex = 0;
  SetStepCount();
  SetFreqMinMax(true);
  if (DisplayMode) {
    ST7735S_defineScrollArea(SCROLL_LEFT_MARGIN, SCROLL_RIGHT_MARGIN);
  }

  DrawLabels();
}

void ToggleFilter(void) {
  bFilterEnabled ^= 1;
  BK4819_EnableFilter(bFilterEnabled);
  bResetSquelch = TRUE;
  bRestartScan = TRUE;
  DrawLabels();
}

void ToggleNarrowWide(void) {
  bNarrow ^= 1;
  BK4819_WriteRegister(0x43, (bNarrow) ? 0x4048 : 0x3028);
  DrawLabels();
}

void IncrementModulation(void) {
  CurrentModulation = (CurrentModulation + 1) % 3;
  DrawCurrentFreq((bRXMode) ? COLOR_GREEN : COLOR_BLUE);
}

uint16_t GetAdjustedLevel(uint16_t Level, uint16_t Low, uint16_t High,
                          uint16_t Scale) {
  uint16_t Value = 0;

  //		Valid range 72-330, converted to 0-100, scaled to % based on
  // Scale to fit on screen.
  if (Level < Low) {
    Level = Low;
  }

  Value = ((((((Level - Low) * 100) / (High - Low)) * 100) * Scale) / 10000);

  if (Value > Scale) {
    Value = Scale;
  }

  return Value;
}

void JumpToVFO(void) {
  if (gSettings.WorkMode) {
    gSettings.WorkMode = FALSE;
    CHANNELS_LoadVfoMode();
  }

#ifdef UART_DEBUG
  Int2Ascii(gSettings.WorkMode, 1);
  UART_printf("gSettings.WorkMode: ");
  UART_printf(gShortString);
  UART_printf("     -----     ");
#endif

  SETTINGS_SaveGlobals();
  gVfoState[gSettings.CurrentVfo].bIsNarrow = bNarrow;
  CHANNELS_UpdateVFOFreq(CurrentFreq);

  bExit = TRUE;
}

void DrawSpectrum(uint16_t ActiveBarColor) {
  uint8_t BarLow;
  uint8_t BarHigh;
  uint16_t Power;
  uint16_t SquelchPower;
  uint8_t BarX;
  uint8_t BarWidth;

  BarLow = RssiLow - 2;
  if ((RssiHigh - RssiLow) < 40) {
    BarHigh = RssiLow + 40;
  } else {
    BarHigh = RssiHigh + 5;
  }

  BarWidth = 160 / CurrentStepCount;

  // Bars
  for (uint8_t i = 0; i < CurrentStepCount; i++) {
    BarX = (i * BarWidth);
    Power = GetAdjustedLevel(RssiValue[i], BarLow, BarHigh, BarScale);
    SquelchPower = GetAdjustedLevel(SquelchLevel, BarLow, BarHigh, BarScale);
    if (Power < SquelchPower) {
      DISPLAY_DrawRectangle1(BarX, BarY, Power, BarWidth,
                             (i == CurrentFreqIndex) ? ActiveBarColor
                                                     : COLOR_FOREGROUND);
      DISPLAY_DrawRectangle1(BarX, BarY + Power, SquelchPower - Power, BarWidth,
                             COLOR_BACKGROUND);
      DISPLAY_DrawRectangle1(BarX, BarY + SquelchPower + 1,
                             BarScale - SquelchPower, BarWidth,
                             COLOR_BACKGROUND);
    } else {
      DISPLAY_DrawRectangle1(BarX, BarY, SquelchPower, BarWidth,
                             (i == CurrentFreqIndex) ? ActiveBarColor
                                                     : COLOR_FOREGROUND);
      DISPLAY_DrawRectangle1(
          BarX, BarY + SquelchPower + 1, Power - SquelchPower, BarWidth,
          (i == CurrentFreqIndex) ? ActiveBarColor : COLOR_FOREGROUND);
      DISPLAY_DrawRectangle1(BarX, BarY + Power + 1, BarScale - Power, BarWidth,
                             COLOR_BACKGROUND);
    }
  }

  // Squelch Line
  Power = GetAdjustedLevel(SquelchLevel, BarLow, BarHigh, BarScale);
  DISPLAY_DrawRectangle1(0, BarY + Power, 1, 160, COLOR_RED);
}

long long Clamp(long long v, long long min, long long max) {
  return v <= min ? min : (v >= max ? max : v);
}

int ConvertDomain(int aValue, int aMin, int aMax, int bMin, int bMax) {
  const int aRange = aMax - aMin;
  const int bRange = bMax - bMin;
  aValue = Clamp(aValue, aMin, aMax);
  return ((aValue - aMin) * bRange + aRange / 2) / aRange + bMin;
}

uint16_t MapColor(uint16_t Level) {
  static const uint32_t pal[] = {
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
  // 64      // min
  // 340 // max
  // Level -= 30;
  // Level <<= 2;

  int16_t RXdBM = (Level >> 1) - 177;
  Level = ConvertDomain(RXdBM, -140, -40, 28, 255);
  uint32_t p = pal[Level];
  uint8_t r = (p >> 16) & 0xff;
  uint8_t g = (p >> 8) & 0xff;
  uint8_t b = (p >> 0) & 0xff;

  b = (b * 249 + 1014) >> 11;
  g = (g * 243 + 505) >> 10;
  r = (r * 249 + 1014) >> 11;

  return COLOR_RGB(r, g, b);
}

void DrawWaterfall() {
  static uint8_t scroll;
  uint16_t High;

  if ((RssiHigh - RssiLow) < 60) {
    High = RssiLow + 60;
  } else {
    High = RssiHigh;
  }

  scroll++;
  scroll %= (SCROLL_RIGHT_MARGIN - SCROLL_LEFT_MARGIN);

  ST7735S_scroll(scroll);

  ST7735S_SetAddrWindow((SCROLL_RIGHT_MARGIN)-scroll, 0,
                        (SCROLL_RIGHT_MARGIN)-scroll, 127);

  for (uint8_t i = 0; i < 127; i++) {
    uint16_t wf = GetAdjustedLevel(RssiValue[i], RssiLow, High, 100);
    wf = MapColor(RssiValue[i]);

    // uint16_t wf = MapColor(RssiValue[i] - RssiLow);

    ST7735S_SendU16(wf); // write to screen using waterfall color from palette
  }

  DISPLAY_DrawRectangle1(52, 0, 128, 3, COLOR_BACKGROUND);
  DISPLAY_DrawRectangle1(52, CurrentFreqIndex, 1, 3, COLOR_FOREGROUND);
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

void CheckKeys(void) {
  static uint8_t KeyHoldTimer;
  static KEY_t Key;
  static KEY_t LastKey;

  Key = KEY_GetButton();
  if (Key == LastKey && Key != KEY_NONE) {
    if (bRXMode) {
      KeyHoldTimer += 10;
    } else {
      KeyHoldTimer++;
    }
  }
  if (Key != LastKey || KeyHoldTimer >= 50) {
    KeyHoldTimer = 0;
    switch (Key) {
    case KEY_NONE:
      break;
    case KEY_EXIT:
      bExit = TRUE;
      return;
    case KEY_MENU:
      JumpToVFO();
      return;
    case KEY_UP:
      if (!bHold) {
        ChangeCenterFreq(TRUE);
      } else {
        ChangeHoldFreq(TRUE);
      }
      break;
    case KEY_DOWN:
      if (!bHold) {
        ChangeCenterFreq(FALSE);
      } else {
        ChangeHoldFreq(FALSE);
      }
      break;
    case KEY_1:
      IncrementStepIndex();
      break;
    case KEY_2:
#ifdef ENABLE_SPECTRUM_PRESETS
      ChangeBandPreset(TRUE);
#endif
      break;
    case KEY_3:
      IncrementModulation();
      break;
    case KEY_4:
      IncrementFreqStepIndex();
      break;
    case KEY_5:
#ifdef ENABLE_SPECTRUM_PRESETS
      ChangeBandPreset(FALSE);
#endif
      break;
    case KEY_6:
      ChangeSquelchLevel(TRUE);
      break;
    case KEY_7:
      bHold ^= 1;
      DrawLabels();
      break;
    case KEY_8:
      ChangeDisplayMode();
      break;
    case KEY_9:
      ChangeSquelchLevel(FALSE);
      break;
    case KEY_0:
      ToggleFilter();
      break;
    case KEY_HASH:
      ToggleNarrowWide();
      break;
    case KEY_STAR:
      IncrementScanDelay();
      break;
    default:
      break;
    }
    LastKey = Key;
  }
}

void Spectrum_StartAudio(void) {
  gReceivingAudio = true;

  gpio_bits_set(GPIOA, BOARD_GPIOA_LED_GREEN);
  gRadioMode = RADIO_MODE_RX;
  OpenAudio(bNarrow, CurrentModulation);
  if (CurrentModulation == 0) {
    BK4819_WriteRegister(0x4D, 0xA080);
    BK4819_WriteRegister(0x4E, 0x6F7C);
  }

  if (CurrentModulation > 0) {
    // AM, SSB
    BK4819_EnableScramble(false);
    BK4819_EnableCompander(false);
    // Set bit 4 of register 73 (Auto Frequency Control Disable)
    uint16_t reg_73 = BK4819_ReadRegister(0x73);
    BK4819_WriteRegister(0x73, reg_73 | 0x10U);
    if (CurrentModulation > 1) {                      // if SSB
      BK4819_WriteRegister(0x43, 0b0010000001011000); // Filter 6.25KHz
      BK4819_WriteRegister(0x37, 0b0001011000001111);
      BK4819_WriteRegister(0x3D, 0b0010101101000101);
      BK4819_WriteRegister(0x48, 0b0000001110101000);
    }
  } else {
    // FM
    BK4819_EnableScramble(false);
    BK4819_EnableCompander(true);
    uint16_t reg_73 = BK4819_ReadRegister(0x73);
    BK4819_WriteRegister(0x73, reg_73 & ~0x10U);
    BK4819_SetAFResponseCoefficients(false, true,
                                     gCalibration.RX_3000Hz_Coefficient);
  }
  SPEAKER_TurnOn(SPEAKER_OWNER_RX);
}

void RunRX(void) {
  bRXMode = TRUE;
  Spectrum_StartAudio();

  while (RssiValue[CurrentFreqIndex] > SquelchLevel) {
    RssiValue[CurrentFreqIndex] = BK4819_GetRSSI();
    CheckKeys();
    if (bExit) {
      RADIO_EndAudio();
      return;
    }
    DrawCurrentFreq(COLOR_GREEN);
    if (!DisplayMode) {
      DrawSpectrum(COLOR_GREEN);
    }
    DELAY_WaitMS(5);
  }

  RADIO_EndAudio();
  bRXMode = FALSE;
}

void Spectrum_Loop(void) {
  uint32_t FreqToCheck;
  CurrentFreqIndex = 0;
  CurrentFreq = FreqMin;
  bResetSquelch = TRUE;
  bRestartScan = FALSE;

  // UI_DrawStatusIcon(139, ICON_BATTERY, true, COLOR_FOREGROUND);
  // UI_DrawBattery(false);

  DrawLabels();

  while (1) {
    FreqToCheck = FreqMin;
    bRestartScan = TRUE;

    for (uint8_t i = 0; i < CurrentStepCount; i++) {

      if (bRestartScan) {
        bRestartScan = FALSE;
        RssiLow = 330;
        RssiHigh = 72;
        i = 0;
      }

      BK4819_set_rf_frequency(FreqToCheck, TRUE);

      DELAY_WaitMS(CurrentScanDelay);

      RssiValue[i] = BK4819_GetRSSI();

      if (RssiValue[i] < RssiLow) {
        RssiLow = RssiValue[i];
      } else if (RssiValue[i] > RssiHigh) {
        RssiHigh = RssiValue[i];
      }

      if (RssiValue[i] > RssiValue[CurrentFreqIndex] && !bHold) {
        CurrentFreqIndex = i;
        CurrentFreq = FreqToCheck;
      }

      FreqToCheck += CurrentFreqStep;

      CheckKeys();
      if (bExit) {
        return;
      }
    }

    if (bResetSquelch) {
      bResetSquelch = FALSE;
      SquelchLevel = RssiHigh + 5;
    }

    if (RssiValue[CurrentFreqIndex] > SquelchLevel) {
      BK4819_set_rf_frequency(CurrentFreq, TRUE);
      DELAY_WaitMS(CurrentScanDelay);
      RunRX();
    }

    DrawCurrentFreq(COLOR_BLUE);

    if (!DisplayMode) {
      DrawSpectrum(COLOR_BLUE);
    } else {
      DrawWaterfall();
    }
  }
}

void APP_Spectrum(void) {
  RADIO_EndAudio(); // Just in case audio is open when spectrum starts

  bExit = FALSE;
  bRXMode = FALSE;

  FreqCenter = gVfoState[gSettings.CurrentVfo].RX.Frequency;
  bNarrow = gVfoState[gSettings.CurrentVfo].bIsNarrow;
  CurrentModulation = gVfoState[gSettings.CurrentVfo].gModulationType;
  CurrentFreqStepIndex = gSettings.FrequencyStep;
  CurrentFreqStep = FREQUENCY_GetStep(CurrentFreqStepIndex);
  // Defaults
  CurrentStepCountIndex = STEPS_64;
  CurrentScanDelay = 4;
  bFilterEnabled = TRUE;
  SquelchLevel = 0;
  bHold = 0;
  DisplayMode = 0;

  SetStepCount();
  SetFreqMinMax(true);

  for (int i = 0; i < 8; i++) {
    gShortString[i] = ' ';
  }

  DISPLAY_Fill(0, 159, 1, 96, COLOR_BACKGROUND);
  Spectrum_Loop();

  StopSpectrum();
}
