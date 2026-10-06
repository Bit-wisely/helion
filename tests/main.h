#ifndef MAIN_H_TEST
#define MAIN_H_TEST

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HAL_OK       = 0x00U,
    HAL_ERROR    = 0x01U,
    HAL_BUSY     = 0x02U,
    HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0U,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct {
    int dummy;
} GPIO_TypeDef;

extern GPIO_TypeDef *GPIOA;
extern GPIO_TypeDef *GPIOB;
extern GPIO_TypeDef *GPIOC;

#define GPIO_PIN_0                 ((uint16_t)0x0001)
#define GPIO_PIN_1                 ((uint16_t)0x0002)
#define GPIO_PIN_2                 ((uint16_t)0x0004)
#define GPIO_PIN_3                 ((uint16_t)0x0008)
#define GPIO_PIN_4                 ((uint16_t)0x0010)
#define GPIO_PIN_6                 ((uint16_t)0x0040)
#define GPIO_PIN_7                 ((uint16_t)0x0080)
#define GPIO_PIN_13                ((uint16_t)0x2000)

#define GPIO_MODE_INPUT            0x00000000U
#define GPIO_MODE_OUTPUT_PP        0x00000001U
#define GPIO_MODE_OUTPUT_OD        0x00000011U
#define GPIO_MODE_AF_PP            0x00000002U
#define GPIO_MODE_AF_OD            0x00000012U
#define GPIO_MODE_ANALOG           0x00000003U

#define GPIO_NOPULL                0x00000000U
#define GPIO_PULLUP                0x00000001U

#define GPIO_SPEED_FREQ_LOW        0x00000002U
#define GPIO_SPEED_FREQ_HIGH       0x00000003U

/* ADC */
typedef struct {
    void *Instance;
    struct {
        uint32_t ScanConvMode;
        uint32_t ContinuousConvMode;
        uint32_t DiscontinuousConvMode;
        uint32_t ExternalTrigConv;
        uint32_t DataAlign;
        uint32_t NbrOfConversion;
    } Init;
} ADC_HandleTypeDef;

typedef struct {
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;

#define ADC1                       ((void *)0x40012400UL)
#define ADC_SCAN_DISABLE           0x00000000U
#define DISABLE                    0
#define ADC_SOFTWARE_START         0x00000000U
#define ADC_DATAALIGN_RIGHT        0x00000000U
#define ADC_CHANNEL_0              0x00000000U
#define ADC_CHANNEL_1              0x00000001U
#define ADC_CHANNEL_2              0x00000002U
#define ADC_CHANNEL_3              0x00000003U
#define ADC_REGULAR_RANK_1         0x00000001U
#define ADC_SAMPLETIME_239CYCLES_5 0x00000007U

/* TIM */
typedef struct {
    void *Instance;
    struct {
        uint32_t Prescaler;
        uint32_t CounterMode;
        uint32_t Period;
        uint32_t ClockDivision;
        uint32_t AutoReloadPreload;
    } Init;
} TIM_HandleTypeDef;

typedef struct {
    uint32_t OCMode;
    uint32_t Pulse;
    uint32_t OCPolarity;
    uint32_t OCFastMode;
} TIM_OC_InitTypeDef;

#define TIM3                       ((void *)0x40000400UL)
#define TIM_COUNTERMODE_UP         0x00000000U
#define TIM_CLOCKDIVISION_DIV1     0x00000000U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 0x00000080U
#define TIM_OCMODE_PWM1            0x00000060U
#define TIM_OCPOLARITY_HIGH        0x00000000U
#define TIM_OCFAST_DISABLE         0x00000000U
#define TIM_CHANNEL_1              0x00000000U
#define TIM_CHANNEL_2              0x00000004U

/* I2C */
typedef struct {
    void *Instance;
    struct {
        uint32_t ClockSpeed;
        uint32_t DutyCycle;
        uint32_t OwnAddress1;
        uint32_t AddressingMode;
        uint32_t DualAddressMode;
        uint32_t OwnAddress2;
        uint32_t GeneralCallMode;
        uint32_t NoStretchMode;
    } Init;
} I2C_HandleTypeDef;

#define I2C1                       ((void *)0x40005400UL)
#define I2C_DUTYCYCLE_2            0x00000000U
#define I2C_ADDRESSINGMODE_7BIT    0x00004000U
#define I2C_DUALADDRESS_DISABLE    0x00000000U
#define I2C_GENERALCALL_DISABLE    0x00000000U
#define I2C_NOSTRETCH_DISABLE      0x00000000U
#define I2C_MEMADD_SIZE_8BIT       0x00000001U

/* IWDG */
typedef struct {
    void *Instance;
    struct {
        uint32_t Prescaler;
        uint32_t Reload;
    } Init;
} IWDG_HandleTypeDef;

#define IWDG                       ((void *)0x40003000UL)
#define IWDG_PRESCALER_64          0x00000004U

/* FLASH */
typedef struct {
    uint32_t TypeErase;
    uint32_t PageAddress;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;

#define FLASH_TYPEERASE_PAGES      0x00U
#define FLASH_TYPEPROGRAM_WORD     0x02U

/* CoreDebug & DWT */
typedef struct {
    uint32_t DEMCR;
} CoreDebug_t;

typedef struct {
    uint32_t CTRL;
    uint32_t CYCCNT;
} DWT_t;

extern CoreDebug_t *CoreDebug;
extern DWT_t *dwt_ptr;
DWT_t *dwt_tick(void);
#define DWT (dwt_tick())
#define CoreDebug_DEMCR_TRCENA_Msk 0x01000000UL
#define DWT_CTRL_CYCCNTENA_Msk     0x00000001UL

/* RCC / Reset flags */
#define RCC_FLAG_IWDGRST           0x20U
#define RESET                      0U
#define FLASH_LATENCY_2            0x02U
#define RCC_OSCILLATORTYPE_HSE     0x01U
#define RCC_HSE_ON                 0x01U
#define RCC_HSE_PREDIV_DIV1        0x00U
#define RCC_PLL_ON                 0x02U
#define RCC_PLLSOURCE_HSE          0x01U
#define RCC_PLL_MUL9               0x07U
#define RCC_CLOCKTYPE_HCLK         0x02U
#define RCC_CLOCKTYPE_SYSCLK       0x01U
#define RCC_CLOCKTYPE_PCLK1        0x04U
#define RCC_CLOCKTYPE_PCLK2        0x08U
#define RCC_SYSCLKSOURCE_PLLCLK    0x02U
#define RCC_SYSCLK_DIV1            0x00U
#define RCC_HCLK_DIV2              0x04U
#define RCC_HCLK_DIV1              0x00U
#define RCC_PERIPHCLK_ADC          0x01U
#define RCC_ADCPCLK2_DIV6          0x02U

typedef struct {
    uint32_t OscillatorType;
    uint32_t HSEState;
    uint32_t HSEPredivValue;
    struct {
        uint32_t PLLState;
        uint32_t PLLSource;
        uint32_t PLLMUL;
    } PLL;
} RCC_OscInitTypeDef;

typedef struct {
    uint32_t ClockType;
    uint32_t SYSCLKSource;
    uint32_t AHBCLKDivider;
    uint32_t APB1CLKDivider;
    uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

typedef struct {
    uint32_t PeriphClockSelection;
    uint32_t AdcClockSelection;
} RCC_PeriphCLKInitTypeDef;

/* Macros */
#define __HAL_RCC_GPIOA_CLK_ENABLE()   ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()   ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()   ((void)0)
#define __HAL_RCC_ADC1_CLK_ENABLE()    ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE()    ((void)0)
#define __HAL_RCC_I2C1_CLK_ENABLE()    ((void)0)
#define __HAL_RCC_I2C1_CLK_DISABLE()   ((void)0)
#define __HAL_RCC_I2C1_FORCE_RESET()   ((void)0)
#define __HAL_RCC_I2C1_RELEASE_RESET() ((void)0)
#define __HAL_DBGMCU_FREEZE_IWDG()     ((void)0)
#define __HAL_RCC_GET_FLAG(f)          (0)
#define __HAL_RCC_CLEAR_RESET_FLAGS()  ((void)0)
#define __disable_irq()                ((void)0)
static inline void NVIC_SystemReset(void) { }
#define __HAL_TIM_ENABLE_OCxPRELOAD(h, ch) ((void)0)
#define __enable_irq()                 ((void)0)

extern uint32_t stub_cmp[2];
extern int stub_pwm_run[2];
#define __HAL_TIM_SET_COMPARE(htim, ch, val) do { \
    stub_cmp[((ch) == TIM_CHANNEL_1) ? 0 : 1] = (uint32_t)(val); \
} while(0)

/* Stub function declarations */
extern uint32_t SystemCoreClock;
uint32_t HAL_GetTick(void);
void SystemClock_Config(void);
void Error_Handler(void);

static inline void HAL_Init(void) {}
static inline void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *i) { (void)p; (void)i; }
static inline void HAL_GPIO_DeInit(GPIO_TypeDef *p, uint32_t pin) { (void)p; (void)pin; }

extern int stub_button_low;
static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) {
    (void)p;
    if (pin == GPIO_PIN_4) return stub_button_low ? GPIO_PIN_RESET : GPIO_PIN_SET;
    return GPIO_PIN_SET;
}
static inline void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s) { (void)p; (void)pin; (void)s; }
static inline void HAL_GPIO_TogglePin(GPIO_TypeDef *p, uint16_t pin) { (void)p; (void)pin; }

static inline HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }

extern uint32_t stub_adc_cur;
extern uint16_t stub_adc[4];
static inline HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c) {
    (void)h;
    stub_adc_cur = c->Channel;
    return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t t) { (void)h; (void)t; return HAL_OK; }
static inline uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) {
    (void)h;
    return (stub_adc_cur < 4) ? stub_adc[stub_adc_cur] : 0;
}
static inline HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h, TIM_OC_InitTypeDef *oc, uint32_t ch) {
    (void)h; (void)oc; (void)ch; return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t ch) {
    (void)h;
    stub_pwm_run[(ch == TIM_CHANNEL_1) ? 0 : 1] = 1;
    return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t ch) {
    (void)h;
    stub_pwm_run[(ch == TIM_CHANNEL_1) ? 0 : 1] = 0;
    return HAL_OK;
}

static inline HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t tr, uint32_t t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t s, uint8_t *d, uint16_t n, uint32_t t);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t s, uint8_t *d, uint16_t n, uint32_t t);

static inline HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h) { (void)h; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
static inline HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t t, uint32_t a, uint64_t d);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *pe);

static inline HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *o) { (void)o; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c, uint32_t f) { (void)c; (void)f; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p) { (void)p; return HAL_OK; }

#endif /* MAIN_H_TEST */
