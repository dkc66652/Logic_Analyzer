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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MPU_Config(void);

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * 应用入口与任务实现已经迁移至 Code/1-App/main/app_main.c。
 * 保留这一段禁用代码仅作为本次 CubeMX 迁移的可追溯参考；后续确认后可删除。
 */
#if 0

/**
 * @brief 将波形区内的横向 swipe 转为窗口移动请求。
 *
 * 拖动期间传入相邻触摸点的增量位移；这里不执行绘制。wave_process_display()
 * 会在 LTDC 帧中断释放后缓冲后，提交最后一个目标窗口。
 */
static void wave_gesture_cb(lv_port_gesture_t gesture,
                            const lv_port_gesture_data_t * data,
                            void * user_data)
{
  const int32_t wave_right = WAVE_AREA_X + WAVE_AREA_WIDTH;

  (void)user_data;

  if(data == NULL ||
     data->start.x < WAVE_AREA_X || data->start.x >= wave_right ||
     data->end.x < WAVE_AREA_X || data->end.x >= wave_right) {
    return;
  }

  if(gesture == LV_PORT_GESTURE_SWIPE_LEFT ||
     gesture == LV_PORT_GESTURE_SWIPE_RIGHT ||
     gesture == LV_PORT_GESTURE_DRAG_MOVE) {
    wave_pan_view_pixels(data->end.x - data->start.x);
  }
  else if(gesture == LV_PORT_GESTURE_PINCH_OUT ||
          gesture == LV_PORT_GESTURE_PINCH_IN ||
          gesture == LV_PORT_GESTURE_PINCH_MOVE) {
    /* end 是当前两指中心；缩放必须围绕这一时刻的实际中心点。 */
    const int32_t anchor_x = data->end.x;
    const bool zoom_in = (gesture == LV_PORT_GESTURE_PINCH_OUT) ||
                         ((gesture == LV_PORT_GESTURE_PINCH_MOVE) &&
                          (data->distance_delta > 0));

    wave_zoom_view_at(anchor_x, zoom_in);
  }
}

/*
 * 唯一允许调用 LVGL API 的任务。
 *
 * 启动顺序：初始化 LVGL 和显示/触摸端口 -> 启动 TIM7 的 1 ms LVGL tick
 * -> 初始化 wave 配置 -> 创建右侧页面导航 -> 根据已启用通道创建 0~599。
 * side_panel_create() 会自动选择“设备配置”页并创建采集按钮。采集运行时，
 * 每一帧绘制完成后读取实际 tick 差，再按经过时间准备下一帧。
 */
static void lvgl_task(void *param)
{
  uint32_t delay_ms;

  (void)param;

  /* 1. 初始化 LVGL 核心，并注册 LTDC 显示刷新回调。 */
  lv_init();
  lv_port_disp_init();

  /* 显示端口必须先成功注册默认 display，后续所有界面都依附在它上面。 */
  if (lv_display_get_default() == NULL)
  {
    Error_Handler();
  }

  /* 触摸第一阶段：只初始化 GT9xxx，暂不向 LVGL 注册输入设备。 */
  lv_port_indev_init();

  /* 2. TIM7 每 1 ms 在中断中调用 lv_tick_inc(1)，提供 LVGL 的时间基准。 */
  if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
  {
    Error_Handler();
  }

  /* 3. 先建立通道配置，再创建右侧页面，最后一次性创建 0~599 区域。 */
  if (!wave_init(lv_screen_active()))
  {
    Error_Handler();
  }
  side_panel_create(lv_screen_active());
  if (!wave_apply_configuration())
  {
    Error_Handler();
  }
  lv_port_indev_set_gesture_callback(wave_gesture_cb, NULL);
  frame_test_cycle_counter_init();
  (void)log_printf("lvgl task started\r\n");

  while (1)
  {
    bool wave_frame_ready;

    /* 4. 先完成上一轮已经失效的对象、触摸事件和 LVGL 内部定时器。 */
    {
      delay_ms = lv_timer_handler();

      /* 删除通道、暂停等停止状态操作也可能等待 Layer 1 的下一次 VSYNC。 */
      wave_process_display();

      /*
       * lv_timer_handler() 返回表示上一轮绘制和 flush 已完成。
       * 此时读取真实 tick 差，换算成 CNT 并准备下一帧；lv_refr_now()
       * 立即消费这次失效，不受 LV_DEF_REFR_PERIOD=16 ms 的上限控制。
       */
      wave_frame_ready = side_panel_capture_process();
      if (wave_frame_ready)
      {
        lv_refr_now(lv_display_get_default());
      }

    }

    if (wave_frame_ready)
    {
      /* 只让出一个系统 tick；下一帧周期主要由真实绘制和 flush 耗时决定。 */
      delay_ms = 1U;
    }
    if (delay_ms < 1U)
    {
      delay_ms = 1U;
    }
    else if (delay_ms > 10U)
    {
      delay_ms = 10U;
    }

    /* 限制为 1~10 ms：既避免空转，也保证触摸和页面切换有足够响应速度。 */
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_TIM7_Init();
  /* USER CODE BEGIN 2 */
	uart_init();
	

  if (!sdram_init() || !sdram_selftest())
  {
    Error_Handler();
  }

  if (lcd_init() != LCD_STATUS_OK)
  {
    Error_Handler();
  }
		
	if (!log_task_creat())
{
    Error_Handler();
}

  (void)log_printf("creating lvgl task\r\n");
  if (xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_DEPTH, NULL,
                  LVGL_TASK_PRIORITY, NULL) != pdPASS)
  {
    Error_Handler();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	vTaskStartScheduler();
	
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
#endif

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
	if (htim->Instance == TIM7) {
    lv_tick_inc(1);
  }
  /* USER CODE END Callback 1 */
}

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

#ifdef  USE_FULL_ASSERT
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
