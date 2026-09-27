/**
  ******************************************************************************
  * @file    kb_port_test.c
  * @brief   移植层端到端测试：真 keyboard.c + 真 keyboard_port.c + 假 HAL/假 USB
  *
  * 假 HAL 用「列驱动状态 + 带二极管的矩阵模型」实时算出行线电平，
  * 因此这一层能真正验证 keyboard_port.c 的列驱动/行读取是否正确，
  * 而不只是验证逻辑层的状态机。
  ******************************************************************************
  */
#include "keyboard.h"
#include "main.h"
#include "stm32f1xx_hal.h"
#include "fake_platform.h"
#include "test_util.h"
#include <string.h>

#define KEY_BIT(row, col)   ((uint16_t)(1U << (((row) * KB_COLS) + (col))))
#define COL_BITS            ((uint32_t)(C1_Pin | C2_Pin | C3_Pin | C4_Pin))

static const char *fmt_report(const uint8_t *r)
{
  static char buf[64];

  if (r == NULL)
  {
    return "<无报表>";
  }

  (void)snprintf(buf, sizeof(buf), "%02X %02X %02X %02X %02X %02X %02X %02X",
                 r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
  return buf;
}

static void reset_all(void)
{
  FakeGpio_Reset();
  FakeUsb_Reset();
  KB_Init();
}

static void run_ms(int ms)
{
  int i;

  for (i = 0; i < ms; i++)
  {
    FakeGpio_AdvanceTick(1U);
    KB_Task();
  }
}

static const uint8_t *report_at(int index)
{
  return FakeUsb_ReportAt(index);
}

static int is_all_zero(const uint8_t *r)
{
  int i;

  for (i = 0; i < (int)KB_REPORT_SIZE; i++)
  {
    if (r[i] != 0U)
    {
      return 0;
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ */

static void test_pin_mapping(void)
{
  TEST_CASE("引脚映射：行 PA0~PA3、列 PA4~PA7（已与用户确认的口径）");

  CHECK_INT(R1_Pin, GPIO_PIN_0, "R1");
  CHECK_INT(R2_Pin, GPIO_PIN_1, "R2");
  CHECK_INT(R3_Pin, GPIO_PIN_2, "R3");
  CHECK_INT(R4_Pin, GPIO_PIN_3, "R4");
  CHECK_INT(C1_Pin, GPIO_PIN_4, "C1");
  CHECK_INT(C2_Pin, GPIO_PIN_5, "C2");
  CHECK_INT(C3_Pin, GPIO_PIN_6, "C3");
  CHECK_INT(C4_Pin, GPIO_PIN_7, "C4");
}

static void test_idle_state(void)
{
  TEST_CASE("初始状态：四列全部处于非导通电平，行线内部下拉，无按键时不误触发");

  reset_all();

  /* 二极管阳极在列线侧：列线空闲必须是低电平，行线必须是内部下拉 */
  CHECK_INT(GPIOA->ODR & COL_BITS, 0, "初始列驱动电平（应全为低）");
  CHECK_INT(FakeGpio_GetRowPull(), GPIO_PULLDOWN, "移植层给行线配置的上下拉");
  CHECK_INT(FakeGpio_PullMismatch(), 0, "行线上下拉与 KB_ROW_ACTIVE_HIGH 不匹配");
  CHECK_INT(KB_Port_ReadRows(), 0x00U, "无按键时的行线电平（下拉应全为低）");
  CHECK_INT(FakeUsb_ReportCount(), 0, "空闲上报次数");
}

static void test_polarity_matches_diode(void)
{
  TEST_CASE("扫描极性：KB_ROW_ACTIVE_HIGH=1（二极管阳极在列线侧，行线必须下拉）");

  CHECK_INT(KB_ROW_ACTIVE_HIGH, 1, "扫描极性宏");

  reset_all();
  CHECK_INT(FakeGpio_GetRowPull(), GPIO_PULLDOWN, "行线上下拉");
  CHECK_INT(FakeGpio_PullMismatch(), 0, "极性配置检查");
}

static void test_single_key_end_to_end(void)
{
  TEST_CASE("端到端：PA0/PA4 交叉键按下 -> 上报 0x04('a')，松开 -> 全 0");

  reset_all();

  FakeMatrix_SetKeys(KEY_BIT(0, 0));
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(FakeUsb_ReportCount(), 1, "按下后的上报次数");
  if (FakeUsb_ReportCount() >= 1)
  {
    const uint8_t *r = report_at(0);

    CHECK_INT(FakeUsb_ReportLenAt(0), KB_REPORT_SIZE, "报表长度");
    CHECK_U8(r[0], 0x00, "修饰键字节");
    CHECK_U8(r[1], 0x00, "保留字节");
    CHECK_U8(r[2], 0x04, "键码字节");
    printf("   按下报表 = %s\n", fmt_report(r));
  }

  FakeMatrix_SetKeys(0U);
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(FakeUsb_ReportCount(), 2, "松开后的上报次数");
  if (FakeUsb_ReportCount() >= 2)
  {
    CHECK(is_all_zero(report_at(1)), "松开报表应全 0，实际 = %s", fmt_report(report_at(1)));
  }
}

static void test_all_keys_end_to_end(void)
{
  int i;

  TEST_CASE("端到端：16 个交叉键全部产生正确键码");

  for (i = 0; i < (int)KB_KEY_COUNT; i++)
  {
    reset_all();

    FakeMatrix_SetKeys((uint16_t)(1U << i));
    run_ms(KB_DEBOUNCE_MS + 5);

    CHECK_INT(FakeUsb_ReportCount(), 1, "上报次数");
    if (FakeUsb_ReportCount() >= 1)
    {
      CHECK_U8(report_at(0)[2], (uint8_t)(0x04U + i), "键码");
      CHECK_U8(report_at(0)[3], 0x00, "多键槽位应为空");
    }
  }
}

static void test_scan_sequence(void)
{
  const int8_t *log;
  int count = 0;
  int i;

  TEST_CASE("端到端扫描时序：4 列依次导通、每列读 4 行，任意时刻最多一列导通");

  reset_all();
  run_ms(1);

  log = FakeGpio_GetActiveColLog(&count);

  CHECK_INT(FakeGpio_MultiLowSeen(), 0, "出现过同时导通多列");
  /* 日志记录的是每一次 HAL_GPIO_ReadPin 时的导通列号：每列读 4 行，共 4x4 = 16 次 */
  CHECK_INT(count, (int)(KB_COLS * KB_ROWS), "一轮扫描读取引脚的次数");

  for (i = 0; i < count; i++)
  {
    CHECK_INT(log[i], i / (int)KB_ROWS, "第 i 次读引脚时应处于导通电平的列号");
  }

  /* 确认扫描结束后列回到空闲（非导通）电平 */
  CHECK_INT(GPIOA->ODR & COL_BITS, 0, "扫描结束后列应全部为低");
}

static void test_diode_anti_ghost(void)
{
  TEST_CASE("端到端防鬼键：L 形三键同按，不出现第四个键");

  reset_all();

  FakeMatrix_SetKeys((uint16_t)(KEY_BIT(0, 0) | KEY_BIT(0, 1) | KEY_BIT(1, 0)));
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK((KB_GetStableMask() & KEY_BIT(1, 1)) == 0U,
        "鬼键 (r1,c1) 被误判，稳定位图 = 0x%04X", KB_GetStableMask());
  CHECK_INT(FakeUsb_ReportCount(), 1, "上报次数");

  if (FakeUsb_ReportCount() >= 1)
  {
    const uint8_t *r = report_at(0);

    CHECK_U8(r[2], 0x04, "键码 1（'a'）");
    CHECK_U8(r[3], 0x05, "键码 2（'b'）");
    CHECK_U8(r[4], 0x08, "键码 3（'e'）");
    CHECK_U8(r[5], 0x00, "不应出现第 4 个键码");
    printf("   三键报表 = %s\n", fmt_report(r));
  }
}

static void test_busy_endpoint_retry(void)
{
  TEST_CASE("端到端端点忙：报表不丢不重，端点空闲后补发");

  reset_all();

  FakeMatrix_SetKeys(KEY_BIT(2, 3));   /* 'l' = 0x0F */
  FakeUsb_SetReady(0U);
  run_ms(50);

  CHECK_INT(FakeUsb_ReportCount(), 0, "端点忙时的上报次数");

  FakeUsb_SetReady(1U);
  run_ms(3);

  CHECK_INT(FakeUsb_ReportCount(), 1, "端点恢复后的上报次数");
  if (FakeUsb_ReportCount() >= 1)
  {
    CHECK_U8(report_at(0)[2], 0x0FU, "补发键码（'l'）");
  }

  run_ms(200);
  CHECK_INT(FakeUsb_ReportCount(), 1, "补发后不应重复上报");
}

int main(void)
{
  printf("=== TinyKey 移植层端到端测试（keyboard_port.c + keyboard.c） ===\n\n");

  test_pin_mapping();
  test_polarity_matches_diode();
  test_idle_state();
  test_single_key_end_to_end();
  test_all_keys_end_to_end();
  test_scan_sequence();
  test_diode_anti_ghost();
  test_busy_endpoint_retry();

  return test_summary("移植层");
}
