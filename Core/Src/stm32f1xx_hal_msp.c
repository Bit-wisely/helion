/**
  ******************************************************************************
  * @file         stm32f1xx_hal_msp.c
  * @brief        MCU Support Package (MSP) peripheral initialization callbacks.
  ******************************************************************************
  */

#include "main.h"

void HAL_MspInit(void) {
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    /* Disable JTAG to free PB3/PB4/PA15 while keeping SWD on PA13/PA14 active */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (hadc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* PA0 -> IN0, PA1 -> IN1, PA2 -> IN2, PA3 -> IN3 */
        GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    }
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef* htim_pwm) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (htim_pwm->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* PA6 -> TIM3_CH1 (Pan), PA7 -> TIM3_CH2 (Tilt) */
        GPIO_InitStruct.Pin = PAN_SERVO_PIN | TILT_SERVO_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef* htim_pwm) {
    if (htim_pwm->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, PAN_SERVO_PIN | TILT_SERVO_PIN);
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C1_CLK_ENABLE();

        /* PB6 -> I2C1_SCL, PB7 -> I2C1_SDA */
        GPIO_InitStruct.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);
    }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* hi2c) {
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_DISABLE();
        HAL_GPIO_DeInit(I2C_GPIO_PORT, I2C_SCL_PIN | I2C_SDA_PIN);
    }
}
