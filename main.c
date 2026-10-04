/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Helion dual-axis solar tracker - corrected firmware (rev 2)
  * @target  STM32F103C8T6 "Blue Pill", HSE 8 MHz -> PLL -> 72 MHz, HAL F1
  *
  * Fixes versus the firmware printed in solar_tracker_hardware_manual.md
  * (numbers refer to hardware_audit_report.md where the audit was right):
  *
  *  - Audit 1  : system INA219 read no longer aliases three fields.
  *  - Audit 2  : HAL MSP callbacks added (clocks + pin modes for ADC/TIM/I2C).
  *  - Audit 3  : Error_Handler() added.
  *  - Audit 4  : real OLED framebuffer + 5x7 text + 8-line dashboard.
  *  - Audit 7  : DWT cycle-counter timing instead of a spin-loop.
  *  - Audit 8  : thresholds renamed *_ADC_SUM, re-based, with confirm counters.
  *  - Audit 9  : servo release/hold is explicit and configurable.
  *  - Audit 11 : button is a non-blocking debounced state machine.
  *  - Audit 15 : calibration is stored in the last flash page.
  *  - Audit 16 : telemetry timer initialised before the loop.
  *  - Audit 19 : IWDG watchdog (4 s) + reset-cause detection.
  *  - Audit 22 : I2C bus clear + peripheral reset + re-init on failure.
  *
  *  Audit items that were NOT changed because they were wrong:
  *  - Audit 5 : 0x399F is the INA219 power-on default and already means
  *              BRNG(bit13)=1 -> 32 V, PG(bits12:11)=11b -> +/-320 mV,
  *              BADC/SADC = 0011b -> 12-bit, MODE = 111b -> continuous.
  *              The audit's "fix" 0x3F9F sets BADC = 1111b, i.e. 128-sample
  *              averaging (68 ms per bus reading), which is worse.
  *  - Audit 17: no overflow exists; bus voltage is handled as uint16.
  *
  *  New protections:
  *  - Per-axis runaway detector (catches wrong PAN_DIR / TILT_DIR, stuck gear).
  *  - Open / shorted LDR detector (holds position instead of chasing noise).
  *  - Calibration sanity checks (rejects dark / saturated / unequal lighting).
  *  - Staggered servo start-up (no simultaneous inrush at power-on).
  *  - Sun search sweep at boot and at dawn (baffle FOV is narrow).
  *  - Tracking never depends on I2C: a dead sensor bus cannot stop the tracker.
  *  - Integer-only telemetry maths (works with newlib-nano, no %f needed).
  *
  *  Pin map (unchanged):
  *    PA0..PA3 ADC LDR TL,TR,BL,BR   PA4 button (to GND)   PA6/PA7 TIM3 CH1/CH2
  *    PB6 SCL / PB7 SDA              PC13 LED (active low)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================== */
/*  Build-time configuration                                                   */
/* ========================================================================== */

#define MAINS_HZ                50      /* 50 or 60: sets ADC averaging window   */

/* Direction: flip to -1 if an axis runs away from the light */
#define PAN_DIR                 (+1)
#define TILT_DIR                (+1)

/* Servo limits (us). Start conservative; widen only after running the sweep
 * test (set SERVO_SWEEP_TEST to 1) and finding where YOUR servos bind.       */
#define PAN_MIN_PULSE           900
#define PAN_MAX_PULSE           2100
#define TILT_MIN_PULSE          1000
#define TILT_MAX_PULSE          2000
#define PAN_CENTER_PULSE        1500
#define TILT_CENTER_PULSE       1500
#define PARK_PAN_PULSE          2000    /* facing east for sunrise            */
#define PARK_TILT_PULSE         1200    /* low, near flat                     */

/* Tracking */
#define DEADBAND                40      /* 40 = 4.0 % normalised difference   */
#define STEP_MIN_US             5
#define STEP_MAX_US             30
#define STEP_DIV                20      /* step = MIN + |err|/DIV, clamped    */
#define RUNAWAY_STEPS           40      /* same-direction steps w/o improvement */
#define RUNAWAY_LIMIT_STEPS     12      /* ...or this many, if the travel limit is then reached */
#define DEMO_INTERVAL_MS        300
#define FIELD_INTERVAL_MS       30000UL
#define FIELD_SETTLE_MS         400
/* 0 = keep PWM on in field mode (holds panel against wind, ~10 mA/servo)
 * 1 = stop PWM between moves (saves power, panel must be perfectly balanced) */
#define FIELD_RELEASE_SERVOS    0

/* Night park. Values are SUMS of four 12-bit ADC readings (0..16380), not lux.
 * Tune on your own divider: log `last_sum` at dusk and pick the thresholds.  */
#define NIGHT_ENTER_ADC_SUM     600
#define NIGHT_EXIT_ADC_SUM      1400
#define NIGHT_CONFIRM_COUNT     5       /* consecutive tracker ticks           */
#define DAY_CONFIRM_COUNT       3

/* Sun search */
#define SEARCH_ROWS             3
#define SEARCH_PAN_STEP_US      100
#define SEARCH_SETTLE_MS        150

/* Benchmarking */
#define K_MISMATCH_X1000        1000    /* manual 9.1: P_fixed_base/P_tracked_base x1000 */

/* INA219 */
#define INA_R_SHUNT_MOHM        100     /* module shunt, milli-ohm            */
#define INA_CONFIG              0x399F  /* 32 V, +/-320 mV, 12-bit, continuous (power-on default) */

/* I2C */
#define I2C_SPEED_HZ            100000  /* 400000 is fine with short wires    */

/* Bring-up helper: 1 = firmware only sweeps the servos and shows the pulse
 * width on the OLED (press button to stop). Use it to find safe limits.      */
#define SERVO_SWEEP_TEST        0

/* ========================================================================== */
/*  Derived constants                                                          */
/* ========================================================================== */

#define LDR_ROUNDS              40U
#define LDR_PERIOD_US           (1000000UL / MAINS_HZ / LDR_ROUNDS)

#define OLED_ADDR7              0x3C
#define INA_TRK_ADDR7           0x40
#define INA_SYS_ADDR7           0x41
#define INA_FIX_ADDR7           0x44

#define CAL_FLASH_ADDR          0x0800FC00UL   /* last 1 KB page of a 64 KB part */
#define CAL_MAGIC               0x48454C31UL   /* "HEL1" */
#define CAL_Q_MIN               2048U          /* 0.5 in Q12 */
#define CAL_Q_MAX               8192U          /* 2.0 in Q12 */

#define FAULT_PAN_RUNAWAY       (1U << 0)
#define FAULT_TILT_RUNAWAY      (1U << 1)
#define FAULT_LDR               (1U << 2)

/* ========================================================================== */
/*  Types and globals                                                          */
/* ========================================================================== */

typedef enum { MODE_DEMO = 0, MODE_FIELD = 1 } TrackerMode;

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
IWDG_HandleTypeDef hiwdg = { .Instance = IWDG };

static Ina219_t ina_trk = { .addr7 = INA_TRK_ADDR7 };
static Ina219_t ina_sys = { .addr7 = INA_SYS_ADDR7 };
static Ina219_t ina_fix = { .addr7 = INA_FIX_ADDR7 };
static bool     oled_online = false;

static uint16_t pan_pulse  = PAN_CENTER_PULSE;
static uint16_t tilt_pulse = TILT_CENTER_PULSE;
static bool     pwm_on = false;
static bool     release_pending = false;
static uint32_t release_at = 0;

static uint32_t cal_q[4] = { 4096, 4096, 4096, 4096 };   /* Q12 gains */

static TrackerMode mode = MODE_DEMO;
static bool     is_parked = false;
static bool     searching = false;
static bool     force_track = false;
static uint32_t faults = 0;
static uint8_t  night_cnt = 0, day_cnt = 0;
static Axis_t   ax_pan, ax_tilt;
static int32_t  err_az = 0, err_el = 0;
static uint32_t last_sum = 0;

static uint64_t charge_trk = 0, charge_fix = 0;   /* 0.1 mA * ms        */
static uint64_t energy_trk = 0, energy_fix = 0;   /* mV * 0.1 mA * ms   */

static char     ui_msg[22] = "";
static uint32_t ui_msg_until = 0;
static uint32_t last_i2c_recover = 0;
static bool     boot_wdg_reset = false;

static uint8_t  fb[8 * 128];   /* SSD1306 framebuffer */

/* ========================================================================== */
/*  Prototypes                                                                 */
/* ========================================================================== */

void SystemClock_Config(void);
void Error_Handler(void);
static void Display_Task(uint32_t now, bool force);

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

static void Set_UI_Message(const char *msg, uint32_t duration_ms)
{
    strncpy(ui_msg, msg, sizeof(ui_msg) - 1);
    ui_msg[sizeof(ui_msg) - 1] = '\0';
    ui_msg_until = HAL_GetTick() + duration_ms;
}

static void LED_Set(bool on)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void LED_Blink(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t i = 0; i < count; i++) {
        LED_Set(true);  Delay_ms(on_ms);
        LED_Set(false); Delay_ms(off_ms);
    }
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

/* Full recovery, rate-limited to once per 2 s so a missing device cannot
 * make the firmware thrash the bus. */
static void I2C_Recover(void)
{
    uint32_t now = HAL_GetTick();
    if (last_i2c_recover != 0 && (now - last_i2c_recover) < 2000U) { return; }
    last_i2c_recover = now ? now : 1;

    HAL_I2C_DeInit(&hi2c1);
    I2C_BusClear();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();      /* clears the F1 "BUSY stuck" erratum */
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

/* Current and voltage come straight from the shunt/bus registers, so the
 * calibration register (0x05) is deliberately not used. */
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
    d->ma10 = (i10 < 0) ? 0 : i10;                            /* ignore noise < 0 */

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
/*  SSD1306 (framebuffer + 5x7 font)                                           */
/* ========================================================================== */

/* ASCII 0x20..0x5A (space..Z). Lower case is mapped to upper case. */
static const uint8_t font5x7[59][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x56,0x20,0x50}, {0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x2A,0x1C,0x7F,0x1C,0x2A}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x00,0x60,0x60,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x72,0x49,0x49,0x49,0x46}, {0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x31}, {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36}, {0x46,0x49,0x49,0x29,0x1E}, {0x00,0x00,0x14,0x00,0x00}, {0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E}, {0x7C,0x12,0x11,0x12,0x7C}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x1C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03}, {0x61,0x59,0x49,0x4D,0x43}
};

static bool OLED_Cmd(uint8_t c)
{
    return HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(OLED_ADDR7 << 1), 0x00,
                             I2C_MEMADD_SIZE_8BIT, &c, 1, 20) == HAL_OK;
}

static bool OLED_Init(void)
{
    static const uint8_t seq[] = {
        0xAE, 0xD5,0x80, 0xA8,0x3F, 0xD3,0x00, 0x40, 0x8D,0x14,
        0x20,0x00,                 /* horizontal addressing mode */
        0xA1, 0xC8, 0xDA,0x12, 0x81,0xCF, 0xD9,0xF1, 0xDB,0x40,
        0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(seq); i++) {
        if (!OLED_Cmd(seq[i])) { return false; }
    }
    return true;
}

static void OLED_Clear(void)
{
    memset(fb, 0, sizeof(fb));
}

static void OLED_Putc(uint8_t page, uint8_t x, char ch)
{
    int c = toupper((unsigned char)ch);
    if (c < 0x20 || c > 0x5A) { c = '?'; }
    const uint8_t *g = font5x7[c - 0x20];
    uint8_t *p = &fb[page * 128 + x];
    for (int i = 0; i < 5; i++) { p[i] = g[i]; }
    p[5] = 0x00;
}

static void OLED_Print(uint8_t page, const char *s)
{
    if (page > 7) { return; }
    uint8_t x = 0;
    while (*s && x <= 122) {
        OLED_Putc(page, x, *s++);
        x += 6;
    }
}

static bool OLED_Flush(void)
{
    if (!OLED_Cmd(0x21) || !OLED_Cmd(0x00) || !OLED_Cmd(0x7F)) { return false; }
    if (!OLED_Cmd(0x22) || !OLED_Cmd(0x00) || !OLED_Cmd(0x07)) { return false; }
    for (uint8_t page = 0; page < 8; page++) {
        if (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(OLED_ADDR7 << 1), 0x40,
                              I2C_MEMADD_SIZE_8BIT, &fb[page * 128], 128, 50) != HAL_OK) {
            return false;
        }
    }
    return true;
}

/* ========================================================================== */
/*  LDR sampling                                                               */
/* ========================================================================== */

static const uint32_t ADC_CH[4] = { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3 };

/* Interleaved oversampling: `rounds` passes over TL,TR,BL,BR, one pass every
 * `period_us`. rounds * period = one mains cycle, which nulls 50/60 Hz flicker.
 * The LDR dividers share the same 3.3 V rail as the ADC reference, so the
 * readings are ratiometric and rail noise largely cancels. */
static void Sample_LDRs(uint16_t out[4], uint32_t rounds, uint32_t period_us)
{
    uint32_t acc[4] = {0}, n[4] = {0};
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;   /* ~21 us: fine for <=50 k source */
    uint32_t ticks = period_us * (SystemCoreClock / 1000000UL);

    for (uint32_t r = 0; r < rounds; r++) {
        uint32_t t0 = DWT->CYCCNT;
        for (int ch = 0; ch < 4; ch++) {
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
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = n[ch] ? (uint16_t)(acc[ch] / n[ch]) : 0;
    }
}

static inline uint32_t Cal_Apply(uint32_t raw, int i)
{
    return (raw * cal_q[i]) >> 12;
}

static void Read_LDRs(uint16_t raw[4], uint32_t c[4], uint32_t rounds, uint32_t period_us)
{
    Sample_LDRs(raw, rounds, period_us);
    for (int i = 0; i < 4; i++) { c[i] = Cal_Apply(raw[i], i); }
}

static uint32_t Read_Light_Sum_Fast(void)
{
    uint16_t raw[4]; uint32_t c[4];
    Read_LDRs(raw, c, 8, 200);
    return c[0] + c[1] + c[2] + c[3];
}

/* One LDR reading ~0 while another is clearly lit means an open wire or a
 * shorted divider resistor; tracking on that would just chase noise. */
static bool LDR_Fault(const uint16_t raw[4])
{
    uint16_t mx = 0, mn = 4095;
    for (int i = 0; i < 4; i++) {
        if (raw[i] > mx) { mx = raw[i]; }
        if (raw[i] < mn) { mn = raw[i]; }
    }
    return (mx > 1500 && mn < 30);
}

/* ========================================================================== */
/*  Calibration (persisted in flash)                                           */
/* ========================================================================== */

static void Cal_Defaults(void)
{
    for (int i = 0; i < 4; i++) { cal_q[i] = 4096; }
}

static void Cal_Load(void)
{
    const uint32_t *p = (const uint32_t *)CAL_FLASH_ADDR;
    uint32_t magic = p[0], w1 = p[1], w2 = p[2], chk = p[3];
    bool ok = (magic == CAL_MAGIC) && (chk == (magic ^ w1 ^ w2 ^ 0xA5A5A5A5UL));
    uint32_t q[4] = { w1 & 0xFFFFU, w1 >> 16, w2 & 0xFFFFU, w2 >> 16 };
    for (int i = 0; ok && i < 4; i++) {
        if (q[i] < CAL_Q_MIN || q[i] > CAL_Q_MAX) { ok = false; }
    }
    if (ok) { for (int i = 0; i < 4; i++) { cal_q[i] = q[i]; } }
    else    { Cal_Defaults(); }
}

static bool Cal_Save(void)
{
    uint32_t w1 = (cal_q[0] & 0xFFFFU) | (cal_q[1] << 16);
    uint32_t w2 = (cal_q[2] & 0xFFFFU) | (cal_q[3] << 16);
    uint32_t words[4] = { CAL_MAGIC, w1, w2, CAL_MAGIC ^ w1 ^ w2 ^ 0xA5A5A5A5UL };

    FLASH_EraseInitTypeDef e = {0};
    uint32_t page_error = 0;
    bool ok = true;

    HAL_IWDG_Refresh(&hiwdg);
    HAL_FLASH_Unlock();
    e.TypeErase   = FLASH_TYPEERASE_PAGES;
    e.PageAddress = CAL_FLASH_ADDR;
    e.NbPages     = 1;
    if (HAL_FLASHEx_Erase(&e, &page_error) != HAL_OK) { ok = false; }
    for (int i = 0; ok && i < 4; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 4U * (uint32_t)i, words[i]) != HAL_OK) {
            ok = false;
        }
    }
    HAL_FLASH_Lock();
    HAL_IWDG_Refresh(&hiwdg);

    if (ok) {   /* read back */
        const uint32_t *p = (const uint32_t *)CAL_FLASH_ADDR;
        for (int i = 0; i < 4; i++) { if (p[i] != words[i]) { ok = false; } }
    }
    return ok;
}

/* Computes gains from a short average of all four LDRs under uniform light.
 * Pure function so it can be unit-tested. Returns false if input is unusable. */
static bool Cal_Compute(const uint16_t raw[4], uint32_t q_out[4])
{
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        if (raw[i] < 200 || raw[i] > 4000) { return false; }   /* dark or saturated */
        sum += raw[i];
    }
    uint32_t avg = sum / 4U;
    for (int i = 0; i < 4; i++) {
        uint32_t q = (avg * 4096U) / raw[i];
        if (q < CAL_Q_MIN || q > CAL_Q_MAX) { return false; }  /* too unequal */
        q_out[i] = q;
    }
    return true;
}

static void Calibrate_Sensor_Offsets(void)
{
    uint16_t raw[4];
    uint32_t q[4];

    LED_Set(true);
    Sample_LDRs(raw, 400, 500);                 /* ~200 ms average */
    if (!Cal_Compute(raw, q)) {
        Set_UI_Message("CAL FAIL: LIGHT", 4000);
        LED_Blink(6, 80, 80);
        return;
    }
    uint32_t old[4];
    for (int i = 0; i < 4; i++) { old[i] = cal_q[i]; cal_q[i] = q[i]; }
    if (Cal_Save()) {
        Set_UI_Message("CAL SAVED", 3000);
        LED_Blink(2, 300, 300);
    } else {
        for (int i = 0; i < 4; i++) { cal_q[i] = old[i]; }
        Set_UI_Message("CAL FAIL: FLASH", 4000);
        LED_Blink(6, 80, 80);
    }
}

/* ========================================================================== */
/*  Servos                                                                     */
/* ========================================================================== */

static void Servo_Boot_Center(void)
{
    /* Stagger the two servos so their start-up surges do not add up. */
    pan_pulse  = PAN_CENTER_PULSE;
    tilt_pulse = TILT_CENTER_PULSE;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    Delay_ms(300);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, tilt_pulse);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    pwm_on = true;
    release_pending = false;
}

static void Servo_Move(uint16_t pan, uint16_t tilt, uint32_t settle_ms, bool release)
{
    pan_pulse  = (uint16_t)Clamp32(pan,  PAN_MIN_PULSE,  PAN_MAX_PULSE);
    tilt_pulse = (uint16_t)Clamp32(tilt, TILT_MIN_PULSE, TILT_MAX_PULSE);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, tilt_pulse);
    if (!pwm_on) {
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
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
        HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
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

/* One axis of the controller. Returns true if the pulse width changed. */
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
        /* Already at the end of travel. If the approach was improving the error,
         * the sun has simply left the mechanical range: report LIMIT, no fault.
         * If the approach was NOT improving, the axis was driven the wrong way
         * (bad PAN_DIR/TILT_DIR, slipped horn, blocked panel): raise the fault. */
        a->at_limit = true;
        if (a->last_dir == d && a->bad >= RUNAWAY_LIMIT_STEPS) { faults |= fault_bit; }
        return false;
    }

    /* Moving the same way again without the error shrinking = wrong direction
     * constant, slipped gear or blocked panel. */
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
    memset(&ax_tilt, 0, sizeof(ax_tilt));
}

static void Run_Sun_Search(void)
{
    searching = true;
    Display_Task(HAL_GetTick(), true);

    uint32_t best = 0;
    uint16_t best_pan = PAN_CENTER_PULSE, best_tilt = TILT_CENTER_PULSE;

    for (int row = 0; row < SEARCH_ROWS; row++) {
        uint16_t t = (uint16_t)(TILT_MIN_PULSE +
                     ((TILT_MAX_PULSE - TILT_MIN_PULSE) * (row + 1)) / (SEARCH_ROWS + 1));
        Servo_Move(PAN_MIN_PULSE, t, 0, false);
        Delay_ms(500);
        for (uint16_t p = PAN_MIN_PULSE; p <= PAN_MAX_PULSE; p = (uint16_t)(p + SEARCH_PAN_STEP_US)) {
            Servo_Move(p, t, 0, false);
            Delay_ms(SEARCH_SETTLE_MS);
            uint32_t s = Read_Light_Sum_Fast();
            if (s > best) { best = s; best_pan = p; best_tilt = t; }
        }
    }
    if (best < NIGHT_ENTER_ADC_SUM) {             /* nothing found: go to centre */
        best_pan = PAN_CENTER_PULSE; best_tilt = TILT_CENTER_PULSE;
    }
    Servo_Move(best_pan, best_tilt, 0, false);
    Delay_ms(500);
    Reset_Axes();
    searching = false;
}

static void Park(void)
{
    Servo_Move(PARK_PAN_PULSE, PARK_TILT_PULSE, 1000, true);   /* released after 1 s */
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
    uint16_t raw[4]; uint32_t c[4];
    Read_LDRs(raw, c, LDR_ROUNDS, LDR_PERIOD_US);
    uint32_t sum = c[0] + c[1] + c[2] + c[3];
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
    if (faults) { return; }                       /* hold position until cleared */

    int32_t left = (int32_t)(c[0] + c[2]), right  = (int32_t)(c[1] + c[3]);
    int32_t top  = (int32_t)(c[0] + c[1]), bottom = (int32_t)(c[2] + c[3]);
    err_az = ((left - right) * 1000) / (left + right + 1);
    err_el = ((top - bottom) * 1000) / (top + bottom + 1);

    bool m1 = Axis_Update(&ax_pan,  err_az, &pan_pulse,  PAN_MIN_PULSE,  PAN_MAX_PULSE,  PAN_DIR,  FAULT_PAN_RUNAWAY);
    bool m2 = Axis_Update(&ax_tilt, err_el, &tilt_pulse, TILT_MIN_PULSE, TILT_MAX_PULSE, TILT_DIR, FAULT_TILT_RUNAWAY);
    if (m1 || m2) {
        Servo_Move(pan_pulse, tilt_pulse,
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
    if (dt_ms > 5000U) { dt_ms = 5000U; }         /* rectangle rule, cap after long blocking ops */

    if (ina_trk.online) {
        charge_trk += (uint64_t)(uint32_t)ina_trk.ma10 * dt_ms;
        energy_trk += (uint64_t)ina_trk.mv * (uint32_t)ina_trk.ma10 * dt_ms;
    }
    if (ina_fix.online) {
        charge_fix += (uint64_t)(uint32_t)ina_fix.ma10 * dt_ms;
        energy_fix += (uint64_t)ina_fix.mv * (uint32_t)ina_fix.ma10 * dt_ms;
    }
}

/* tenths of a percent: tracked*K / fixed - 1 */
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
    if (!oled_online && I2C_Probe(OLED_ADDR7)) { oled_online = OLED_Init(); }
}

static uint8_t Missing_Devices(void)
{
    return (uint8_t)(!ina_trk.online + !ina_sys.online + !ina_fix.online + !oled_online);
}

/* ========================================================================== */
/*  Dashboard                                                                  */
/* ========================================================================== */

static const char *Status_Text(void)
{
    if (faults)    { return "FAULT"; }
    if (searching) { return "SEARCH"; }
    if (is_parked) { return "PARK"; }
    return "TRACK";
}

static void Display_Task(uint32_t now, bool force)
{
    static uint32_t last = 0;
    if (!oled_online) { return; }
    if (!force && (now - last) < 1000U) { return; }
    last = now;

    char l[48];
    OLED_Clear();

    snprintf(l, sizeof(l), "HELION %s %s", (mode == MODE_DEMO) ? "DEMO" : "FIELD", Status_Text());
    OLED_Print(0, l);

    if (ina_trk.online)
        snprintf(l, sizeof(l), "TRK %lu.%02luV %ld.%ldMA", (unsigned long)(ina_trk.mv / 1000),
                 (unsigned long)((ina_trk.mv % 1000) / 10), (long)(ina_trk.ma10 / 10), (long)(ina_trk.ma10 % 10));
    else snprintf(l, sizeof(l), "TRK --");
    OLED_Print(1, l);

    if (ina_fix.online)
        snprintf(l, sizeof(l), "FIX %lu.%02luV %ld.%ldMA", (unsigned long)(ina_fix.mv / 1000),
                 (unsigned long)((ina_fix.mv % 1000) / 10), (long)(ina_fix.ma10 / 10), (long)(ina_fix.ma10 % 10));
    else snprintf(l, sizeof(l), "FIX --");
    OLED_Print(2, l);

    snprintf(l, sizeof(l), "PWR T%lu F%lu MW", (unsigned long)ina_trk.mw, (unsigned long)ina_fix.mw);
    OLED_Print(3, l);

    int32_t g;
    if (Gain_Tenths(&g)) {
        int32_t ag = (g < 0) ? -g : g;
        snprintf(l, sizeof(l), "GAIN %c%ld.%ld PCT", (g < 0) ? '-' : '+', (long)(ag / 10), (long)(ag % 10));
    } else {
        snprintf(l, sizeof(l), "GAIN --");
    }
    OLED_Print(4, l);

    unsigned long et = (unsigned long)(energy_trk / 3600000000ULL);   /* mWh x10 */
    unsigned long ef = (unsigned long)(energy_fix / 3600000000ULL);
    snprintf(l, sizeof(l), "E T%lu.%lu F%lu.%lu MWH", et / 10, et % 10, ef / 10, ef % 10);
    OLED_Print(5, l);

    if (ina_sys.online)
        snprintf(l, sizeof(l), "SYS %lu.%02luV %ld.%ldMA", (unsigned long)(ina_sys.mv / 1000),
                 (unsigned long)((ina_sys.mv % 1000) / 10), (long)(ina_sys.ma10 / 10), (long)(ina_sys.ma10 % 10));
    else snprintf(l, sizeof(l), "SYS --");
    OLED_Print(6, l);

    if (now < ui_msg_until) {
        snprintf(l, sizeof(l), "%s", ui_msg);
    } else {
        const char *f = (faults & FAULT_LDR) ? "LDR!" :
                        (faults & FAULT_PAN_RUNAWAY) ? "PAN!" :
                        (faults & FAULT_TILT_RUNAWAY) ? "TILT!" :
                        (ax_pan.at_limit || ax_tilt.at_limit) ? "LIMIT" : "OK";
        snprintf(l, sizeof(l), "AZ%+04ld EL%+04ld %s", (long)err_az, (long)err_el, f);
    }
    OLED_Print(7, l);

    if (!OLED_Flush()) {
        oled_online = false;
        I2C_Recover();
    }
}

static void LED_Task(uint32_t now)
{
    bool on;
    uint32_t t = now % 2000U;
    if (faults)                { on = ((now / 125U) & 1U) != 0; }                 /* 4 Hz fast blink    */
    else if (Missing_Devices()){ on = (t < 100U) || (t >= 300U && t < 400U); }    /* double blink       */
    else if (is_parked)        { on = (now % 3000U) < 50U; }                      /* rare blip          */
    else                       { on = t < 50U; }                                  /* heartbeat          */
    LED_Set(on);
}

/* ========================================================================== */
/*  Button: debounced, non-blocking                                            */
/*    short press : toggle DEMO/FIELD and clear faults                         */
/*    hold > 2 s  : calibrate LDR gains                                        */
/* ========================================================================== */

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
            mode = (mode == MODE_DEMO) ? MODE_FIELD : MODE_DEMO;
            faults = 0;
            Reset_Axes();
            force_track = true;
            if (mode == MODE_FIELD && FIELD_RELEASE_SERVOS) {
                release_at = now + 1000U; release_pending = true;
            } else {
                release_pending = false;
            }
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
    /* Widens the swept range by 50 us each cycle. Watch / listen for the
     * point where a servo starts to grind, press the button there, and set
     * the *_MIN/MAX_PULSE limits about 50 us inside the number on screen. */
    char l[48];
    for (uint16_t half = 100; half <= 800; half = (uint16_t)(half + 50)) {
        for (int dir = 0; dir < 2; dir++) {
            uint16_t p = dir ? (uint16_t)(1500 + half) : (uint16_t)(1500 - half);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, p);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, p);
            OLED_Clear();
            OLED_Print(0, "SERVO SWEEP TEST");
            snprintf(l, sizeof(l), "PULSE %u US", (unsigned)p);
            OLED_Print(3, l);
            OLED_Print(6, "BUTTON = STOP");
            (void)OLED_Flush();
            for (int i = 0; i < 12; i++) {
                Delay_ms(100);
                if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET) {
                    for (;;) { HAL_IWDG_Refresh(&hiwdg); }   /* hold final position */
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
    __HAL_RCC_GPIOC_CLK_ENABLE();

    g.Pin = GPIO_PIN_13; g.Mode = GPIO_MODE_OUTPUT_PP; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

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
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }  /* PA6 */
    oc.Pulse = TILT_CENTER_PULSE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_2) != HAL_OK) { Error_Handler(); }  /* PA7 */
}

/* ---- MSP callbacks (audit issue 2). If you generated the project with
 * CubeMX and enabled these peripherals there, delete the duplicates in
 * stm32f1xx_hal_msp.c or the linker will report multiple definitions. ---- */

void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance == ADC1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
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
        g.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
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
        g.Pull  = GPIO_NOPULL;                 /* pull-ups are on the modules */
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
    pclk.AdcClockSelection    = RCC_ADCPCLK2_DIV6;     /* 72 / 6 = 12 MHz (limit 14 MHz) */
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) { Error_Handler(); }
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {                                         /* LED flicker = hard fault in init */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        for (volatile uint32_t i = 0; i < 400000UL; i++) { }
    }
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
    HAL_ADCEx_Calibration_Start(&hadc1);               /* mandatory after power-up */
    Cal_Load();

    I2C_BusClear();
    MX_I2C1_Init();

    /* Detect and initialise I2C devices. Missing ones are retried later;
     * tracking works without any of them. */
    oled_online = I2C_Probe(OLED_ADDR7) && OLED_Init();
    Ina219_t *list[3] = { &ina_trk, &ina_sys, &ina_fix };
    for (int i = 0; i < 3; i++) {
        list[i]->online = I2C_Probe(list[i]->addr7) && INA_Init(list[i]);
    }

    if (oled_online) {
        char l[48];
        OLED_Clear();
        OLED_Print(0, "HELION SOLAR TRACKER");
        snprintf(l, sizeof(l), "OLED:OK  INA1:%s", ina_trk.online ? "OK" : "--");
        OLED_Print(2, l);
        snprintf(l, sizeof(l), "INA2:%s  INA3:%s", ina_sys.online ? "OK" : "--", ina_fix.online ? "OK" : "--");
        OLED_Print(3, l);
        if (boot_wdg_reset) { OLED_Print(5, "LAST RESET: WATCHDOG"); }
        (void)OLED_Flush();
        Delay_ms(2000);
    }

#if SERVO_SWEEP_TEST
    __HAL_DBGMCU_FREEZE_IWDG();
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64; hiwdg.Init.Reload = 2500;
    HAL_IWDG_Init(&hiwdg);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    Servo_Sweep_Test();
    for (;;) { HAL_IWDG_Refresh(&hiwdg); }
#endif

    /* Watchdog: ~4 s (LSI 40 kHz / 64 = 625 Hz, reload 2500). Frozen while halted in the debugger. */
    __HAL_DBGMCU_FREEZE_IWDG();
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload    = 2500;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) { Error_Handler(); }

    Servo_Boot_Center();
    Delay_ms(600);

    /* Dark at power-up -> park; otherwise locate the sun first. */
    uint16_t raw0[4]; uint32_t c0[4];
    Read_LDRs(raw0, c0, LDR_ROUNDS, LDR_PERIOD_US);
    last_sum = c0[0] + c0[1] + c0[2] + c0[3];
    if (last_sum < NIGHT_EXIT_ADC_SUM) { Park(); }
    else                               { Run_Sun_Search(); }

    uint32_t t_track = HAL_GetTick();
    uint32_t t_telem = HAL_GetTick();      /* audit 16: never start from 0 */

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
            Telemetry_Task(now - t_telem);
            t_telem = now;
        }

        I2C_Maintenance(now);
        Display_Task(now, false);
        LED_Task(now);
    }
}
#endif /* HELION_UNIT_TEST */
