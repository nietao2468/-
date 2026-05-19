/**
 ******************************************************************************
 * @file    lcd12232.c
 * @brief   LCD12232 (ST7565) Driver v2 — 对比度修正 + 中文显示
 *
 * 修改记录:
 *   v1 → v2:
 *     1. LCD_ADJUST_DEFAULT: 0x12 → 0x26  (修复斜视才可见的对比度问题)
 *     2. 新增 ASCII_Font[95][16]            (完整 8x16 可打印字符)
 *     3. 新增 CN_Font_Test[2][32]           ("测""试" 16x16 字模)
 *     4. 新增 LCD12232_ShowString()
 *     5. 新增 LCD12232_ShowChinese()
 *     6. 新增 LCD12232_ShowTestString()
 ******************************************************************************
 */

#include "lcd12232.h"
#include <string.h>
#include "lcdfont.h"


/* ════════════════════════════════════════════════════════════════════════════
 *  内部状态
 * ════════════════════════════════════════════════════════════════════════════ */
static uint8_t s_contrast = LCD_ADJUST_DEFAULT;
extern const uint8_t ASCII_8x8_Font[95][8];
/* ════════════════════════════════════════════════════════════════════════════
 *  底层 SPI 发送
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_WriteCmd(uint8_t cmd)
{
    LCD_A0_CMD();
    LCD_CS_LOW();
    HAL_SPI_Transmit(&hspi2, &cmd, 1, HAL_MAX_DELAY);
    LCD_CS_HIGH();
}

void LCD12232_WriteData(uint8_t dat)
{
    LCD_A0_DATA();
    LCD_CS_LOW();
    HAL_SPI_Transmit(&hspi2, &dat, 1, HAL_MAX_DELAY);
    LCD_CS_HIGH();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  地址设置
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_SetPos(uint8_t page, uint8_t col)
{
    LCD12232_WriteCmd(0xB0 | (page & 0x07));
    LCD12232_WriteCmd(0x10 | ((col >> 4) & 0x0F));
    LCD12232_WriteCmd(0x00 | (col & 0x0F));
}

/* ════════════════════════════════════════════════════════════════════════════
 *  初始化序列
 *  关键修正: ADJUST 从 0x12 提高到 0x26，解决正视角对比度不足问题
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_Init(void)
{
    /* 硬件复位 */
    LCD_RST_HIGH();
    HAL_Delay(40);
    LCD_RST_LOW();
    HAL_Delay(20);
    LCD_RST_HIGH();
    HAL_Delay(20);

    LCD12232_WriteCmd(0xE2);    /* 软件复位 */
    HAL_Delay(10);

    LCD12232_WriteCmd(0xA2);    /* Bias 1/9 */
    HAL_Delay(2);
    LCD12232_WriteCmd(0xA0);    /* ADC Normal */
    HAL_Delay(2);
    LCD12232_WriteCmd(0xC8);    /* SHL Reverse (若显示上下颠倒改为0xC0) */
    HAL_Delay(2);

    LCD12232_WriteCmd(0x22);    /* 内部电阻比 (1+Rb/Ra)=7.0，提升驱动电压 */
    HAL_Delay(2);

    /* ★ 对比度: 0x26 = 38，比原 0x12=18 高，正视角清晰 */
    LCD12232_WriteCmd(0x81);
    LCD12232_WriteCmd(s_contrast);
    HAL_Delay(2);

    LCD12232_WriteCmd(0x2D);    /* 升压电路分步启动 */
    HAL_Delay(2);
    LCD12232_WriteCmd(0x2E);
    HAL_Delay(2);
    LCD12232_WriteCmd(0x2F);
    HAL_Delay(2);

    LCD12232_WriteCmd(0xF8);    /* Booster Ratio */
    LCD12232_WriteCmd(0x00);
    HAL_Delay(2);

    LCD12232_WriteCmd(0x40);    /* 显示起始行 = 0 */
    HAL_Delay(2);
    LCD12232_WriteCmd(0xA4);    /* 正常显示 */
    HAL_Delay(2);
    LCD12232_WriteCmd(0xAF);    /* 开显示 */
    HAL_Delay(2);

    LCD12232_Clear();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  显示函数
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_Clear(void)
{
    uint8_t page, col;
    for (page = 0; page < 8; page++) {
        LCD12232_SetPos(page, 0);
        for (col = 0; col < 128; col++)
            LCD12232_WriteData(0x00);
    }
}

void LCD12232_DispPic(const uint8_t *pic)
{
    uint8_t page, col;
    uint16_t idx = 0;
    for (page = 0; page < 8; page++) {
        LCD12232_SetPos(page, 0);
        for (col = 0; col < 128; col++)
            LCD12232_WriteData(pic[idx++]);
    }
}

void LCD12232_DispFram(uint8_t x, uint8_t y)
{
    uint8_t page, i;
    for (page = 0; page < 8; page++) {
        LCD12232_SetPos(page, 0);
        for (i = 0; i < 64; i++) {
            LCD12232_WriteData(x);
            LCD12232_WriteData(y);
        }
    }
}

void LCD12232_DispDots(uint8_t x, uint8_t y)
{
    uint8_t page, i, j;
    for (page = 0; page < 8; page++) {
        LCD12232_SetPos(page, 0);
        for (i = 0; i < 32; i++) {
            for (j = 0; j < 2; j++) LCD12232_WriteData(x);
            for (j = 0; j < 2; j++) LCD12232_WriteData(y);
        }
    }
}

void LCD12232_ShowDigit(uint8_t page, uint8_t col, uint8_t digit)
{
    uint8_t j;
    digit %= 10;
    LCD12232_SetPos(page, col);
    for (j = 0; j < 8; j++)
        LCD12232_WriteData(ASC_Font[digit * 16 + j]);
    LCD12232_SetPos(page + 1, col);
    for (j = 8; j < 16; j++)
        LCD12232_WriteData(ASC_Font[digit * 16 + j]);
}


void LCD12232_ShowString8x8(uint8_t page, uint8_t col, const char *str) {
    uint8_t j;
    while (*str && col <= 122) {
        uint8_t idx = (uint8_t)(*str);
        if (idx < 0x20 || idx > 0x7E) { str++; col += 6; continue; }
        idx -= 0x20;
        LCD12232_SetPos(page, col);
        for (j = 0; j < 8; j++) {
            LCD12232_WriteData(ASCII_8x8_Font[idx][j]);
        }
        col += 6;   // 字符宽度6像素（5点阵+1空隙）
        str++;
    }
}


void LCD12232_SetContrast(uint8_t val)
{
    uint8_t j, tens, ones;
    s_contrast = val & 0x3F;
    LCD12232_WriteCmd(0x81);
    LCD12232_WriteCmd(s_contrast);
    tens = s_contrast / 10;
    ones = s_contrast % 10;
    LCD12232_SetPos(2, 4);
    for (j = 0; j < 8; j++)  LCD12232_WriteData(ASC_Font[tens * 16 + j]);
    LCD12232_SetPos(3, 4);
    for (j = 8; j < 16; j++) LCD12232_WriteData(ASC_Font[tens * 16 + j]);
    LCD12232_SetPos(2, 12);
    for (j = 0; j < 8; j++)  LCD12232_WriteData(ASC_Font[ones * 16 + j]);
    LCD12232_SetPos(3, 12);
    for (j = 8; j < 16; j++) LCD12232_WriteData(ASC_Font[ones * 16 + j]);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ASCII 字符串显示 (8x16)
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_ShowString(uint8_t page, uint8_t col, const char *str)
{
    uint8_t j;

    while (*str && col <= 120) {
        uint8_t idx = (uint8_t)(*str);
        if (idx < 0x20 || idx > 0x7E) { str++; col += 8; continue; }
        idx -= 0x20;
        LCD12232_SetPos(page, col);
        for (j = 0; j < 8; j++)
            LCD12232_WriteData(ASCII_Font[idx][j]);
        LCD12232_SetPos(page + 1, col);
        for (j = 8; j < 16; j++)
            LCD12232_WriteData(ASCII_Font[idx][j]);
        col += 8;
        str++;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  汉字显示 (16x16)
 *  idx: CN_Font_Test 数组索引 (0=测, 1=试)
 * ════════════════════════════════════════════════════════════════════════════ */
void LCD12232_ShowChinese(uint8_t page, uint8_t col, uint8_t idx)
{
    uint8_t j;
    LCD12232_SetPos(page, col);
    for (j = 0; j < 16; j++)
        LCD12232_WriteData(CN_Font_Test[idx][j]);
    LCD12232_SetPos(page + 1, col);
    for (j = 16; j < 32; j++)
        LCD12232_WriteData(CN_Font_Test[idx][j]);
}


void LCD12232_DispPic_122x32(const uint8_t *pic)
{
    uint8_t page, col;
    for (page = 0; page < 4; page++) {
        LCD12232_SetPos(page, 0);
        for (col = 0; col < 122; col++) {
            LCD12232_WriteData(pic[page * 122 + col]);
        }
    }
}

void LCD12232_FillRow(uint8_t page, uint8_t start_col, uint8_t end_col, uint8_t data) {
    if (start_col > end_col) return;
    LCD12232_SetPos(page, start_col);
    for (uint8_t col = start_col; col <= end_col; col++) {
        LCD12232_WriteData(data);
    }
}
// 反色显示字符串 (1: 白色背景黑色字体)
void LCD12232_ShowString8x8Inv(uint8_t page, uint8_t col, const char *str) {
    uint8_t j;
    while (*str && col <= 122) {
        uint8_t idx = (uint8_t)(*str);
        if (idx < 0x20 || idx > 0x7E) { str++; col += 6; continue; }
        idx -= 0x20;
        LCD12232_SetPos(page, col);
        for (j = 0; j < 8; j++) {
            uint8_t data = ASCII_8x8_Font[idx][j];
            LCD12232_WriteData(~data);  // 按位取反，实现黑色字体
        }
        col += 6;
        str++;
    }
}
