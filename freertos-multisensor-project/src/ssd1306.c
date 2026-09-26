/* SSD1306 OLED driver: a 1 KB framebuffer, text drawing with the 5x7 font, and
   blocking I2C transfers. I2C1 is used only by the OLED.

   Framebuffer layout matches the panel's horizontal addressing mode: 8 pages
   of 8 pixel rows, each page 128 bytes wide. In each byte, bit 0 is the top
   row of that page. */

#include "ssd1306.h"
#include "font5x7.h"
#include <string.h>

#define SSD1306_I2C_ADDRESS (0x3C << 1) /* HAL takes the 8-bit form */
#define SSD1306_CONTROL_COMMANDS 0x00   /* control byte: the rest are commands */
#define SSD1306_CONTROL_DATA 0x40       /* control byte: the rest are pixel data */

/* The timeout covers a whole transfer, including time other tasks spend
   preempting DisplayTask, so it is set well above the 25 ms a full update
   takes. */
#define SSD1306_I2C_TIMEOUT_MS 250

static I2C_HandleTypeDef hi2c1;
static uint8_t framebuffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

static HAL_StatusTypeDef sendCommands(const uint8_t *commands, uint16_t count)
{
    return HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDRESS, SSD1306_CONTROL_COMMANDS,
                             I2C_MEMADD_SIZE_8BIT, (uint8_t *)commands, count,
                             SSD1306_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef ssd1306Init(void)
{
    /* PB6/PB7 as open-drain alternate function, configured before the I2C
       clock is enabled, as CubeMX does. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000; /* fast mode; needs PCLK1 >= 4 MHz (it is 8 MHz) */
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Standard power-up sequence for a 128x64 module with the internal charge
       pump. */
    static const uint8_t init[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide ratio / oscillator frequency: default */
        0xA8, 0x3F, /* multiplex ratio: 64 rows */
        0xD3, 0x00, /* no vertical offset */
        0x40,       /* start at RAM row 0 */
        0x8D, 0x14, /* charge pump on */
        0x20, 0x00, /* horizontal addressing mode */
        0xA1,       /* column 127 maps to SEG0 (not mirrored left-right) */
        0xC8,       /* scan COM63 to COM0 (not upside down) */
        0xDA, 0x12, /* COM pins: alternative configuration, for 64 rows */
        0x81, 0xCF, /* contrast */
        0xD9, 0xF1, /* pre-charge period */
        0xDB, 0x40, /* VCOMH deselect level */
        0xA4,       /* show RAM contents */
        0xA6,       /* normal, not inverted */
        0xAF,       /* display on */
    };
    if (sendCommands(init, sizeof init) != HAL_OK)
    {
        return HAL_ERROR;
    }

    ssd1306Clear();
    return ssd1306Update();
}

void ssd1306Clear(void)
{
    memset(framebuffer, 0, sizeof framebuffer);
}

static void setPixel(int x, int y)
{
    if (x >= 0 && x < SSD1306_WIDTH && y >= 0 && y < SSD1306_HEIGHT)
    {
        framebuffer[(y / 8) * SSD1306_WIDTH + x] |= (uint8_t)(1u << (y % 8));
    }
}

static void drawChar(int x, int y, char c, int scale)
{
    if (c < FONT5X7_FIRST || c > FONT5X7_LAST)
    {
        c = '?';
    }
    const uint8_t *glyph = font5x7[c - FONT5X7_FIRST];

    for (int col = 0; col < 5; col++)
    {
        for (int row = 0; row < 8; row++)
        {
            if (glyph[col] & (1u << row))
            {
                for (int dx = 0; dx < scale; dx++)
                {
                    for (int dy = 0; dy < scale; dy++)
                    {
                        setPixel(x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

void ssd1306DrawText(int x, int y, const char *text, int scale)
{
    for (; *text != '\0'; text++)
    {
        drawChar(x, y, *text, scale);
        x += SSD1306_CHAR_WIDTH * scale;
    }
}

HAL_StatusTypeDef ssd1306SetDisplayOn(bool on)
{
    const uint8_t command = on ? 0xAF : 0xAE;
    return sendCommands(&command, 1);
}

HAL_StatusTypeDef ssd1306Update(void)
{
    /* Write the whole panel: columns 0-127, pages 0-7. */
    static const uint8_t window[] = {0x21, 0, SSD1306_WIDTH - 1, 0x22, 0, SSD1306_HEIGHT / 8 - 1};
    if (sendCommands(window, sizeof window) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDRESS, SSD1306_CONTROL_DATA,
                             I2C_MEMADD_SIZE_8BIT, framebuffer, sizeof framebuffer,
                             SSD1306_I2C_TIMEOUT_MS);
}
