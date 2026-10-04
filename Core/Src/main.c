/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Production Dual-Axis Solar Tracker & Telemetry Controller.
  *                   Target: STM32F103C8T6 (Blue Pill) @ 72 MHz
  ******************************************************************************
  */

#include "main.h"
#include "ina219.h"
#include "ssd1306.h"
#include "tracker.h"

/* Peripheral Handles */
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim3;
IWDG_HandleTypeDef hiwdg;

/* Global System Telemetry State */
SystemTelemetry telemetry = {0};

/* Private Function Prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
static void MX_IWDG_Init(void);

/**
  * @brief  Main program entry point.
  */
int main(void) {
    /* Reset all peripherals, initialize Flash interface and Systick */
    HAL_Init();

    /* Configure system clock to 72 MHz via HSE crystal */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_TIM3_Init();

    /* Pre-emptively clear any I2C bus lockup before peripheral init */
    I2C_Bus_Recovery();
    MX_I2C1_Init();

    /* 1. I2C Bus Detection Scan */
    bool bus_ok = I2C_Scan(&hi2c1);
    if (!bus_ok) {
        /* Blink slow warning on PC13 if an I2C device is missing */
        for (int i = 0; i < 6; i++) {
            HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
            HAL_Delay(150);
        }
    }

    /* 2. Initialize OLED Display */
    SSD1306_Init(&hi2c1);
    SSD1306_SetCursor(10, 20);
    SSD1306_WriteString("HELION TRACKER", Font_6x8, SSD1306_COLOR_WHITE);
    SSD1306_SetCursor(10, 36);
    SSD1306_WriteString("INITIALIZING...", Font_6x8, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen(&hi2c1);

    /* 3. Initialize INA219 Telemetry Sensors (Config: 0x399F) */
    INA219_Init(&hi2c1, INA219_ADDR_TRACKED);
    INA219_Init(&hi2c1, INA219_ADDR_SYSTEM);
    INA219_Init(&hi2c1, INA219_ADDR_FIXED);

    /* 4. Initialize Tracker & Staggered Servo Centering */
    Tracker_Init(&htim3, &hadc1);

    /* 5. Initialize Hardware Watchdog (2-second timeout) */
    MX_IWDG_Init();

    /* Initialize Timing Variables */
    uint32_t last_telemetry_tick = HAL_GetTick();
    uint32_t last_tracking_tick = HAL_GetTick();
    telemetry.mode = MODE_DEMO;

    /* Non-blocking Button State Machine */
    uint32_t btn_press_start = 0;
    bool btn_was_pressed = false;

    /* Turn Status LED ON (active low on Blue Pill PC13) */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);

    while (1) {
        uint32_t now = HAL_GetTick();

        /* Refresh Watchdog */
        HAL_IWDG_Refresh(&hiwdg);

        /* ------------------ NON-BLOCKING BUTTON HANDLER ------------------ */
        if (HAL_GPIO_ReadPin(BTN_GPIO_PORT, BTN_PIN) == GPIO_PIN_RESET) {
            if (!btn_was_pressed) {
                btn_press_start = now;
                btn_was_pressed = true;
            }
        } else if (btn_was_pressed) {
            uint32_t duration = now - btn_press_start;
            btn_was_pressed = false;

            if (duration >= 2000) {
                /* Long Press (>2s): Sensor Calibration */
                SSD1306_SetCursor(10, 28);
                SSD1306_WriteString("CALIBRATING...", Font_6x8, SSD1306_COLOR_WHITE);
                SSD1306_UpdateScreen(&hi2c1);
                Tracker_Calibrate_Offsets(&hadc1);
                HAL_Delay(300);
            } else if (duration >= 50) {
                /* Short Press (>50ms): Toggle Tracking Mode */
                telemetry.mode = (telemetry.mode == MODE_DEMO) ? MODE_FIELD : MODE_DEMO;
            }
        }

        /* ------------------ TRACKING LOOP ------------------ */
        uint32_t tracking_interval = (telemetry.mode == MODE_DEMO) ? 300 : 30000;
        if (now - last_tracking_tick >= tracking_interval) {
            last_tracking_tick = now;
            Tracker_Update_Loop(&htim3, &hadc1, &telemetry);
        }

        /* ------------------ TELEMETRY & DISPLAY LOOP ------------------ */
        if (now - last_telemetry_tick >= 500) {
            float dt_hours = (float)(now - last_telemetry_tick) / 3600000.0f;
            last_telemetry_tick = now;

            /* Read Tracked PV Panel */
            INA219_ReadData(&hi2c1, INA219_ADDR_TRACKED,
                            &telemetry.v_tracked, &telemetry.i_tracked, &telemetry.p_tracked);

            /* Read Fixed Baseline PV Panel */
            INA219_ReadData(&hi2c1, INA219_ADDR_FIXED,
                            &telemetry.v_fixed, &telemetry.i_fixed, &telemetry.p_fixed);

            /* Read System Input Rail (Fix: Separate pointers, no aliasing) */
            INA219_ReadData(&hi2c1, INA219_ADDR_SYSTEM,
                            &telemetry.v_system, &telemetry.i_system, &telemetry.p_system);

            /* Accumulate Energy Yield (mAh) */
            telemetry.mah_tracked += (telemetry.i_tracked * dt_hours);
            telemetry.mah_fixed   += (telemetry.i_fixed * dt_hours);

            /* Render Dashboard to OLED */
            SSD1306_DrawDashboard(&hi2c1, &telemetry);
        }
    }
}

/**
  * @brief System Clock Configuration (72 MHz via HSE 8 MHz + PLL)
  */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /* Enable HSE and configure PLL (8 MHz * 9 = 72 MHz) */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Select PLL as system clock source and configure bus dividers */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // 36 MHz max
    RCC_ClkInitStruct.APB2CLKDivider = RCC_SYSCLK_DIV1; // 72 MHz max

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }

    /* ADC Clock Prescaler = 72 MHz / 6 = 12 MHz (Max allowable ADC clock is 14 MHz) */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief ADC1 Initialization Function
  */
static void MX_ADC1_Init(void) {
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }
    HAL_ADCEx_Calibration_Start(&hadc1);
}

/**
  * @brief I2C1 Initialization Function
  */
static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief TIM3 Initialization Function (50 Hz PWM on CH1 & CH2)
  */
static void MX_TIM3_Init(void) {
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 71;             // 72 MHz / (71 + 1) = 1 MHz (1 tick = 1 us)
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 19999;             // 20000 us = 20 ms (50 Hz PWM period)
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) {
        Error_Handler();
    }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = SERVO_CENTER_PULSE;  // 1500 us initial
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    /* Channel 1: Pan Servo (PA6) */
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
    /* Channel 2: Tilt Servo (PA7) */
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief IWDG Initialization Function (2-Second Hardware Watchdog)
  */
static void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64; // ~40 kHz / 64 = 625 Hz
    hiwdg.Init.Reload = 1250;                 // 1250 / 625 Hz = 2.00 seconds
    HAL_IWDG_Init(&hiwdg);
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Configure PC13 (Status LED) as push-pull output */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);

    /* Configure PA4 (Button) as input with pull-up */
    GPIO_InitStruct.Pin = BTN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BTN_GPIO_PORT, &GPIO_InitStruct);
}

/**
  * @brief Error Handler Function
  */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
        for (volatile uint32_t i = 0; i < 300000; i++);
    }
}
