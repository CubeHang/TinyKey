/**
  ******************************************************************************
  * @file    fake_hal.c
  * @brief   假 GPIO + 带二极管的 4x4 矩阵电气模型
  *
  * 硬件接线（与工程实际一致）：
  *   行线 -> 按键 -> 二极管阴极 -> 二极管阳极 -> 列线
  *   二极管只能从列线流向行线，故 KB_ROW_ACTIVE_HIGH = 1：
  *     列线被驱动到高电平（导通电平）时，该列按键按下才能把行线抬到高电平；
  *     其余列为低，二极管反偏截止，串扰回路不存在（防鬼键）。
  *
  * 模型行为：
  *   - 行线空闲电平取自移植层**真实配置的**上下拉（GPIO_PULLUP -> 高，否则低），
  *     因此如果 keyboard_port.c 配错了极性，测试会直接失败；
  *   - 每次读行线时记录"当前处于导通电平的列号"，用于校验扫描时序；
  *   - 同时出现两列以上处于导通电平会被标记为错误（正常扫描不允许）。
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "keyboard.h"
#include "fake_platform.h"
#include <string.h>

#if KB_ROW_ACTIVE_HIGH
#define FAKE_COL_ACTIVE_LEVEL   1U   /* 列高电平 -> 二极管正向导通 */
#define FAKE_ROW_PRESSED_LEVEL  1U   /* 行线被抬到高电平 -> 判定按下 */
#else
#define FAKE_COL_ACTIVE_LEVEL   0U
#define FAKE_ROW_PRESSED_LEVEL  0U
#endif

static GPIO_TypeDef s_gpio_a;

GPIO_TypeDef *const GPIOA = &s_gpio_a;

uint32_t SystemCoreClock = 72000000U;

static uint32_t s_tick;
static uint16_t s_keys;
static uint32_t s_row_pull;      /* 移植层给行线配置的上下拉 */
static int8_t   s_active_col_log[FAKE_LOG_MAX];
static int      s_active_col_count;
static int      s_multi_active_seen;

void FakeGpio_Reset(void)
{
  s_gpio_a.IDR = 0U;
  s_gpio_a.ODR = 0U;   /* 上电时列线还没被驱动，KB_Init() 应把它们置为导通电平之外 */
  s_gpio_a.BSRR = 0U;
  s_gpio_a.BRR = 0U;

  s_tick = 0U;
  s_keys = 0U;
  s_row_pull = GPIO_NOPULL;
  s_active_col_count = 0;
  s_multi_active_seen = 0;
  (void)memset(s_active_col_log, 0, sizeof(s_active_col_log));
}

void FakeMatrix_SetKeys(uint16_t mask)
{
  s_keys = mask;
}

uint16_t FakeMatrix_GetKeys(void)
{
  return s_keys;
}

const int8_t *FakeGpio_GetActiveColLog(int *count)
{
  if (count != NULL)
  {
    *count = s_active_col_count;
  }
  return s_active_col_log;
}

int FakeGpio_MultiLowSeen(void)
{
  return s_multi_active_seen;
}

uint32_t FakeGpio_GetRowPull(void)
{
  return s_row_pull;
}

int FakeGpio_PullMismatch(void)
{
#if KB_ROW_ACTIVE_HIGH
  return (s_row_pull != GPIO_PULLDOWN) ? 1 : 0;
#else
  return (s_row_pull != GPIO_PULLUP) ? 1 : 0;
#endif
}

void FakeGpio_AdvanceTick(uint32_t ms)
{
  s_tick += ms;
}

void FakeGpio_SetTick(uint32_t ms)
{
  s_tick = ms;
}

uint32_t FakeGpio_GetTick(void)
{
  return s_tick;
}

/**
  * @brief 由「列驱动状态(ODR) + 配置的上下拉 + 按键集合」实时算出「行线电平(IDR)」
  *        模拟的是组合逻辑，因此每次读行线前都会重新求值。
  */
static void fake_matrix_resolve(void)
{
  uint32_t odr = s_gpio_a.ODR;
  uint32_t idr = 0U;
  uint32_t row_idle = (s_row_pull == GPIO_PULLUP) ? 1UL : 0UL;
  int active_cols = 0;
  int last_active_col = -1;
  int row;
  int col;

  for (col = 0; col < 4; col++)
  {
    uint32_t level = ((odr >> (4 + col)) & 1UL);

    if (level == (uint32_t)FAKE_COL_ACTIVE_LEVEL)   /* 该列处于导通电平 */
    {
      active_cols++;
      last_active_col = col;
    }
  }

  if (active_cols > 1)
  {
    s_multi_active_seen = 1;
  }

  if (s_active_col_count < FAKE_LOG_MAX)
  {
    s_active_col_log[s_active_col_count] = (int8_t)last_active_col;
    s_active_col_count++;
  }

  for (row = 0; row < 4; row++)
  {
    uint32_t level = row_idle;   /* 空闲电平由配置的上下拉决定 */

    for (col = 0; col < 4; col++)
    {
      uint32_t col_level = ((odr >> (4 + col)) & 1UL);

      if (col_level != (uint32_t)FAKE_COL_ACTIVE_LEVEL)
      {
        continue;   /* 该列不处于导通电平 -> 二极管反偏截止 */
      }
      if (((s_keys >> (row * 4 + col)) & 1U) != 0U)
      {
        level = (uint32_t)FAKE_ROW_PRESSED_LEVEL;   /* 二极管导通，行线被拉到按下电平 */
        break;
      }
    }

    if (level != 0UL)
    {
      idr |= (1UL << row);
    }
  }

  s_gpio_a.IDR = idr;
}

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
  if ((GPIOx == NULL) || (GPIO_Init == NULL))
  {
    return;
  }

  if (GPIOx != &s_gpio_a)
  {
    return;
  }

  if (GPIO_Init->Mode == GPIO_MODE_INPUT)
  {
    s_row_pull = GPIO_Init->Pull;
  }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  if (GPIOx == NULL)
  {
    return;
  }

  if (PinState == GPIO_PIN_RESET)
  {
    GPIOx->ODR &= ~((uint32_t)GPIO_Pin);
  }
  else
  {
    GPIOx->ODR |= (uint32_t)GPIO_Pin;
  }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  if (GPIOx == &s_gpio_a)
  {
    fake_matrix_resolve();
  }

  return ((GPIOx->IDR & (uint32_t)GPIO_Pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

uint32_t HAL_GetTick(void)
{
  return s_tick;
}

void HAL_Delay(uint32_t Delay)
{
  s_tick += Delay;
}
