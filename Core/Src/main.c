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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tmc5160.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
UART_HandleTypeDef huart2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
static void DEBUG_UART_Init(void);
static void log_tmc_state(const char *tag);
static bool wait_for_position(uint32_t timeout_ms);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Debug UART on USART2 PA2 (TX) / PA3 (RX), 115200 8N1.
 * Kept out of the .ioc on purpose: CubeMX doesn't know about it, so it lives
 * entirely in USER CODE sections and survives regeneration. */
static void DEBUG_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

  /* Unbuffered stdout: newlib would otherwise malloc a 1 KiB stdio buffer,
   * which doesn't fit the 0x200 heap. */
  setvbuf(stdout, NULL, _IONBF, 0);
}

/* printf() lands here via _write() in syscalls.c */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}

static void log_tmc_state(const char *tag)
{
  uint32_t gstat = tmc5160_read(TMC5160_REG_GSTAT);
  uint32_t ramp  = tmc5160_read(TMC5160_REG_RAMP_STAT);
  int32_t  xact  = (int32_t)tmc5160_read(TMC5160_REG_XACTUAL);
  /* VACTUAL is a 24-bit signed field */
  int32_t  vact  = ((int32_t)(tmc5160_read(TMC5160_REG_VACTUAL) << 8)) >> 8;
  uint32_t drv   = tmc5160_read(TMC5160_REG_DRV_STATUS);

  printf("[%s] GSTAT=0x%lX RAMP_STAT=0x%03lX XACTUAL=%ld VACTUAL=%ld DRV_STATUS=0x%08lX\r\n",
         tag, gstat, ramp, (long)xact, (long)vact, drv);

  if (gstat & 0x1u)   printf("  ! GSTAT.reset: chip reset since last check (power/brown-out?)\r\n");
  if (gstat & 0x2u)   printf("  ! GSTAT.drv_err: driver shut down, see DRV_STATUS\r\n");
  if (gstat & 0x4u)   printf("  ! GSTAT.uv_cp: charge pump undervoltage - check VS supply\r\n");
  if (drv & (1u << 25)) printf("  ! DRV_STATUS.ot: overtemperature shutdown\r\n");
  else if (drv & (1u << 26)) printf("  ! DRV_STATUS.otpw: overtemperature warning\r\n");
  if (drv & (1u << 27)) printf("  ! DRV_STATUS.s2ga: short to GND, phase A\r\n");
  if (drv & (1u << 28)) printf("  ! DRV_STATUS.s2gb: short to GND, phase B\r\n");
  if (drv & (1u << 12)) printf("  ! DRV_STATUS.s2vsa: short to supply, phase A\r\n");
  if (drv & (1u << 13)) printf("  ! DRV_STATUS.s2vsb: short to supply, phase B\r\n");
  /* open-load flags are only meaningful while the motor is moving slowly */
  if (drv & (1u << 29)) printf("  ! DRV_STATUS.ola: open load, phase A (motor wire?)\r\n");
  if (drv & (1u << 30)) printf("  ! DRV_STATUS.olb: open load, phase B (motor wire?)\r\n");
}

static bool wait_for_position(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint32_t last_log = start;

  while (!tmc5160_position_reached())
  {
    uint32_t now = HAL_GetTick();
    if ((now - start) >= timeout_ms)
    {
      printf("TIMEOUT: position not reached after %lu ms\r\n", (unsigned long)timeout_ms);
      log_tmc_state("timeout");
      return false;
    }
    if ((now - last_log) >= 500u)
    {
      log_tmc_state("moving");
      last_log = now;
    }
    HAL_Delay(10);
  }
  return true;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  DEBUG_UART_Init();
  printf("\r\n=== stepper-ctl-newboard boot ===\r\n");

  /* Give the TMC5160 time to come out of power-on reset */
  HAL_Delay(10);

  uint32_t ioin = tmc5160_read(TMC5160_REG_IOIN);
  printf("IOIN=0x%08lX version=0x%02lX (expect 0x30)\r\n",
         ioin, (ioin >> 24) & 0xFFu);

  if (!tmc5160_init())
  {
    /* No answer on SPI: check driver supply (VS), wiring and CSN */
    printf("TMC5160 init FAILED: no/bad answer on SPI\r\n");
    if (ioin == 0x00000000u)
    {
      printf("  MISO stuck low: check VS supply, SDO wiring, CSN\r\n");
    }
    else if (ioin == 0xFFFFFFFFu)
    {
      printf("  MISO stuck high: check SDO wiring, CSN\r\n");
    }
    Error_Handler();
  }
  printf("TMC5160 init OK\r\n");

  /* Read back a config register to prove the MOSI/write path works too
   * (IOIN only proves we can read) */
  printf("CHOPCONF readback=0x%08lX (wrote 0x000100C3)\r\n",
         tmc5160_read(TMC5160_REG_CHOPCONF));
  log_tmc_state("after init");

  tmc5160_set_driver_enabled(true);
  printf("driver enabled (DRV_ENN low)\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Demo: sweep 5 revolutions forward, pause, then back to the start.
     * The TMC5160's ramp generator does all the motion; we just set
     * targets and poll until each move completes. */
    printf("\r\nmove to XTARGET=%ld (+5 rev)\r\n",
           (long)(5 * TMC5160_USTEPS_PER_REV));
    tmc5160_move_to(5 * TMC5160_USTEPS_PER_REV);
    if (wait_for_position(15000u))
    {
      printf("position reached\r\n");
    }
    HAL_Delay(500);

    printf("move to XTARGET=0\r\n");
    tmc5160_move_to(0);
    if (wait_for_position(15000u))
    {
      printf("position reached\r\n");
    }
    HAL_Delay(500);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TMC_CSN_GPIO_Port, TMC_CSN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TMC_DRV_ENN_GPIO_Port, TMC_DRV_ENN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : TMC_CSN_Pin */
  GPIO_InitStruct.Pin = TMC_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TMC_CSN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TMC_DRV_ENN_Pin */
  GPIO_InitStruct.Pin = TMC_DRV_ENN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TMC_DRV_ENN_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  /* HAL_UART_Transmit is a harmless no-op if the UART isn't up yet */
  printf("FATAL: Error_Handler - halting\r\n");
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
