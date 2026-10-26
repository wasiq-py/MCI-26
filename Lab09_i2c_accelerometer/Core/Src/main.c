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
#include <stdio.h>
#include <string.h>
#include <math.h>



/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Struct to store sensor data */
typedef struct {
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;

    float acc_x;
    float acc_y;
    float acc_z;

    float offset_x;
    float offset_y;
    float offset_z;

    float angle;
} LSM_Data;



/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* I2C addresses (from lab manual) */
#define LSM_WRITE_ADDR   0x32
#define LSM_READ_ADDR    0x33

/* Register addresses */
#define WHO_AM_I_A       0x0F
#define CTRL_REG1_A      0x20
#define CTRL_REG4_A      0x23
#define OUT_X_L_A        0x28
#define OUT_X_H_A        0x29
#define OUT_Y_L_A        0x2A
#define OUT_Y_H_A        0x2B
#define OUT_Z_L_A        0x2C
#define OUT_Z_H_A        0x2D

/* Constants */
#define ACC_SCALE        3.9      /* mg per count (LSM303AGR, normal mode) */
#define RAD_TO_DEG       57.2958
#define CALIB_SAMPLES    20



/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */
LSM_Data lsm;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USB_PCD_Init(void);
/* USER CODE BEGIN PFP */
uint8_t LSM_WhoAmI(void);
void    LSM_Init(void);
void    LSM_GetRaw(LSM_Data *data);
void    LSM_Calibrate(LSM_Data *data);
void    LSM_Read(LSM_Data *data);
void    LSM_Print(LSM_Data *data);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*
 * LSM_WhoAmI
 * Task 1: Reads the WHO_AM_I_A register (0x0F) of the accelerometer.
 * This register always holds the fixed ID 0x33, so reading it back
 * proves that I2C communication with the sensor is working.
 */
uint8_t LSM_WhoAmI(void)
{
    uint8_t id = 0;
    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, WHO_AM_I_A, 1, &id, 1, 100);
    return id;
}

/*
 * LSM_Init
 * Wakes up the accelerometer and sets its mode.
 * CTRL_REG1_A = 0x67 -> data rate 200 Hz, normal power, X/Y/Z axes enabled
 * CTRL_REG4_A = 0x00 -> normal mode, +/-2g range
 */
void LSM_Init(void)
{
    uint8_t value;

    value = 0x67;
    HAL_I2C_Mem_Write(&hi2c1, LSM_WRITE_ADDR, CTRL_REG1_A, 1, &value, 1, 100);

    value = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, LSM_WRITE_ADDR, CTRL_REG4_A, 1, &value, 1, 100);
}

/*
 * LSM_GetRaw
 * Reads the low and high byte of all three axes and joins them into
 * signed 16-bit numbers: raw = (high << 8) | low.
 * Used by both LSM_Calibrate and LSM_Read.
 */
void LSM_GetRaw(LSM_Data *data)
{
    uint8_t low, high;

    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_X_L_A, 1, &low, 1, 100);
    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_X_H_A, 1, &high, 1, 100);
    data->raw_x = (int16_t)((high << 8) | low);

    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_Y_L_A, 1, &low, 1, 100);
    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_Y_H_A, 1, &high, 1, 100);
    data->raw_y = (int16_t)((high << 8) | low);

    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_Z_L_A, 1, &low, 1, 100);
    HAL_I2C_Mem_Read(&hi2c1, LSM_READ_ADDR, OUT_Z_H_A, 1, &high, 1, 100);
    data->raw_z = (int16_t)((high << 8) | low);
}

/*
 * LSM_Calibrate
 * Called ONCE before the while loop, with the board flat and still.
 * Takes 20 readings, converts each to g, and averages them.
 * These averages are the sensor's offsets (its error at rest), which
 * LSM_Read subtracts from every later reading.
 *
 * Conversion to g: the 10-bit data sits in the top of the 16-bit value,
 * so shift right by 6, multiply by 3.9 mg per count, divide by 1000.
 */

void LSM_Calibrate(LSM_Data *data)
{
    float sum_x = 0, sum_y = 0, sum_z = 0;

    for (int i = 0; i < CALIB_SAMPLES; i++)
    {
        LSM_GetRaw(data);
        sum_x = sum_x + (data->raw_x >> 6) * ACC_SCALE / 1000.0;
        sum_y = sum_y + (data->raw_y >> 6) * ACC_SCALE / 1000.0;
        sum_z = sum_z + (data->raw_z >> 6) * ACC_SCALE / 1000.0;
        HAL_Delay(10);
    }

    data->offset_x = sum_x / CALIB_SAMPLES;
    data->offset_y = sum_y / CALIB_SAMPLES;
    data->offset_z = (sum_z / CALIB_SAMPLES) - 1.0;
  }

/*
 * LSM_Read
 * Called every loop. Reads the sensor, converts to g, subtracts the
 * calibration offsets, and calculates the tilt angle.
 *
 * Angle: atan2(acc_x, z) gives the tilt in radians, RAD_TO_DEG turns it
 * into degrees. z here is BEFORE offset removal, so it still contains
 * gravity (~1 g when flat), which the angle needs.
 */
void LSM_Read(LSM_Data *data)
{
    LSM_GetRaw(data);

    /* convert raw values to g */
    float x = (data->raw_x >> 6) * ACC_SCALE / 1000.0;
    float y = (data->raw_y >> 6) * ACC_SCALE / 1000.0;
    float z = (data->raw_z >> 6) * ACC_SCALE / 1000.0;

    /* remove calibration offsets */
    data->acc_x = x - data->offset_x;
    data->acc_y = y - data->offset_y;
    data->acc_z = z - data->offset_z;

    /* tilt angle in degrees */
    data->angle = atan2(data->acc_x, z) * RAD_TO_DEG;
}

/*
 * LSM_Print
 * Sends the processed values over UART as one labelled,
 * comma-separated line, e.g.  ax:0.004, ay:-0.008, az:0.002, angle:0.45
 */
void LSM_Print(LSM_Data *data)
{
    char msg[80];
    sprintf(msg, "ax:%.3f, ay:%.3f, az:%.3f, angle:%.2f\r\n",
            data->acc_x, data->acc_y, data->acc_z, data->angle);
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
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
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USB_PCD_Init();
  /* USER CODE BEGIN 2 */
  char msg[32];

  HAL_Delay(2000);                      /* time to open the terminal */

  /* Task 1: check WHO_AM_I */
  sprintf(msg, "WHO_AM_I = 0x%02X\r\n", LSM_WhoAmI());
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);

  /* Task 2: init and calibrate once */
  LSM_Init();
  HAL_Delay(200);
  LSM_Calibrate(&lsm);


  /* USER CODE END 2 */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    LSM_Read(&lsm);
    LSM_Print(&lsm);
    HAL_Delay(100);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_USART1
                              |RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.USBClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00201D2B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

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
  hspi1.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DRDY_Pin MEMS_INT3_Pin MEMS_INT4_Pin MEMS_INT1_Pin
                           MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = DRDY_Pin|MEMS_INT3_Pin|MEMS_INT4_Pin|MEMS_INT1_Pin
                          |MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : CS_I2C_SPI_Pin LD4_Pin LD3_Pin LD5_Pin
                           LD7_Pin LD9_Pin LD10_Pin LD8_Pin
                           LD6_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

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