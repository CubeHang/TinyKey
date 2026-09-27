/**
  ******************************************************************************
  * @file    keyboard.h
  * @brief   TinyKey: 4x4 矩阵键盘 -> USB HID 键盘 的公共接口
  *
  * 本模块分两层：
  *   [逻辑层] keyboard.c   —— 纯 C，无任何 HAL/USB 依赖，可在 PC 上直接编译测试
  *   [移植层] keyboard_port.c —— GPIO 读写、微秒延时、毫秒时钟、USB 上报
  *
  * 扫描思路（二极管阳极在列线侧，极性由 KB_ROW_ACTIVE_HIGH 决定）：
  *   行线 PA0~PA3 内部下拉输入，空闲为低；
  *   依次把每一列拉高、其余列保持低，读行线：为高即该行列交叉键按下；
  *   每个按键串联二极管（按键 -> 阴极，阳极 -> 列线），因此只有被拉高的列具备
  *   导通条件，其余列二极管反偏截止，串扰回路被阻断 —— 鬼键在电气层就不存在。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Exported constants --------------------------------------------------------*/

/** 矩阵规模 */
#define KB_ROWS                 4U
#define KB_COLS                 4U
#define KB_KEY_COUNT            (KB_ROWS * KB_COLS)   /* 16 */

/** 扫描极性（由二极管方向决定）
 *
 *  本工程实际接线：行线 -> 按键 -> 二极管阴极 -> 二极管阳极 -> 列线
 *  二极管只能从列线流向行线，因此：
 *    列线必须"逐列拉高、其余拉低"；
 *    行线必须用内部下拉保持默认低，读到高电平才表示按下。
 *  故 KB_ROW_ACTIVE_HIGH = 1。
 *
 *  若你的板子二极管方向相反（阳极在行线侧），把本宏改成 0 即可：
 *  keyboard_port.c 会自动把行线改配为内部上拉、列线改为逐列拉低，扫描逻辑随之反向。
 */
#define KB_ROW_ACTIVE_HIGH      1

/** 扫描周期：主循环节流到每 1 ms 扫描一轮 */
#define KB_SCAN_PERIOD_MS       1U

/** 消抖窗口：连续 KB_DEBOUNCE_MS 次采样一致才认为状态改变（10 次 x 1 ms = 10 ms） */
#define KB_DEBOUNCE_MS          10U

/** 列拉低后等待电平稳定的时间（微秒） */
#define KB_COL_SETTLE_US        5U

/** USB HID 键盘报表长度：修饰键(1) + 保留(1) + 键码(6) */
#define KB_REPORT_SIZE          8U

/** 标准键盘协议上限：除修饰键外最多同时上报 6 个键码（6KRO） */
#define KB_MAX_KEYS             6U

/** 报表内各字段的字节偏移 */
#define KB_REPORT_OFF_MODIFIER  0U
#define KB_REPORT_OFF_RESERVED  1U
#define KB_REPORT_OFF_KEYS      2U

/* Exported types ------------------------------------------------------------*/

/** 一个物理按键的定义 */
typedef struct
{
  uint8_t row;      /* 行号 0~3 */
  uint8_t col;      /* 列号 0~3 */
  uint8_t hid;      /* HID 键盘键码（Usage ID） */
  char    ch;       /* 对应字符，仅用于调试打印 */
} KB_KeyDef;

/* Exported variables --------------------------------------------------------*/

/** 键码映射表，下标 = row * KB_COLS + col，共 KB_KEY_COUNT 项 */
extern const KB_KeyDef KB_KeyMap[KB_KEY_COUNT];

/* Exported functions -- 逻辑层 ----------------------------------------------*/

/**
  * @brief  初始化键盘状态机并初始化移植层
  */
void KB_Init(void);

/**
  * @brief  主循环周期调用；内部自行节流到 KB_SCAN_PERIOD_MS，
  *         并在必要时把「期望报表」通过 USB 发出（端点忙时下个周期自动重试）
  */
void KB_Task(void);

/**
  * @brief  读取当前稳定按键位图
  * @retval bit(row * KB_COLS + col) 为 1 表示该键稳定按下
  */
uint16_t KB_GetStableMask(void);

/**
  * @brief  由稳定按键位图生成 8 字节 HID 键盘报表
  * @param  mask   按键位图
  * @param  report 输出缓冲，长度必须 >= KB_REPORT_SIZE
  */
void KB_BuildReport(uint16_t mask, uint8_t report[KB_REPORT_SIZE]);

/**
  * @brief  按键定义查询
  * @param  key_index 0 ~ KB_KEY_COUNT-1
  * @retval 按键定义指针；索引越界返回 NULL
  */
const KB_KeyDef *KB_KeyAt(uint16_t key_index);

/* Exported functions -- 移植层（目标工程由 keyboard_port.c 实现，宿主机测试提供桩）---*/

/** 初始化 GPIO 与延时资源 */
void     KB_Port_Init(void);

/** 拉高（KB_ROW_ACTIVE_HIGH=1）第 col 列（0~KB_COLS-1），其余列拉到非导通电平 */
void     KB_Port_SelectColumn(uint8_t col);

/** 所有列回到非导通电平（空闲态，所有二极管反偏，行线不会误触发） */
void     KB_Port_ReleaseColumns(void);

/** 读取 4 根行线原始电平：bit0~bit3 对应 row0~row3，1 = 高电平 */
uint8_t  KB_Port_ReadRows(void);

/** 微秒级忙等延时 */
void     KB_Port_DelayUs(uint32_t us);

/** 毫秒时钟（目标工程直接用 HAL_GetTick） */
uint32_t KB_Port_Millis(void);

/**
  * @brief  尝试上报一帧 8 字节键盘报表
  * @param  report 报表缓冲（非 const，USB 库要求可写指针）
  * @retval true = 已提交给 USB 端点；false = 端点忙或设备未就绪，调用方应稍后重试
  */
bool     KB_Port_SendReport(uint8_t report[KB_REPORT_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* __KEYBOARD_H__ */
