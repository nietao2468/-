/**
 ******************************************************************************
 * @file    encoder.h
 * @brief   Rotary Encoder Driver + Main Interface State Machine
 *
 * 引脚 (GL206-AES67-PREAMP 原理图):
 *   PB3 → ENCOD-KEY (编码器按键, 低有效)
 *   PB4 → ENCOD-LT  (编码器 A 相)
 *   PB5 → ENCOD-RT  (编码器 B 相)
 ******************************************************************************
 */
#ifndef __ENCODER_H
#define __ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/* ── 引脚定义 ── */
#define ENC_KEY_PIN     GPIO_PIN_3      /* PB3 - ENCOD-KEY */
#define ENC_KEY_PORT    GPIOB
#define ENC_A_PIN       GPIO_PIN_4      /* PB4 - ENCOD-LT  */
#define ENC_A_PORT      GPIOB
#define ENC_B_PIN       GPIO_PIN_5      /* PB5 - ENCOD-RT  */
#define ENC_B_PORT      GPIOB

/* ── 音量/增益范围 ── */
#define VOLUME_MIN      (-60)
#define VOLUME_MAX      (12)
#define VOLUME_DEFAULT  (0)

/* ── 应用状态枚举 ── */
typedef enum {
    APP_MAIN1_INPUT,    /* 主界面1: INPUT SETTINGS  */
    APP_MAIN2_OUTPUT,   /* 主界面2: OUTPUT SETTINGS */
    APP_MENU_AUDIO      /* AUDIO 菜单               */
} AppState_t;

/* ── 编码器硬件 API ── */
void Encoder_Init(void);
int8_t Encoder_GetVolume(void);
bool Encoder_NeedRefresh(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/* ── 主界面状态机 API ── */
AppState_t App_GetState(void);
void App_Init(void);
void App_ProcessRotate(int8_t direction);
void App_ProcessClick(void);
void App_ProcessDoubleClick(void);
void App_Show(void);
void App_Update(void);
void App_GoToMain1(void);   /* 切换并显示主界面1 */

#ifdef __cplusplus
}
#endif
#endif /* __ENCODER_H */
