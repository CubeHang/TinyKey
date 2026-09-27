/**
  ******************************************************************************
  * @file    stm32f1xx_hal.h  (宿主机测试桩)
  * @brief   最小的 HAL/GPIO 替身：GPIO 端口建模为带 IDR/ODR 的寄存器结构，
  *          HAL_GPIO_WritePin() 的位掩码语义与真实 HAL 一致，因此可以真实验证
  *          keyboard_port.c 的“拉低某一列、其余列拉高”行为。
  *
  *          真实工程使用的是 Drivers/STM32F1xx_HAL_Driver/Inc 下的同名文件。
  ******************************************************************************
  */
#ifndef __STM32F1XX_HAL_H_TEST_SHIM__
#define __STM32F1XX_HAL_H_TEST_SHIM__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx.h"

/* ---- GPIO 位定义（与真实 HAL 一致） ---------------------------------- */
#define GPIO_PIN_0    ((uint16_t)0x0001)
#define GPIO_PIN_1    ((uint16_t)0x0002)
#define GPIO_PIN_2    ((uint16_t)0x0004)
#define GPIO_PIN_3    ((uint16_t)0x0008)
#define GPIO_PIN_4    ((uint16_t)0x0010)
#define GPIO_PIN_5    ((uint16_t)0x0020)
#define GPIO_PIN_6    ((uint16_t)0x0040)
#define GPIO_PIN_7    ((uint16_t)0x0080)
#define GPIO_PIN_8    ((uint16_t)0x0100)
#define GPIO_PIN_9    ((uint16_t)0x0200)
#define GPIO_PIN_10   ((uint16_t)0x0400)
#define GPIO_PIN_11   ((uint16_t)0x0800)
#define GPIO_PIN_12   ((uint16_t)0x1000)
#define GPIO_PIN_13   ((uint16_t)0x2000)
#define GPIO_PIN_14   ((uint16_t)0x4000)
#define GPIO_PIN_15   ((uint16_t)0x8000)

typedef enum
{
  GPIO_PIN_RESET = 0,
  GPIO_PIN_SET
} GPIO_PinState;

/* ---- GPIO 模式/上下拉/速度（取值与真实 HAL 一致） -------------------- */
#define GPIO_MODE_INPUT         0x00000000U
#define GPIO_MODE_OUTPUT_PP     0x00000001U
#define GPIO_NOPULL             0x00000000U
#define GPIO_PULLUP             0x00000001U
#define GPIO_PULLDOWN           0x00000002U
#define GPIO_SPEED_FREQ_HIGH    0x00000003U

typedef struct
{
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
} GPIO_InitTypeDef;

/* ---- GPIO 端口寄存器（只保留测试用得到的成员） ----------------------- */
typedef struct
{
  volatile uint32_t IDR;   /* 输入数据寄存器：行线电平由二极管矩阵模型驱动 */
  volatile uint32_t ODR;   /* 输出数据寄存器：列线驱动状态 */
  volatile uint32_t BSRR;
  volatile uint32_t BRR;
} GPIO_TypeDef;

extern GPIO_TypeDef *const GPIOA;

/* ---- HAL 接口 -------------------------------------------------------- */
void          HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
void          HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
uint32_t      HAL_GetTick(void);
void          HAL_Delay(uint32_t Delay);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1XX_HAL_H_TEST_SHIM__ */
