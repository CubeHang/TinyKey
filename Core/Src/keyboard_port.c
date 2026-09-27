/**
  ******************************************************************************
  * @file    keyboard_port.c
  * @brief   TinyKey 移植层：GPIO 列驱动/行读取、微秒延时、毫秒时钟、USB HID 上报
  *
  * 引脚：
  *   row1~row4     : PA0 / PA1 / PA2 / PA3   输入 + 内部上拉/下拉（由扫描极性决定）
  *   column1~col4  : PA4 / PA5 / PA6 / PA7   推挽输出，空闲为非导通电平
  *
  * 二极管方向（决定扫描极性）：
  *   行线 -> 按键 -> 二极管阴极 -> 二极管阳极 -> 列线
  *   二极管只能从列线流向行线，因此 KB_ROW_ACTIVE_HIGH = 1：
  *     行线内部**下拉**，逐列**拉高**，行线读到高 = 按下。
  *   本文件在 KB_Port_Init() 里按该宏重新确认一次上下拉与输出极性，
  *   因此不依赖 CubeMX 里 gpio.c 的默认值，改一个宏即可适配反向二极管。
  *
  * 若日后改引脚，只需改 gpio.c/.ioc 以及下面的 KB_GPIO_PORT。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "keyboard.h"
#include "main.h"
#include "usb_device.h"
#include "usbd_hid.h"

/* Private define ------------------------------------------------------------*/

/** 行线与列线所在的 GPIO 端口（当前全部在 GPIOA） */
#define KB_GPIO_PORT    GPIOA

/** 行线所有引脚 */
#define KB_ROW_PINS     ((uint16_t)(R1_Pin | R2_Pin | R3_Pin | R4_Pin))
/** 列线所有引脚 */
#define KB_COL_PINS     ((uint16_t)(C1_Pin | C2_Pin | C3_Pin | C4_Pin))

/** 列线的"导通"电平（能让对应二极管正向导通的电平） */
#if KB_ROW_ACTIVE_HIGH
#define KB_COL_ACTIVE_STATE    GPIO_PIN_SET
#define KB_COL_IDLE_STATE      GPIO_PIN_RESET
#define KB_ROW_PULL            GPIO_PULLDOWN
#else
#define KB_COL_ACTIVE_STATE    GPIO_PIN_RESET
#define KB_COL_IDLE_STATE      GPIO_PIN_SET
#define KB_ROW_PULL            GPIO_PULLUP
#endif

/* Private variables ---------------------------------------------------------*/

static const uint16_t s_row_pin[KB_ROWS] = { R1_Pin, R2_Pin, R3_Pin, R4_Pin };
static const uint16_t s_col_pin[KB_COLS] = { C1_Pin, C2_Pin, C3_Pin, C4_Pin };

/* Exported functions --------------------------------------------------------*/

void KB_Port_Init(void)
{
  GPIO_InitTypeDef init = {0};

  /* 行线：输入 + 内部上拉/下拉。
     下拉时行线空闲为低，只有被扫描列拉高且按键按下时才会被二极管抬到约 2.7V。 */
  init.Pin = KB_ROW_PINS;
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = KB_ROW_PULL;
  HAL_GPIO_Init(KB_GPIO_PORT, &init);

  /* 列线：推挽输出 */
  init.Pin = KB_COL_PINS;
  init.Mode = GPIO_MODE_OUTPUT_PP;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(KB_GPIO_PORT, &init);

  KB_Port_ReleaseColumns();
}

void KB_Port_ReleaseColumns(void)
{
  HAL_GPIO_WritePin(KB_GPIO_PORT, KB_COL_PINS, KB_COL_IDLE_STATE);
}

void KB_Port_SelectColumn(uint8_t col)
{
  if (col >= KB_COLS)
  {
    KB_Port_ReleaseColumns();
    return;
  }

  /* 先把四列全部拉到非导通电平，再单独把目标列驱动到导通电平：
     保证任何时刻最多只有一列处于导通电平，其余列对应的二极管反偏截止。 */
  KB_Port_ReleaseColumns();
  HAL_GPIO_WritePin(KB_GPIO_PORT, s_col_pin[col], KB_COL_ACTIVE_STATE);
}

uint8_t KB_Port_ReadRows(void)
{
  uint8_t rows = 0U;
  uint8_t row;

  for (row = 0U; row < KB_ROWS; row++)
  {
    if (HAL_GPIO_ReadPin(KB_GPIO_PORT, s_row_pin[row]) == GPIO_PIN_SET)
    {
      rows |= (uint8_t)(1U << row);
    }
  }

  return rows;
}

void KB_Port_DelayUs(uint32_t us)
{
  /* 这里只要求“不短于”列电平的建立时间（行线节点 RC 约 1us），不需要精确计时，
     因此用保守的忙等：72 MHz 下每次循环约 4 个周期，按 SystemCoreClock/4 折算。
     volatile 保证 -O3 下循环不会被优化掉。 */
  volatile uint32_t loops = us * (SystemCoreClock / 4000000U);

  while (loops > 0U)
  {
    loops--;
    __NOP();
  }
}

uint32_t KB_Port_Millis(void)
{
  return HAL_GetTick();
}

bool KB_Port_SendReport(uint8_t report[KB_REPORT_SIZE])
{
  /* USBD_HID_SendReport() 在端点忙时会静默丢包（ST 库的既有行为），
     所以先查询端点状态；返回 false 时调用方（KB_Task）会在下个周期重试。 */
  if (USBD_HID_IsReady(&hUsbDeviceFS) == 0U)
  {
    return false;
  }

  if (USBD_HID_SendReport(&hUsbDeviceFS, report, (uint16_t)KB_REPORT_SIZE) != USBD_OK)
  {
    return false;
  }

  return true;
}
