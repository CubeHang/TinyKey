/**
  ******************************************************************************
  * @file    keyboard.c
  * @brief   TinyKey 逻辑层：矩阵扫描时序、四态消抖、键码映射、HID 报表生成
  *
  * 本文件是**纯 C**：不包含任何 HAL / USB 头文件，所有硬件相关操作都通过
  * keyboard.h 里声明的 KB_Port_* 接口完成。因此可以在 PC 上用 gcc 直接编译
  * 并配合假移植层跑单元测试（见 tests/）。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "keyboard.h"
#include <string.h>

/* Private define ------------------------------------------------------------*/

/** 消抖需要连续一致的采样次数（由毫秒窗口和扫描周期推导，改 header 里的配置即可） */
#define KB_DEBOUNCE_SAMPLES   ((KB_DEBOUNCE_MS + KB_SCAN_PERIOD_MS - 1U) / KB_SCAN_PERIOD_MS)

/* Private types -------------------------------------------------------------*/

/** 每个按键独立的消抖状态机（对应用户文档 5.2 节的三态机，多拆出释放消抖） */
typedef enum
{
  KB_ST_IDLE = 0,          /* 空闲态：按键未按下 */
  KB_ST_PRESS_DEBOUNCE,    /* 按下消抖态：已检测到低电平，等待确认 */
  KB_ST_PRESSED,           /* 按下保持态：稳定按下 */
  KB_ST_RELEASE_DEBOUNCE   /* 释放消抖态：已检测到高电平，等待确认 */
} KB_StateTypeDef;

/* Exported variables --------------------------------------------------------*/

/** 键位映射：下标 = row * KB_COLS + col，与用户文档的表格一一对应（Key1~Key16 -> a~p） */
const KB_KeyDef KB_KeyMap[KB_KEY_COUNT] =
{
  { 0U, 0U, 0x04U, 'a' },   /* Key1  row0 col0 */
  { 0U, 1U, 0x05U, 'b' },   /* Key2  row0 col1 */
  { 0U, 2U, 0x06U, 'c' },   /* Key3  row0 col2 */
  { 0U, 3U, 0x07U, 'd' },   /* Key4  row0 col3 */
  { 1U, 0U, 0x08U, 'e' },   /* Key5  row1 col0 */
  { 1U, 1U, 0x09U, 'f' },   /* Key6  row1 col1 */
  { 1U, 2U, 0x0AU, 'g' },   /* Key7  row1 col2 */
  { 1U, 3U, 0x0BU, 'h' },   /* Key8  row1 col3 */
  { 2U, 0U, 0x0CU, 'i' },   /* Key9  row2 col0 */
  { 2U, 1U, 0x0DU, 'j' },   /* Key10 row2 col1 */
  { 2U, 2U, 0x0EU, 'k' },   /* Key11 row2 col2 */
  { 2U, 3U, 0x0FU, 'l' },   /* Key12 row2 col3 */
  { 3U, 0U, 0x10U, 'm' },   /* Key13 row3 col0 */
  { 3U, 1U, 0x11U, 'n' },   /* Key14 row3 col1 */
  { 3U, 2U, 0x12U, 'o' },   /* Key15 row3 col2 */
  { 3U, 3U, 0x13U, 'p' },   /* Key16 row3 col3 */
};

/* Private variables ---------------------------------------------------------*/

static KB_StateTypeDef s_state[KB_KEY_COUNT];          /* 每键状态机当前状态 */
static uint8_t         s_debounce_count[KB_KEY_COUNT]; /* 每键连续一致采样计数 */
static uint16_t        s_stable_mask;                  /* 稳定按下的按键位图 */
static uint8_t         s_sent_report[KB_REPORT_SIZE];  /* 最近一次成功发出的报表 */
static uint32_t        s_last_scan_ms;                 /* 上一轮扫描的时刻 */
static bool            s_scan_started;                 /* 是否已扫描过至少一轮 */

/* Private function prototypes -----------------------------------------------*/

static void kb_update_key(uint16_t index, bool raw_pressed);

/* Exported functions --------------------------------------------------------*/

void KB_Init(void)
{
  uint16_t i;

  for (i = 0U; i < KB_KEY_COUNT; i++)
  {
    s_state[i] = KB_ST_IDLE;
    s_debounce_count[i] = 0U;
  }

  for (i = 0U; i < KB_REPORT_SIZE; i++)
  {
    s_sent_report[i] = 0U;
  }

  s_stable_mask = 0U;
  s_last_scan_ms = 0U;
  s_scan_started = false;

  KB_Port_Init();
  /* 上电即让所有列回到非导通电平：所有二极管反偏截止，行线保持默认电平，不会误触发 */
  KB_Port_ReleaseColumns();
}

void KB_Task(void)
{
  uint32_t now_ms = KB_Port_Millis();
  uint8_t  desired[KB_REPORT_SIZE];

  /* ---- 1. 按 KB_SCAN_PERIOD_MS 节流，执行一轮完整扫描 ---- */
  if ((s_scan_started == false) ||
      ((uint32_t)(now_ms - s_last_scan_ms) >= KB_SCAN_PERIOD_MS))
  {
    uint8_t col;
    uint8_t row;

    s_last_scan_ms = now_ms;
    s_scan_started = true;

    for (col = 0U; col < KB_COLS; col++)
    {
      uint8_t rows;

      /* 逐列驱动到导通电平：只让当前列具备二极管导通条件 */
      KB_Port_SelectColumn(col);
      KB_Port_DelayUs(KB_COL_SETTLE_US);
      rows = KB_Port_ReadRows();

      for (row = 0U; row < KB_ROWS; row++)
      {
        /* 行线电平按二极管方向解读：
         * KB_ROW_ACTIVE_HIGH=1（阳极在列线侧）：行线被拉高 = 按下
         * KB_ROW_ACTIVE_HIGH=0（阳极在行线侧）：行线被拉低 = 按下
         * 二极管已阻断串扰，无需再判鬼键。 */
        bool row_high = ((rows & (uint8_t)(1U << row)) != 0U);
#if KB_ROW_ACTIVE_HIGH
        bool raw_pressed = row_high;
#else
        bool raw_pressed = (row_high == false);
#endif

        kb_update_key((uint16_t)((row * KB_COLS) + col), raw_pressed);
      }
    }

    KB_Port_ReleaseColumns();
  }

  /* ---- 2. 收敛式上报：期望报表 != 已发送报表 时尝试发送，成功后才记账 ----
   * KB_Port_SendReport() 在 IN 端点忙时返回 false，此时不更新 s_sent_report，
   * 下个周期会带着同一份期望报表重试 —— 既不丢状态，也不会重复发送。 */
  KB_BuildReport(s_stable_mask, desired);

  if (memcmp(desired, s_sent_report, KB_REPORT_SIZE) != 0)
  {
    if (KB_Port_SendReport(desired))
    {
      (void)memcpy(s_sent_report, desired, KB_REPORT_SIZE);
    }
  }
}

uint16_t KB_GetStableMask(void)
{
  return s_stable_mask;
}

void KB_BuildReport(uint16_t mask, uint8_t report[KB_REPORT_SIZE])
{
  uint16_t i;
  uint8_t  slot = 0U;

  /* byte0 修饰键、byte1 保留、byte2~7 键码：本键盘无修饰键，前两字节恒为 0 */
  for (i = 0U; i < KB_REPORT_SIZE; i++)
  {
    report[i] = 0U;
  }

  /* 按 key_index 升序填充，超过 6 个键时截断（标准 6KRO 行为） */
  for (i = 0U; (i < KB_KEY_COUNT) && (slot < KB_MAX_KEYS); i++)
  {
    if ((mask & (uint16_t)(1U << i)) != 0U)
    {
      report[KB_REPORT_OFF_KEYS + slot] = KB_KeyMap[i].hid;
      slot++;
    }
  }
}

const KB_KeyDef *KB_KeyAt(uint16_t key_index)
{
  if (key_index >= KB_KEY_COUNT)
  {
    return NULL;
  }

  return &KB_KeyMap[key_index];
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  单个按键的消抖状态机
  * @param  index       按键下标（row * KB_COLS + col）
  * @param  raw_pressed 本轮扫描该键是否被读到按下
  *
  * 空闲态 ──读到低──▶ 按下消抖 ──连续 N 次低──▶ 按下保持
  *   ▲                    │(读到高，抖动)             │(读到高)
  *   └──连续 N 次高── 释放消抖 ◀──────────────────────┘
  * 抖动导致电平反复时会退回原稳定态，因此不会产生毛刺事件。
  */
static void kb_update_key(uint16_t index, bool raw_pressed)
{
  switch (s_state[index])
  {
    case KB_ST_IDLE:
      if (raw_pressed)
      {
        s_state[index] = KB_ST_PRESS_DEBOUNCE;
        s_debounce_count[index] = 0U;
      }
      break;

    case KB_ST_PRESS_DEBOUNCE:
      if (raw_pressed)
      {
        s_debounce_count[index]++;
        if (s_debounce_count[index] >= (uint8_t)KB_DEBOUNCE_SAMPLES)
        {
          s_state[index] = KB_ST_PRESSED;
          s_stable_mask |= (uint16_t)(1U << index);
        }
      }
      else
      {
        s_state[index] = KB_ST_IDLE;   /* 抖动，回到空闲态 */
        s_debounce_count[index] = 0U;
      }
      break;

    case KB_ST_PRESSED:
      if (raw_pressed == false)
      {
        s_state[index] = KB_ST_RELEASE_DEBOUNCE;
        s_debounce_count[index] = 0U;
      }
      break;

    case KB_ST_RELEASE_DEBOUNCE:
    default:
      if (raw_pressed)
      {
        s_state[index] = KB_ST_PRESSED;  /* 抖动，恢复按下态 */
        s_debounce_count[index] = 0U;
      }
      else
      {
        s_debounce_count[index]++;
        if (s_debounce_count[index] >= (uint8_t)KB_DEBOUNCE_SAMPLES)
        {
          s_state[index] = KB_ST_IDLE;
          s_stable_mask &= (uint16_t)~(uint16_t)(1U << index);
        }
      }
      break;
  }
}
