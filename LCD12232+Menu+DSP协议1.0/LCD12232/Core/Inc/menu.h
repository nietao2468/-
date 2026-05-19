/**
 ******************************************************************************
 * @file    menu.h
 * @brief   AUDIO Menu System — 长按主界面进入
 ******************************************************************************
 */
#ifndef __MENU_H
#define __MENU_H

#include <stdint.h>
#include <stdbool.h>

void Menu_Init(void);
void Menu_ProcessRotate(int8_t direction);
void Menu_ProcessClick(void);
void Menu_ProcessLongPress(void);
void Menu_Show(void);

bool Menu_IsActive(void);
void Menu_Enter(void);
void Menu_Exit(void);

#endif
