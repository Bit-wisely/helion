/**
  ******************************************************************************
  * @file    main.c
  * @brief   Helion single-axis solar tracker firmware (laptop-display revision)
  * @target  STM32F103C8T6 "Blue Pill", HSE 8 MHz -> PLL -> 72 MHz, STM32 HAL (F1)
  *
  *  Hardware
  *    PA0  ADC1_IN0   Left  LDR divider      PA1  ADC1_IN1   Right LDR divider
  *    PA4  Button to GND (internal pull-up)  PA6  TIM3_CH1   Pan servo (50 Hz PWM)
  *    PB6  I2C1_SCL / PB7 I2C1_SDA           PA9  USART1_TX  -> USB-TTL adapter RX
  *    PA10 USART1_RX  <- USB-TTL adapter TX
  *    I2C devices: INA219 0x40 (tracked panel), 0x41 (system input),
  *                 0x44 (fixed reference panel)
  *
  *  There is no on-board display and no LED: everything is shown on the laptop.
  *  The firmware prints one telemetry line per second (115200 8N1) and event
  *  lines for anything that changes. See README.md "Serial protocol".
  *
  *  Behaviour
  *    - Tracking is independent of I2C and of the serial link: a dead telemetry
  *      bus or an unplugged laptop never stops the servo.
  *    - Boot: centre servo -> if dark, park; otherwise sweep to find the sun.
  *    - Faults (servo holds position, reported as flt=... and "#EVT FAULT ..."):
  *        runaway : servo keeps moving without the error shrinking
  *                  (wrong PAN_DIR, slipped horn, blocked panel)
  *        LDR     : one channel ~0 while the other is lit (open wire / bad divider)
  *    - Calibration (hold button > 2 s, or "CAL" from the laptop, under uniform
  *      light) is stored in flash.
  *    - 4 s independent watchdog; a watchdog reset is reported in the boot banner.
  *    - Telemetry maths is integer-only (no float printf needed).
  *
  *  Button : short press = DEMO <-> FIELD (and clears faults), hold > 2 s = calibrate
  *  Laptop : text commands MODE / CAL / CLEAR / STATUS (one per line)
  ******************************************************************************
  */

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define HELION_FW_VERSION       "5.0"

typedef enum { MODE_DEMO = 0, MODE_FIELD = 1 } TrackerMode;

/* ========================================================================== */
/*  Build-time configuration                                                   */
/* ========================================================================== */

#define MAINS_HZ                50      /* 50 or 60: sets ADC averaging window   */

/* Direction: flip to -1 if the servo runs away from the light */
#define PAN_DIR                 (+1)

/* Servo limits (us). Start conservative; widen only after running the sweep
 * test (set SERVO_SWEEP_TEST to 1) and finding where YOUR servo binds.       */
#define PAN_MIN_PULSE           900
#define PAN_MAX_PULSE           2100
#define PAN_CENTER_PULSE        1500
/* 1 = stop PWM after parking (saves ~10 mA, panel is free to move in wind at
 * night). 0 = keep holding the park position (default, safer for the gears). */
#define PARK_RELEASE_SERVO      0
#define PARK_PAN_PULSE          2000    /* night rest position: aim at where the
                                         * sun rises. Higher pulse = towards the
                                         * RIGHT LDR when PAN_DIR is +1.       */

/* Tracking */
#define DEADBAND                40      /* 40 = 4.0 % normalised difference   */
#define STEP_MIN_US             5
#define STEP_MAX_US             30
#define STEP_DIV                20      /* step = MIN + |err|/DIV, clamped    */
#define RUNAWAY_STEPS           40      /* same-direction steps w/o improvement */
#define RUNAWAY_LIMIT_STEPS     12
/* Mode after power-up / watchdog reset. Use MODE_FIELD for a permanent outdoor
 * install so a power glitch does not leave the tracker in fast DEMO mode.    */
#define BOOT_MODE               MODE_DEMO
#define DEMO_INTERVAL_MS        300
#define FIELD_INTERVAL_MS       30000UL
#define FIELD_SETTLE_MS         400
/* 0 = keep PWM on in field mode (holds panel against wind, ~10 mA/servo)
 * 1 = stop PWM between moves (saves power, panel must be perfectly balanced) */
#define FIELD_RELEASE_SERVOS    0

/* Night park. Values are SUMS of two 12-bit ADC readings (0..8190).
 * Tune on your own divider: log `last_sum` at dusk and pick thresholds.      */
#define NIGHT_ENTER_ADC_SUM     400
#define NIGHT_EXIT_ADC_SUM      900
#define NIGHT_CONFIRM_COUNT     5       /* consecutive tracker ticks           */
#define DAY_CONFIRM_COUNT       3

/* Sun search — single azimuth sweep */
#define SEARCH_PAN_STEP_US      100
#define SEARCH_SETTLE_MS        150

/* Benchmarking */
#define K_MISMATCH_X1000        1000    /* manual 9.1: P_fixed_base/P_tracked_base x1000 */

/* INA219 */
#define INA_R_SHUNT_MOHM        100     /* module shunt, milli-ohm            */
#define INA_CONFIG              0x399F  /* 32 V, +/-320 mV, 12-bit, continuous */

/* 1 = three INA219 power sensors on I2C (full project).
 * 0 = bare tracker rig (LDRs + servo only): the I2C bus is never touched.
 * Override from the build system: -DHAVE_INA219=0 (see platformio.ini). */
#ifndef HAVE_INA219
#define HAVE_INA219             1
#endif

/* I2C */
#define I2C_SPEED_HZ            100000

/* Serial link to the laptop (USART1: PA9 TX, PA10 RX) */
#define SERIAL_BAUD             115200UL
#define SERIAL_INTERVAL_MS      1000U   /* one telemetry line per interval     */

/* Bring-up helper: 1 = firmware only sweeps the servo and prints the pulse
 * width on the serial port (press button to stop). Use it to find safe limits. */
#define SERVO_SWEEP_TEST        0

/* ========================================================================== */
/*  Derived constants                                                          */
/* ========================================================================== */

#define LDR_ROUNDS              40U
#define LDR_PERIOD_US           (1000000UL / MAINS_HZ / LDR_ROUNDS)

#define INA_TRK_ADDR7           0x40
#define INA_SYS_ADDR7           0x41
#define INA_FIX_ADDR7           0x44

/* Flash: last 1 KB page of a 64 KB part */
#define CAL_FLASH_ADDR          0x0800FC00UL
#define CAL_MAGIC               0x48454C33UL   /* "HEL3" record marker        */
#ifndef CAL_WORD                                /* overridden by the host tests */
#define CAL_WORD(i)     (((const volatile uint32_t *)CAL_FLASH_ADDR)[i])
#endif
#define CAL_Q_MIN               2048U          /* 0.5 in Q12 */
#define CAL_Q_MAX               8192U          /* 2.0 in Q12 */

#define FAULT_PAN_RUNAWAY       (1U << 0)
#define FAULT_LDR               (1U << 1)

/* ========================================================================== */
/*  Types and globals                                                          */
/* ========================================================================== */

typedef struct {
    uint8_t  addr7;
    bool     online;
    uint8_t  fail;
    uint32_t mv;      /* bus voltage, mV                       */
    int32_t  ma10;    /* current in units of 0.1 mA (>= 0)     */
    uint32_t mw;      /* power, mW                             */
} Ina219_t;

typedef struct {
    int8_t   last_dir;
    uint16_t bad;
    int32_t  prev_abs;
    bool     at_limit;
} Axis_t;

ADC_HandleTypeDef  hadc1;
I2C_HandleTypeDef  hi2c1;
TIM_HandleTypeDef  htim3;
UART_HandleTypeDef huart1;
IWDG_HandleTypeDef hiwdg = { .Instance = IWDG };

static Ina219_t ina_trk = { .addr7 = INA_TRK_ADDR7 };
static Ina219_t ina_sys = { .addr7 = INA_SYS_ADDR7 };
static Ina219_t ina_fix = { .addr7 = INA_FIX_ADDR7 };

/* Single-axis: only pan servo */
static uint16_t pan_pulse  = PAN_CENTER_PULSE;
static bool     pwm_on = false;
static bool     release_pending = false;
static uint32_t release_at = 0;

/* Two calibration Q12 gains — one per LDR (Left, Right) */
static uint32_t cal_q[2] = { 4096, 4096 };

static TrackerMode mode = BOOT_MODE;
static bool     is_parked = false;
static bool     searching = false;
static bool     force_track = false;
static uint32_t faults = 0;
static uint8_t  night_cnt = 0, day_cnt = 0;
static Axis_t   ax_pan;
static int32_t  err_az = 0;
static uint32_t last_sum = 0;

static uint64_t energy_trk = 0, energy_fix = 0;   /* mV * 0.1 mA * ms   */

static uint32_t last_i2c_recover = 0;
static bool     boot_wdg_reset = false;

/* Serial link state */
static char     rx_line[16];
static uint8_t  rx_len = 0;
static bool     rx_overflow = false;
static uint32_t last_faults_sent = 0;
static uint8_t  last_ina_mask_sent = 0xFF;
static bool     last_parked_sent = false;

/* ========================================================================== */
/*  Prototypes                                                                 */
/* ========================================================================== */

void SystemClock_Config(void);
void Error_Handler(void);
static void Serial_Write(const char *b, uint16_t n);
static void Serial_SendTelemetry(void);
static void Serial_Event(const char *msg);
static void Toggle_Mode(void);

/* ========================================================================== */
/*  Small helpers                                                              */
/* ========================================================================== */

static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline void DWT_Delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000UL);
    while ((DWT->CYCCNT - start) < ticks) { }
}

/* Delay that keeps the watchdog fed. Use instead of HAL_Delay everywhere. */
static void Delay_ms(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < ms) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}

static int32_t Clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static void Serial_Event(const char *msg)
{
    char b[48];
    int n = snprintf(b, sizeof(b), "#EVT %s\r\n", msg);
    if (n > 0) { Serial_Write(b, (uint16_t)((n < (int)sizeof(b)) ? n : (int)sizeof(b) - 1)); }
}

/* ========================================================================== */
/*  I2C: init, bus clear, recovery                                             */
/* ========================================================================== */

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = I2C_SPEED_HZ;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

/* Clock out up to 9 pulses so a slave that is holding SDA low can finish its
 * byte, then issue a STOP. Both pins are driven open-drain. */
static void I2C_BusClear(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    DWT_Delay_us(10);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); DWT_Delay_us(5);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   DWT_Delay_us(5);
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) { break; }
    }
    /* STOP: SDA low -> SCL high -> SDA high */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); DWT_Delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   DWT_Delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);   DWT_Delay_us(5);
}

/* Full recovery, rate-limited to once per 2 s. */
static void I2C_Recover(void)
{
    uint32_t now = HAL_GetTick();
    if (last_i2c_recover != 0 && (now - last_i2c_recover) < 2000U) { return; }
    last_i2c_recover = now ? now : 1;

    HAL_I2C_DeInit(&hi2c1);
    I2C_BusClear();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    MX_I2C1_Init();
}

static bool I2C_Probe(uint8_t addr7)
{
    return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 2, 10) == HAL_OK;
}

/* ========================================================================== */
/*  INA219                                                                     */
/* ========================================================================== */

static bool INA_WriteReg(uint8_t addr7, uint8_t reg, uint16_t val)
{
    uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr7 << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, b, 2, 20) == HAL_OK;
}

static bool INA_ReadReg(uint8_t addr7, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr7 << 1), reg,
                         I2C_MEMADD_SIZE_8BIT, b, 2, 20) != HAL_OK) {
        return false;
    }
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

static bool INA_Init(Ina219_t *d)
{
    d->fail = 0;
    return INA_WriteReg(d->addr7, 0x00, INA_CONFIG);
}

static bool INA_Read(Ina219_t *d)
{
    uint16_t rbus, rshunt;
    if (!INA_ReadReg(d->addr7, 0x02, &rbus))   { return false; }
    if (!INA_ReadReg(d->addr7, 0x01, &rshunt)) { return false; }

    d->mv = (uint32_t)(rbus >> 3) * 4U;                       /* 4 mV / LSB   */

    int32_t s = (int16_t)rshunt;                              /* 10 uV / LSB  */
    int32_t i10 = (s * 100) / INA_R_SHUNT_MOHM;               /* 0.1 mA units */
    d->ma10 = (i10 < 0) ? 0 : i10;

    d->mw = (uint32_t)(((uint64_t)d->mv * (uint32_t)d->ma10) / 10000ULL);
    return true;
}

static void Ina_Clear(Ina219_t *d)
{
    d->mv = 0; d->ma10 = 0; d->mw = 0;
}

static void Ina_Poll(Ina219_t *d)
{
    if (!d->online) { Ina_Clear(d); return; }
    if (INA_Read(d)) {
        d->fail = 0;
    } else if (++d->fail >= 3) {
        d->online = false;
        Ina_Clear(d);
        I2C_Recover();
    }
}

/* ========================================================================== */
/*  Serial transport (USART1, blocking TX, polled RX)                          */
/* ========================================================================== */

#ifndef SERIAL_RXNE      /* overridden by the host tests */
#define SERIAL_RXNE()   __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)
#define SERIAL_READ()   ((uint8_t)(huart1.Instance->DR & 0xFFU))
#endif

static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = SERIAL_BAUD;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

/* TX never blocks on the receiver: a 200-byte line takes ~18 ms at 115200 and
 * is shifted out whether or not a laptop is connected. */
static void Serial_Write(const char *b, uint16_t n)
{
    HAL_IWDG_Refresh(&hiwdg);
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)(uintptr_t)b, n, 100);
    HAL_IWDG_Refresh(&hiwdg);
}

static bool Serial_ReadByte(uint8_t *c)
{
    if (!SERIAL_RXNE()) { return false; }
    *c = SERIAL_READ();         /* reading DR also clears RXNE and any ORE/FE */
    return true;
}

/* ========================================================================== */
/*  LDR sampling (2 channels: Left=CH0, Right=CH1)                            */
/* ========================================================================== */

static const uint32_t ADC_CH[2] = { ADC_CHANNEL_0, ADC_CHANNEL_1 };

/* Interleaved oversampling over `rounds` passes, one pass per `period_us`.
 * rounds × period_us = one mains cycle -> nulls 50/60 Hz flicker.           */
static void Sample_LDRs(uint16_t out[2], uint32_t rounds, uint32_t period_us)
{
    uint32_t acc[2] = {0}, n[2] = {0};
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    uint32_t ticks = period_us * (SystemCoreClock / 1000000UL);

    for (uint32_t r = 0; r < rounds; r++) {
        uint32_t t0 = DWT->CYCCNT;
        for (int ch = 0; ch < 2; ch++) {
            cfg.Channel = ADC_CH[ch];
            if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) { continue; }
            HAL_ADC_Start(&hadc1);
            if (HAL_ADC_PollForConversion(&hadc1, 2) == HAL_OK) {
                acc[ch] += HAL_ADC_GetValue(&hadc1);
                n[ch]++;
            }
            HAL_ADC_Stop(&hadc1);
        }
        while ((DWT->CYCCNT - t0) < ticks) { }
    }
    for (int ch = 0; ch < 2; ch++) {
        out[ch] = n[ch] ? (uint16_t)(acc[ch] / n[ch]) : 0;
    }
}

static inline uint32_t Cal_Apply(uint32_t raw, int i)
{
    return (raw * cal_q[i]) >> 12;
}

static void Read_LDRs(uint16_t raw[2], uint32_t c[2], uint32_t rounds, uint32_t period_us)
{
    Sample_LDRs(raw, rounds, period_us);
    for (int i = 0; i < 2; i++) { c[i] = Cal_Apply(raw[i], i); }
}

static uint32_t Read_Light_Sum(void)
{
    uint16_t raw[2]; uint32_t c[2];
    Read_LDRs(raw, c, LDR_ROUNDS, LDR_PERIOD_US);
    return c[0] + c[1];
}

/* One LDR reading ~0 while the other is lit means an open wire or shorted
 * divider resistor; tracking on that would just chase noise.                 */
static bool LDR_Fault(const uint16_t raw[2])
{
    uint16_t mx = (raw[0] > raw[1]) ? raw[0] : raw[1];
    uint16_t mn = (raw[0] < raw[1]) ? raw[0] : raw[1];
    return (mx > 1500 && mn < 30);
}

/* ========================================================================== */
/*  Calibration (persisted in flash)                                           */
/* ========================================================================== */

static void Cal_Defaults(void)
{
    cal_q[0] = 4096; cal_q[1] = 4096;
}

static void Cal_Load(void)
{
    uint32_t magic = CAL_WORD(0), w1 = CAL_WORD(1), chk = CAL_WORD(2);
    /* Pack: w1 low 16 = cal_q[0], w1 high 16 = cal_q[1] */
    bool ok = (magic == CAL_MAGIC) && (chk == (magic ^ w1 ^ 0xA5A5A5A5UL));
    uint32_t q[2] = { w1 & 0xFFFFU, w1 >> 16 };
    for (int i = 0; ok && i < 2; i++) {
        if (q[i] < CAL_Q_MIN || q[i] > CAL_Q_MAX) { ok = false; }
    }
    if (ok) { cal_q[0] = q[0]; cal_q[1] = q[1]; }
    else    { Cal_Defaults(); }
}

static bool Cal_Save(void)
{
    uint32_t w1 = (cal_q[0] & 0xFFFFU) | (cal_q[1] << 16);
    uint32_t words[3] = { CAL_MAGIC, w1, CAL_MAGIC ^ w1 ^ 0xA5A5A5A5UL };

    FLASH_EraseInitTypeDef e = {0};
    uint32_t page_error = 0;
    bool ok = true;

    HAL_IWDG_Refresh(&hiwdg);
    HAL_FLASH_Unlock();
    e.TypeErase   = FLASH_TYPEERASE_PAGES;
    e.PageAddress = CAL_FLASH_ADDR;
    e.NbPages     = 1;
    if (HAL_FLASHEx_Erase(&e, &page_error) != HAL_OK) { ok = false; }
    for (int i = 0; ok && i < 3; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              CAL_FLASH_ADDR + 4U * (uint32_t)i, words[i]) != HAL_OK) {
            ok = false;
        }
    }
    HAL_FLASH_Lock();
    HAL_IWDG_Refresh(&hiwdg);

    if (ok) {
        for (int i = 0; i < 3; i++) { if (CAL_WORD(i) != words[i]) { ok = false; } }
    }
    return ok;
}

/* Computes 2-channel gains from a short average under uniform light. */
static bool Cal_Compute(const uint16_t raw[2], uint32_t q_out[2])
{
    uint32_t sum = 0;
    for (int i = 0; i < 2; i++) {
        if (raw[i] < 200 || raw[i] > 4000) { return false; }
        sum += raw[i];
    }
    uint32_t avg = sum / 2U;
    for (int i = 0; i < 2; i++) {
        uint32_t q = (avg * 4096U) / raw[i];
        if (q < CAL_Q_MIN || q > CAL_Q_MAX) { return false; }
        q_out[i] = q;
    }
    return true;
}

static void Calibrate_Sensor_Offsets(void)
{
    uint16_t raw[2];
    uint32_t q[2];

    Sample_LDRs(raw, 400, 500);                 /* ~200 ms average */
    if (!Cal_Compute(raw, q)) {
        Serial_Event("CAL FAIL LIGHT (needs even, not dark, not saturated light)");
        return;
    }
    uint32_t old[2];
    for (int i = 0; i < 2; i++) { old[i] = cal_q[i]; cal_q[i] = q[i]; }
    if (Cal_Save()) {
        Serial_Event("CAL SAVED");
    } else {
        for (int i = 0; i < 2; i++) { cal_q[i] = old[i]; }
        Serial_Event("CAL FAIL FLASH");
    }
}

/* ========================================================================== */
/*  Servo (single channel: TIM3 CH1 = PA6)                                    */
/* ========================================================================== */

static void Servo_Boot_Center(void)
{
    pan_pulse = PAN_CENTER_PULSE;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    pwm_on = true;
    release_pending = false;
}

static void Servo_Move(uint16_t pan, uint32_t settle_ms, bool release)
{
    pan_pulse = (uint16_t)Clamp32((int32_t)pan, PAN_MIN_PULSE, PAN_MAX_PULSE);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
    if (!pwm_on) {
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
        pwm_on = true;
    }
    if (release) {
        release_at = HAL_GetTick() + settle_ms;
        release_pending = true;
    } else {
        release_pending = false;
    }
}

static void Servo_Task(uint32_t now)
{
    if (release_pending && (int32_t)(now - release_at) >= 0) {
        HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
        pwm_on = false;
        release_pending = false;
    }
}

static bool Release_After_Move(void)
{
    return (mode == MODE_FIELD) && FIELD_RELEASE_SERVOS;
}

/* ========================================================================== */
/*  Tracking                                                                   */
/* ========================================================================== */

/* Single axis controller. Returns true if the pulse width changed. */
static bool Axis_Update(Axis_t *a, int32_t err, uint16_t *pulse,
                        uint16_t lo, uint16_t hi, int dir, uint32_t fault_bit)
{
    int32_t ae = (err < 0) ? -err : err;
    a->at_limit = false;

    if (ae <= DEADBAND) {
        a->last_dir = 0; a->bad = 0; a->prev_abs = ae;
        return false;
    }

    int32_t d    = ((err > 0) ? 1 : -1) * dir;
    int32_t step = STEP_MIN_US + ae / STEP_DIV;
    if (step > STEP_MAX_US) { step = STEP_MAX_US; }

    int32_t np = Clamp32((int32_t)*pulse + d * step, lo, hi);
    if (np == (int32_t)*pulse) {
        a->at_limit = true;
        if (a->last_dir == d && a->bad >= RUNAWAY_LIMIT_STEPS) { faults |= fault_bit; }
        return false;
    }

    if (a->last_dir == d && ae >= a->prev_abs) {
        if (++a->bad >= RUNAWAY_STEPS) {
            faults |= fault_bit;
            return false;
        }
    } else {
        a->bad = 0;
    }
    a->last_dir = (int8_t)d;
    a->prev_abs = ae;
    *pulse = (uint16_t)np;
    return true;
}

static void Reset_Axes(void)
{
    memset(&ax_pan, 0, sizeof(ax_pan));
}

static void Run_Sun_Search(void)
{
    searching = true;
    Serial_SendTelemetry();          /* show st=SEARCH on the laptop */

    uint32_t best = 0;
    uint16_t best_pan = PAN_CENTER_PULSE;

    /* Single 1D sweep across the full azimuth range */
    for (uint16_t p = PAN_MIN_PULSE; p <= PAN_MAX_PULSE; p = (uint16_t)(p + SEARCH_PAN_STEP_US)) {
        Servo_Move(p, 0, false);
        Delay_ms(SEARCH_SETTLE_MS);
        uint32_t s = Read_Light_Sum();
        if (s > best) { best = s; best_pan = p; }
    }

    if (best < NIGHT_ENTER_ADC_SUM) {      /* nothing found: go to centre */
        best_pan = PAN_CENTER_PULSE;
    }
    Servo_Move(best_pan, 0, false);
    Delay_ms(500);
    Reset_Axes();
    searching = false;
}

static void Park(void)
{
    Servo_Move(PARK_PAN_PULSE, 1000, PARK_RELEASE_SERVO);
    is_parked = true;
    night_cnt = 0; day_cnt = 0;
    Reset_Axes();
}

static void Unpark(void)
{
    is_parked = false;
    night_cnt = 0; day_cnt = 0;
    Run_Sun_Search();
}

static void Tracker_Step(void)
{
    uint16_t raw[2]; uint32_t c[2];
    Read_LDRs(raw, c, LDR_ROUNDS, LDR_PERIOD_US);
    uint32_t sum = c[0] + c[1];
    last_sum = sum;

    if (is_parked) {
        if (sum >= NIGHT_EXIT_ADC_SUM) { if (++day_cnt >= DAY_CONFIRM_COUNT) { Unpark(); } }
        else                           { day_cnt = 0; }
        return;
    }
    if (sum < NIGHT_ENTER_ADC_SUM) {
        if (++night_cnt >= NIGHT_CONFIRM_COUNT) { Park(); }
        return;
    }
    night_cnt = 0;

    if (LDR_Fault(raw)) { faults |= FAULT_LDR; } else { faults &= ~FAULT_LDR; }
    if (faults) { return; }

    /* Normalized single-axis azimuth error:
     *   err > 0 -> sun is to the right  -> pan right
     *   err < 0 -> sun is to the left   -> pan left   */
    int32_t left  = (int32_t)c[0];
    int32_t right = (int32_t)c[1];
    err_az = ((right - left) * 1000) / (right + left + 1);

    bool moved = Axis_Update(&ax_pan, err_az, &pan_pulse,
                             PAN_MIN_PULSE, PAN_MAX_PULSE, PAN_DIR, FAULT_PAN_RUNAWAY);
    if (moved) {
        Servo_Move(pan_pulse,
                   (mode == MODE_FIELD) ? FIELD_SETTLE_MS : 0, Release_After_Move());
    }
}

/* ========================================================================== */
/*  Telemetry                                                                  */
/* ========================================================================== */

static void Telemetry_Task(uint32_t dt_ms)
{
    Ina_Poll(&ina_trk);
    Ina_Poll(&ina_fix);
    Ina_Poll(&ina_sys);
    if (dt_ms > 5000U) { dt_ms = 5000U; }

    if (ina_trk.online) {
        energy_trk += (uint64_t)ina_trk.mv * (uint32_t)ina_trk.ma10 * dt_ms;
    }
    if (ina_fix.online) {
        energy_fix += (uint64_t)ina_fix.mv * (uint32_t)ina_fix.ma10 * dt_ms;
    }
}

static bool Gain_Tenths(int32_t *out)
{
    if (!ina_trk.online || !ina_fix.online || ina_fix.mw < 50U) { return false; }
    int64_t r = ((int64_t)ina_trk.mw * K_MISMATCH_X1000) / (int64_t)ina_fix.mw;
    *out = (int32_t)(r - 1000);
    return true;
}

static void I2C_Maintenance(uint32_t now)
{
    static uint32_t last = 0;
    if ((now - last) < 5000U) { return; }
    last = now;

    Ina219_t *list[3] = { &ina_trk, &ina_sys, &ina_fix };
    for (int i = 0; i < 3; i++) {
        if (!list[i]->online && I2C_Probe(list[i]->addr7)) {
            list[i]->online = INA_Init(list[i]);
        }
    }
}

/* ========================================================================== */
/*  Laptop telemetry                                                           */
/*                                                                             */
/*  $HEL,key=value,...*HH\r\n   HH = XOR of every character between $ and *   */
/*  #...                        free text (banner, events), no checksum        */
/* ========================================================================== */

static const char *Status_Text(void)
{
    if (faults)    { return "FAULT"; }
    if (searching) { return "SEARCH"; }
    if (is_parked) { return "PARK"; }
    return "TRACK";
}

static uint8_t Ina_Mask(void)
{
    return (uint8_t)((ina_trk.online ? 1U : 0U) | (ina_sys.online ? 2U : 0U) |
                     (ina_fix.online ? 4U : 0U));
}

/* Appends "text" to buf at *n, never past size-1. Returns false when full. */
static bool Buf_Add(char *buf, size_t size, size_t *n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *n, size - *n, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= size - *n) { return false; }
    *n += (size_t)w;
    return true;
}

/* Builds one telemetry line (without CR/LF) into buf. Returns its length. */
static size_t Telemetry_Format(char *buf, size_t size)
{
    size_t n = 0;
    char gain[12];
    int32_t g;
    if (Gain_Tenths(&g)) { snprintf(gain, sizeof(gain), "%ld", (long)g); }
    else                 { snprintf(gain, sizeof(gain), "na"); }

    (void)Buf_Add(buf, size, &n,
        "$HEL,t=%lu,mode=%s,st=%s,flt=%lu,az=%ld,pan=%u,sum=%lu,lim=%u,ina=%u,"
        "tv=%lu,ti=%ld,tp=%lu,fv=%lu,fi=%ld,fp=%lu,sv=%lu,si=%ld,sp=%lu,"
        "gain=%s,et=%lu,ef=%lu",
        (unsigned long)(HAL_GetTick() / 1000U),
        (mode == MODE_DEMO) ? "DEMO" : "FIELD", Status_Text(),
        (unsigned long)faults, (long)err_az, (unsigned)pan_pulse,
        (unsigned long)last_sum, ax_pan.at_limit ? 1U : 0U, (unsigned)Ina_Mask(),
        (unsigned long)ina_trk.mv, (long)ina_trk.ma10, (unsigned long)ina_trk.mw,
        (unsigned long)ina_fix.mv, (long)ina_fix.ma10, (unsigned long)ina_fix.mw,
        (unsigned long)ina_sys.mv, (long)ina_sys.ma10, (unsigned long)ina_sys.mw,
        gain,
        (unsigned long)(energy_trk / 3600000000ULL),
        (unsigned long)(energy_fix / 3600000000ULL));

    uint8_t x = 0;
    for (size_t i = 1; i < n; i++) { x ^= (uint8_t)buf[i]; }
    (void)Buf_Add(buf, size, &n, "*%02X", (unsigned)x);
    return n;
}

static void Serial_SendTelemetry(void)
{
    char b[300];
    size_t n = Telemetry_Format(b, sizeof(b) - 2);
    b[n++] = '\r'; b[n++] = '\n';
    Serial_Write(b, (uint16_t)n);
}

static void Serial_Banner(void)
{
    char b[96];
    int n = snprintf(b, sizeof(b),
                     "\r\n#HELION single-axis solar tracker fw=%s baud=%lu\r\n",
                     HELION_FW_VERSION, (unsigned long)SERIAL_BAUD);
    if (n > 0) { Serial_Write(b, (uint16_t)n); }
    n = snprintf(b, sizeof(b), "#BOOT ina=%u wdg_reset=%u mode=%s\r\n",
                 (unsigned)Ina_Mask(), boot_wdg_reset ? 1U : 0U,
                 (mode == MODE_DEMO) ? "DEMO" : "FIELD");
    if (n > 0) { Serial_Write(b, (uint16_t)n); }
    last_ina_mask_sent = Ina_Mask();
}

/* Commands from the laptop, one per line: MODE, CAL, CLEAR, STATUS. */
static void Serial_Command(const char *cmd)
{
    if      (strcmp(cmd, "MODE") == 0)   { Toggle_Mode(); }
    else if (strcmp(cmd, "CLEAR") == 0)  { faults = 0; Reset_Axes(); force_track = true;
                                           Serial_Event("FAULTS CLEARED"); }
    else if (strcmp(cmd, "CAL") == 0)    { Calibrate_Sensor_Offsets(); }
    else if (strcmp(cmd, "STATUS") == 0) { Serial_SendTelemetry(); }
    else                                 { Serial_Event("UNKNOWN COMMAND"); }
}

static void Serial_RxTask(void)
{
    uint8_t c;
    for (int guard = 0; guard < 64 && Serial_ReadByte(&c); guard++) {
        if (c == '\r' || c == '\n') {
            if (rx_len > 0 && !rx_overflow) {
                rx_line[rx_len] = '\0';
                Serial_Command(rx_line);
            } else if (rx_overflow) {
                Serial_Event("UNKNOWN COMMAND");
            }
            rx_len = 0; rx_overflow = false;
        } else if (rx_len < sizeof(rx_line) - 1U) {
            rx_line[rx_len++] = (char)toupper((int)c);
        } else {
            rx_overflow = true;
        }
    }
}

/* Called every main-loop pass: sends event lines on change and one telemetry
 * line per SERIAL_INTERVAL_MS. */
static void Serial_Task(uint32_t now)
{
    static uint32_t last = 0;

    Serial_RxTask();

    if (faults != last_faults_sent) {
        if ((faults & FAULT_PAN_RUNAWAY) && !(last_faults_sent & FAULT_PAN_RUNAWAY)) {
            Serial_Event("FAULT PAN RUNAWAY (check PAN_DIR / horn / blocked panel)");
        }
        if ((faults & FAULT_LDR) && !(last_faults_sent & FAULT_LDR)) {
            Serial_Event("FAULT LDR (open wire or bad divider)");
        }
        if (!faults) { Serial_Event("FAULTS CLEARED"); }
        last_faults_sent = faults;
    }
    if (is_parked != last_parked_sent) {
        Serial_Event(is_parked ? "PARKED (dark)" : "UNPARKED (light)");
        last_parked_sent = is_parked;
    }
    if (Ina_Mask() != last_ina_mask_sent) {
        char b[40];
        snprintf(b, sizeof(b), "INA ONLINE MASK %u (7 = all)", (unsigned)Ina_Mask());
        Serial_Event(b);
        last_ina_mask_sent = Ina_Mask();
    }

    if ((now - last) >= SERIAL_INTERVAL_MS) {
        last = now;
        Serial_SendTelemetry();
    }
}

/* ========================================================================== */
/*  Button: debounced, non-blocking                                            */
/*    short press : toggle DEMO/FIELD and clear faults                         */
/*    hold > 2 s  : calibrate LDR gains                                        */
/*  (the same actions are available from the laptop: MODE, CAL, CLEAR)        */
/* ========================================================================== */

/* DEMO <-> FIELD, clears faults. Used by the button and the MODE command. */
static void Toggle_Mode(void)
{
    mode = (mode == MODE_DEMO) ? MODE_FIELD : MODE_DEMO;
    faults = 0;
    Reset_Axes();
    force_track = true;
    if (mode == MODE_FIELD && FIELD_RELEASE_SERVOS) {
        release_at = HAL_GetTick() + 1000U; release_pending = true;
    } else {
        release_pending = false;
    }
    Serial_Event((mode == MODE_DEMO) ? "MODE DEMO" : "MODE FIELD");
}

static void Button_Task(uint32_t now)
{
    static bool raw_prev = false, stable = false, long_done = false;
    static uint32_t last_change = 0, press_start = 0;

    bool pressed = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET);
    if (pressed != raw_prev) { raw_prev = pressed; last_change = now; }

    if ((now - last_change) >= 30U && pressed != stable) {
        stable = pressed;
        if (stable) {
            press_start = now; long_done = false;
        } else if (!long_done) {                          /* released: short press */
            Toggle_Mode();
        }
    }
    if (stable && !long_done && (now - press_start) >= 2000U) {
        long_done = true;
        Calibrate_Sensor_Offsets();
    }
}

/* ========================================================================== */
/*  Optional servo sweep test (SERVO_SWEEP_TEST = 1)                           */
/* ========================================================================== */

#if SERVO_SWEEP_TEST
static void Servo_Sweep_Test(void)
{
    char l[48];
    Serial_Event("SERVO SWEEP TEST - press the button to stop");
    for (uint16_t half = 100; half <= 600; half = (uint16_t)(half + 50)) {
        for (int dir = 0; dir < 2; dir++) {
            uint16_t p = dir ? (uint16_t)(1500 + half) : (uint16_t)(1500 - half);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, p);
            snprintf(l, sizeof(l), "SWEEP PULSE %u US", (unsigned)p);
            Serial_Event(l);
            for (int i = 0; i < 12; i++) {
                Delay_ms(100);
                if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET) {
                    Serial_Event("SWEEP STOPPED");
                    for (;;) { HAL_IWDG_Refresh(&hiwdg); }
                }
            }
        }
    }
}
#endif

/* ========================================================================== */
/*  Peripheral init                                                            */
/* ========================================================================== */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
}

static void MX_ADC1_Init(void)
{
    hadc1.Instance                   = ADC1;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM3_Init(void)
{
    TIM_OC_InitTypeDef oc = {0};
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 71;           /* 72 MHz / 72 = 1 MHz -> 1 us tick   */
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 19999;        /* 20 ms frame = 50 Hz                */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) { Error_Handler(); }

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = PAN_CENTER_PULSE;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    /* PA6 — single pan servo */
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }
    /* New pulse width takes effect at the next frame: no runt pulse mid-period */
    __HAL_TIM_ENABLE_OCxPRELOAD(&htim3, TIM_CHANNEL_1);
}

/* ---- MSP callbacks ---- */

void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance == ADC1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* Only PA0 (Left LDR) and PA1 (Right LDR) — PA2 & PA3 removed */
        g.Pin  = GPIO_PIN_0 | GPIO_PIN_1;
        g.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *h)
{
    if (h->Instance == TIM3) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* Only PA6 (Pan servo) — PA7 (Tilt servo) removed */
        g.Pin   = GPIO_PIN_6;
        g.Mode  = GPIO_MODE_AF_PP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *h)
{
    if (h->Instance == I2C1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C1_CLK_ENABLE();
        g.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
        g.Mode  = GPIO_MODE_AF_OD;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g);
    }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *h)
{
    if (h->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    if (h->Instance == USART1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin   = GPIO_PIN_9;                  /* TX -> adapter RX */
        g.Mode  = GPIO_MODE_AF_PP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &g);
        g.Pin   = GPIO_PIN_10;                 /* RX <- adapter TX; pull-up keeps  */
        g.Mode  = GPIO_MODE_INPUT;             /* the idle level high when the     */
        g.Pull  = GPIO_PULLUP;                 /* adapter is unplugged             */
        HAL_GPIO_Init(GPIOA, &g);
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef pclk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;                 /* 8 MHz x 9 = 72 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

    pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    pclk.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) { Error_Handler(); }
}

/* Init failure (e.g. HSE did not start). Wait a couple of seconds, then reset
 * and try again: an unattended unit must not sit dead after a transient
 * start-up problem. The pause stops a permanent fault from reset-looping at
 * full speed. (Clocks may be unusable here, so this is a plain busy loop.) */
void Error_Handler(void)
{
    __disable_irq();
    for (volatile uint32_t d = 0; d < 8000000UL; d++) { }
    NVIC_SystemReset();
}

/* ========================================================================== */
/*  Entry point                                                                */
/* ========================================================================== */

#ifndef HELION_UNIT_TEST
int main(void)
{
    HAL_Init();
    boot_wdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET);
    __HAL_RCC_CLEAR_RESET_FLAGS();

    SystemClock_Config();
    DWT_Init();
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_TIM3_Init();
    HAL_ADCEx_Calibration_Start(&hadc1);
    Cal_Load();

    MX_USART1_UART_Init();

    if (HAVE_INA219) {
        HAL_Delay(100);             /* let the INA219s finish power-up */
        I2C_BusClear();
        MX_I2C1_Init();

        Ina219_t *list[3] = { &ina_trk, &ina_sys, &ina_fix };
        for (int i = 0; i < 3; i++) {
            list[i]->online = I2C_Probe(list[i]->addr7) && INA_Init(list[i]);
        }
    }

#if SERVO_SWEEP_TEST
    __HAL_DBGMCU_FREEZE_IWDG();
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64; hiwdg.Init.Reload = 2500;
    HAL_IWDG_Init(&hiwdg);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    Servo_Sweep_Test();
    for (;;) { HAL_IWDG_Refresh(&hiwdg); }
#endif

    __HAL_DBGMCU_FREEZE_IWDG();
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload    = 2500;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) { Error_Handler(); }

    Serial_Banner();
    Servo_Boot_Center();
    Delay_ms(600);

    /* Dark at power-up -> park; otherwise locate the sun. */
    uint16_t raw0[2]; uint32_t c0[2];
    Read_LDRs(raw0, c0, LDR_ROUNDS, LDR_PERIOD_US);
    last_sum = c0[0] + c0[1];
    if (last_sum < NIGHT_EXIT_ADC_SUM) { Park(); }
    else                               { Run_Sun_Search(); }

    last_parked_sent = is_parked;

    uint32_t t_track = HAL_GetTick();
    uint32_t t_telem = HAL_GetTick();

    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        uint32_t now = HAL_GetTick();

        Button_Task(now);
        Servo_Task(now);

        uint32_t interval = (mode == MODE_DEMO) ? DEMO_INTERVAL_MS : FIELD_INTERVAL_MS;
        if (force_track || (now - t_track) >= interval) {
            force_track = false;
            t_track = now;
            Tracker_Step();
        }

        if ((now - t_telem) >= 500U) {
            if (HAVE_INA219) { Telemetry_Task(now - t_telem); }
            t_telem = now;
        }

        if (HAVE_INA219) { I2C_Maintenance(now); }
        Serial_Task(now);
    }
}
#endif /* HELION_UNIT_TEST */
