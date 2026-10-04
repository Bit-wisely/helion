/**
  ******************************************************************************
  * @file           : ina219.h
  * @brief          : High-side current, voltage, and power sensor driver.
  ******************************************************************************
  */

#ifndef __INA219_H
#define __INA219_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* I2C 7-bit Hardware Addresses */
#define INA219_ADDR_TRACKED     0x40    // A0=Open, A1=Open
#define INA219_ADDR_SYSTEM      0x41    // A0=Bridged, A1=Open
#define INA219_ADDR_FIXED       0x44    // A0=Open, A1=Bridged

/* Registers */
#define INA219_REG_CONFIG       0x00
#define INA219_REG_SHUNTVOLTAGE 0x01
#define INA219_REG_BUSVOLTAGE   0x02
#define INA219_REG_POWER        0x03
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIBRATION  0x05

/**
 * Default Configuration: 0x399F
 * Bits [15:14] = 00 (No reset)
 * Bit 13       = 1  (BRNG: 32V Bus Voltage Range)
 * Bits [12:11] = 11 (PG: ±320mV Shunt Range, Gain /8)
 * Bits [10:7]  = 0010 (BADC: 12-bit, 1 sample, 532us)
 * Bits [6:3]   = 0010 (SADC: 12-bit, 1 sample, 532us)
 * Bits [2:0]   = 111 (MODE: Shunt and Bus, Continuous)
 */
#define INA219_CONFIG_DEFAULT   0x399F

/* Driver API */
HAL_StatusTypeDef INA219_Init(I2C_HandleTypeDef *hi2c, uint16_t addr_7bit);
HAL_StatusTypeDef INA219_ReadData(I2C_HandleTypeDef *hi2c, uint16_t addr_7bit, float *voltage_v, float *current_ma, float *power_mw);
void I2C_Bus_Recovery(void);
bool I2C_Scan(I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* __INA219_H */
