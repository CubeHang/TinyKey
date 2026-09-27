/**
  ******************************************************************************
  * @file    fake_usb_report.c
  * @brief   移植层测试用的假 USB HID 上报通道
  *
  * 刻意复现 ST 库的一个真实行为：USBD_HID_SendReport() 在端点忙时**仍然返回
  * USBD_OK，但报表被静默丢弃**。因此置 ready=0 时本假实现不记录任何报表，
  * 用来验证 keyboard.c 的收敛式上报确实做到了“不丢不重”。
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "usbd_def.h"
#include "usbd_hid.h"
#include "usb_device.h"
#include "fake_platform.h"
#include <string.h>

USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t  s_ready;
static uint8_t  s_reports[FAKE_REPORT_LOG][8];
static uint16_t s_report_len[FAKE_REPORT_LOG];
static int      s_report_count;

void FakeUsb_Reset(void)
{
  s_ready = 1U;
  s_report_count = 0;
  (void)memset(s_reports, 0, sizeof(s_reports));
  (void)memset(s_report_len, 0, sizeof(s_report_len));
}

void FakeUsb_SetReady(uint8_t ready)
{
  s_ready = (ready != 0U) ? 1U : 0U;
}

int FakeUsb_ReportCount(void)
{
  return s_report_count;
}

const uint8_t *FakeUsb_ReportAt(int index)
{
  if ((index < 0) || (index >= s_report_count))
  {
    return NULL;
  }
  return s_reports[index];
}

uint16_t FakeUsb_ReportLenAt(int index)
{
  if ((index < 0) || (index >= s_report_count))
  {
    return 0U;
  }
  return s_report_len[index];
}

uint8_t USBD_HID_IsReady(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return s_ready;
}

uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  (void)pdev;

  if (s_ready == 0U)
  {
    return (uint8_t)USBD_OK;   /* 真实库的坑：忙时照样返回 OK，但数据丢了 */
  }

  if (s_report_count < FAKE_REPORT_LOG)
  {
    uint16_t n = (len < 8U) ? len : 8U;

    (void)memcpy(s_reports[s_report_count], report, n);
    s_report_len[s_report_count] = len;
    s_report_count++;
  }

  return (uint8_t)USBD_OK;
}
