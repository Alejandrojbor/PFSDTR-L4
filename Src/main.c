/**
  ******************************************************************************
  *  Name        : main.c
  *  Author      : Alejandro Borghero, Gabriel Eggly, Matías Micheletto
  *  Version     :
  *  Copyright   : GPLv3
  *  Description : Planificación Óptima de un Sistema Multiprocesador de Tiempo 
  *                Real con Restricciones de Precedencia, Comunicación y Energía
  ******************************************************************************
  * 
  *  Copyright (c) 2016 Alejandro Borghero <alejandro.borghero@uns.edu.ar>
  *
  *  This program is free software: you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation, either version 3 of the License.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
  * 
  ******************************************************************************
  *
  * TODO:
  * 			Verify the can TxMessage and RxMessage  to the handler (hcan)
  * 			Program Queues for tasks
  * 			Verificar si el TickHook se llama antes o despues del incremento del
  * 				TickCount y ajustar xTick
  * 			Verificar demoras de conmutación de frecuencia:
  * 				1- Demora en conmutación del core clock (puede usarse MCO para medirlo)
  * 				2- Demora en conmutación del timer4 (FreeRTOS tick)
  *
  *
  *
  ******************************************************************************
  *
  * Conexionado bus CAN en microUSB
  *
  * RX_CAN -> PA11 -> R67 -> Blanco
	* TX_CAN -> PA12 -> R68 -> Azul
	*
	* RX_CAN -> PA11 -> R67 -> Blanco
	* TX_CAN -> PA12 -> R68 -> Verde
  *
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define CPU_CNT             // Corregir error de compilacion cuando se define en main.h
/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;

LCD_HandleTypeDef hlcd;

QSPI_HandleTypeDef hqspi;

SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;

SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;

RCC_OscInitTypeDef RCC_OscInitStruct;

//uint32_t current_freq = 100;
uint8_t frq = 25;

//enum SysFreq{F_025, F_050, F_075, F_100};
//SysFreq current_freq = F_100;

const uint8_t periods[TASK_CNT] = {10,10,15,15,15};

// Tasks execution time
const uint8_t wcets[TASK_CNT] = {2,2,3,3,3};

// Precedences {Tx,Rx}
const uint8_t msgs[MSG_CNT][2] = { {1,2},{3,4},{3,5}};
// Queues
QueueHandle_t xQueues[MSG_CNT] = {NULL,};
				
// Jobs and power level asignation to each cpu (calculated with the optimizer)
const uint8_t job_cpu[TASK_CNT][6] = {{1,2,2}, {1,2,2}, {2,1,0}, {2,2,0},{1,1,0}};
				  
const uint8_t job_freq[TASK_CNT][6] = {{050,050,050},
                                       {025,050,100},
                                       {050,050,000},
                                       {100,100,000},
                                       {100,050,000},
																			};
				       
uint8_t job_cnt[TASK_CNT]; 			// Number of instances for each task

const uint8_t plan[ CPU_CNT ][HYPERPERIOD] = {{1,1,1,1,2,2,2,2,2,2,2,2,5,5,5,3,3,3,3,3,3,5,5,5,5,5,5,0,0,0},
																				 {3,3,3,3,3,3,4,4,4,0,1,1,1,1,2,2,2,2,0,0,1,1,1,1,2,2,4,4,4,0}};

const uint8_t plan_freq[ CPU_CNT ][HYPERPERIOD] = {{50,50,50,50,25,25,25,25,25,25,25,25,100,100,100,50,50,50,50,50,50,50,50,50,50,50,50,0,0,0},
		 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	{50,50,50,50,50,50,100,100,100,0,50,50,50,50,50,50,50,50,0,0,50,50,50,50,100,100,100,100,100,0}};

TaskHandle_t xHandles[TASK_CNT];


/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void Error_Handler(uint8_t);
void vUtilsEatCpu( UBaseType_t ticks );
void vApplicationTickHook(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_LCD_Init(void);
static void MX_QUADSPI_Init(void);
static void MX_SAI1_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CAN1_Init(void);

static void TEST_CAN( void* pvParams );
void TEST_GenericFuncV2( void* params);
void TEST_GenericFuncV3( void* params);
//static void Task_Body( void* pvParams );
//static void Task_Body1( void* pvParams );
//static void Task_Body2( void* pvParams );
//static void Task_Body3( void* pvParams );


/* Private function prototypes -----------------------------------------------*/
HAL_StatusTypeDef ALE_CAN_Receive_IT(CAN_HandleTypeDef *hcan, uint8_t FIFONumber);
HAL_StatusTypeDef CAN_Polling(void);


int main(void)
{
  /* MCU Configuration----------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  //MX_LCD_Init();
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);  // Put on MCO pin the: System clock selected
  MX_QUADSPI_Init();
  MX_SAI1_Init();
  MX_SPI2_Init();
  MX_USART2_UART_Init();
  MX_CAN1_Init();



  /* add mutexes, ... */

  /* add semaphores, ... */

  /* start timers, add new ones, ... */

  /* Thread creation -------------------------------------------------------------*/
  //Task definition example for vTaskSuspend and vTaskResume
  //  xTaskCreate( Task_Body, "TaskName", 128, NULL, configMAX_PRIORITIES-1, xHandles[TASK_CNT] );
for (uint8_t tasknum=1; tasknum <= TASK_CNT; ++tasknum)
	xTaskCreate( TEST_GenericFuncV3, NULL, 128, (void *)(&tasknum), configMAX_PRIORITIES-1, xHandles[tasknum-1] );
	//  xTaskCreate( Task_Body, NULL, 128, NULL, configMAX_PRIORITIES-1, NULL );
	//  xTaskCreate( Task_Body1, NULL, 128, NULL, configMAX_PRIORITIES-1, NULL );
	//  xTaskCreate( Task_Body2, NULL, 128, NULL, configMAX_PRIORITIES-1, NULL );
	//  xTaskCreate( Task_Body3, NULL, 128, NULL, configMAX_PRIORITIES-1, NULL );



  /* Queues creation -------------------------------------------------------------*/
for (uint8_t i=0; i<TASK_CNT; ++i)
{
	xQueues[i] = xQueueCreate( 4, sizeof( uint8_t ) );
	if(xQueues[i] == NULL)
	{
		Error_Handler(7);
	}
}


 
  /* Start scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */

  while (1)
  {
  }
}

/** System Clock Configuration
*/
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

    /**Initializes the CPU, AHB and APB busses clocks 
    */
          
// Configuration schemes
// +-------------+-----------------------------------------------+
// |   Power     |           Variables                           |
// +-------------+-----------------------------------------------+
// |    100%     |  RCC_OscInitStruct.PLL.PLLN = 20;             |
// |             |  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;  |
// +-------------+-----------------------------------------------+
// |     75%     |  RCC_OscInitStruct.PLL.PLLN = 30;             |
// |             |  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV4;  |
// +-------------+-----------------------------------------------+
// |     50%     |  RCC_OscInitStruct.PLL.PLLN = 20;             |
// |             |  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV4;  |
// +-------------+-----------------------------------------------+
// |     25%     |  RCC_OscInitStruct.PLL.PLLN = 20;             |
// |             |  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV8;  |
// +-------------+-----------------------------------------------+

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler(1);
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler(1);
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_USART2
                              |RCC_PERIPHCLK_SAI1|RCC_PERIPHCLK_I2C1
                              |RCC_PERIPHCLK_I2C2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
  PeriphClkInit.I2c2ClockSelection = RCC_I2C2CLKSOURCE_PCLK1;
  PeriphClkInit.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLLSAI1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 24;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_SAI1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler(1);
  }

    /**Configure the main internal regulator output voltage 
    */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler(1);
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 15, 0);
}

/* CAN1 init function */
static void MX_CAN1_Init(void)
{
  CAN_FilterConfTypeDef  sFilterConfig;
  static CanTxMsgTypeDef        TxMessage;
  static CanRxMsgTypeDef        RxMessage;
  
  // Configuration of interface
  hcan1.Instance = CAN1;
  hcan1.pTxMsg = &TxMessage;
  hcan1.pRxMsg = &RxMessage;
  hcan1.Init.TTCM = DISABLE;
  hcan1.Init.ABOM = DISABLE;
  hcan1.Init.AWUM = DISABLE;
  hcan1.Init.NART = DISABLE;
  hcan1.Init.RFLM = DISABLE;
  hcan1.Init.TXFP = DISABLE;
  
  // See RM0351 - pag. 1474
  //	BaudRate = 1 / NominalBitTime
  //	NominalBitTime = tq + tBS1 + tBS2

  //	tBS1 = tq x ( TS1[3:0] + 1 )
  //	tBS2 = tq x ( TS2[2:0] + 1 )
  //	tq = ( BRP[9:0] + 1 ) x tPCLK


  hcan1.Init.Mode = CAN_MODE_NORMAL;
//  hcan1.Init.Mode = CAN_MODE_LOOPBACK;		// Loopback mode, restore NORMAL mode at the end of the test


  //  Prescaler  =  PCLK1 / ( BaudRate * total_of_tq )
  //  total_of_tq = CAN_SJW + CAN_BS1 + CAN_BS2
  //  total of tq = ( PCKL1 / Prescaler ) / BaudRate

//  Values ranges ( RM0351 - pag. 1473 )
//  	  CAN_SJW = 1 a  4 TQ        --> Typically 1TQ
//  	  CAN_BS1 = 1 a 16 TQ
//  	  CAN_BS2 = 1 a  8 TQ
//  	  Prescaler = 1 a 1024

//  Sampling is typically used between 75% (ARINC 825) and 87.5% (CANopen)
//  Thus (( CAN_SJW + CAN_BS1 ) / ( CAN_SJW + CAN_BS1 + CAN_BS2 )) = 0.875 ( RM0351 - pag. 1474 )

//  total of tq = ( PCKL1 / Prescaler ) / BaudRate = ( 40,000,000 / 1,000,000 ) / Prescaler
  // 		 for Prescaler  = 2	=>	total_of_tq = 20
  // 		 for Prescaler  = 4	=>	total_of_tq = 10
  // 		 for Prescaler  = 5	=>	total_of_tq = 8
  // 		 for Prescaler  = 8	=>	total_of_tq = 5
  // 		 for Prescaler  = 10	=>	total_of_tq = 4

//  Using CAN_SJW = 1TQ , CAN_BS1 = 16TQ , CAN_BS2 = 4TQ result:
//  	  	  total_of_tq = CAN_SJW + CAN_BS1 + CAN_BS2 = 20
//  	  	  sampling = (( CAN_SJW + CAN_BS1 ) / ( CAN_SJW + CAN_BS1 + CAN_BS2 )) = 0.85
//	Prescaler = PCLK1 / ( BaudRate * total_of_tq ) = ( 40,000,000 / 20 ) / 1,000,000 = 2
//	Prescaler = PCLK1 / ( BaudRate * total_of_tq ) = ( 20,000,000 / 20 ) / 1,000,000 = 1

// Update
//  To vary the sysclk frequency without affect the Baudrate, we must change the prescaler of
//   PCLK1 to keep it at 10,000,000 for 25, 50 and 100% power schemes.

//   total_of_tq = PCLK1 / (  BaudRate * Prescaler )
//    for PCLK1 = 10,000,000  ;  Baudrate = 1,000,000  ; Prescaler = 1  yields  total_of_tq = 10
//
//  Using CAN_SJW = 1TQ , CAN_BS1 = 7TQ , CAN_BS2 = 2TQ result:
//  	  	  total_of_tq = CAN_SJW + CAN_BS1 + CAN_BS2 = 10
//  	  	  sampling = (( CAN_SJW + CAN_BS1 ) / ( CAN_SJW + CAN_BS1 + CAN_BS2 )) = 0.8

//  The same strategy can be used to 75% power scheme, but we need to change the PCLK1 frequency
//   to 15,000,000, which yields

//   total_of_tq = PCLK1 / (  BaudRate * Prescaler )
//    for PCLK1 = 15,000,000  ; Baudrate = 1,000,000 ; Prescaler = 1  yields  total_of_tq = 15
//
//  Using CAN_SJW = 1TQ , CAN_BS1 = 11TQ , CAN_BS2 = 3TQ result:
//  	  	  total_of_tq = CAN_SJW + CAN_BS1 + CAN_BS2 = 15
//  	  	  sampling = (( CAN_SJW + CAN_BS1 ) / ( CAN_SJW + CAN_BS1 + CAN_BS2 )) = 0.8

  hcan1.Init.SJW = CAN_SJW_1TQ;
  hcan1.Init.BS1 = CAN_BS1_16TQ;
  hcan1.Init.BS2 = CAN_BS2_3TQ;
  hcan1.Init.Prescaler = 1;

  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler(hcan1.State);
  }
//// Filter configuration
//	sFilterConfig.FilterNumber = 0;
//	sFilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
//	sFilterConfig.FilterScale = CAN_FILTERSCALE_16BIT;
//	sFilterConfig.FilterIdHigh = 0x0000;			// See RM0351 pag. 1631 - Filter bank scale and mode configuration
//	// Task number 1
//	sFilterConfig.FilterIdLow = 0x0000;
//	sFilterConfig.FilterMaskIdHigh = 0x0001;
//	sFilterConfig.FilterMaskIdLow = 0x0000;
//	sFilterConfig.FilterFIFOAssignment = 0;
//	sFilterConfig.FilterActivation = ENABLE;
//	sFilterConfig.BankNumber = 0;
//
//	if(HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
//	{
//		/* Filter configuration Error */
//		Error_Handler(1);
//	}
	// Filter configuration
  // There are 4 filters of 16 bits each in ID list mode per bank
	sFilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
	sFilterConfig.FilterScale = CAN_FILTERSCALE_16BIT;
	sFilterConfig.FilterMaskIdHigh = 0x0000;
	sFilterConfig.FilterMaskIdLow = 0x0000;
	sFilterConfig.FilterFIFOAssignment = 0;
	sFilterConfig.BankNumber = 0;
	sFilterConfig.FilterActivation = ENABLE;

	for (uint8_t i=0; i<TASK_CNT; ++i)
	{
		//Bank Number (increases 1 in 4)
		if(i)	sFilterConfig.BankNumber += i%4?0:1;
		// Filter Number (from 0 to 3 for every bank)
		sFilterConfig.FilterNumber = i%4;
		// The Identifier contains CPU Number and task number i-1 (Starting task from 1)
		// See RM0351 pag. 1631 - Filter bank scale and mode configuration
		//
		//	    Filter ID High	   |     Filter ID Low
		//  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
		// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		// | CPU |        Task Number       | 0| 0|   000  |
		// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		sFilterConfig.FilterIdHigh = ((i+1) >> 2) & (THIS_CPU<<6);
		sFilterConfig.FilterIdLow = (i+1) << 6;

		if(HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
		{
			/* Filter configuration Error */
			Error_Handler(1);
		}
	}


// Configure transmission process
	hcan1.pTxMsg->StdId = 0x11;
	hcan1.pTxMsg->ExtId = 0x0;
	hcan1.pTxMsg->RTR = CAN_RTR_DATA;
	hcan1.pTxMsg->IDE = CAN_ID_STD;
	hcan1.pTxMsg->DLC = 1;

}

/* I2C1 init function */
static void MX_I2C1_Init(void)
{

  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00404C74;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler(1);
  }

    /**Configure Analogue filter 
    */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* I2C2 init function */
static void MX_I2C2_Init(void)
{

  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00404C74;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler(1);
  }

    /**Configure Analogue filter 
    */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* LCD init function */
static void MX_LCD_Init(void)
{

  hlcd.Instance = LCD;
  hlcd.Init.Prescaler = LCD_PRESCALER_1;
  hlcd.Init.Divider = LCD_DIVIDER_16;
  hlcd.Init.Duty = LCD_DUTY_1_4;
  hlcd.Init.Bias = LCD_BIAS_1_4;
  hlcd.Init.VoltageSource = LCD_VOLTAGESOURCE_INTERNAL;
  hlcd.Init.Contrast = LCD_CONTRASTLEVEL_0;
  hlcd.Init.DeadTime = LCD_DEADTIME_0;
  hlcd.Init.PulseOnDuration = LCD_PULSEONDURATION_0;
  hlcd.Init.MuxSegment = LCD_MUXSEGMENT_DISABLE;
  hlcd.Init.BlinkMode = LCD_BLINKMODE_OFF;
  hlcd.Init.BlinkFrequency = LCD_BLINKFREQUENCY_DIV8;
  hlcd.Init.HighDrive = LCD_HIGHDRIVE_DISABLE;
  if (HAL_LCD_Init(&hlcd) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* QUADSPI init function */
static void MX_QUADSPI_Init(void)
{

  hqspi.Instance = QUADSPI;
  hqspi.Init.ClockPrescaler = 255;
  hqspi.Init.FifoThreshold = 1;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_NONE;
  hqspi.Init.FlashSize = 1;
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* SAI1 init function */
static void MX_SAI1_Init(void)
{

  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA1.Init.DataSize = SAI_DATASIZE_24;
  hsai_BlockA1.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockA1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_192K;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai_BlockA1.FrameInit.FrameLength = 8;
  hsai_BlockA1.FrameInit.ActiveFrameLength = 1;
  hsai_BlockA1.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai_BlockA1.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockA1.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  hsai_BlockA1.SlotInit.FirstBitOffset = 0;
  hsai_BlockA1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockA1.SlotInit.SlotNumber = 1;
  hsai_BlockA1.SlotInit.SlotActive = 0x00000000;
  if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK)
  {
    Error_Handler(1);
  }

  hsai_BlockB1.Instance = SAI1_Block_B;
  hsai_BlockB1.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockB1.Init.AudioMode = SAI_MODESLAVE_RX;
  hsai_BlockB1.Init.DataSize = SAI_DATASIZE_24;
  hsai_BlockB1.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockB1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockB1.Init.Synchro = SAI_SYNCHRONOUS;
  hsai_BlockB1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockB1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockB1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockB1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockB1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockB1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai_BlockB1.FrameInit.FrameLength = 8;
  hsai_BlockB1.FrameInit.ActiveFrameLength = 1;
  hsai_BlockB1.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai_BlockB1.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockB1.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  hsai_BlockB1.SlotInit.FirstBitOffset = 0;
  hsai_BlockB1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockB1.SlotInit.SlotNumber = 1;
  hsai_BlockB1.SlotInit.SlotActive = 0x00000000;
  if (HAL_SAI_Init(&hsai_BlockB1) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* SPI2 init function */
static void MX_SPI2_Init(void)
{

  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/* USART2 init function */
static void MX_USART2_UART_Init(void)
{

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_7B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler(1);
  }

}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, AUDIO_RST_Pin|LD_G_Pin|XL_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD_R_Pin|M3V3_REG_ON_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GYRO_CS_GPIO_Port, GYRO_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : AUDIO_RST_Pin */
  GPIO_InitStruct.Pin = AUDIO_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(AUDIO_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MFX_IRQ_OUT_Pin */
  GPIO_InitStruct.Pin = MFX_IRQ_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MFX_IRQ_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC0 MAG_INT_Pin MAG_DRDY_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_0|MAG_INT_Pin|MAG_DRDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : JOY_CENTER_Pin JOY_LEFT_Pin JOY_RIGHT_Pin JOY_UP_Pin 
                           JOY_DOWN_Pin */
  GPIO_InitStruct.Pin = JOY_CENTER_Pin|JOY_LEFT_Pin|JOY_RIGHT_Pin|JOY_UP_Pin 
                          |JOY_DOWN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : MFX_WAKEUP_Pin */
  GPIO_InitStruct.Pin = MFX_WAKEUP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MFX_WAKEUP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD_R_Pin */
  GPIO_InitStruct.Pin = LD_R_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LD_R_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD_G_Pin */
  GPIO_InitStruct.Pin = LD_G_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LD_G_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : EXT_RST_Pin GYRO_INT1_Pin */
  GPIO_InitStruct.Pin = EXT_RST_Pin|GYRO_INT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : GYRO_CS_Pin */
  GPIO_InitStruct.Pin = GYRO_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GYRO_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : M3V3_REG_ON_Pin */
  GPIO_InitStruct.Pin = M3V3_REG_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(M3V3_REG_ON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GYRO_INT2_Pin */
  GPIO_InitStruct.Pin = GYRO_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GYRO_INT2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : XL_CS_Pin */
  GPIO_InitStruct.Pin = XL_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(XL_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : XL_INT_Pin */
  GPIO_InitStruct.Pin = XL_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(XL_INT_GPIO_Port, &GPIO_InitStruct);

}

/**
  * @brief  Transmits and Receives a correct CAN frame by polling
  * @param  None
  * @retval HAL status
  */
HAL_StatusTypeDef CAN_Polling(void)
{


// Transmission start
  hcan1.pTxMsg->Data[0] = 0xCA;
  hcan1.pTxMsg->Data[1] = 0xFE;

  if(HAL_CAN_Transmit(&hcan1, 10) != HAL_OK)
  {
    /* Transmission Error */
    Error_Handler(1);
  }

  if(HAL_CAN_GetState(&hcan1) != HAL_CAN_STATE_READY)
  {
    return HAL_ERROR;
  }

// Reception start
  if(HAL_CAN_Receive(&hcan1, CAN_FIFO0,10) != HAL_OK)
  {
    /* Reception Error */
    Error_Handler(1);
  }

  if(HAL_CAN_GetState(&hcan1) != HAL_CAN_STATE_READY)
  {
    return HAL_ERROR;
  }

  if (hcan1.pRxMsg->StdId != 0x11)
  {
    return HAL_ERROR;
  }

  if (hcan1.pRxMsg->IDE != CAN_ID_STD)
  {
    return HAL_ERROR;
  }

  if (hcan1.pRxMsg->DLC != 2)
  {
    return HAL_ERROR;
  }

  if ((hcan1.pRxMsg->Data[0]<<8|hcan1.pRxMsg->Data[1]) != 0xCAFE)
  {
    return HAL_ERROR;
  }

  return HAL_OK; /* Test Passed */


}

/**
  * @brief  CAN Rx complete callback
  * @param  hcan:       Pointer to a CAN_HandleTypeDef structure that contains
  *         the configuration information for the specified CAN.
  * @retval None
  */
void HAL_CAN_RxCpltCallback(CAN_HandleTypeDef* hcan)
{
	BaseType_t xHigherPriorityTaskWoken;

//  if ((hcan->pRxMsg->StdId == 0x11)&&(hcan->pRxMsg->IDE == CAN_ID_STD) && (hcan->pRxMsg->DLC == 2))
//  {
////  	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_RESET);
//    switch(hcan->pRxMsg->Data[0])
//    {
//    /* Shutdown leds */
//    case 0:
//    	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_RESET);
//    	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_SET);
//    	break;
//    case 1:
//    	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_SET);
//    	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_SET);
//    	break;
//    case 2:
//    	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_SET);
//    	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_RESET);
//    	break;
//    case 3:
//    	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_RESET);
//    	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_RESET);
//    	break;
//    case 4:
//    	break;
//    default:
//    	break;
//    }
//  }

//  while(HAL_CAN_Receive_IT(hcan, CAN_FIFO0)  != HAL_OK)
//  	{
//  	HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN);
// 		HAL_Delay(50);
//
//  	}

	/* We have not woken a task at the start of the ISR. */
	xHigherPriorityTaskWoken = pdFALSE;
  // Sends the received message from CAN to the corresponding queue
	xQueueSendFromISR( xQueues[hcan->pRxMsg->FMI], &hcan->pRxMsg->Data[0], &xHigherPriorityTaskWoken );

// Rearm the receive interrupt
  if(ALE_CAN_Receive_IT(hcan, CAN_FIFO0) != HAL_OK)
  {
    /* Reception Error */
    Error_Handler(6);
  }

  if( xHigherPriorityTaskWoken )
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
  * @brief  Update SystemCoreClock variable according to Clock Register Values.
  *         The SystemCoreClock variable contains the core clock (HCLK), it can
  *         be used by the user application to setup the SysTick timer or configure
  *         other parameters. Modified to run in the Tick Hook of FreeRTOS.
  *
  * @note   Each time the core clock (HCLK) changes, this function must be called
  *         to update SystemCoreClock variable value. Otherwise, any configuration
  *         based on this variable will be incorrect.
  *
  * @note   - The system frequency computed by this function is not the real
  *           frequency in the chip. It is calculated based on the predefined
  *           constant and the selected clock source:
  *
  *           - If SYSCLK source is MSI, SystemCoreClock will contain the MSI_VALUE(*)
  *
  *           - If SYSCLK source is HSI, SystemCoreClock will contain the HSI_VALUE(**)
  *
  *           - If SYSCLK source is HSE, SystemCoreClock will contain the HSE_VALUE(***)
  *
  *           - If SYSCLK source is PLL, SystemCoreClock will contain the HSE_VALUE(***)
  *             or HSI_VALUE(*) or MSI_VALUE(*) multiplied/divided by the PLL factors.
  *
  *         (*) MSI_VALUE is a constant defined in stm32l4xx_hal.h file (default value
  *             4 MHz) but the real value may vary depending on the variations
  *             in voltage and temperature.
  *
  *         (**) HSI_VALUE is a constant defined in stm32l4xx_hal.h file (default value
  *              16 MHz) but the real value may vary depending on the variations
  *              in voltage and temperature.
  *
  *         (***) HSE_VALUE is a constant defined in stm32l4xx_hal.h file (default value
  *              8 MHz), user has to ensure that HSE_VALUE is same as the real
  *              frequency of the crystal used. Otherwise, this function may
  *              have wrong result.
  *
  *         - The result of this function could be not correct when using fractional
  *           value for HSE crystal.
  *
  * @param  None
  * @retval None
  */
inline void ALE_SystemCoreClockUpdate(void)
{
  uint32_t tmp = 0, msirange = 0, pllvco = 0, pllr = 2, pllsource = 0, pllm = 2;

  /* Get MSI Range frequency--------------------------------------------------*/
  if((RCC->CR & RCC_CR_MSIRGSEL) == RESET)
  { /* MSISRANGE from RCC_CSR applies */
    msirange = (RCC->CSR & RCC_CSR_MSISRANGE) >> 8;
  }
  else
  { /* MSIRANGE from RCC_CR applies */
    msirange = (RCC->CR & RCC_CR_MSIRANGE) >> 4;
  }
  /*MSI frequency range in HZ*/
  msirange = MSIRangeTable[msirange];

  /* Get SYSCLK source -------------------------------------------------------*/
  switch (RCC->CFGR & RCC_CFGR_SWS)
  {
    case 0x00:  /* MSI used as system clock source */
      SystemCoreClock = msirange;
      break;

    case 0x04:  /* HSI used as system clock source */
      SystemCoreClock = HSI_VALUE;
      break;

    case 0x08:  /* HSE used as system clock source */
      SystemCoreClock = HSE_VALUE;
      break;

    case 0x0C:  /* PLL used as system clock  source */
      /* PLL_VCO = (HSE_VALUE or HSI_VALUE or MSI_VALUE/ PLLM) * PLLN
         SYSCLK = PLL_VCO / PLLR
         */
      pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC);
      pllm = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLM) >> 4) + 1 ;

      switch (pllsource)
      {
        case 0x02:  /* HSI used as PLL clock source */
          pllvco = (HSI_VALUE / pllm);
          break;

        case 0x03:  /* HSE used as PLL clock source */
          pllvco = (HSE_VALUE / pllm);
          break;

        default:    /* MSI used as PLL clock source */
          pllvco = (msirange / pllm);
          break;
      }
      pllvco = pllvco * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 8);
      pllr = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLR) >> 25) + 1) * 2;
      SystemCoreClock = pllvco/pllr;
      break;

    default:
      SystemCoreClock = msirange;
      break;
  }
  /* Compute HCLK clock frequency --------------------------------------------*/
  /* Get HCLK prescaler */
  tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4)];
  /* HCLK clock frequency */
  SystemCoreClock >>= tmp;
}

/**
  * @brief  Receives a correct CAN frame. Modified to unlock the can interface.
  * @param  hcan:       Pointer to a CAN_HandleTypeDef structure that contains
  *         the configuration information for the specified CAN.
  * @param  FIFONumber: Specify the FIFO number
  * @retval HAL status
  */
HAL_StatusTypeDef ALE_CAN_Receive_IT(CAN_HandleTypeDef* hcan, uint8_t FIFONumber)
{

  /* Check the parameters */
  assert_param(IS_CAN_FIFO(FIFONumber));


	if(hcan->State == HAL_CAN_STATE_BUSY_TX)
	{
		/* Change CAN state */
		hcan->State = HAL_CAN_STATE_BUSY_TX_RX;
	}
	else
	{
		/* Change CAN state */
		hcan->State = HAL_CAN_STATE_BUSY_RX;

		/* Set CAN error code to none */
		hcan->ErrorCode = HAL_CAN_ERROR_NONE;

		/* Enable Error warning Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_EWG);

		/* Enable Error passive Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_EPV);

		/* Enable Bus-off Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_BOF);

		/* Enable Last error code Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_LEC);

		/* Enable Error Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_ERR);
	}

	if(FIFONumber == CAN_FIFO0)
	{
		/* Enable FIFO 0 message pending Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_FMP0);
	}
	else
	{
		/* Enable FIFO 1 message pending Interrupt */
		__HAL_CAN_ENABLE_IT(hcan, CAN_IT_FMP1);
	}

  /* Return function status */
  return HAL_OK;
}


static void TEST_CAN( void* pvParams )
{
	/* A pointer to the subject task's name, which is a standard NULL terminated
	 * C string. */
	char *pcTaskName = pcTaskGetTaskName( NULL );

	portTickType xLastWakeTime=(portTickType)(0);

	uint8_t idx=0;

// Initiates the interrupt reception process
//  if(HAL_CAN_Receive_IT(&hcan1, CAN_FIFO0) != HAL_OK)
//  {
//    /* Reception Error */
//    Error_Handler(1);
//  }
 ALE_CAN_Receive_IT(&hcan1, CAN_FIFO0);

	for (;;)
	{
//		sprintf( "%s -- %d\n\r", pcTaskName, xTaskGetTickCount() );

		// The led flashes to show that the systems is working
//		HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN);

/*
// 		Transmission and reception in Loopback mode (Polling)
		if ( CAN_Polling() == HAL_ERROR )
				HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_SET);
		else
			HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_SET);
*/

		if(HAL_GPIO_ReadPin(JOY_RIGHT_GPIO_Port, JOY_RIGHT_Pin) == BUTTON_PRESSED)
		{
			idx%=3;
			// Set the data to be transmitted
			hcan1.pTxMsg->Data[0] = idx++;
			hcan1.pTxMsg->Data[1] = 0xAD;

//			HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_SET);
//			HAL_CAN_Transmit(&hcan1, 10);

			// Transmits the data
			if(HAL_CAN_Transmit(&hcan1, 10) != HAL_OK)
			{
				/* Transmition Error */
				Error_Handler(5);
			}
			HAL_Delay(10);
		}
		vTaskDelayUntil(&xLastWakeTime,100);
		//vTaskDelay(1000);
	}

	/* If the tasks ever leaves the for cycle, kill it. */
	vTaskDelete( NULL );
}

static void TEST_LedSwitch1( void* pvParams )
{
	/* A pointer to the subject task's name, which is a standard NULL terminated
	 * C string. */
	portTickType xLastWakeTime = xTaskGetTickCount();
	const portTickType taskPeriod = (portTickType) 110;

	for (;;)
	{
//		sprintf( "%s -- %d\n\r", pcTaskName, xTaskGetTickCount() );
		//HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN);
		LED_GREEN_ON();
		LED_RED_OFF();

		vTaskDelayUntil(&xLastWakeTime, taskPeriod);
		//vTaskDelay(1000);
	}

	/* If the tasks ever leaves the for cycle, kill it. */
	vTaskDelete( NULL );
}

static void TEST_LedSwitch2( void* pvParams )
{
	/* A pointer to the subject task's name, which is a standard NULL terminated
	 * C string. */
	portTickType xLastWakeTime = xTaskGetTickCount();
	const portTickType taskPeriod = (portTickType) 120;

	for (;;)
	{
//		sprintf( "%s -- %d\n\r", pcTaskName, xTaskGetTickCount() );
		//HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
		LED_GREEN_OFF();
		LED_RED_ON();

		vTaskDelayUntil(&xLastWakeTime, taskPeriod);
		//vTaskDelay(1000);
	}

	/* If the tasks ever leaves the for cycle, kill it. */
	vTaskDelete( NULL );
}


static void TEST_FreqSwitch( void* pvParams )
{
	/* A pointer to the subject task's name, which is a standard NULL terminated
	 * C string. */
	portTickType xLastWakeTime = xTaskGetTickCount();

	for (;;)
	{
		LED_GREEN_ON();
		//HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN);
		//HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED);
		LED_RED_ON();
		//sprintf( "%s -- %d\n\r", pcTaskName, xTaskGetTickCount() );
		vTaskDelayUntil(&xLastWakeTime,450);
		frq = frq == 25? 75 : 25;
	}

	/* If the tasks ever leaves the for cycle, kill it. */
	vTaskDelete( NULL );
}


void TEST_GenericFuncV3( void* params)
// Generic task (The task number is passed as "params" argument )
{

  // Actual task number
  uint8_t this_task = *((uint8_t*) params);
  uint8_t cpu_dest = 0;
  uint8_t instance=0;
  // Instance counter. It begins in -1 because the initial increment
//  int this_job = -1;
  uint8_t usDataSend=5, usDataReceive, i;

  for(;;)
  {
  	// TEST - Keep the green LED on while task 1 is running in cpu 1 (task 3 for cpu 2)
  	if( (THIS_CPU == 1 && this_task == 1) || (THIS_CPU == 2 && this_task == 3) )
  		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_RESET);
//  	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_RESET);

  	// Wait for TickHook unlock the task with a notification ( Offline Scheduling)
  	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  	// TEST - Keep the green LED on while task 1 is running in cpu 1 (task 3 for cpu 2)
  	if( (THIS_CPU == 1 && this_task == 1) || (THIS_CPU == 2 && this_task == 3) )
  		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN, GPIO_PIN_SET);
//  	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED, GPIO_PIN_SET);

  	// Receive messages from all precedent tasks
  	for ( i=0; i<MSG_CNT; ++i )
  	  		if(msgs[i][1] == this_task)
  	  			xQueueReceive(xQueues[this_task-1], &usDataReceive, portMAX_DELAY);

    // Simulates the work done by the the task
    vUtilsEatCpu( (TickType_t) ((double) wcets[this_task]) );

  	// Send messages to all successor tasks
  	for ( i=0; i<MSG_CNT; ++i )
  		// Check if this task sends messages
  		if(msgs[i][0] == this_task)
  		{
  			// OJO Verificar número de instancia
  			instance = (uint8_t)(xTaskGetTickCount()%HYPERPERIOD)/periods[this_task-1];
  			cpu_dest= job_cpu[this_task-1][instance];
  			// Check if the successor task is in this cpu
  			if( cpu_dest == THIS_CPU )
  				xQueueSend(xQueues[msgs[i][1]-1], &usDataSend, portMAX_DELAY);
  			else
  			{
  				hcan1.pTxMsg->StdId = (cpu_dest<<9)	| msgs[i][1];
  				hcan1.pTxMsg->Data[0]= usDataSend;
  				 if(HAL_CAN_Transmit_IT(&hcan1) != HAL_OK)
  				  {
  				    /* Transmission Error */
  				    Error_Handler(1);
  				  }
  			}
  		}

  }

  vTaskDelete( NULL );
}


void TEST_GenericFuncV2( void* params)
// Generic task (The task number is passed as "params" argument )
{

  // Actual task number
  uint8_t this_task = *((uint8_t*) params);
  // Instance counter. It begins in -1 because the initial incremnte
  int this_job = -1;

  for(;;)
  {
  	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Simulate the work doing by the the task
    vUtilsEatCpu( (TickType_t) ((double) wcets[this_task]) );


  }

  vTaskDelete( NULL );
}


void TEST_GenericFunc( void* params)
// Tarea generica (el numero de tarea se pasa como argumento)
{
  TickType_t xPreviousWakeTime = xTaskGetTickCount();

  uint8_t this_task = *((uint8_t*) params); // Numero de la tarea actual
  int this_job = -1; // Contador de instancias. Inicializa en -1 porque se incrementa al principio

  for(;;)
  {
    // Incrementar numero de instancia (sin sobrepasar el numero maximo de instancias para esta tarea)
    this_job = (this_job == job_cnt[this_task]-1) ? 0 : this_job + 1;

    // Si esta instancia esta asignada a este cpu, ejecutar, si no, saltear
    if( job_cpu[this_task][this_job] == THIS_CPU )
    {
      // Configurar frecuencia de esta instancia de acuerdo a lo calculado
      //vSetSystemFreq( job_freq[this_task][this_job] ); // La funcion verifica si es la misma frecuecia que la actual

      // Comer cpu (simula el tiempo consumido por esta tarea), multiplicando el tiempo de ejecucion
      // dependiendo de la frecuencia de operacion del sistema
      vUtilsEatCpu( (TickType_t) ((double) wcets[this_task]) );
    }

    vTaskDelayUntil( &xPreviousWakeTime, (TickType_t) periods[this_task] );
  }

  vTaskDelete( NULL );
}


void vUtilsEatCpu( UBaseType_t ticks )
{
	BaseType_t xI;

    //BaseType_t xLim = ( ticks * ONE_TICK ) / 5;
	BaseType_t xLim = ( ticks * ONE_TICK ); // 100;

	for( xI = 0; xI < xLim; xI++ )
	{
		asm("nop");
	}
}


void vApplicationTickHook(void)
{
	/* Configure the main PLL clock source, multiplication and division factors. */

	TickType_t xTick;
	uint32_t PLLRDiv = 0;
  uint32_t AHBDiv = RCC_SYSCLK_DIV4;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

/* There are two possibilities for task suspend.
 * The first one is suspend the task suspends itself:
 * vTaskSuspend( NULL );
 * The other one is another task (i.e. TickHook) suspends the task as:
 * vTaskSuspend( xHandle );
 */
	xTick = xTaskGetTickCountFromISR() % HYPERPERIOD;
	// Verificar si el TickHook se llama antes o despues del incremento del TickCount y ajustar xTick
	// (En teoria xTickCount se incrementa previo al al llamado de TickHook, verificar con hardware
//	Para igualar en todos los uC las demoras introducidas por los cambios de frecuencia se solicitará el cambio de
//	 la misma, incluso si en la siguiente ranura se usa la misma frecuencia.
//	if ( plan[THIS_CPU][xTick] != plan[THIS_CPU][ (xTick+1) % HYPERPERIOD ] )
//	{
//		if ( plan_freq[THIS_CPU][xTick] != plan_freq[THIS_CPU][ (xTick+1) % HYPERPERIOD ] )
//		{
			switch( plan_freq[THIS_CPU-1][(xTick) % HYPERPERIOD] )
			{
			case 25:						// 10MHz
				PLLRDiv = 3;
				AHBDiv = RCC_SYSCLK_DIV1;
				break;
			case 50:						// 20MHz
				PLLRDiv = 1;
				AHBDiv = RCC_SYSCLK_DIV2;
				break;
			case 100:						// 40MHz
				PLLRDiv = 0;
				AHBDiv = RCC_SYSCLK_DIV4;
				break;
			default: 						// 40MHz
				PLLRDiv = 0;
				AHBDiv = RCC_SYSCLK_DIV1;		// Free slots run at 25 MHz
			}

			/* Enable HSI clock */
			RCC->CR |= RCC_CR_HSION;

			/* Wait till HSI is ready */
			while (!(RCC->CR & RCC_CR_HSIRDY));

			/* Select HSI clock as main clock */
			RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW)) | RCC_CFGR_SW_HSI;

			/* Disable PLL */
			RCC->CR &= ~RCC_CR_PLLON;

			/* Set PLL settings */
			RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLR_MASK) | ((PLLRDiv << RCC_PLLR_POS) & RCC_PLLR_MASK);
			RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE) |  AHBDiv;

			/* Enable PLL */
			RCC->CR |= RCC_CR_PLLON;
			RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN;

			/* Wait till PLL is ready */
			while (!(RCC->CR & RCC_CR_PLLRDY));

			/* Enable PLL as main clock */
			RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW)) | RCC_CFGR_SW_PLL;

			/* Disable HSI clock */
			RCC->CR &= ~RCC_CR_HSION;

			/* Update system core clock variable */
			ALE_SystemCoreClockUpdate();
//		}
		/* Notify the task that the transmission is complete. */
		vTaskNotifyGiveFromISR( xHandles[ (uint8_t)(plan[xTick %HYPERPERIOD][THIS_CPU-1]) -1 ], &xHigherPriorityTaskWoken );

		/* If xHigherPriorityTaskWoken is now set to pdTRUE then a context switch
		   should be performed to ensure the interrupt returns directly to the highest
		   priority task.  The macro used for this purpose is dependent on the port in
		   use and may be called portEND_SWITCHING_ISR(). */
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
//	}
}


/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM4) {
    HAL_IncTick();
  }
}

/**
  * @brief  This function is executed in case of error occurrence. The red led
  * will flash nerror times.
  * @param  nerror : Number of error.
  * @retval None
  */
void Error_Handler(uint8_t nerror)
{
	uint8_t i;
	LED_RED_OFF();
  while(1) 
  {
		for(i=0; i < nerror; ++i)
		{
			LED_RED_TOGGLE();
			HAL_Delay(500);
			LED_RED_TOGGLE();
			HAL_Delay(500);
		}
		HAL_Delay(1500);
  }

}

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

}

#endif

/**
  * @}
  */ 

/**
  * @}
*/ 

/******************* Copyright (c) 2016 Alejandro Borghero *****END OF FILE****/
