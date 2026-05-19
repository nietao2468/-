/**
 ******************************************************************************
 * @file    encoder.c
 * @brief   Rotary Encoder Driver + Main Interface (MAIN1 / MAIN2)
 ******************************************************************************
 */

#include "encoder.h"
#include "lcd12232.h"
#include "dl04d.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 *  硬件编码器变量
 * ================================================================ */
static volatile int8_t   s_volume = VOLUME_DEFAULT;
static volatile bool     s_needRefresh = true;
static volatile uint32_t s_encTickStamp = 0;
#define ENC_DEBOUNCE_MS  5u

/* ================================================================
 *  主界面状态变量
 * ================================================================ */
static AppState_t g_appState = APP_MAIN1_INPUT;

/* 输入参数 */
static int8_t  g_inputGain    = 0;       /* -60 ~ +12 dB */
static uint8_t g_inputMute    = 0;       /* 0=OFF, 1=ON  */
static uint8_t g_inputInverse = 0;       /* 0=OFF, 1=ON  */

/* 输出参数 */
static int8_t  g_outputGain    = 0;
static uint8_t g_outputMute    = 0;
static uint8_t g_outputInverse = 0;

/* 光标与编辑 */
static uint8_t  g_cursorLine = 0;        /* 0=GAINS, 1=MUTE, 2=INVERSE */
static bool     g_editing    = false;    /* MUTE/INVERSE 编辑模式闪烁  */
static uint32_t g_editTick   = 0;
static bool     g_editBlink  = true;
#define EDIT_BLINK_MS  400u

/* ================================================================
 *  辅助：获取当前界面对应的参数指针
 * ================================================================ */
static int8_t*  GetGainPtr(void)    { return (g_appState == APP_MAIN1_INPUT) ? &g_inputGain    : &g_outputGain; }
static uint8_t* GetMutePtr(void)    { return (g_appState == APP_MAIN1_INPUT) ? &g_inputMute    : &g_outputMute; }
static uint8_t* GetInversePtr(void) { return (g_appState == APP_MAIN1_INPUT) ? &g_inputInverse : &g_outputInverse; }

/* ================================================================
 *  显示主界面 (MAIN1 / MAIN2)
 * ================================================================ */
static void ShowMainPage(void)
{
    LCD12232_Clear();

    /* ── 第一行: 标题居中 ── */
    const char *title;
    uint8_t titleLen;
    if (g_appState == APP_MAIN1_INPUT) {
        title = "INPUT SETTINGS";
        titleLen = 14;
    } else {
        title = "OUTPUT SETTINGS";
        titleLen = 15;
    }
    uint8_t titleCol = (122 - titleLen * 6) / 2;
    LCD12232_ShowString8x8(0, titleCol, title);

    int8_t  gain = *GetGainPtr();
    uint8_t mute = *GetMutePtr();
    uint8_t inv  = *GetInversePtr();

    /* ── 第二行: GAINS ── */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "GAINS: %+03ddB", (int)gain);
        bool hl = (g_cursorLine == 0);
        if (g_editing && g_cursorLine == 0) hl = g_editBlink;
        if (hl) {
            LCD12232_FillRow(1, 0, 121, 0xFF);
            LCD12232_ShowString8x8Inv(1, 0, buf);
        } else {
            LCD12232_ShowString8x8(1, 0, buf);
        }
    }

    /* ── 第三行: MUTE ── */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "MUTE: %s", mute ? "ON " : "OFF");
        bool hl = (g_cursorLine == 1);
        if (g_editing && g_cursorLine == 1) hl = g_editBlink;
        if (hl) {
            LCD12232_FillRow(2, 0, 121, 0xFF);
            LCD12232_ShowString8x8Inv(2, 0, buf);
        } else {
            LCD12232_ShowString8x8(2, 0, buf);
        }
    }

    /* ── 第四行: INVERSE ── */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "INVERSE: %s", inv ? "ON " : "OFF");
        bool hl = (g_cursorLine == 2);
        if (g_editing && g_cursorLine == 2) hl = g_editBlink;
        if (hl) {
            LCD12232_FillRow(3, 0, 121, 0xFF);
            LCD12232_ShowString8x8Inv(3, 0, buf);
        } else {
            LCD12232_ShowString8x8(3, 0, buf);
        }
    }
}

/* ================================================================
 *  发送当前参数到 DSP
 * ================================================================ */
static void SendGainToDsp(void)
{
    int8_t gain = *GetGainPtr();
    if (g_appState == APP_MAIN1_INPUT) {
        DL04D_SetAllInputGain(gain);
    } else {
        DL04D_SetAllOutputGain(gain);
    }
}

static void SendMuteToDsp(void)
{
    uint8_t mute = *GetMutePtr();
    if (g_appState == APP_MAIN1_INPUT) {
        DL04D_SetAllInputMute(mute);
    } else {
        DL04D_SetAllOutputMute(mute);
    }
}

static void SendInverseToDsp(void)
{
    uint8_t inv = *GetInversePtr();
    if (g_appState == APP_MAIN1_INPUT) {
        DL04D_SetAllInputInverse(inv);
    } else {
        DL04D_SetAllOutputInverse(inv);
    }
}

/* ================================================================
 *  编码器旋转处理 — 主界面
 *  非编辑模式: 旋转移动光标 (GAINS ↔ MUTE ↔ INVERSE)
 *  编辑模式:   旋转调节当前行数值
 * ================================================================ */
void App_ProcessRotate(int8_t direction)
{
    if (g_editing) {
        /* 编辑模式: 旋转调节数值 */
        if (g_cursorLine == 0) {
            /* GAINS: 调节增益 (-60 ~ +12) */
            int8_t *gain = GetGainPtr();
            if (direction > 0 && *gain < VOLUME_MAX) {
                (*gain)++;
            } else if (direction < 0 && *gain > VOLUME_MIN) {
                (*gain)--;
            } else {
                return;
            }
        } else if (g_cursorLine == 1) {
            *GetMutePtr() = !(*GetMutePtr());
        } else {
            *GetInversePtr() = !(*GetInversePtr());
        }
        g_editTick = HAL_GetTick();
        g_editBlink = true;
        ShowMainPage();
    } else {
        /* 非编辑模式: 旋转移动光标 */
        if (direction > 0) {
            g_cursorLine = (g_cursorLine + 1) % 3;
        } else {
            g_cursorLine = (g_cursorLine == 0) ? 2 : (g_cursorLine - 1);
        }
        ShowMainPage();
    }
}

/* ================================================================
 *  编码器单击处理 — 主界面
 *  非编辑模式: 按下进入编辑 (保持原值不变, 旋转再调)
 *  编辑模式:   按下确认, 发送 DSP, 留在当前行
 * ================================================================ */
void App_ProcessClick(void)
{
    if (g_editing) {
        /* 编辑模式: 确认并退出, 发送 DSP, 留在当前行 */
        g_editing = false;
        if (g_cursorLine == 0) {
            SendGainToDsp();
        } else if (g_cursorLine == 1) {
            SendMuteToDsp();
        } else {
            SendInverseToDsp();
        }
        ShowMainPage();
    } else {
        /* 非编辑模式: 进入编辑, 保持原值不变 */
        g_editing = true;
        g_editTick = HAL_GetTick();
        g_editBlink = true;
        ShowMainPage();
    }
}

/* ================================================================
 *  编码器双击处理 — 切换 MAIN1 / MAIN2 (500ms 内双击)
 * ================================================================ */
void App_ProcessDoubleClick(void)
{
    g_editing = false;
    if (g_appState == APP_MAIN1_INPUT) {
        g_appState = APP_MAIN2_OUTPUT;
    } else {
        g_appState = APP_MAIN1_INPUT;
    }
    g_cursorLine = 0;
    ShowMainPage();
}

/* ================================================================
 *  编辑闪烁定时器 (主循环中每 10ms 调用)
 * ================================================================ */
static void App_UpdateBlink(void)
{
    if (!g_editing) return;
    uint32_t now = HAL_GetTick();
    if ((now - g_editTick) >= EDIT_BLINK_MS) {
        g_editTick = now;
        g_editBlink = !g_editBlink;
        ShowMainPage();
    }
}

/* ================================================================
 *  对外 API
 * ================================================================ */
AppState_t App_GetState(void)
{
    return g_appState;
}

void App_Init(void)
{
    g_appState    = APP_MAIN1_INPUT;
    g_inputGain   = VOLUME_DEFAULT;
    g_inputMute   = 0;
    g_inputInverse = 0;
    g_outputGain  = VOLUME_DEFAULT;
    g_outputMute  = 0;
    g_outputInverse = 0;
    g_cursorLine  = 0;
    g_editing     = false;
    ShowMainPage();
}

void App_Show(void)
{
    ShowMainPage();
}

void App_GoToMain1(void)
{
    g_appState   = APP_MAIN1_INPUT;
    g_cursorLine = 0;
    g_editing    = false;
    ShowMainPage();
}

/* ================================================================
 *  编码器硬件初始化
 * ================================================================ */
void Encoder_Init(void)
{
    s_volume = VOLUME_DEFAULT;
    s_needRefresh = true;
}

/* ================================================================
 *  中断回调: A相下降沿触发，读取B相电平判断方向
 * ================================================================ */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();
    if (GPIO_Pin == ENC_A_Pin) {
        if ((now - s_encTickStamp) >= ENC_DEBOUNCE_MS) {
            s_encTickStamp = now;
            int8_t dir;
            if (HAL_GPIO_ReadPin(ENC_B_PORT, ENC_B_Pin) == GPIO_PIN_SET) {
                dir = 1;   /* 顺时针 */
            } else {
                dir = -1;  /* 逆时针 */
            }

            if (Menu_IsActive()) {
                Menu_ProcessRotate(dir);
            } else {
                App_ProcessRotate(dir);
            }
        }
    }
    /* 按键由 multi_button 处理 */
}

/* ================================================================
 *  兼容旧接口
 * ================================================================ */
int8_t Encoder_GetVolume(void)
{
    return s_volume;
}

bool Encoder_NeedRefresh(void)
{
    if (s_needRefresh) {
        s_needRefresh = false;
        return true;
    }
    return false;
}

/* ================================================================
 *  主界面刷新入口 (供外部定时调用)
 * ================================================================ */
void App_Update(void)
{
    App_UpdateBlink();
}
