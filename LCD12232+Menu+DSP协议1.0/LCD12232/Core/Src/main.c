/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include "dl04d.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd12232.h"
#include "encoder.h"
#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "multi_button.h"
#include "dma.h"
#include "usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
extern const unsigned char gImage_1[488];
extern const unsigned char gImage_2[488];
extern const unsigned char gImage_3[488];
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
static Button enc_btn;

static uint8_t read_enc_key(uint8_t id) {
    (void)id;
    return HAL_GPIO_ReadPin(ENC_KEY_PORT, ENC_KEY_PIN);
}

static void on_single_click(void *arg) {
    (void)arg;
    if (Menu_IsActive()) {
        Menu_ProcessClick();
    } else {
        App_ProcessClick();
    }
}

static void on_double_click(void *arg) {
    (void)arg;
    if (!Menu_IsActive()) {
        App_ProcessDoubleClick();
    }
}

static void on_long_press_start(void *arg) {
    (void)arg;
    if (Menu_IsActive()) {
        Menu_ProcessLongPress();
    } else {
        /* 主界面长按 3s → 进入 AUDIO 菜单 */
        Menu_Enter();
    }
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
#define RX_BUFFER_SIZE  256
uint8_t g_rxBufferDMA[RX_BUFFER_SIZE];
volatile uint16_t g_rxLength = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */



/* ================================================================ */
int main(void)
{
    /* MCU Init */
    HAL_Init();
    SystemClock_Config();

    /* Peripheral Init (CubeMX generated) */
    MX_GPIO_Init();
    MX_SPI2_Init();
    MX_TIM3_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_rxBufferDMA, RX_BUFFER_SIZE);

    /* Backlight PWM (50% duty) */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 800);

    HAL_Delay(500);          /* Wait for DL-04D boot */

    /* Init menu (backlight) */
    Menu_Init();

    /* Init encoder button (multi_button library) */
    button_init(&enc_btn, read_enc_key, 0, 0);
    button_attach(&enc_btn, SINGLE_CLICK,     on_single_click);
    button_attach(&enc_btn, DOUBLE_CLICK,     on_double_click);
    button_attach(&enc_btn, LONG_PRESS_START, on_long_press_start);
    button_start(&enc_btn);

    /* LCD Init + Show logo */
    LCD12232_Init();
    LCD12232_DispPic_122x32(gImage_2);

    HAL_Delay(3000);      /* Splash 3s */
    LCD12232_Clear();

    /* Encoder init + Enter Main Interface 1 */
    Encoder_Init();
    App_Init();

    /* Main loop */
    while (1)
    {
        if (!Menu_IsActive()) {
            App_Update();   /* 编辑闪烁计时 */
        }
        HAL_Delay(10);
    }
}


/**
  * @brief System Clock Configuration
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* Power config */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* Oscillator config */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* Clock config */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
                              | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART1 && Size > 0)
  {
    HAL_UART_Transmit(&huart1, g_rxBufferDMA, Size, 100);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_rxBufferDMA, RX_BUFFER_SIZE);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
}
#endif
