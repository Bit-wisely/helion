/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   Target: STM32F103C8T6 @ 72 MHz
  *                   Single-axis azimuth tracker — 2x LDR (PA0, PA1), 1x Servo (PA6)
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
    MODE_DEMO = 0,   // Rapid 300 ms response for indoor demonstration
    MODE_FIELD = 1   // 30 s gated tracking for outdoor field use
} TrackerMode;

typedef struct {
    float v_tracked;    // V
    float i_tracked;    // mA
    float p_tracked;    // mW
    float v_fixed;      // V
    float i_fixed;      // mA
    float p_fixed;      // mW
    float v_system;     // V (System input rail before buck)
    float i_system;     // mA
    float p_system;     // mW
    float mah_tracked;  // Accumulated mAh
    float mah_fixed;    // Accumulated mAh
    int32_t err_pan;    // Normalized Azimuth error (-100% to +100%)
    bool is_parked;     // True if night-parked or low light
    TrackerMode mode;   // Active mode
} SystemTelemetry;

/* Exported constants --------------------------------------------------------*/
#define LED_PIN                 GPIO_PIN_13
#define LED_GPIO_PORT           GPIOC
#define BTN_PIN                 GPIO_PIN_4
#define BTN_GPIO_PORT           GPIOA

/* Single-axis: only pan servo on PA6 (TIM3 CH1) */
#define PAN_SERVO_PIN           GPIO_PIN_6
#define PAN_SERVO_PORT          GPIOA

#define I2C_SCL_PIN             GPIO_PIN_6
#define I2C_SDA_PIN             GPIO_PIN_7
#define I2C_GPIO_PORT           GPIOB

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
