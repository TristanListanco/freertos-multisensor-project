#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SSD1306 128x64 monochrome OLED on I2C1 (PB6 = SCL, PB7 = SDA), address 0x3C.

   Drawing goes into a RAM framebuffer; ssd1306Update sends it to the panel.
   None of these functions lock anything: DisplayTask owns the OLED and is the
   only task that may call them (lab step 26). */

#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_CHAR_WIDTH 6  /* 5-pixel glyph plus a 1-pixel gap, at scale 1 */
#define SSD1306_CHAR_HEIGHT 8 /* 7-pixel glyph plus room for descenders */

/* Sets up I2C1, configures the panel and blanks it. */
HAL_StatusTypeDef ssd1306Init(void);

/* Clears the framebuffer. */
void ssd1306Clear(void);

/* Draws text with its top-left corner at (x, y). Each font pixel becomes a
   scale x scale block. Characters outside printable ASCII draw as '?';
   anything off-screen is clipped. */
void ssd1306DrawText(int x, int y, const char *text, int scale);

/* Sends the framebuffer to the panel. Takes about 25 ms at 400 kHz. */
HAL_StatusTypeDef ssd1306Update(void);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */
