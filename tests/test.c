/* Host-side unit tests for Core/Src/main.c (single-axis Helion firmware).
 * Build:  gcc -std=c11 -Wall -Wextra -Wshadow -I. test.c -o test && ./test
 * tests/main.h is a fake of the STM32 HAL for the PC; it is NOT the real header. */
#define HELION_UNIT_TEST 1
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stub state ---- */
GPIO_TypeDef gA, gB, gC; GPIO_TypeDef *GPIOA = &gA, *GPIOB = &gB, *GPIOC = &gC;
int stub_button_low = 0;
CoreDebug_t cd; DWT_t dwt; CoreDebug_t *CoreDebug = &cd; DWT_t *dwt_ptr = &dwt;
DWT_t *dwt_tick(void) { dwt.CYCCNT += 2000; return &dwt; }
uint32_t SystemCoreClock = 72000000;
uint16_t stub_adc[4] = {0}; uint32_t stub_adc_cur; uint32_t stub_cmp[2]; int stub_pwm_run[2];
static uint32_t tick = 0;
uint32_t HAL_GetTick(void) { tick += 1; dwt.CYCCNT += SystemCoreClock / 1000; return tick; }
static uint32_t flash_w[256];
static uint16_t ina_bus = 0, ina_shunt = 0; static int i2c_fail = 0;
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t s, uint8_t *d, uint16_t n, uint32_t t)
{ (void)h; (void)a; (void)r; (void)s; (void)d; (void)n; (void)t; return i2c_fail ? HAL_TIMEOUT : HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t s, uint8_t *d, uint16_t n, uint32_t t)
{ (void)h; (void)a; (void)s; (void)n; (void)t; if (i2c_fail) return HAL_TIMEOUT;
  uint16_t v = (r == 2) ? ina_bus : ina_shunt; d[0] = (uint8_t)(v >> 8); d[1] = (uint8_t)(v & 0xFF); return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t tr, uint32_t t)
{ (void)h; (void)a; (void)tr; (void)t; return i2c_fail ? HAL_TIMEOUT : HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *pe)
{ (void)e; (void)pe; memset(flash_w, 0xFF, sizeof flash_w); return HAL_OK; }

static char uart_buf[1024];
static uint16_t uart_len = 0;
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    (void)huart; (void)Timeout;
    if (uart_len + Size < sizeof(uart_buf)) {
        memcpy(uart_buf + uart_len, pData, Size);
        uart_len += Size;
        uart_buf[uart_len] = '\0';
    }
    return HAL_OK;
}
static void clear_uart(void) { uart_len = 0; uart_buf[0] = '\0'; }

/* Cal_Load / Cal_Save read the flash through this macro; point it at the fake page */
#define CAL_WORD(i) (flash_w[i])
#include "../Core/Src/main.c"

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t t, uint32_t a, uint64_t d)
{ (void)t; flash_w[(a - CAL_FLASH_ADDR) / 4] = (uint32_t)d; return HAL_OK; }

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)
static void set_adc(uint16_t l, uint16_t r) { stub_adc[0] = l; stub_adc[1] = r; }

int main(void)
{
    /* --- USART1 init and I2C probing --- */
    MX_USART1_UART_Init();
    CHECK(huart1.Init.BaudRate == 115200, "uart: configured for 115200 baud");
    CHECK(huart1.Init.WordLength == UART_WORDLENGTH_8B && huart1.Init.StopBits == UART_STOPBITS_1, "uart: 8N1 framing");
    CHECK(!I2C_Probe(0x3C), "i2c: probe explicitly skips OLED 0x3C");

    /* --- INA219 decoding --- */
    ina_bus = (uint16_t)((5120 / 4) << 3); ina_shunt = 1234;      /* 5.120 V, 12.34 mV -> 123.4 mA */
    Ina219_t d = { .addr7 = 0x40 };
    CHECK(INA_Read(&d) && d.mv == 5120 && d.ma10 == 1234, "INA219: 5.120 V / 123.4 mA decode");
    CHECK(d.mw == 631, "INA219: 5120 mV x 123.4 mA = 631 mW");
    ina_shunt = (uint16_t)(-5); INA_Read(&d);
    CHECK(d.ma10 == 0, "INA219: negative noise clamped to 0");
    ina_bus = (uint16_t)((9000 / 4) << 3); ina_shunt = 8000; INA_Read(&d);
    CHECK(d.mv == 9000 && d.ma10 == 8000 && d.mw == 7200, "INA219: 9.0 V / 800 mA / 7200 mW");
    ina_bus = (uint16_t)(((26000 / 4) << 3) | 1); INA_Read(&d);
    CHECK(d.mv == 26000, "INA219: 26 V reads correctly, OVF flag ignored");

    /* --- Axis controller --- */
    Axis_t a; uint16_t p;
    memset(&a, 0, sizeof a); p = 1500; faults = 0;
    CHECK(!Axis_Update(&a, 30, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY) && p == 1500, "axis: inside deadband holds");
    CHECK(Axis_Update(&a, 100, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY) && p == 1510, "axis: err +100 -> +10 us");
    p = 1500; memset(&a, 0, sizeof a);
    Axis_Update(&a, 1000, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY); CHECK(p == 1530, "axis: step capped at 30 us");
    p = 1500; memset(&a, 0, sizeof a); Axis_Update(&a, -200, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY);
    CHECK(p < 1500, "axis: negative error moves the other way");
    p = 1500; memset(&a, 0, sizeof a); Axis_Update(&a, 200, &p, 900, 2100, -1, FAULT_PAN_RUNAWAY);
    CHECK(p < 1500, "axis: PAN_DIR = -1 inverts");

    p = 1500; memset(&a, 0, sizeof a); faults = 0; int moved = 0;
    for (int i = 0; i < 200 && !faults; i++) { if (Axis_Update(&a, 400, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY)) moved++; }
    CHECK((faults & FAULT_PAN_RUNAWAY) && moved <= RUNAWAY_STEPS + 1, "axis: error never shrinking -> runaway fault");
    CHECK(p >= 900 && p <= 2100, "axis: pulse stays inside limits");

    p = 2000; memset(&a, 0, sizeof a); faults = 0;
    for (int i = 0; i < 60 && !faults; i++) Axis_Update(&a, 600, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY);
    CHECK(!(faults & FAULT_PAN_RUNAWAY) && a.at_limit && p == 2100, "axis: reaching the limit early is LIMIT, not a fault");

    p = 1500; memset(&a, 0, sizeof a); faults = 0; int e2 = 900;
    for (int i = 0; i < 120; i++) { Axis_Update(&a, e2, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY); if (e2 > 200) e2 -= 8; }
    CHECK(faults == 0 && p == 2100 && a.at_limit, "axis: improving error into the end stop -> LIMIT, no fault");

    p = 1500; memset(&a, 0, sizeof a); faults = 0; int e = 900;
    for (int i = 0; i < 200; i++) { Axis_Update(&a, e, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY); if (e > 20) e -= 15; }
    CHECK(faults == 0, "axis: converging error does not fault");

    p = 2100; memset(&a, 0, sizeof a); faults = 0;
    for (int i = 0; i < 100; i++) Axis_Update(&a, 500, &p, 900, 2100, +1, FAULT_PAN_RUNAWAY);
    CHECK(faults == 0 && a.at_limit && p == 2100, "axis: sun beyond travel -> LIMIT, no false fault");

    /* --- LDR fault --- */
    uint16_t ok[2] = {2000, 2100}, open_[2] = {2000, 10}, dusk[2] = {20, 25}, shade[2] = {2400, 300};
    CHECK(!LDR_Fault(ok) && LDR_Fault(open_), "LDR fault: open wire detected");
    CHECK(!LDR_Fault(dusk) && !LDR_Fault(shade), "LDR fault: dusk and baffle shadow are not faults");

    /* --- Calibration maths --- */
    uint32_t q[2]; uint16_t good[2] = {2000, 2200};
    CHECK(Cal_Compute(good, q), "cal: normal input accepted");
    CHECK(abs((int)((good[0] * q[0]) >> 12) - 2100) <= 2 && abs((int)((good[1] * q[1]) >> 12) - 2100) <= 2, "cal: both channels converge to the mean");
    uint16_t dark[2] = {100, 120}, sat[2] = {4095, 4095}, uneq[2] = {3000, 500}, zero[2] = {0, 2000};
    CHECK(!Cal_Compute(dark, q) && !Cal_Compute(sat, q) && !Cal_Compute(uneq, q) && !Cal_Compute(zero, q),
          "cal: dark / saturated / unequal / zero rejected");

    /* --- Calibration persisted in flash --- */
    cal_q[0] = 4300; cal_q[1] = 3900;
    CHECK(Cal_Save(), "flash: save + read-back verify");
    cal_q[0] = cal_q[1] = 4096; Cal_Load();
    CHECK(cal_q[0] == 4300 && cal_q[1] == 3900, "flash: values survive a reload");
    flash_w[1] ^= 1; Cal_Load();
    CHECK(cal_q[0] == 4096 && cal_q[1] == 4096, "flash: corrupted record -> defaults");
    memset(flash_w, 0xFF, sizeof flash_w); cal_q[0] = 5000; Cal_Load();
    CHECK(cal_q[0] == 4096, "flash: blank page -> defaults");

    /* --- Night logic --- */
    Servo_Move(1500, 0, false);
    set_adc(50, 50); is_parked = false; night_cnt = 0; faults = 0; Cal_Defaults();
    for (int i = 0; i < NIGHT_CONFIRM_COUNT - 1; i++) Tracker_Step();
    CHECK(!is_parked, "night: not parked before the confirm count");
    Tracker_Step(); CHECK(is_parked, "night: parked after the confirm count");
    CHECK(stub_cmp[0] == PARK_PAN_PULSE, "night: servo moved to the park position");
    set_adc(1000, 1000);
    for (int i = 0; i < DAY_CONFIRM_COUNT; i++) Tracker_Step();
    CHECK(!is_parked && !searching, "day: unparks after the confirm count and finishes the sun search");
    night_cnt = 0; set_adc(50, 50); Tracker_Step(); set_adc(1000, 1000); Tracker_Step();
    CHECK(!is_parked && night_cnt == 0, "night: one dark sample followed by light does not park");

    /* --- Sun search picks the brightest pan position --- */
    set_adc(1000, 1000); Run_Sun_Search();
    CHECK(pan_pulse >= PAN_MIN_PULSE && pan_pulse <= PAN_MAX_PULSE, "search: result inside limits");

    /* --- Tracking direction and sensor layout (Left = ADC0, Right = ADC1) --- */
    is_parked = false; faults = 0; Reset_Axes(); pan_pulse = 1500;
    set_adc(1000, 3000);
    Tracker_Step(); CHECK(err_az > 0 && pan_pulse > 1500, "tracking: right brighter -> err > 0, pulse increases (PAN_DIR=+1)");
    Reset_Axes(); pan_pulse = 1500; set_adc(3000, 1000);
    Tracker_Step(); CHECK(err_az < 0 && pan_pulse < 1500, "tracking: left brighter -> err < 0, pulse decreases");
    Reset_Axes(); pan_pulse = 1500; set_adc(2000, 2000);
    Tracker_Step(); CHECK(pan_pulse == 1500, "tracking: balanced light -> no movement");
    set_adc(2000, 5); faults = 0; Tracker_Step();
    CHECK((faults & FAULT_LDR) && pan_pulse == 1500, "tracking: open LDR -> fault, position held");
    set_adc(2000, 2000); Tracker_Step(); CHECK(!(faults & FAULT_LDR), "tracking: LDR fault clears when the sensor returns");

    /* --- Tracking independent of I2C --- */
    ina_trk.online = true;
    i2c_fail = 1; for (int i = 0; i < 10; i++) Telemetry_Task(500);
    CHECK(!ina_trk.online, "i2c: repeated failures mark the device offline");
    Reset_Axes(); pan_pulse = 1500; set_adc(1000, 3000); Tracker_Step();
    CHECK(pan_pulse > 1500, "i2c: tracker still works with the bus dead");
    i2c_fail = 0;

    /* --- Servo release logic --- */
    Servo_Move(1500, 100, true); uint32_t t0 = HAL_GetTick();
    Servo_Task(t0); CHECK(stub_pwm_run[0] == 1, "servo: still on before the settle time");
    Servo_Task(t0 + 200); CHECK(stub_pwm_run[0] == 0, "servo: released after the settle time");
    Servo_Move(1500, 0, false); CHECK(stub_pwm_run[0] == 1, "servo: next move re-enables PWM");
    Servo_Move(5000, 0, false); CHECK(stub_cmp[0] == PAN_MAX_PULSE, "servo: pulse clamped to the upper limit");
    Servo_Move(10, 0, false);   CHECK(stub_cmp[0] == PAN_MIN_PULSE, "servo: pulse clamped to the lower limit");

    /* --- Gain readout --- */
    ina_trk.online = ina_fix.online = true; ina_trk.mw = 1100; ina_fix.mw = 1000;
    int32_t g; CHECK(Gain_Tenths(&g) && g == 100, "gain: +10.0 % when tracked = 1.1 x fixed");
    ina_fix.mw = 10; CHECK(!Gain_Tenths(&g), "gain: hidden when the reference panel is dark");

    /* --- Status text states (replaces PC13 LED indications) --- */
    ina_trk.online = ina_sys.online = ina_fix.online = true;
    faults = FAULT_LDR; CHECK(strcmp(Status_Text(), "FAULT") == 0, "status: FAULT when faults active");
    faults = 0; is_parked = true; CHECK(strcmp(Status_Text(), "PARKED") == 0, "status: PARKED when parked");
    is_parked = false; ina_trk.online = false; CHECK(strcmp(Status_Text(), "I2C MISSING") == 0, "status: I2C MISSING when sensor offline");
    ina_trk.online = true; searching = true; CHECK(strcmp(Status_Text(), "SEARCH") == 0, "status: SEARCH when sun searching");
    searching = false; CHECK(strcmp(Status_Text(), "TRACK") == 0, "status: TRACK during normal tracking");

    /* --- Laptop serial display telemetry format --- */
    ina_trk.online = ina_sys.online = ina_fix.online = true;
    ina_trk.mv = 5810; ina_trk.ma10 = 3120; ina_trk.mw = 1813;
    ina_fix.mv = 5790; ina_fix.ma10 = 2680; ina_fix.mw = 1552;
    ina_sys.mv = 8920; ina_sys.ma10 = 4100;
    energy_trk = 3600000000ULL * 42;
    energy_fix = 3600000000ULL * 36;
    err_az = 12; last_sum = 5120; faults = 0; mode = MODE_FIELD;
    clear_uart();
    Display_Task(HAL_GetTick() + 1000, true);
    const char *expected =
        "HELION,mode=FIELD,st=TRACK,az=+012,S=5120\r\n"
        "TRK,v_mV=5810,i_mA=312,p_mW=1813\r\n"
        "FIX,v_mV=5790,i_mA=268,p_mW=1552\r\n"
        "SYS,v_mV=8920,i_mA=410\r\n"
        "GAIN_x10=+168,E_T_x10=42,E_F_x10=36\r\n";
    CHECK(strcmp(uart_buf, expected) == 0, "telemetry: matches exact design specification block");
    CHECK(strlen(uart_buf) > 0 && strlen(uart_buf) < sizeof(uart_buf), "telemetry: transmission buffer within bounds");

    clear_uart();
    Set_UI_Message("CAL SAVED", 3000);
    CHECK(strcmp(uart_buf, "CAL SAVED\r\n") == 0, "ui message: transmitted immediately over serial");

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
