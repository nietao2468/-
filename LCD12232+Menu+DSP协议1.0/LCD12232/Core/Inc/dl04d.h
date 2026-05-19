/* dl04d.h - DIGISYN-AES67 Serial Port Control Protocol */
#ifndef DL04D_H
#define DL04D_H

#include "main.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart1;
#define DL04D_UART      (&huart1)
#define DL04D_TIMEOUT   500
#define DL04D_RX_BUFLEN 512

/* Frame types */
#define TYPE_INTERNAL   0x30   /* internal communication (text mode) */
#define TYPE_UDP        0x31   /* UDP tunnel (binary mode) */

/* Core API: mute control via setMute (matrix mute, inArr/outArr) */
HAL_StatusTypeDef DL04D_SendMuteAll(uint8_t mute);
HAL_StatusTypeDef DL04D_SendMuteIn (uint8_t ch, uint8_t mute);
HAL_StatusTypeDef DL04D_SendMuteOut(uint8_t ch, uint8_t mute);
HAL_StatusTypeDef DL04D_GetMute    (void);
uint8_t           DL04D_LastResultOK(void);

/* Physical channel mute API (setPhyMuteRx / setPhyMuteTx) */
HAL_StatusTypeDef DL04D_SetPhyOutputMute(uint8_t ch, uint8_t mute);
HAL_StatusTypeDef DL04D_SetPhyInputMute(uint8_t ch, uint8_t mute);

/* Set all 4 input channel gains simultaneously (-60 ~ +12 dB) */
HAL_StatusTypeDef DL04D_SetAllInputGain(int8_t db);

/* Set all 4 output channel gains simultaneously (-60 ~ +12 dB) */
HAL_StatusTypeDef DL04D_SetAllOutputGain(int8_t db);

/* Set all 4 input channels mute (0=unmute, 1=mute) */
HAL_StatusTypeDef DL04D_SetAllInputMute(uint8_t mute);

/* Set all 4 output channels mute (0=unmute, 1=mute) */
HAL_StatusTypeDef DL04D_SetAllOutputMute(uint8_t mute);

/* Set all 4 input channels polarity/inverse (0=normal, 1=inverse) */
HAL_StatusTypeDef DL04D_SetAllInputInverse(uint8_t inv);

/* Set all 4 output channels polarity/inverse (0=normal, 1=inverse) */
HAL_StatusTypeDef DL04D_SetAllOutputInverse(uint8_t inv);

/* Internal communication */
void DL04D_GetIP(void);

#endif
