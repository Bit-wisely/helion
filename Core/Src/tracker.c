/**
  ******************************************************************************
  * @file           : tracker.c
  * @brief          : Dual-axis solar tracking algorithms and servo actuation.
  ******************************************************************************
  */

#include "tracker.h"

/* Calibration factors clamped in safe bounds */
static float cal_factors[4] = {1.0f, 1.0f, 1.0f, 1.0f};

/* Current pulse widths (us) */
static uint16_t pan_pulse = SERVO_CENTER_PULSE;
static uint16_t tilt_pulse = SERVO_CENTER_PULSE;

/* Runaway detection state */
static int32_t last_abs_err_pan = 9999;
static int32_t last_abs_err_tilt = 9999;
static uint8_t runaway_pan_count = 0;
static uint8_t runaway_tilt_count = 0;
static bool runaway_fault = false;

/* Clamping helper */
static inline uint16_t Clamp_Pulse(uint16_t val, uint16_t min_v, uint16_t max_v) {
    if (val < min_v) return min_v;
    if (val > max_v) return max_v;
    return val;
}

void Tracker_DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
}

void Tracker_DWT_Delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < ticks);
}

void Tracker_Init(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc) {
    Tracker_DWT_Init();

    /* Staggered servo soft-start: center Pan first, then Tilt after 300ms
       to prevent simultaneous stall current spike on 5V rail */
    pan_pulse = SERVO_CENTER_PULSE;
    tilt_pulse = SERVO_CENTER_PULSE;

    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1);
    HAL_Delay(300);

    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, tilt_pulse);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_2);
    HAL_Delay(300);

    /* PWM remains active to provide continuous holding torque against wind */
}

uint16_t Tracker_Read_Filtered_Channel(ADC_HandleTypeDef *hadc, uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(hadc, &sConfig);

    uint32_t accumulator = 0;
    /* 40 samples at exactly 500 us intervals = 20.0 ms integration window (50 Hz / 60 Hz mains rejection) */
    for (int i = 0; i < 40; i++) {
        HAL_ADC_Start(hadc);
        if (HAL_ADC_PollForConversion(hadc, 10) == HAL_OK) {
            accumulator += HAL_ADC_GetValue(hadc);
        }
        Tracker_DWT_Delay_us(500);
    }
    HAL_ADC_Stop(hadc);

    return (uint16_t)(accumulator / 40);
}

void Tracker_Calibrate_Offsets(ADC_HandleTypeDef *hadc) {
    uint32_t raw[4] = {0};

    for (int s = 0; s < 16; s++) {
        raw[0] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0);
        raw[1] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1);
        raw[2] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_2);
        raw[3] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_3);
    }

    float avg = 0.0f;
    for (int i = 0; i < 4; i++) {
        raw[i] /= 16;
        /* Protect against loose wires or disconnected LDRs (reading ~0) */
        if (raw[i] < 50) raw[i] = 50;
        if (raw[i] > 4000) raw[i] = 4000;
        avg += (float)raw[i];
    }
    avg /= 4.0f;

    for (int i = 0; i < 4; i++) {
        cal_factors[i] = avg / (float)raw[i];
        /* Clamp multipliers within realistic variance bounds [0.25, 4.0] */
        if (cal_factors[i] < 0.25f) cal_factors[i] = 0.25f;
        if (cal_factors[i] > 4.00f) cal_factors[i] = 4.00f;
    }
}

void Tracker_Park(TIM_HandleTypeDef *htim) {
    /* Night stow: Face flat or East */
    pan_pulse = SERVO_CENTER_PULSE;
    tilt_pulse = TILT_MIN_PULSE + 200; // Flat orientation
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, tilt_pulse);
}

void Tracker_Dawn_Scan(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc) {
    /* Broad sweep from East to West to re-acquire the sun when lost outside baffle FOV */
    for (uint16_t p = PAN_MIN_PULSE; p <= PAN_MAX_PULSE; p += 100) {
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, p);
        HAL_Delay(100);
        uint32_t sum = Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0) +
                       Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1) +
                       Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_2) +
                       Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_3);
        if (sum > NIGHT_EXIT_ADC_SUM) {
            pan_pulse = p;
            break;
        }
    }
}

void Tracker_Update_Loop(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc, SystemTelemetry *telem) {
    if (runaway_fault) {
        HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
        return;
    }

    /* 1. Read calibrated LDR channels */
    float tl = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0) * cal_factors[0];
    float tr = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1) * cal_factors[1];
    float bl = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_2) * cal_factors[2];
    float br = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_3) * cal_factors[3];

    float total_light = tl + tr + bl + br;

    /* 2. Check Night Parking / Hysteresis */
    if (total_light < NIGHT_ENTER_ADC_SUM) {
        telem->is_parked = true;
        telem->err_pan = 0;
        telem->err_tilt = 0;
        Tracker_Park(htim);
        return;
    } else if (telem->is_parked) {
        if (total_light > NIGHT_EXIT_ADC_SUM) {
            telem->is_parked = false;
        } else {
            return;
        }
    }

    /* 3. Normalized Error Calculation (Immune to ambient flux) */
    float top = tl + tr;
    float bottom = bl + br;
    float left = tl + bl;
    float right = tr + br;

    int32_t err_pan = (int32_t)(((right - left) / total_light) * 1000.0f);
    int32_t err_tilt = (int32_t)(((top - bottom) / total_light) * 1000.0f);

    telem->err_pan = err_pan / 10;   // In percent (-100% to +100%)
    telem->err_tilt = err_tilt / 10;

    int32_t abs_pan = (err_pan >= 0) ? err_pan : -err_pan;
    int32_t abs_tilt = (err_tilt >= 0) ? err_tilt : -err_tilt;

    bool moved = false;

    /* 4. Azimuth (Pan) Adjustment with Runaway Protection */
    if (abs_pan > DEADBAND_PERCENT) {
        if (abs_pan >= last_abs_err_pan) {
            runaway_pan_count++;
            if (runaway_pan_count >= MAX_RUNAWAY_STEPS) {
                runaway_fault = true; // Tripped: error is increasing, abort
                return;
            }
        } else {
            runaway_pan_count = 0;
        }
        last_abs_err_pan = abs_pan;

        if (err_pan > 0) {
            pan_pulse = Clamp_Pulse(pan_pulse + (PAN_DIR * STEP_TICKS), PAN_MIN_PULSE, PAN_MAX_PULSE);
        } else {
            pan_pulse = Clamp_Pulse(pan_pulse - (PAN_DIR * STEP_TICKS), PAN_MIN_PULSE, PAN_MAX_PULSE);
        }
        moved = true;
    } else {
        runaway_pan_count = 0;
        last_abs_err_pan = 9999;
    }

    /* 5. Elevation (Tilt) Adjustment with Runaway Protection */
    if (abs_tilt > DEADBAND_PERCENT) {
        if (abs_tilt >= last_abs_err_tilt) {
            runaway_tilt_count++;
            if (runaway_tilt_count >= MAX_RUNAWAY_STEPS) {
                runaway_fault = true;
                return;
            }
        } else {
            runaway_tilt_count = 0;
        }
        last_abs_err_tilt = abs_tilt;

        if (err_tilt > 0) {
            tilt_pulse = Clamp_Pulse(tilt_pulse + (TILT_DIR * STEP_TICKS), TILT_MIN_PULSE, TILT_MAX_PULSE);
        } else {
            tilt_pulse = Clamp_Pulse(tilt_pulse - (TILT_DIR * STEP_TICKS), TILT_MIN_PULSE, TILT_MAX_PULSE);
        }
        moved = true;
    } else {
        runaway_tilt_count = 0;
        last_abs_err_tilt = 9999;
    }

    /* 6. Apply Actuation */
    if (moved) {
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, tilt_pulse);
    }
}
