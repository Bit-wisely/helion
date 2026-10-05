/**
  ******************************************************************************
  * @file           : tracker.c
  * @brief          : Single-axis (Azimuth) solar tracking algorithms and servo actuation.
  *
  *  Hardware:
  *    - Left  LDR : PA0 (ADC_CHANNEL_0) — faces left/west
  *    - Right LDR : PA1 (ADC_CHANNEL_1) — faces right/east
  *    - Pan Servo : PA6 (TIM3 CH1, 50 Hz PWM)
  *
  *  Error formula (normalized, immune to ambient flux):
  *    err_pan = ((right - left) / (right + left)) × 1000
  *    Range: -1000 (fully left-lit) to +1000 (fully right-lit)
  *    Deadband: ±DEADBAND_PERCENT (4.0%)
  ******************************************************************************
  */

#include "tracker.h"

/* Calibration factors — Q12 fixed-point (4096 = 1.0×), one per LDR channel */
static float cal_factors[2] = {1.0f, 1.0f};

/* Current pulse width (us) for the pan servo */
static uint16_t pan_pulse = SERVO_CENTER_PULSE;

/* Runaway detection state */
static int32_t last_abs_err_pan = 9999;
static uint8_t runaway_pan_count = 0;
static bool runaway_fault = false;

/* Clamping helper */
static inline uint16_t Clamp_Pulse(uint16_t val, uint16_t min_v, uint16_t max_v) {
    if (val < min_v) return min_v;
    if (val > max_v) return max_v;
    return val;
}

/* ========================================================================== */
/*  DWT cycle-counter timing                                                   */
/* ========================================================================== */

void Tracker_DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCNT = 0;
}

void Tracker_DWT_Delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < ticks);
}

/* ========================================================================== */
/*  Initialisation                                                             */
/* ========================================================================== */

void Tracker_Init(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc) {
    (void)hadc;
    Tracker_DWT_Init();

    /* Single servo soft-start: center Pan servo only */
    pan_pulse = SERVO_CENTER_PULSE;
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1);
    HAL_Delay(300);

    /* PWM remains active to provide continuous holding torque against wind */
}

/* ========================================================================== */
/*  ADC: oversampled single-channel read                                       */
/* ========================================================================== */

uint16_t Tracker_Read_Filtered_Channel(ADC_HandleTypeDef *hadc, uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(hadc, &sConfig);

    uint32_t accumulator = 0;
    /* 40 samples at exactly 500 us intervals = 20.0 ms integration window
       (rejects 50 Hz / 60 Hz mains flicker) */
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

/* ========================================================================== */
/*  Calibration                                                                */
/* ========================================================================== */

void Tracker_Calibrate_Offsets(ADC_HandleTypeDef *hadc) {
    uint32_t raw[2] = {0};

    /* Average 16 readings per LDR channel for a stable baseline */
    for (int s = 0; s < 16; s++) {
        raw[0] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0);  /* Left  LDR */
        raw[1] += Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1);  /* Right LDR */
    }

    float avg = 0.0f;
    for (int i = 0; i < 2; i++) {
        raw[i] /= 16;
        /* Protect against loose wires or disconnected LDRs (reading ~0) */
        if (raw[i] < 50)   raw[i] = 50;
        if (raw[i] > 4000) raw[i] = 4000;
        avg += (float)raw[i];
    }
    avg /= 2.0f;

    for (int i = 0; i < 2; i++) {
        cal_factors[i] = avg / (float)raw[i];
        /* Clamp multipliers within realistic variance bounds [0.25, 4.0] */
        if (cal_factors[i] < 0.25f) cal_factors[i] = 0.25f;
        if (cal_factors[i] > 4.00f) cal_factors[i] = 4.00f;
    }
}

/* ========================================================================== */
/*  Night parking                                                              */
/* ========================================================================== */

void Tracker_Park(TIM_HandleTypeDef *htim) {
    /* Night stow: face east (sunrise position) */
    pan_pulse = SERVO_CENTER_PULSE;
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
}

/* ========================================================================== */
/*  Dawn sun-search sweep (azimuth only)                                       */
/* ========================================================================== */

void Tracker_Dawn_Scan(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc) {
    /* Sweep from one end of travel to the other to re-acquire the sun
       when it has moved outside the baffle field-of-view overnight */
    for (uint16_t p = PAN_MIN_PULSE; p <= PAN_MAX_PULSE; p += 100) {
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, p);
        HAL_Delay(100);

        uint32_t sum =
            Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0) +
            Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1);

        if (sum > NIGHT_EXIT_ADC_SUM) {
            pan_pulse = p;
            break;
        }
    }
}

/* ========================================================================== */
/*  Main tracking step                                                         */
/* ========================================================================== */

void Tracker_Update_Loop(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc, SystemTelemetry *telem) {
    if (runaway_fault) {
        HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
        return;
    }

    /* 1. Read calibrated LDR channels */
    float ldr_left  = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_0) * cal_factors[0];
    float ldr_right = (float)Tracker_Read_Filtered_Channel(hadc, ADC_CHANNEL_1) * cal_factors[1];

    float total_light = ldr_left + ldr_right;

    /* 2. Night parking / hysteresis check */
    if (total_light < NIGHT_ENTER_ADC_SUM) {
        telem->is_parked = true;
        telem->err_pan   = 0;
        Tracker_Park(htim);
        return;
    } else if (telem->is_parked) {
        if (total_light > NIGHT_EXIT_ADC_SUM) {
            telem->is_parked = false;
        } else {
            return;
        }
    }

    /* 3. Normalized single-axis error calculation
     *    err_pan > 0  → sun is to the right  → rotate right (increase pulse)
     *    err_pan < 0  → sun is to the left   → rotate left  (decrease pulse)
     *    Scale: ±1000 (i.e. ±100.0%)
     */
    int32_t err_pan = (int32_t)(((ldr_right - ldr_left) / total_light) * 1000.0f);
    telem->err_pan  = err_pan / 10;   /* in percent: -100% to +100% */

    int32_t abs_pan = (err_pan >= 0) ? err_pan : -err_pan;

    /* 4. Azimuth adjustment with runaway protection */
    if (abs_pan > DEADBAND_PERCENT) {
        if (abs_pan >= last_abs_err_pan) {
            runaway_pan_count++;
            if (runaway_pan_count >= MAX_RUNAWAY_STEPS) {
                runaway_fault = true;   /* error is increasing — abort */
                return;
            }
        } else {
            runaway_pan_count = 0;
        }
        last_abs_err_pan = abs_pan;

        if (err_pan > 0) {
            pan_pulse = Clamp_Pulse((uint16_t)(pan_pulse + (PAN_DIR * STEP_TICKS)),
                                    PAN_MIN_PULSE, PAN_MAX_PULSE);
        } else {
            pan_pulse = Clamp_Pulse((uint16_t)(pan_pulse - (PAN_DIR * STEP_TICKS)),
                                    PAN_MIN_PULSE, PAN_MAX_PULSE);
        }

        /* 5. Apply actuation */
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, pan_pulse);
    } else {
        /* Within deadband — clear runaway counters */
        runaway_pan_count = 0;
        last_abs_err_pan  = 9999;
    }
}
