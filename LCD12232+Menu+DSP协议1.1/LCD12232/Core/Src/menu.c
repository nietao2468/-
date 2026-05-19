/**
 ******************************************************************************
 * @file    menu.c
 * @brief   Menu System — 长按主界面 3s 进入主菜单
 *
 * 菜单层级:
 *   MENU_MAIN (RETURN / AUDIO / SETTINGS)
 *     ├─ AUDIO → LOAD/SAVE PRESET → MENU_EQ → MENU_EQ2
 *     └─ SETTINGS → MENU_SYS → INFO / DEFAULT
 *   长按返回上一级, 主菜单长按/点击RETURN返回 MAIN1
 ******************************************************************************
 */

#include "menu.h"
#include "lcd12232.h"
#include "encoder.h"
#include "dl04d.h"
#include "tim.h"
#include <string.h>
#include <stdio.h>

/* ── 菜单状态枚举 ── */
typedef enum {
    MENU_MAIN,
    MENU_AUDIO,
    MENU_PRESET_LOAD,
    MENU_PRESET_SAVE,
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

/* ── 延迟操作 (从中断上下文推迟到主循环执行，避免阻塞 SysTick) ── */
static volatile uint8_t s_pendingEnter = 0;  /* 1=进入SAVE列表, 2=进入LOAD列表 */
static volatile uint8_t s_pendingExec  = 0;  /* 1=执行SAVE,  2=执行LOAD */
static char   s_pendingName[PRESET_NAME_LEN];

/* ── 预设列表页面 (SAVE / LOAD 共用) ── */
static uint8_t  presetCursor = 0;        /* 光标在列表中的绝对索引 (0 ~ presetCount) */
static uint8_t  presetPageStart = 0;     /* 当前页显示的第一个预设索引 */
static bool     presetEditing = false;   /* true=当前行处于"动作选择"模式 */
static uint8_t  presetEditAction = 0;    /* 0=SAVE/LOAD, 1=CANCEL (仅在 presetEditing 下有效) */
static bool     presetIsSave = false;    /* true=SAVE 模式, false=LOAD 模式 */
static uint8_t  presetCount = 0;         /* 缓存的预设数量 */
static char     s_lastSaveName[PRESET_NAME_LEN];  /* 最近保存的名称 */

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


/* ── 辅助 ── */
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
    const char *lines[3] = {"<-RETURN", "LOAD PRESET", "SAVE PRESET"};

    for (int i = 0; i < 3; i++) {
        char buf[20];
        if (i == audioIndex) {
            snprintf(buf, sizeof(buf), "%s", lines[i]);
            LCD12232_FillRow(i + 1, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(i + 1, 0, buf);
        } else {
            snprintf(buf, sizeof(buf), " %s", lines[i]);
            LCD12232_ShowString8x8(i + 1, 0, buf);
        }
    }
}

/* ── 从预设名称中剥离 "NN_" / "NN-" 序号前缀 (协议: 前3字符=序号) ── */
static const char* PresetDisplayName(const char *name) {
    uint8_t len = strlen(name);
    if (len >= 4 && name[0] >= '0' && name[0] <= '9' &&
                   name[1] >= '0' && name[1] <= '9' &&
                   (name[2] == '_' || name[2] == '-')) {
        return name + 3;  /* 跳过 "01_" 或 "01-" */
    }
    return name;
}

/* ── 预设列表显示 (SAVE / LOAD 共用) ──
 *   行0: 标题居中
 *   行1~3: 预设条目 "NAME  ACTION"
 *   光标所在行反白; 若处于编辑模式则闪烁 ACTION 部分 */
static void ShowPresetListPage(void) {
    LCD12232_Clear();

    /* 行0: 标题 */
    const char *title = presetIsSave ? "SAVE PRESET" : "LOAD PRESET";
    uint8_t titleLen = (presetIsSave ? 10 : 10);
    uint8_t titleCol = (128 - titleLen * 6) / 2;
    LCD12232_ShowString8x8(0, titleCol, title);

    /* 行1~3: 预设条目 */
    uint8_t totalItems = presetCount + 1;  /* +1 for "<-RETURN" */
    for (uint8_t row = 0; row < 3; row++) {
        uint8_t globalIdx = presetPageStart + row;
        if (globalIdx >= totalItems) break;  /* 超出范围留空 */

        char buf[22];
        const char *actionStr;
        bool isCursor = (globalIdx == presetCursor);

        if (globalIdx < presetCount) {
            /* 预设条目: 显示名去掉 "NN_" 前缀 */
            const char *name = PresetDisplayName(DL04D_GetPresetName(globalIdx));
            if (isCursor && presetEditing) {
                actionStr = presetEditAction ? "CANCEL" : (presetIsSave ? "SAVE" : "LOAD");
            } else {
                actionStr = presetIsSave ? "SAVE" : "LOAD";
            }
            snprintf(buf, sizeof(buf), "%-13.13s %-6s", name, actionStr);
        } else {
            /* "<-RETURN" 条目 */
            actionStr = "";
            snprintf(buf, sizeof(buf), "<-RETURN");
        }

        uint8_t rowPage = row + 1;
        if (isCursor && !presetEditing) {
            /* 导航模式: 光标行反白 */
            LCD12232_FillRow(rowPage, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(rowPage, 0, buf);
        } else if (isCursor && presetEditing) {
            /* 编辑模式: 光标行反白 */
            LCD12232_FillRow(rowPage, 0, 127, 0xFF);
            LCD12232_ShowString8x8Inv(rowPage, 0, buf);
        } else {
            LCD12232_ShowString8x8(rowPage, 0, buf);
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

/* ================================================================
 *  预设列表进入 (主循环中调用，避免在中断里阻塞)
 * ================================================================ */
static void EnterPresetList(bool isSave) {
    DL04D_GetPresetList();
    presetCount = DL04D_GetPresetCount();
    presetIsSave = isSave;
    presetCursor = 0;
    presetPageStart = 0;
    presetEditing = false;
    presetEditAction = 0;

    if (isSave) {
        menuState = MENU_PRESET_SAVE;
    } else {
        menuState = MENU_PRESET_LOAD;
    }
    ShowPresetListPage();
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

        case MENU_PRESET_LOAD:
        case MENU_PRESET_SAVE:
            {
                uint8_t totalItems = presetCount + 1;  /* +1 for "<-RETURN" */
                if (presetEditing) {
                    /* 编辑模式: 旋转切换 SAVE/LOAD ↔ CANCEL */
                    presetEditAction = (presetEditAction == 0) ? 1 : 0;
                    ShowPresetListPage();
                } else {
                    /* 导航模式: 旋转移动光标 */
                    if (direction > 0) {
                        if (presetCursor < totalItems - 1) {
                            presetCursor++;
                            if (presetCursor >= presetPageStart + 3)
                                presetPageStart = presetCursor - 2;
                            ShowPresetListPage();
                        }
                    } else if (direction < 0) {
                        if (presetCursor > 0) {
                            presetCursor--;
                            if (presetCursor < presetPageStart)
                                presetPageStart = presetCursor;
                            ShowPresetListPage();
                        }
                    }
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
                ShowAudioMenu();
            } else if (mainIndex == 2) {
                menuState = MENU_SETTINGS;
                settingsIndex = 0;
                editingBright = false;
                ShowSettingsMenu();
            }
            break;

        case MENU_AUDIO:
            if (audioIndex == 0) {
                menuState = MENU_MAIN;
                mainIndex = 1;
                ShowMainMenu();
            } else if (audioIndex == 1) {
                /* LOAD PRESET → 延迟到主循环读取预设列表 */
                s_pendingEnter = 2;
            } else if (audioIndex == 2) {
                /* SAVE PRESET → 延迟到主循环读取预设列表 */
                s_pendingEnter = 1;
            }
            break;

        case MENU_PRESET_LOAD:
        case MENU_PRESET_SAVE:
            if (presetEditing) {
                /* 编辑模式: 按下确认动作 */
                presetEditing = false;
                if (presetEditAction == 0) {
                    /* 执行 SAVE 或 LOAD (延迟到主循环) */
                    const char *name = DL04D_GetPresetName(presetCursor);
                    strncpy(s_pendingName, name, PRESET_NAME_LEN - 1);
                    s_pendingName[PRESET_NAME_LEN - 1] = '\0';
                    s_pendingExec = presetIsSave ? 1 : 2;
                }
                ShowPresetListPage();
            } else {
                /* 导航模式: 按下进入编辑或返回 */
                uint8_t totalItems = presetCount + 1;
                if (presetCursor == totalItems - 1) {
                    /* "<-RETURN" → 返回 AUDIO 菜单 */
                    menuState = MENU_AUDIO;
                    audioIndex = presetIsSave ? 2 : 1;
                    ShowAudioMenu();
                } else if (presetCount > 0) {
                    /* 进入动作选择模式 */
                    presetEditing = true;
                    presetEditAction = 0;  /* 默认=SAVE/LOAD */
                    ShowPresetListPage();
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

        case MENU_PRESET_LOAD:
        case MENU_PRESET_SAVE:
            if (presetEditing) {
                /* 长按退出编辑模式 */
                presetEditing = false;
                ShowPresetListPage();
            } else {
                menuState = MENU_AUDIO;
                audioIndex = presetIsSave ? 2 : 1;
                ShowAudioMenu();
            }
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

        case MENU_EQ:
            if (editingEq) {
                editingEq = false;
                ShowEqPage();
            } else {
                menuState = MENU_AUDIO;
                audioIndex = 2;
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
        case MENU_PRESET_LOAD:    ShowPresetListPage(); break;
        case MENU_PRESET_SAVE:    ShowPresetListPage(); break;
        case MENU_SETTINGS:       ShowSettingsMenu(); break;
        case MENU_SYS:            ShowSysPage(); break;
        case MENU_INFO:           ShowInfoPage(); break;
        case MENU_DEFAULT:        ShowDefaultPage(); break;
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

/* ── 延迟任务处理 (主循环中每 10ms 调用，避免在中断上下文阻塞) ── */
void Menu_ProcessPending(void) {
    if (s_pendingEnter) {
        uint8_t mode = s_pendingEnter;
        s_pendingEnter = 0;
        EnterPresetList(mode == 1);
    }
    if (s_pendingExec) {
        uint8_t mode = s_pendingExec;
        s_pendingExec = 0;
        if (mode == 1) {
            /* 执行 SAVE */
            LCD12232_Clear();
            LCD12232_ShowString8x8(1, 0, "SAVING...");
            if (DL04D_SavePreset(s_pendingName)) {
                strncpy(s_lastSaveName, s_pendingName, PRESET_NAME_LEN - 1);
                s_lastSaveName[PRESET_NAME_LEN - 1] = '\0';
                presetCount = DL04D_GetPresetCount();
                ShowPresetListPage();
            } else {
                LCD12232_ShowString8x8(2, 0, "SAVE FAILED");
                HAL_Delay(1000);
                ShowPresetListPage();
            }
        } else {
            /* 执行 LOAD */
            LCD12232_Clear();
            LCD12232_ShowString8x8(1, 0, "LOADING...");
            if (DL04D_LoadPreset(s_pendingName)) {
                menuState = MENU_MAIN;
                mainIndex = 1;
                ShowMainMenu();
            } else {
                LCD12232_ShowString8x8(2, 0, "LOAD FAILED");
                HAL_Delay(1000);
                ShowPresetListPage();
            }
        }
    }
}
