/**
  ******************************************************************************
  * @file           : tracker.h
  * @brief          : Dual-axis solar tracking algorithms and servo actuation.
  ******************************************************************************
  */

#ifndef __TRACKER_H
#define __TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "main.h"

/* Direction signs: +1 or -1 */
#define PAN_DIR                 (+1)
#define TILT_DIR                (+1)

/* Pulse limits (microseconds) avoiding mechanical binding */
#define PAN_MIN_PULSE           750
#define PAN_MAX_PULSE           2250
#define TILT_MIN_PULSE          850
#define TILT_MAX_PULSE          2150
#define SERVO_CENTER_PULSE      1500

/* Tracking Control Parameters */
#define DEADBAND_PERCENT        40      // 40 = 4.0% normalized error deadband
#define STEP_TICKS              15      // ~1.5 degree increment per move
#define MAX_RUNAWAY_STEPS       5       // Consecutive non-reducing error steps before trip

/* Night Parking & Dawn Recovery (Sum of 4 ADC Channels, range 0-16380) */
#define NIGHT_ENTER_ADC_SUM     800     // Average <200 counts per LDR -> Night Park
#define NIGHT_EXIT_ADC_SUM      1500    // Average >375 counts per LDR -> Resume Tracking

/* API */
void Tracker_Init(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc);
void Tracker_DWT_Init(void);
void Tracker_DWT_Delay_us(uint32_t us);
uint16_t Tracker_Read_Filtered_Channel(ADC_HandleTypeDef *hadc, uint32_t channel);
void Tracker_Update_Loop(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc, SystemTelemetry *telem);
void Tracker_Calibrate_Offsets(ADC_HandleTypeDef *hadc);
void Tracker_Dawn_Scan(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc);
void Tracker_Park(TIM_HandleTypeDef *htim);

#ifdef __cplusplus
}
#endif

#endif /* __TRACKER_H */
