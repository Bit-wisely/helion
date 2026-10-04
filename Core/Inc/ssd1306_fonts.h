/**
  ******************************************************************************
  * @file           : ssd1306_fonts.h
  * @brief          : Font definitions for SSD1306 OLED display driver.
  ******************************************************************************
  */

#ifndef __SSD1306_FONTS_H
#define __SSD1306_FONTS_H

#include <stdint.h>

typedef struct {
    const uint8_t FontWidth;    /* Font width in pixels */
    const uint8_t FontHeight;   /* Font height in pixels */
    const uint8_t *data;        /* Pointer to data font data array */
} FontDef;

extern const FontDef Font_6x8;
extern const FontDef Font_7x10;

#endif /* __SSD1306_FONTS_H */
