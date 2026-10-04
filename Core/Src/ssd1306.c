/**
  ******************************************************************************
  * @file           : ssd1306.c
  * @brief          : Complete SSD1306 128x64 OLED driver and telemetry dashboard.
  ******************************************************************************
  */

#include "ssd1306.h"

/* 128x64 1-bit framebuffer: 1024 bytes */
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t Inverted;
    uint8_t Initialized;
} SSD1306_t;

static SSD1306_t SSD1306;

static HAL_StatusTypeDef SSD1306_WriteCommand(I2C_HandleTypeDef *hi2c, uint8_t byte) {
    return HAL_I2C_Mem_Write(hi2c, SSD1306_I2C_ADDR, 0x00, 1, &byte, 1, 50);
}

HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c) {
    /* Wait for screen to power up */
    HAL_Delay(50);

    /* Init sequence */
    if (SSD1306_WriteCommand(hi2c, 0xAE) != HAL_OK) return HAL_ERROR; // Display Off
    SSD1306_WriteCommand(hi2c, 0x20); // Set Memory Addressing Mode
    SSD1306_WriteCommand(hi2c, 0x00); // 00b = Horizontal Addressing Mode
    SSD1306_WriteCommand(hi2c, 0xB0); // Set Page Start Address for Page Addressing Mode
    SSD1306_WriteCommand(hi2c, 0xC8); // Set COM Output Scan Direction
    SSD1306_WriteCommand(hi2c, 0x00); // Set low column address
    SSD1306_WriteCommand(hi2c, 0x10); // Set high column address
    SSD1306_WriteCommand(hi2c, 0x40); // Set start line address
    SSD1306_WriteCommand(hi2c, 0x81); // Set contrast control register
    SSD1306_WriteCommand(hi2c, 0xFF);
    SSD1306_WriteCommand(hi2c, 0xA1); // Set segment re-map 0 to 127
    SSD1306_WriteCommand(hi2c, 0xA6); // Set normal display
    SSD1306_WriteCommand(hi2c, 0xA8); // Set multiplex ratio(1 to 64)
    SSD1306_WriteCommand(hi2c, 0x3F); //
    SSD1306_WriteCommand(hi2c, 0xA4); // 0xa4,Output follows RAM content;0xa5,Output ignores RAM content
    SSD1306_WriteCommand(hi2c, 0xD3); // Set display offset
    SSD1306_WriteCommand(hi2c, 0x00); // Not offset
    SSD1306_WriteCommand(hi2c, 0xD5); // Set display clock divide ratio/oscillator frequency
    SSD1306_WriteCommand(hi2c, 0xF0); // Set divide ratio
    SSD1306_WriteCommand(hi2c, 0xD9); // Set pre-charge period
    SSD1306_WriteCommand(hi2c, 0x22);
    SSD1306_WriteCommand(hi2c, 0xDA); // Set com pins hardware configuration
    SSD1306_WriteCommand(hi2c, 0x12);
    SSD1306_WriteCommand(hi2c, 0xDB); // Set vcomh
    SSD1306_WriteCommand(hi2c, 0x20); // 0x20,0.77xVcc
    SSD1306_WriteCommand(hi2c, 0x8D); // Set DC-DC enable
    SSD1306_WriteCommand(hi2c, 0x14); //
    SSD1306_WriteCommand(hi2c, 0xAF); // Turn on SSD1306 panel

    SSD1306_Fill(SSD1306_COLOR_BLACK);
    SSD1306_UpdateScreen(hi2c);

    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;
    SSD1306.Initialized = 1;

    return HAL_OK;
}

void SSD1306_Fill(uint8_t color) {
    memset(SSD1306_Buffer, (color == SSD1306_COLOR_BLACK) ? 0x00 : 0xFF, sizeof(SSD1306_Buffer));
}

void SSD1306_UpdateScreen(I2C_HandleTypeDef *hi2c) {
    for (uint8_t m = 0; m < 8; m++) {
        SSD1306_WriteCommand(hi2c, 0xB0 + m);
        SSD1306_WriteCommand(hi2c, 0x00);
        SSD1306_WriteCommand(hi2c, 0x10);

        HAL_I2C_Mem_Write(hi2c, SSD1306_I2C_ADDR, 0x40, 1, &SSD1306_Buffer[SSD1306_WIDTH * m], SSD1306_WIDTH, 100);
    }
}

void SSD1306_DrawPixel(uint8_t x, uint8_t y, uint8_t color) {
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;

    if (color == SSD1306_COLOR_WHITE) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    } else {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

void SSD1306_SetCursor(uint8_t x, uint8_t y) {
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

char SSD1306_WriteChar(char ch, FontDef Font, uint8_t color) {
    if (ch < 32 || ch > 126) return 0;
    if (SSD1306_WIDTH < (SSD1306.CurrentX + Font.FontWidth) ||
        SSD1306_HEIGHT < (SSD1306.CurrentY + Font.FontHeight)) {
        return 0;
    }

    for (uint32_t i = 0; i < Font.FontHeight; i++) {
        for (uint32_t j = 0; j < Font.FontWidth; j++) {
            uint8_t byte = Font.data[(ch - 32) * Font.FontWidth + j];
            if ((byte >> i) & 0x01) {
                SSD1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, (color == SSD1306_COLOR_WHITE) ? SSD1306_COLOR_WHITE : SSD1306_COLOR_BLACK);
            } else {
                SSD1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, (color == SSD1306_COLOR_WHITE) ? SSD1306_COLOR_BLACK : SSD1306_COLOR_WHITE);
            }
        }
    }

    SSD1306.CurrentX += Font.FontWidth;
    return ch;
}

char SSD1306_WriteString(const char* str, FontDef Font, uint8_t color) {
    while (*str) {
        if (SSD1306_WriteChar(*str, Font, color) != *str) {
            return *str;
        }
        str++;
    }
    return *str;
}

void SSD1306_DrawDashboard(I2C_HandleTypeDef *hi2c, const SystemTelemetry *telem) {
    char buf[32];
    SSD1306_Fill(SSD1306_COLOR_BLACK);

    /* Line 0: Header */
    SSD1306_SetCursor(2, 0);
    SSD1306_WriteString("HELION SOLAR TRACKER", Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 1: Mode & Park State */
    SSD1306_SetCursor(2, 9);
    snprintf(buf, sizeof(buf), "MODE:%s  %s",
             (telem->mode == MODE_DEMO) ? "DEMO" : "FIELD",
             telem->is_parked ? "[PARKED]" : "[ACTIVE]");
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 2: Tracked Panel Power & Energy */
    SSD1306_SetCursor(2, 19);
    snprintf(buf, sizeof(buf), "TRK:%4.0fmW %5.1fmAh", telem->p_tracked, telem->mah_tracked);
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 3: Fixed Reference Panel Power & Energy */
    SSD1306_SetCursor(2, 29);
    snprintf(buf, sizeof(buf), "FIX:%4.0fmW %5.1fmAh", telem->p_fixed, telem->mah_fixed);
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 4: System Parasitic Power & Input Voltage */
    SSD1306_SetCursor(2, 39);
    snprintf(buf, sizeof(buf), "SYS:%4.0fmW  %4.1fV", telem->p_system, telem->v_system);
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 5: Relative Yield Gain */
    float net_gain = 0.0f;
    if (telem->mah_fixed > 0.01f) {
        net_gain = ((telem->mah_tracked - telem->mah_fixed) / telem->mah_fixed) * 100.0f;
    }
    SSD1306_SetCursor(2, 49);
    snprintf(buf, sizeof(buf), "GAIN:%+5.1f%%", net_gain);
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    /* Line 6: Normalized Error */
    SSD1306_SetCursor(74, 49);
    snprintf(buf, sizeof(buf), "P:%+2ld T:%+2ld", (long)telem->err_pan, (long)telem->err_tilt);
    SSD1306_WriteString(buf, Font_6x8, SSD1306_COLOR_WHITE);

    SSD1306_UpdateScreen(hi2c);
}
