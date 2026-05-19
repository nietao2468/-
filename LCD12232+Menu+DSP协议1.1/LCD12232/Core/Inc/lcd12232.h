///**
// ******************************************************************************
// * @file    lcd12232.h
// * @brief   LCD12232 (ST7565) Driver for STM32G0B1CC - HAL Library
// * @board   GL206-AES67-PREAMP
// *
// * 引脚对应 (原理图):
// *   PB11 → SPI2_MOSI → MOSI-LCD
// *   PB13 → SPI2_SCK  → SCL-LCD
// *   PB12 → GPIO_Out  → NSS-LCD  (CS,  低有效, 软件控制)
// *   PB15 → GPIO_Out  → A0-LCD   (0=命令, 1=数据)
// *   PA8  → GPIO_Out  → RES-LCD  (RST, 低有效)
// *   PC7  → TIM3_CH2  → PWM-LCD  (背光亮度)
// *
// * CubeMX SPI2 配置:
// *   Mode            : Transmit Only Master
// *   Data Size       : 8 Bits
// *   First Bit       : MSB First
// *   CPOL            : Low  (空闲低电平)
// *   CPHA            : 1 Edge (第一边沿采样, Mode 0)
// *   NSS Signal Type : Software
// *   Prescaler       : /4 → 4 MHz (推荐稳定值)
// ******************************************************************************
// */

//#ifndef __LCD12232_H
//#define __LCD12232_H

//#ifdef __cplusplus
//extern "C" {
//#endif

//#include "main.h"

///* ============================================================
// *  引脚宏定义 — 与 CubeMX User Label 保持一致
// *  若 CubeMX 生成了 LCD_CS_Pin 等宏，此处无需修改
// * ============================================================ */
//#define LCD_CS_PIN          GPIO_PIN_12
//#define LCD_CS_PORT         GPIOB

//#define LCD_A0_PIN          GPIO_PIN_15
//#define LCD_A0_PORT         GPIOB

//#define LCD_RST_PIN         GPIO_PIN_8
//#define LCD_RST_PORT        GPIOA

///* ============================================================
// *  内联操作宏
// * ============================================================ */
//#define LCD_CS_LOW()        HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
//#define LCD_CS_HIGH()       HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
//#define LCD_A0_CMD()        HAL_GPIO_WritePin(LCD_A0_PORT,  LCD_A0_PIN,  GPIO_PIN_RESET)
//#define LCD_A0_DATA()       HAL_GPIO_WritePin(LCD_A0_PORT,  LCD_A0_PIN,  GPIO_PIN_SET)
//#define LCD_RST_LOW()       HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
//#define LCD_RST_HIGH()      HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)

///* ============================================================
// *  对比度初始值 (0x00~0x3F，推荐 0x10~0x28)
// *  对应 51 代码中的 ADJUST=18 (十进制) = 0x12
// * ============================================================ */
//#define LCD_ADJUST_DEFAULT  0x12U

///* ============================================================
// *  外部 SPI 句柄声明 — 在 spi.h 中由 CubeMX 生成
// * ============================================================ */
//extern SPI_HandleTypeDef hspi2;

///* ============================================================
// *  字模数组 (0~9, 8x16点阵)
// * ============================================================ */
//extern const uint8_t ASC_Font[10 * 16];

///* ============================================================
// *  API 函数声明
// * ============================================================ */

///**
// * @brief  完整初始化序列: 硬件复位 → 软件复位 → 参数配置 → 开显示
// *         必须在其他所有显示函数之前调用一次
// */
//void LCD12232_Init(void);

///**
// * @brief  发送命令字节 (A0=0)
// */
//void LCD12232_WriteCmd(uint8_t cmd);

///**
// * @brief  发送数据字节 (A0=1)
// */
//void LCD12232_WriteData(uint8_t dat);

///**
// * @brief  设置页/列地址 (page: 0~7, col: 0~127)
// */
//void LCD12232_SetPos(uint8_t page, uint8_t col);

///**
// * @brief  全屏清除 (全写 0x00)
// */
//void LCD12232_Clear(void);

///**
// * @brief  全屏显示图片
// * @param  pic  指向 1024 字节图像数组的指针
// *              格式: 8页 × 128列，每字节纵向8像素，MSB在下
// */
//void LCD12232_DispPic(const uint8_t *pic);

///**
// * @brief  全屏交替填充两个字节 (条纹/全亮/全灭测试)
// * @param  x  偶数列字节
// * @param  y  奇数列字节
// */
//void LCD12232_DispFram(uint8_t x, uint8_t y);

///**
// * @brief  全屏点阵填充 (每2列写x，再2列写y，循环32组)
// */
//void LCD12232_DispDots(uint8_t x, uint8_t y);

///**
// * @brief  更新对比度并在屏幕第2~3页显示数值 (十位+个位)
// * @param  val  对比度值 0x00~0x3F
// */
//void LCD12232_SetContrast(uint8_t val);

///**
// * @brief  在指定位置显示单个数字 '0'~'9' (8x16点阵，占2页高)
// * @param  page  起始页 0~6
// * @param  col   起始列 0~120
// * @param  digit 数字 0~9
// */
//void LCD12232_ShowDigit(uint8_t page, uint8_t col, uint8_t digit);

//#ifdef __cplusplus
//}
//#endif

//#endif /* __LCD12232_H */
/**
 ******************************************************************************
 * @file    lcd12232.h
 * @brief   LCD12232 (ST7565) Driver — STM32G0B1CC HAL
 * @version v2  对比度修正 + 中文显示支持
 *
 * 引脚 (GL206-AES67-PREAMP):
 *   PB11 → SPI2_MOSI    PB13 → SPI2_SCK
 *   PB12 → LCD_CS(低有效,软件GPIO)
 *   PB15 → LCD_A0       PA8  → LCD_RST
 *   PC7  → TIM3_CH2(背光PWM)
 ******************************************************************************
 */
#ifndef __LCD12232_H
#define __LCD12232_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ── 引脚宏 (与 CubeMX User Label 一致) ── */
#define LCD_CS_PIN      GPIO_PIN_12
#define LCD_CS_PORT     GPIOB
#define LCD_A0_PIN      GPIO_PIN_15
#define LCD_A0_PORT     GPIOB
#define LCD_RST_PIN     GPIO_PIN_8
#define LCD_RST_PORT    GPIOA

/* ── GPIO 操作 ── */
#define LCD_CS_LOW()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
#define LCD_CS_HIGH()   HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
#define LCD_A0_CMD()    HAL_GPIO_WritePin(LCD_A0_PORT,  LCD_A0_PIN,  GPIO_PIN_RESET)
#define LCD_A0_DATA()   HAL_GPIO_WritePin(LCD_A0_PORT,  LCD_A0_PIN,  GPIO_PIN_SET)
#define LCD_RST_LOW()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
#define LCD_RST_HIGH()  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)

/* ── 对比度默认值
 *    v1: 0x12 → 太低，斜着才能看到
 *    v2: 0x26 → 正视角清晰，可在 0x20~0x30 范围调整 ── */
//#define LCD_ADJUST_DEFAULT  0x26U
#define LCD_ADJUST_DEFAULT  0x12
/* ── SPI 句柄 ── */
extern SPI_HandleTypeDef hspi2;

///* ── 字模外部声明 ── */
//extern const uint8_t ASC_Font[10 * 16];       /* 0~9, 8x16 */
//extern const uint8_t ASCII_Font[95][16];       /* 空格~'~', 8x16 */
//extern const uint8_t CN_Font_Test[2][32];      /* "测试" 16x16, 2字 */

/* ── API ── */
void LCD12232_Init(void);
void LCD12232_WriteCmd(uint8_t cmd);
void LCD12232_WriteData(uint8_t dat);
void LCD12232_SetPos(uint8_t page, uint8_t col);
void LCD12232_Clear(void);
void LCD12232_DispPic(const uint8_t *pic);
void LCD12232_DispFram(uint8_t x, uint8_t y);
void LCD12232_DispDots(uint8_t x, uint8_t y);
void LCD12232_SetContrast(uint8_t val);
void LCD12232_ShowDigit(uint8_t page, uint8_t col, uint8_t digit);
void LCD12232_DispPic_122x32(const uint8_t *pic);         /* Image2Lcd 512字节 */

/**
 * @brief 显示 8x16 ASCII 字符串 (空格~'~')
 * @param page  起始页 0~6
 * @param col   起始列 0~120
 * @param str   以'\0'结尾的字符串
 */
void LCD12232_ShowString(uint8_t page, uint8_t col, const char *str);

/**
 * @brief 显示 16x16 汉字 (字模需在 CN_Font 数组中)
 * @param page  起始页 0~6
 * @param col   起始列 0~112
 * @param idx   字模索引
 */
void LCD12232_ShowChinese(uint8_t page, uint8_t col, uint8_t idx);

/**
 * @brief 一键显示 "LCD12232测试" 测试字符串
 *        ASCII 部分用 8x16，汉字 "测试" 用 16x16
 *        全部显示在 page0~1，col0 起始
 */
void LCD12232_ShowTestString(void);
void LCD12232_FillRow(uint8_t page, uint8_t start_col, uint8_t end_col, uint8_t data);
void LCD12232_ShowString8x8Inv(uint8_t page, uint8_t col, const char *str);
#ifdef __cplusplus
}
#endif
#endif /* __LCD12232_H */
