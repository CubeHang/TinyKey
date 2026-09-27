/**
  ******************************************************************************
  * @file    kb_logic_test.c
  * @brief   逻辑层测试：真 keyboard.c + 测试内置的假移植层
  *
  * 覆盖：空闲不误触发、抖动抑制、16 键映射、多键合并、6KRO 截断、
  *       端点忙时的“不丢不重”、列扫描时序、二极管防鬼键。
  ******************************************************************************
  */
#include "keyboard.h"
#include "test_util.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* 假移植层：把「列驱动状态 + 物理按键集合」按二极管矩阵语义映射成行线电平 */
/* ------------------------------------------------------------------ */

#define LOG_MAX   8192
#define REP_MAX   64

/* 极性派生量：与 keyboard.h 的 KB_ROW_ACTIVE_HIGH 保持一致 */
#if KB_ROW_ACTIVE_HIGH
#define FAKE_COL_ACTIVE_LEVEL   1U   /* 列高电平 -> 二极管导通 */
#define FAKE_ROW_PRESSED_LEVEL  1U   /* 行线被抬到高 -> 按下 */
#define FAKE_ROW_IDLE_LEVEL     0U   /* 行线内部下拉 -> 空闲低 */
#else
#define FAKE_COL_ACTIVE_LEVEL   0U
#define FAKE_ROW_PRESSED_LEVEL  0U
#define FAKE_ROW_IDLE_LEVEL     1U
#endif

static uint32_t g_tick;
static uint16_t g_keys;         /* 物理按键集合，bit(row*4+col) */
static uint8_t  g_active_mask;  /* 当前处于导通电平的列位图 */
static uint8_t  g_ready;        /* USB 端点是否空闲 */
static uint8_t  g_reports[REP_MAX][8];
static int      g_report_count;
static int8_t   g_col_log[LOG_MAX];
static int      g_col_log_count;
static int      g_multi_low_seen;

void KB_Port_Init(void)
{
  g_active_mask = 0U;
}

void KB_Port_ReleaseColumns(void)
{
  g_active_mask = 0U;
}

void KB_Port_SelectColumn(uint8_t col)
{
  g_active_mask = (uint8_t)(1U << col);
}

uint8_t KB_Port_ReadRows(void)
{
  uint8_t rows = 0U;
  uint8_t row;
  uint8_t col;
  int active_cols = 0;
  int8_t last_active = -1;

  for (col = 0U; col < KB_COLS; col++)
  {
    if ((g_active_mask & (uint8_t)(1U << col)) != 0U)
    {
      active_cols++;
      last_active = (int8_t)col;
    }
  }

  if (active_cols > 1)
  {
    g_multi_low_seen = 1;
  }

  if (g_col_log_count < LOG_MAX)
  {
    g_col_log[g_col_log_count] = last_active;
    g_col_log_count++;
  }

  for (row = 0U; row < KB_ROWS; row++)
  {
    uint8_t level = FAKE_ROW_IDLE_LEVEL;   /* 行线空闲电平由内部上下拉决定 */

    for (col = 0U; col < KB_COLS; col++)
    {
      if ((g_active_mask & (uint8_t)(1U << col)) == 0U)
      {
        continue;         /* 该列不在导通电平 -> 二极管反偏 */
      }
      if (((g_keys >> ((row * KB_COLS) + col)) & 1U) != 0U)
      {
        level = FAKE_ROW_PRESSED_LEVEL;
        break;
      }
    }

    if (level != 0U)
    {
      rows |= (uint8_t)(1U << row);
    }
  }

  return rows;
}

void KB_Port_DelayUs(uint32_t us)
{
  (void)us;
}

uint32_t KB_Port_Millis(void)
{
  return g_tick;
}

bool KB_Port_SendReport(uint8_t report[KB_REPORT_SIZE])
{
  if (g_ready == 0U)
  {
    return false;   /* 端点忙：调用方必须稍后重试，且不得记账 */
  }

  if (g_report_count < REP_MAX)
  {
    (void)memcpy(g_reports[g_report_count], report, KB_REPORT_SIZE);
    g_report_count++;
  }

  return true;
}

/* ------------------------------------------------------------------ */
/* 测试辅助                                                            */
/* ------------------------------------------------------------------ */

#define KEY_BIT(row, col)   ((uint16_t)(1U << (((row) * KB_COLS) + (col))))

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
  g_tick = 0U;
  g_keys = 0U;
  g_active_mask = 0U;
  g_ready = 1U;
  g_report_count = 0;
  g_col_log_count = 0;
  g_multi_low_seen = 0;
  (void)memset(g_reports, 0, sizeof(g_reports));
  (void)memset(g_col_log, 0, sizeof(g_col_log));

  KB_Init();
}

static void press(uint16_t keys) { g_keys = keys; }
static void release_all(void) { g_keys = 0U; }

/** 推进 ms 毫秒，每毫秒跑一次 KB_Task() */
static void run_ms(int ms)
{
  int i;

  for (i = 0; i < ms; i++)
  {
    g_tick++;
    KB_Task();
  }
}

static const uint8_t *report_at(int index)
{
  if ((index < 0) || (index >= g_report_count))
  {
    return NULL;
  }
  return g_reports[index];
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

static void test_idle_no_report(void)
{
  TEST_CASE("空闲：不产生任何报表");

  reset_all();
  run_ms(100);

  CHECK_INT(g_report_count, 0, "空闲时上报次数");
  CHECK_INT(g_multi_low_seen, 0, "出现过同时拉低多列");
}

static void test_init_idle_state(void)
{
  TEST_CASE("初始化：所有列处于非导通电平（空闲态不会误触发）");

  reset_all();

  CHECK_INT(g_active_mask, 0, "KB_Init 后仍处于导通电平的列");
  CHECK_INT(KB_GetStableMask(), 0, "初始稳定按键位图");
}

static void test_bounce_single_key(void)
{
  TEST_CASE("单键 + 抖动：只上报一次 0x04('a')，松开归零");

  reset_all();

  /* 前 4ms 模拟机械抖动：低-高-低-高 */
  press(KEY_BIT(0, 0)); run_ms(1);
  release_all();        run_ms(1);
  press(KEY_BIT(0, 0)); run_ms(1);
  release_all();        run_ms(1);

  CHECK_INT(g_report_count, 0, "抖动期间不应上报");

  /* 稳定按下 30ms */
  press(KEY_BIT(0, 0));
  run_ms(30);

  CHECK_INT(g_report_count, 1, "稳定按下后的上报次数");
  if (g_report_count >= 1)
  {
    const uint8_t *r = report_at(0);
    CHECK_U8(r[0], 0x00, "修饰键字节");
    CHECK_U8(r[1], 0x00, "保留字节");
    CHECK_U8(r[2], 0x04, "键码字节（应为 'a'）");
    CHECK_U8(r[3], 0x00, "空槽 1");
    CHECK_U8(r[4], 0x00, "空槽 2");
    CHECK_U8(r[5], 0x00, "空槽 3");
    CHECK_U8(r[6], 0x00, "空槽 4");
    CHECK_U8(r[7], 0x00, "空槽 5");
    printf("   按下报表 = %s\n", fmt_report(r));
  }

  /* 保持按住 200ms：引脚重复（由主机负责），不应重复上报 */
  run_ms(200);
  CHECK_INT(g_report_count, 1, "持续按住时的上报次数");

  /* 松开 */
  release_all();
  run_ms(30);

  CHECK_INT(g_report_count, 2, "松开后的上报次数");
  if (g_report_count >= 2)
  {
    CHECK(is_all_zero(report_at(1)), "松开报表应全 0，实际 = %s", fmt_report(report_at(1)));
  }
}

static void test_all_16_keys_mapping(void)
{
  int i;

  TEST_CASE("16 键全覆盖：键码与坐标映射表一致");

  for (i = 0; i < (int)KB_KEY_COUNT; i++)
  {
    const KB_KeyDef *def = KB_KeyAt((uint16_t)i);
    uint8_t expected_hid;
    uint8_t expected_ch;

    reset_all();

    CHECK(def != NULL, "KB_KeyAt(%d) 返回 NULL", i);
    if (def == NULL)
    {
      continue;
    }

    expected_hid = (uint8_t)(0x04U + i);
    expected_ch = (uint8_t)('a' + i);

    CHECK_U8(def->row, i / (int)KB_COLS, "行号");
    CHECK_U8(def->col, i % (int)KB_COLS, "列号");
    CHECK_U8(def->hid, expected_hid, "HID 键码");
    CHECK_U8(def->ch, expected_ch, "对应字符");

    press((uint16_t)(1U << i));
    run_ms(KB_DEBOUNCE_MS + 5);

    CHECK_INT(g_report_count, 1, "单键上报次数");
    if (g_report_count >= 1)
    {
      const uint8_t *r = report_at(0);

      CHECK_U8(r[2], expected_hid, "上报键码");
      CHECK(r[3] == 0U, "槽 2 应为空（多键误报），实际 = %s", fmt_report(r));
    }
  }

  CHECK(KB_KeyAt((uint16_t)KB_KEY_COUNT) == NULL, "越界索引应返回 NULL");
}

static void test_multi_key_merge(void)
{
  TEST_CASE("多键同按：合并到同一份报表且按索引升序");

  reset_all();

  press((uint16_t)(KEY_BIT(0, 0) | KEY_BIT(0, 1)));
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(g_report_count, 1, "两键同按的上报次数");
  if (g_report_count >= 1)
  {
    const uint8_t *r = report_at(0);

    CHECK_U8(r[2], 0x04, "第一个键码（'a'）");
    CHECK_U8(r[3], 0x05, "第二个键码（'b'）");
    CHECK_U8(r[4], 0x00, "后续槽位应为空");
    printf("   双键报表 = %s\n", fmt_report(r));
  }

  /* 再补按第三个键：应产生新的一份报表 */
  press((uint16_t)(KEY_BIT(0, 0) | KEY_BIT(0, 1) | KEY_BIT(3, 3)));
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(g_report_count, 2, "追加按键后的上报次数");
  if (g_report_count >= 2)
  {
    const uint8_t *r = report_at(1);

    CHECK_U8(r[2], 0x04, "键码 1");
    CHECK_U8(r[3], 0x05, "键码 2");
    CHECK_U8(r[4], 0x13, "键码 3（'p'）");
    printf("   三键报表 = %s\n", fmt_report(r));
  }
}

static void test_six_kro_limit(void)
{
  int i;

  TEST_CASE("8 键同按：按标准 6KRO 只上报前 6 个，不越界");

  reset_all();

  for (i = 0; i < 8; i++)
  {
    g_keys |= (uint16_t)(1U << i);
  }
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(g_report_count, 1, "上报次数");
  if (g_report_count >= 1)
  {
    const uint8_t *r = report_at(0);

    for (i = 0; i < 6; i++)
    {
      CHECK_U8(r[KB_REPORT_OFF_KEYS + i], (uint8_t)(0x04U + i), "前 6 个键码");
    }
    printf("   8 键报表 = %s\n", fmt_report(r));
  }
}

static void test_endpoint_busy_retry(void)
{
  TEST_CASE("端点忙：报表既不丢失也不重复，端点空闲后恰好补发一次");

  reset_all();
  g_ready = 0U;   /* IN 端点忙 */

  press(KEY_BIT(1, 2));   /* 'g' = 0x0A */
  run_ms(50);

  CHECK_INT(g_report_count, 0, "端点忙时不应有报表写入");

  g_ready = 1U;           /* 端点空闲 */
  run_ms(3);

  CHECK_INT(g_report_count, 1, "端点恢复后的上报次数");
  if (g_report_count >= 1)
  {
    CHECK_U8(report_at(0)[2], 0x0AU, "补发的键码（'g'）");
  }

  run_ms(200);
  CHECK_INT(g_report_count, 1, "补发后不应重复上报");

  /* 按键期间端点再次变忙，再松开：状态变化不能被吞掉 */
  release_all();
  g_ready = 0U;
  run_ms(50);
  CHECK_INT(g_report_count, 1, "端点忙时松开不应写入");

  g_ready = 1U;
  run_ms(3);
  CHECK_INT(g_report_count, 2, "端点恢复后应补发全 0 报表");
  if (g_report_count >= 2)
  {
    CHECK(is_all_zero(report_at(1)), "补发的松开报表应全 0，实际 = %s", fmt_report(report_at(1)));
  }
}

static void test_column_scan_sequence(void)
{
  int i;
  int start;

  TEST_CASE("扫描时序：每轮按 0->1->2->3 逐列驱动到导通电平，且任意时刻最多一列导通");

  reset_all();
  g_col_log_count = 0;
  run_ms(1);

  CHECK_INT(g_multi_low_seen, 0, "出现过同时导通多列");
  CHECK_INT(g_col_log_count, (int)KB_COLS, "一轮扫描读取行线的次数");

  start = 0;
  if (g_col_log_count >= (int)KB_COLS)
  {
    for (i = 0; i < (int)KB_COLS; i++)
    {
      CHECK_INT(g_col_log[start + i], i, "第 i 次读取时应处于导通电平的列号");
    }
  }

  /* 再跑 3 轮，确认时序稳定重复 */
  g_col_log_count = 0;
  run_ms(3);
  CHECK_INT(g_col_log_count, (int)(KB_COLS * 3), "3 轮扫描的读取次数");
  for (i = 0; i < g_col_log_count; i++)
  {
    CHECK_INT(g_col_log[i], i % (int)KB_COLS, "连续扫描的列号序列");
  }}

static void test_diode_no_ghost(void)
{
  TEST_CASE("二极管防鬼键：L 形三键同按不会凭空出现第四个键");

  reset_all();

  /* 经典鬼键组合：(r0,c0) + (r0,c1) + (r1,c0) 若无二极管会误判出 (r1,c1) */
  press((uint16_t)(KEY_BIT(0, 0) | KEY_BIT(0, 1) | KEY_BIT(1, 0)));
  run_ms(KB_DEBOUNCE_MS + 5);

  CHECK_INT(g_report_count, 1, "上报次数");
  CHECK((KB_GetStableMask() & KEY_BIT(1, 1)) == 0U,
        "鬼键 (r1,c1) 被误判为按下，稳定位图 = 0x%04X", KB_GetStableMask());

  if (g_report_count >= 1)
  {
    const uint8_t *r = report_at(0);

    CHECK_U8(r[2], 0x04, "键码 1（'a'）");
    CHECK_U8(r[3], 0x05, "键码 2（'b'）");
    CHECK_U8(r[4], 0x08, "键码 3（'e'）");
    CHECK_U8(r[5], 0x00, "不应出现第 4 个键码");
    printf("   三键报表 = %s\n", fmt_report(r));
  }
}

int main(void)
{
  printf("=== TinyKey 逻辑层测试（keyboard.c） ===\n\n");

  test_idle_no_report();
  test_init_idle_state();
  test_bounce_single_key();
  test_all_16_keys_mapping();
  test_multi_key_merge();
  test_six_kro_limit();
  test_endpoint_busy_retry();
  test_column_scan_sequence();
  test_diode_no_ghost();

  return test_summary("逻辑层");
}
