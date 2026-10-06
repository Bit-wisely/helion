/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Cortex-M3 exception handlers.
  ******************************************************************************
  */

#include "main.h"

/* Any fatal CPU fault: reset immediately. An unattended tracker must restart
 * and carry on rather than hang with the servo unpowered or mis-pointed. */
void NMI_Handler(void)        { NVIC_SystemReset(); }
void HardFault_Handler(void)  { NVIC_SystemReset(); }
void MemManage_Handler(void)  { NVIC_SystemReset(); }
void BusFault_Handler(void)   { NVIC_SystemReset(); }
void UsageFault_Handler(void) { NVIC_SystemReset(); }

void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

void SysTick_Handler(void)
{
    HAL_IncTick();
}
