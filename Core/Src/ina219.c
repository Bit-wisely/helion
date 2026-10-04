/**
  ******************************************************************************
  * @file           : ina219.c
  * @brief          : INA219 high-side power monitor driver and I2C recovery.
  ******************************************************************************
  */

#include "ina219.h"
#include "main.h"

HAL_StatusTypeDef INA219_Init(I2C_HandleTypeDef *hi2c, uint16_t addr_7bit) {
    uint8_t cfg[3];
    uint16_t dev_addr = (addr_7bit << 1);

    cfg[0] = INA219_REG_CONFIG;
    cfg[1] = (uint8_t)((INA219_CONFIG_DEFAULT >> 8) & 0xFF); // 0x39
    cfg[2] = (uint8_t)(INA219_CONFIG_DEFAULT & 0xFF);        // 0x9F

    /* Write 0x399F: 32V Bus, ±320mV Shunt (Gain /8), 12-bit continuous */
    return HAL_I2C_Master_Transmit(hi2c, dev_addr, cfg, 3, 100);
}

HAL_StatusTypeDef INA219_ReadData(I2C_HandleTypeDef *hi2c, uint16_t addr_7bit,
                                 float *voltage_v, float *current_ma, float *power_mw) {
    uint8_t reg;
    uint8_t data[2];
    uint16_t dev_addr = (addr_7bit << 1);
    HAL_StatusTypeDef status;

    /* 1. Read Bus Voltage Register (0x02) */
    reg = INA219_REG_BUSVOLTAGE;
    status = HAL_I2C_Master_Transmit(hi2c, dev_addr, &reg, 1, 50);
    if (status != HAL_OK) return status;

    status = HAL_I2C_Master_Receive(hi2c, dev_addr, data, 2, 50);
    if (status != HAL_OK) return status;

    int16_t raw_v = (int16_t)((data[0] << 8) | data[1]);
    /* Bits [15:3] are voltage; 4 mV per LSB */
    *voltage_v = (float)(raw_v >> 3) * 0.004f;

    /* 2. Read Shunt Voltage Register (0x01) */
    reg = INA219_REG_SHUNTVOLTAGE;
    status = HAL_I2C_Master_Transmit(hi2c, dev_addr, &reg, 1, 50);
    if (status != HAL_OK) return status;

    status = HAL_I2C_Master_Receive(hi2c, dev_addr, data, 2, 50);
    if (status != HAL_OK) return status;

    int16_t raw_shunt = (int16_t)((data[0] << 8) | data[1]);
    /* Shunt LSB = 10 uV. With R_shunt = 0.1 Ohm, I = V_shunt / 0.1 Ohm = raw_shunt * 10uV / 0.1 = raw_shunt * 0.1 mA */
    *current_ma = (float)raw_shunt * 0.1f;
    if (*current_ma < 0.0f) *current_ma = 0.0f; // Panel is purely generation/load

    /* 3. Calculate Power (mW) directly from V and I */
    *power_mw = (*voltage_v) * (*current_ma);

    return HAL_OK;
}

void I2C_Bus_Recovery(void) {
    /* STM32F1 I2C busy flag errata workaround:
       Generate 9 clock pulses on SCL to release any slave holding SDA low */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = I2C_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 200; d++);
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        for (volatile int d = 0; d < 200; d++);
    }
}

bool I2C_Scan(I2C_HandleTypeDef *hi2c) {
    bool found_oled = false;
    bool found_trk = false;
    bool found_sys = false;
    bool found_fix = false;

    for (uint16_t addr = 1; addr < 128; addr++) {
        if (HAL_I2C_IsDeviceReady(hi2c, (addr << 1), 2, 10) == HAL_OK) {
            if (addr == 0x3C) found_oled = true;
            if (addr == INA219_ADDR_TRACKED) found_trk = true;
            if (addr == INA219_ADDR_SYSTEM)  found_sys = true;
            if (addr == INA219_ADDR_FIXED)   found_fix = true;
        }
    }

    return (found_oled && found_trk && found_sys && found_fix);
}
