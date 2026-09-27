/**
  ******************************************************************************
  * @file    fake_platform.h
  * @brief   宿主机测试用到的假 GPIO / 假 USB 钩子声明
  ******************************************************************************
  */
#ifndef __FAKE_PLATFORM_H__
#define __FAKE_PLATFORM_H__

#include <stdint.h>

/** 记录列驱动/上报序列的最大长度 */
#define FAKE_LOG_MAX      4096
#define FAKE_REPORT_LOG   64

/* ---- 假 GPIO + 二极管矩阵模型（fake_hal.c） -------------------------- */

/** 复位 GPIO 与矩阵模型（清空日志、按键集合、IDR/ODR） */
void     FakeGpio_Reset(void);

/** 设置当前"物理按下"的按键集合，bit(row*4+col) = 1 */
void     FakeMatrix_SetKeys(uint16_t mask);
uint16_t FakeMatrix_GetKeys(void);

/** 取每次读行线时"处于导通电平的列号"日志：0~3 = 该列处于导通电平，-1 = 没有任何列 */
const int8_t *FakeGpio_GetActiveColLog(int *count);

/** 是否出现过"同时有两列以上处于导通电平"（正常扫描绝不允许） */
int      FakeGpio_MultiLowSeen(void);

/** 移植层给行线配置的上下拉（GPIO_PULLUP / GPIO_PULLDOWN / GPIO_NOPULL） */
uint32_t FakeGpio_GetRowPull(void);

/** 行线上下拉与 KB_ROW_ACTIVE_HIGH 期望的极性是否不一致（1 = 配置错了） */
int      FakeGpio_PullMismatch(void);

/** 追加一次时间推进（毫秒） */
void     FakeGpio_AdvanceTick(uint32_t ms);
uint32_t FakeGpio_GetTick(void);
void     FakeGpio_SetTick(uint32_t ms);

/* ---- 假 USB 上报路径（fake_usb_report.c，仅移植层测试链接） ---------- */

void            FakeUsb_Reset(void);
void            FakeUsb_SetReady(uint8_t ready);
int             FakeUsb_ReportCount(void);
const uint8_t  *FakeUsb_ReportAt(int index);
uint16_t        FakeUsb_ReportLenAt(int index);

/* ---- 假 USB 描述符路径（fake_usb_desc.c，仅描述符测试链接） ---------- */

void            FakeDesc_Reset(void);
const uint8_t  *FakeDesc_CtlSendDataBuf(void);
uint16_t        FakeDesc_CtlSendDataLen(void);
int             FakeDesc_CtlSendDataCount(void);
int             FakeDesc_CtlErrorCount(void);
int             FakeDesc_PrepareRxCount(void);
uint16_t        FakeDesc_PrepareRxLen(void);
uint8_t         FakeDesc_OpenEpAddr(void);
uint8_t         FakeDesc_OpenEpType(void);
uint16_t        FakeDesc_OpenEpMps(void);
int             FakeDesc_OpenEpCount(void);

#endif /* __FAKE_PLATFORM_H__ */
