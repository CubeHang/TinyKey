/**
  ******************************************************************************
  * @file    fake_usb_desc.c
  * @brief   描述符测试用的 USB 库桩：捕获 usbd_hid.c 真正交给主机的那些字节
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#include "fake_platform.h"
#include <string.h>

static uint8_t  s_send_buf[512];
static uint16_t s_send_len;
static int      s_send_count;
static int      s_error_count;
static int      s_prepare_rx_count;
static uint16_t s_prepare_rx_len;
static uint8_t  s_open_ep_addr;
static uint8_t  s_open_ep_type;
static uint16_t s_open_ep_mps;
static int      s_open_ep_count;

void FakeDesc_Reset(void)
{
  s_send_len = 0U;
  s_send_count = 0;
  s_error_count = 0;
  s_prepare_rx_count = 0;
  s_prepare_rx_len = 0U;
  s_open_ep_addr = 0U;
  s_open_ep_type = 0U;
  s_open_ep_mps = 0U;
  s_open_ep_count = 0;
  (void)memset(s_send_buf, 0, sizeof(s_send_buf));
}

const uint8_t *FakeDesc_CtlSendDataBuf(void) { return s_send_buf; }
uint16_t       FakeDesc_CtlSendDataLen(void) { return s_send_len; }
int            FakeDesc_CtlSendDataCount(void) { return s_send_count; }
int            FakeDesc_CtlErrorCount(void) { return s_error_count; }
int            FakeDesc_PrepareRxCount(void) { return s_prepare_rx_count; }
uint16_t       FakeDesc_PrepareRxLen(void) { return s_prepare_rx_len; }
uint8_t        FakeDesc_OpenEpAddr(void) { return s_open_ep_addr; }
uint8_t        FakeDesc_OpenEpType(void) { return s_open_ep_type; }
uint16_t       FakeDesc_OpenEpMps(void) { return s_open_ep_mps; }
int            FakeDesc_OpenEpCount(void) { return s_open_ep_count; }

/* ---- usbd_conf.h 声明的静态分配 -------------------------------------- */
void *USBD_static_malloc(uint32_t size)
{
  static uint32_t mem[16];
  (void)size;
  (void)memset(mem, 0, sizeof(mem));
  return mem;
}

void USBD_static_free(void *p)
{
  (void)p;
}

/* ---- 底层端点操作 ---------------------------------------------------- */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps)
{
  (void)pdev;
  s_open_ep_addr = ep_addr;
  s_open_ep_type = ep_type;
  s_open_ep_mps = ep_mps;
  s_open_ep_count++;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  (void)pdev;
  (void)ep_addr;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                    uint8_t *pbuf, uint16_t size)
{
  (void)pdev;
  (void)ep_addr;
  (void)pbuf;
  (void)size;
  return USBD_OK;
}

/* ---- EP0 控制传输 ---------------------------------------------------- */
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
  (void)pdev;

  if (len > (uint16_t)sizeof(s_send_buf))
  {
    len = (uint16_t)sizeof(s_send_buf);
  }

  (void)memcpy(s_send_buf, pbuf, len);
  s_send_len = len;
  s_send_count++;

  return USBD_OK;
}

void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  (void)pdev;
  (void)req;
  s_error_count++;
}

USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
  (void)pdev;
  (void)pbuf;
  s_prepare_rx_len = len;
  s_prepare_rx_count++;
  return USBD_OK;
}
