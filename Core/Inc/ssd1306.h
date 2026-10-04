/**
  ******************************************************************************
  * @file           : ssd1306.h
  * @brief          : SSD1306 128x64 OLED display driver header.
  ******************************************************************************
  */

#ifndef __SSD1306_H
#define __SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "ssd1306_fonts.h"
#include "main.h"

#define SSD1306_I2C_ADDR        (0x3C << 1)
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

/* Color inversion */
#define SSD1306_COLOR_BLACK     0x00
#define SSD1306_COLOR_WHITE     0x01

/* API */
HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c);
void SSD1306_Fill(uint8_t color);
void SSD1306_UpdateScreen(I2C_HandleTypeDef *hi2c);
void SSD1306_DrawPixel(uint8_t x, uint8_t y, uint8_t color);
char SSD1306_WriteChar(char ch, FontDef Font, uint8_t color);
char SSD1306_WriteString(const char* str, FontDef Font, uint8_t color);
void SSD1306_SetCursor(uint8_t x, uint8_t y);
void SSD1306_DrawDashboard(I2C_HandleTypeDef *hi2c, const SystemTelemetry *telem);

#ifdef __cplusplus
}
#endif

#endif /* __SSD1306_H */
