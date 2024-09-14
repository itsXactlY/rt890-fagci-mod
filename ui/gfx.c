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

#include "ui/gfx.h"
#include "driver/st7735s.h"
#include "misc.h"

uint16_t gColorForeground;
uint16_t gColorBackground;

uint16_t COLOR_BACKGROUND;
uint16_t COLOR_FOREGROUND;
uint16_t COLOR_RED;
uint16_t COLOR_GREEN;
uint16_t COLOR_BLUE;
uint16_t COLOR_GREY;
uint16_t COLOR_GREY_DARK;
uint16_t COLOR_YELLOW;

void DISPLAY_FillNoReset(uint8_t X0, uint8_t X1, uint8_t Y0, uint8_t Y1,
                         uint16_t Color) {
  ST7735S_SetAddrWindow(X0, Y0, X1, Y1);
  for (uint32_t i = 0; i < (X1 - X0 + 1) * (Y1 - Y0 + 1); ++i) {
    ST7735S_SendU16(Color);
  }
}

void DISPLAY_ResetWindow() { ST7735S_SetAddrWindow(0, 0, 159, 127); }

void DISPLAY_Fill(uint8_t X0, uint8_t X1, uint8_t Y0, uint8_t Y1,
                  uint16_t Color) {
  DISPLAY_FillNoReset(X0, X1, Y0, Y1, Color);
  DISPLAY_ResetWindow();
}

void DISPLAY_FillColor(uint16_t Color) { DISPLAY_Fill(0, 159, 0, 127, Color); }

void DISPLAY_DrawRectangle0(uint8_t X, uint8_t Y, uint8_t W, uint8_t H,
                            uint16_t Color) {
  DISPLAY_Fill(X, X + W - 1, Y, Y + H - 1, Color);
}

void DISPLAY_DrawRectangle1(uint8_t X, uint8_t Y, uint8_t H, uint8_t W,
                            uint16_t Color) {
  DISPLAY_Fill(X, X + W - 1, Y, Y + H - 1, Color);
}

void DISPLAY_DrawRectangle0Nr(uint8_t X, uint8_t Y, uint8_t W, uint8_t H,
                              uint16_t Color) {
  DISPLAY_FillNoReset(X, X + W - 1, Y, Y + H - 1, Color);
}

void DISPLAY_DrawRectangle1Nr(uint8_t X, uint8_t Y, uint8_t H, uint8_t W,
                              uint16_t Color) {
  DISPLAY_FillNoReset(X, X + W - 1, Y, Y + H - 1, Color);
}

void UI_SetColors(uint8_t DarkMode) {
  if (DarkMode) {
    COLOR_BACKGROUND = COLOR_RGB(0, 0, 0);
    COLOR_FOREGROUND = COLOR_RGB(31, 63, 31);
  } else {
    COLOR_BACKGROUND = COLOR_RGB(31, 63, 31);
    COLOR_FOREGROUND = COLOR_RGB(0, 0, 0);
  }
  COLOR_RED = COLOR_RGB(31, 0, 0);
  COLOR_GREEN = COLOR_RGB(0, 63, 0);
  COLOR_BLUE = COLOR_RGB(7, 15, 31);
  COLOR_GREY = COLOR_RGB(16, 32, 16);
  COLOR_GREY_DARK = COLOR_RGB(8, 16, 8);
  COLOR_YELLOW = COLOR_RGB(31, 63, 0);

  gColorBackground = COLOR_BACKGROUND;
  gColorForeground = COLOR_FOREGROUND;

  DISPLAY_FillColor(COLOR_BACKGROUND);
}

void DrawVLine(uint8_t x, uint8_t y, uint8_t h, uint16_t color) {
  // ST7735S_DrawFastLine(x, y, h, color, true);
  DISPLAY_DrawRectangle0(x, y, 1, h, color);
}

void DrawHLine(uint8_t x, uint8_t y, uint8_t h, uint16_t color) {
  // ST7735S_DrawFastLine(x, y, h, color, false);
  DISPLAY_DrawRectangle0(x, y, h, 1, color);
}
