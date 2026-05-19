/**
 ******************************************************************************
 * @file    menu.c
 * @brief   Menu System — 长按主界面 3s 进入主菜单
 *
 * 菜单层级:
 *   MENU_MAIN (RETURN / AUDIO / SETTINGS)
 *     ├─ AUDIO → PRESET / DELAY → MENU_EQ → MENU_EQ2
 *     └─ SETTINGS → MENU_SYS → INFO / DEFAULT
 *   长按返回上一级, 主菜单长按/点击RETURN返回 MAIN1
 ******************************************************************************
 */

#include "menu.h"
#include "lcd12232.h"
#include "encoder.h"
#include "tim.h"
#include <string.h>
#include <stdio.h>

/* ── 菜单状态枚举 ── */
typedef enum {
    MENU_MAIN,
    MENU_AUDIO,
    MENU_PRESET_SELECT,
    MENU_SETTINGS,
    MENU_SYS,
    MENU_INFO,
    MENU_DEFAULT,
    MENU_EQ,
    MENU_EQ2
} MenuState;

static MenuState menuState = MENU_MAIN;
static bool menuActive = false;

/* ── MAIN 主菜单 ── */
static uint8_t mainIndex = 0;

/* ── AUDIO 子菜单 ── */
static uint8_t audioIndex = 0;
static bool editingDelay = false;

/* ── SETTINGS 子菜单 ── */
static uint8_t settingsIndex = 0;
static bool editingBright = false;

/* ── SYS 子页面 ── */
static uint8_t sysIndex = 0;
static bool editingContrast = false;
static uint8_t g_contrast = 5;

/* ── DEFAULT 页面 ── */
static uint8_t defaultPhase = 0;
static uint8_t defaultSel = 0;

/* ── EQ 子页面 ── */
static uint8_t eqIndex = 0;
static bool editingEq = false;
static int8_t g_hiEq = 0;
static int8_t g_midEq = 0;
static int8_t g_lowEq = 0;

/* ── EQ2 子页面 ── */
static uint8_t eq2Index = 0;
static bool editingEq2 = false;
static uint8_t g_lowCut = 0;
static uint8_t g_bluetooth = 0;

static const char* lowCutOptions[] = {"OFF", "80Hz", "100Hz", "120Hz", "150Hz"};
static const char* btOptions[] = {"OFF", "ON", "TWS"};

/* ── 背光 ── */
static uint8_t g_lcdDim = 1;
static uint8_t g_brightness = 5;

/* ── 预设 ── */
static uint8_t presetIndex = 0;
static const char* g_selectedPreset = "MUSIC";
static uint8_t g_delayMode = 0;

typedef struct {
    const char *name;
    uint8_t page;
    uint8_t startCol;
    uint8_t width;
} PresetItem;

static const PresetItem presetItems[] = {
    {"MUSIC",    2, 16, 5},
    {"SPEECH",   2, 64, 6},
    {"LIVE",     3, 16, 4},
    {"DJ",       3, 46, 2},
    {"MONITOR",  3, 64, 7}
};
#define PRESET_COUNT 5

/* ── 辅助 ── */
static void FillRect(uint8_t page, uint8_t startCol, uint8_t widthPixels) {
    if (widthPixels == 0) return;
    LCD12232_SetPos(page, startCol);
    for (uint8_t i = 0; i < widthPixels; i++) {
        LCD12232_WriteData(0xFF);
    }
}

static void SetBacklight(uint8_t brightness) {
    if (brightness < 1) brightness = 1;
    if (brightness > 10) brightness = 10;
    uint32_t pwm = 200 + (brightness - 1) * 80;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, pwm);
}

static void UpdateBacklight(void) {
    if (g_lcdDim == 0) {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
    } else {
        SetBacklight(g_brightness);
    }
}

/* ================================================================
 *  显示函数
 * ================================================================ */

static void ShowMainMenu(void) {
    LCD12232_Clear();
    const char *lines[3] = {"<-RETURN", "AUDIO", "SETTINGS"};
    for (int i = 0; i < 3; i++) {
        char buf[20];
        if (i == mainIndex) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowAudioMenu(void) {
    LCD12232_Clear();
    char presetLine[20];
    snprintf(presetLine, sizeof(presetLine), "PRESET:%s", g_selectedPreset);
    const char *delayLine = g_delayMode ? "DELAY:MS" : "DELAY:OFF";
    const char *lines[3] = {"<-RETURN", presetLine, delayLine};

    for (int i = 0; i < 3; i++) {
        char buf[20];
        uint8_t hl = (!editingDelay && i == audioIndex) ||
                     (editingDelay && i == 2);
        if (hl) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowSettingsMenu(void) {
    LCD12232_Clear();
    char dimLine[20];
    snprintf(dimLine, sizeof(dimLine), "LCD DIM:%s", g_lcdDim ? "ON" : "OFF");
    char brightLine[20];
    snprintf(brightLine, sizeof(brightLine), "BRIGHT:%d", g_brightness);
    const char *lines[3] = {"<-RETURN", dimLine, brightLine};

    for (int i = 0; i < 3; i++) {
        char buf[20];
        uint8_t hl = (!editingBright && i == settingsIndex) ||
                     (editingBright && i == 2);
        if (hl) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowSysPage(void) {
    LCD12232_Clear();
    char contrastLine[20];
    snprintf(contrastLine, sizeof(contrastLine), "CONTRAST:%d", g_contrast);
    const char *lines[3] = {contrastLine, "INFO", "DEFAULT"};

    for (int i = 0; i < 3; i++) {
        char buf[20];
        uint8_t hl = (!editingContrast && i == sysIndex) ||
                     (editingContrast && i == 0);
        if (hl) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowInfoPage(void) {
    LCD12232_Clear();
    uint8_t infoCol = (128 - 4 * 6) / 2;
    LCD12232_ShowString8x8(1, infoCol, "INFO");
    const char *verStr = "SOFTWARE : V 1.1";
    uint8_t verCol = (128 - 17 * 6) / 2;
    LCD12232_ShowString8x8(3, verCol, verStr);
}

static void ShowDefaultPage(void) {
    LCD12232_Clear();
    uint8_t titleCol = (128 - 7 * 6) / 2;
    LCD12232_ShowString8x8(1, titleCol, "DEFAULT");

    if (defaultPhase == 0) {
        uint8_t cancelCol = 20;
        uint8_t resetCol  = 80;
        if (defaultSel == 0) {
            LCD12232_FillRow(3, cancelCol, 5 * 6, 0xFF);
            LCD12232_ShowString8x8Inv(3, cancelCol, "CANCEL");
            LCD12232_ShowString8x8(3, resetCol, "RESET");
        } else {
            LCD12232_ShowString8x8(3, cancelCol, "CANCEL");
            LCD12232_FillRow(3, resetCol, 4 * 6, 0xFF);
            LCD12232_ShowString8x8Inv(3, resetCol, "RESET");
        }
    } else {
        uint8_t cancelCol = 20;
        uint8_t yesCol    = 85;
        if (defaultSel == 0) {
            LCD12232_FillRow(3, cancelCol, 5 * 6, 0xFF);
            LCD12232_ShowString8x8Inv(3, cancelCol, "CANCEL");
            LCD12232_ShowString8x8(3, yesCol, "YES");
        } else {
            LCD12232_ShowString8x8(3, cancelCol, "CANCEL");
            LCD12232_FillRow(3, yesCol, 2 * 6, 0xFF);
            LCD12232_ShowString8x8Inv(3, yesCol, "YES");
        }
    }
}

static void ShowEqPage(void) {
    LCD12232_Clear();
    char hiLine[20], midLine[20], lowLine[20];
    snprintf(hiLine,  sizeof(hiLine),  "%-3s EQ:%+03d dB", "HI",  g_hiEq);
    snprintf(midLine, sizeof(midLine), "%-3s EQ:%+03d dB", "MID", g_midEq);
    snprintf(lowLine, sizeof(lowLine), "%-3s EQ:%+03d dB", "LOW", g_lowEq);
    const char *lines[3] = {hiLine, midLine, lowLine};

    for (int i = 0; i < 3; i++) {
        char buf[20];
        uint8_t hl = (i == eqIndex);
        if (hl) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowEq2Page(void) {
    LCD12232_Clear();
    char cutLine[20], btLine[20];
    snprintf(cutLine, sizeof(cutLine), "%-9s:%s", "LOW CUT",  lowCutOptions[g_lowCut]);
    snprintf(btLine,  sizeof(btLine),  "%-9s:%s", "BLUETOOYH", btOptions[g_bluetooth]);
    const char *lines[2] = {cutLine, btLine};

    for (int i = 0; i < 2; i++) {
        char buf[20];
        uint8_t hl = (i == eq2Index);
        if (hl) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

static void ShowPresetSelectPage(void) {
    LCD12232_Clear();
    uint8_t titleLen = 6;
    uint8_t titleStartCol = (128 - titleLen * 6) / 2;
    LCD12232_ShowString8x8(1, titleStartCol, "PRESET");
    LCD12232_ShowString8x8(2, 16, "MUSIC");
    LCD12232_ShowString8x8(2, 64, "SPEECH");
    LCD12232_ShowString8x8(3, 16, "LIVE");
    LCD12232_ShowString8x8(3, 46, "DJ");
    LCD12232_ShowString8x8(3, 64, "MONITOR");
    const PresetItem *item = &presetItems[presetIndex];
    FillRect(item->page, item->startCol, item->width * 6);
    LCD12232_ShowString8x8Inv(item->page, item->startCol, item->name);
}

/* ================================================================
 *  预设选择
 * ================================================================ */
static void EnterPresetSelect(void) {
    menuState = MENU_PRESET_SELECT;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (strcmp(g_selectedPreset, presetItems[i].name) == 0) {
            presetIndex = i;
            break;
        }
    }
    ShowPresetSelectPage();
}

static void ExitPresetSelect(void) {
    menuState = MENU_AUDIO;
    editingDelay = false;
    audioIndex = 1;
    ShowAudioMenu();
}

static void ProcessPresetRotate(int8_t direction) {
    if (direction > 0) {
        presetIndex = (presetIndex + 1) % PRESET_COUNT;
    } else if (direction < 0) {
        presetIndex = (presetIndex + PRESET_COUNT - 1) % PRESET_COUNT;
    }
    ShowPresetSelectPage();
}

static void ProcessPresetClick(void) {
    g_selectedPreset = presetItems[presetIndex].name;
    ExitPresetSelect();
}

/* ================================================================
 *  对外接口
 * ================================================================ */

void Menu_Init(void) {
    UpdateBacklight();
}

void Menu_ProcessRotate(int8_t direction) {
    if (!menuActive) return;

    switch (menuState) {
        case MENU_MAIN:
            if (direction > 0) {
                mainIndex = (mainIndex + 1) % 3;
                ShowMainMenu();
            } else if (direction < 0) {
                mainIndex = (mainIndex == 0) ? 2 : (mainIndex - 1);
                ShowMainMenu();
            }
            break;

        case MENU_AUDIO:
            if (editingDelay) {
                g_delayMode = !g_delayMode;
                ShowAudioMenu();
                break;
            }
            if (direction > 0) {
                if (audioIndex < 2) {
                    audioIndex++;
                    ShowAudioMenu();
                } else {
                    menuState = MENU_EQ;
                    eqIndex = 0;
                    editingEq = false;
                    ShowEqPage();
                }
            } else if (direction < 0) {
                if (audioIndex > 0) {
                    audioIndex--;
                    ShowAudioMenu();
                }
            }
            break;

        case MENU_SETTINGS:
            if (editingBright) {
                if (direction > 0) {
                    if (g_brightness < 10) { g_brightness++; SetBacklight(g_brightness); ShowSettingsMenu(); }
                } else if (direction < 0) {
                    if (g_brightness > 1)  { g_brightness--; SetBacklight(g_brightness); ShowSettingsMenu(); }
                }
                break;
            }
            if (direction > 0) {
                if (settingsIndex < 2) {
                    settingsIndex++;
                    ShowSettingsMenu();
                } else {
                    menuState = MENU_SYS;
                    sysIndex = 0;
                    editingContrast = false;
                    ShowSysPage();
                }
            } else if (direction < 0) {
                if (settingsIndex > 0) {
                    settingsIndex--;
                    ShowSettingsMenu();
                }
            }
            break;

        case MENU_SYS:
            if (editingContrast) {
                if (direction > 0)      { if (g_contrast < 10) g_contrast++; }
                else if (direction < 0) { if (g_contrast > 1)  g_contrast--; }
                ShowSysPage();
                break;
            }
            if (direction > 0) {
                if (sysIndex < 2) { sysIndex++; ShowSysPage(); }
            } else if (direction < 0) {
                if (sysIndex > 0) { sysIndex--; ShowSysPage(); }
                else {
                    menuState = MENU_SETTINGS;
                    settingsIndex = 2;
                    editingBright = false;
                    ShowSettingsMenu();
                }
            }
            break;

        case MENU_INFO:
            break;

        case MENU_DEFAULT:
            defaultSel = (defaultSel == 0) ? 1 : 0;
            ShowDefaultPage();
            break;

        case MENU_PRESET_SELECT:
            ProcessPresetRotate(direction);
            break;

        case MENU_EQ:
            if (editingEq) {
                int8_t *eqVal = NULL;
                if (eqIndex == 0) eqVal = &g_hiEq;
                else if (eqIndex == 1) eqVal = &g_midEq;
                else eqVal = &g_lowEq;
                if (direction > 0)      { if (*eqVal < 12)  (*eqVal)++; }
                else if (direction < 0) { if (*eqVal > -12) (*eqVal)--; }
                ShowEqPage();
                break;
            }
            if (direction > 0) {
                if (eqIndex < 2) { eqIndex++; ShowEqPage(); }
                else {
                    menuState = MENU_EQ2;
                    eq2Index = 0;
                    editingEq2 = false;
                    ShowEq2Page();
                }
            } else if (direction < 0) {
                if (eqIndex > 0) { eqIndex--; ShowEqPage(); }
                else {
                    menuState = MENU_AUDIO;
                    audioIndex = 2;
                    editingDelay = false;
                    ShowAudioMenu();
                }
            }
            break;

        case MENU_EQ2:
            if (editingEq2) {
                uint8_t *val = (eq2Index == 0) ? &g_lowCut : &g_bluetooth;
                uint8_t maxIdx = (eq2Index == 0) ? 4 : 2;
                if (direction > 0)      { if (*val < maxIdx) (*val)++; }
                else if (direction < 0) { if (*val > 0)      (*val)--; }
                ShowEq2Page();
                break;
            }
            if (direction > 0) {
                if (eq2Index < 1) { eq2Index++; ShowEq2Page(); }
            } else if (direction < 0) {
                if (eq2Index > 0) { eq2Index--; ShowEq2Page(); }
                else {
                    menuState = MENU_EQ;
                    eqIndex = 2;
                    editingEq = false;
                    ShowEqPage();
                }
            }
            break;
    }
}

void Menu_ProcessClick(void) {
    if (!menuActive) return;

    switch (menuState) {
        case MENU_MAIN:
            if (mainIndex == 0) {
                Menu_Exit();
            } else if (mainIndex == 1) {
                menuState = MENU_AUDIO;
                audioIndex = 0;
                editingDelay = false;
                ShowAudioMenu();
            } else if (mainIndex == 2) {
                menuState = MENU_SETTINGS;
                settingsIndex = 0;
                editingBright = false;
                ShowSettingsMenu();
            }
            break;

        case MENU_AUDIO:
            if (editingDelay) {
                editingDelay = false;
                ShowAudioMenu();
            } else {
                if (audioIndex == 0) {
                    menuState = MENU_MAIN;
                    mainIndex = 1;
                    ShowMainMenu();
                } else if (audioIndex == 1) {
                    EnterPresetSelect();
                } else if (audioIndex == 2) {
                    editingDelay = true;
                    ShowAudioMenu();
                }
            }
            break;

        case MENU_SETTINGS:
            if (editingBright) {
                editingBright = false;
                ShowSettingsMenu();
            } else {
                if (settingsIndex == 0) {
                    menuState = MENU_MAIN;
                    mainIndex = 2;
                    ShowMainMenu();
                } else if (settingsIndex == 1) {
                    g_lcdDim = !g_lcdDim;
                    UpdateBacklight();
                    ShowSettingsMenu();
                } else if (settingsIndex == 2) {
                    editingBright = true;
                    ShowSettingsMenu();
                }
            }
            break;

        case MENU_SYS:
            if (editingContrast) {
                editingContrast = false;
                ShowSysPage();
            } else {
                if (sysIndex == 0) {
                    editingContrast = true;
                    ShowSysPage();
                } else if (sysIndex == 1) {
                    menuState = MENU_INFO;
                    ShowInfoPage();
                } else if (sysIndex == 2) {
                    menuState = MENU_DEFAULT;
                    defaultPhase = 0;
                    defaultSel = 0;
                    ShowDefaultPage();
                }
            }
            break;

        case MENU_INFO:
            menuState = MENU_SYS;
            sysIndex = 1;
            ShowSysPage();
            break;

        case MENU_DEFAULT:
            if (defaultPhase == 0) {
                if (defaultSel == 0) {
                    menuState = MENU_SYS;
                    sysIndex = 2;
                    ShowSysPage();
                } else {
                    defaultPhase = 1;
                    defaultSel = 0;
                    ShowDefaultPage();
                }
            } else {
                menuState = MENU_SYS;
                sysIndex = 2;
                ShowSysPage();
            }
            break;

        case MENU_PRESET_SELECT:
            ProcessPresetClick();
            break;

        case MENU_EQ:
            if (editingEq) {
                editingEq = false;
                ShowEqPage();
            } else {
                editingEq = true;
                ShowEqPage();
            }
            break;

        case MENU_EQ2:
            if (editingEq2) {
                editingEq2 = false;
                ShowEq2Page();
            } else {
                editingEq2 = true;
                ShowEq2Page();
            }
            break;
    }
}

void Menu_ProcessLongPress(void) {
    if (!menuActive) return;

    switch (menuState) {
        case MENU_MAIN:
            Menu_Exit();
            break;

        case MENU_AUDIO:
            menuState = MENU_MAIN;
            mainIndex = 1;
            ShowMainMenu();
            break;

        case MENU_SETTINGS:
            if (editingBright) {
                editingBright = false;
                ShowSettingsMenu();
            } else {
                menuState = MENU_MAIN;
                mainIndex = 2;
                ShowMainMenu();
            }
            break;

        case MENU_SYS:
            if (editingContrast) {
                editingContrast = false;
                ShowSysPage();
            } else {
                menuState = MENU_SETTINGS;
                settingsIndex = 2;
                editingBright = false;
                ShowSettingsMenu();
            }
            break;

        case MENU_INFO:
            menuState = MENU_SYS;
            sysIndex = 1;
            ShowSysPage();
            break;

        case MENU_DEFAULT:
            menuState = MENU_SYS;
            sysIndex = 2;
            ShowSysPage();
            break;

        case MENU_PRESET_SELECT:
            menuState = MENU_AUDIO;
            editingDelay = false;
            audioIndex = 1;
            ShowAudioMenu();
            break;

        case MENU_EQ:
            if (editingEq) {
                editingEq = false;
                ShowEqPage();
            } else {
                menuState = MENU_AUDIO;
                audioIndex = 2;
                editingDelay = false;
                ShowAudioMenu();
            }
            break;

        case MENU_EQ2:
            if (editingEq2) {
                editingEq2 = false;
                ShowEq2Page();
            } else {
                menuState = MENU_EQ;
                eqIndex = 2;
                editingEq = false;
                ShowEqPage();
            }
            break;
    }
}

void Menu_Show(void) {
    if (!menuActive) return;
    switch (menuState) {
        case MENU_MAIN:           ShowMainMenu(); break;
        case MENU_AUDIO:          ShowAudioMenu(); break;
        case MENU_SETTINGS:       ShowSettingsMenu(); break;
        case MENU_SYS:            ShowSysPage(); break;
        case MENU_INFO:           ShowInfoPage(); break;
        case MENU_DEFAULT:        ShowDefaultPage(); break;
        case MENU_PRESET_SELECT:  ShowPresetSelectPage(); break;
        case MENU_EQ:             ShowEqPage(); break;
        case MENU_EQ2:            ShowEq2Page(); break;
    }
}

bool Menu_IsActive(void) {
    return menuActive;
}

void Menu_Enter(void) {
    menuActive = true;
    menuState = MENU_MAIN;
    mainIndex = 0;
    ShowMainMenu();
}

void Menu_Exit(void) {
    menuActive = false;
    menuState = MENU_MAIN;
    App_GoToMain1();  /* 返回主界面1 */
}
