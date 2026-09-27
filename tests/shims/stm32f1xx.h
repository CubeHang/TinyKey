/**
  ******************************************************************************
  * @file    stm32f1xx.h  (宿主机测试桩)
  * @brief   只为让目标代码在 PC 上编译通过的最小替换，不模拟任何外设。
  *          真实工程使用的是 Drivers/CMSIS/... 下的同名文件。
  ******************************************************************************
  */
#ifndef __STM32F1XX_H_TEST_SHIM__
#define __STM32F1XX_H_TEST_SHIM__

#include <stdint.h>

/* system_stm32f1xx.c 提供的系统时钟，宿主机上由 fake_hal.c 固定为 72MHz */
extern uint32_t SystemCoreClock;

/* keyboard_port.c 用到的 CMSIS 内建 */
#ifndef __NOP
#define __NOP()   do { } while (0)
#endif

#endif /* __STM32F1XX_H_TEST_SHIM__ */
