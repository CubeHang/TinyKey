/**
  ******************************************************************************
  * @file    hid_desc_test.c
  * @brief   HID 描述符测试：直接编译**真实的** usbd_hid.c，检查将要交给主机的每一个字节
  *
  * 这一层回答的是「电脑到底会看到什么设备」：
  *   - 配置描述符：HID 类 / Boot 子类 / 键盘协议 / 总线供电
  *   - HID 描述符：报告描述符长度必须是 63
  *   - 端点描述符：中断 IN，wMaxPacketSize 必须是 8
  *   - GET_DESCRIPTOR(HID_REPORT_DESC) 返回的 63 字节必须与标准键盘描述符逐字节一致
  *   - SET_REPORT（主机下发 LED 状态）必须被接受而不是 STALL
  ******************************************************************************
  */
#include "usbd_core.h"
#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "fake_platform.h"
#include "test_util.h"
#include <string.h>

/* 独立参考：标准 USB HID 键盘报告描述符，共 63 字节。
   本数组按 HID 规范条目逐条书写，用于与 usbd_hid.c 中的字节流做逐字节比对。 */
static const uint8_t k_expected_report_desc[] =
{
  0x05, 0x01,   /* Usage Page (Generic Desktop)     */
  0x09, 0x06,   /* Usage (Keyboard)                 */
  0xA1, 0x01,   /* Collection (Application)         */
  0x05, 0x07,   /*   Usage Page (Keyboard)          */
  0x19, 0xE0,   /*   Usage Minimum (Left Control)   */
  0x29, 0xE7,   /*   Usage Maximum (Right GUI)      */
  0x15, 0x00,   /*   Logical Minimum (0)            */
  0x25, 0x01,   /*   Logical Maximum (1)            */
  0x75, 0x01,   /*   Report Size (1)                */
  0x95, 0x08,   /*   Report Count (8)               */
  0x81, 0x02,   /*   Input (Data,Var,Abs)           */
  0x95, 0x01,   /*   Report Count (1)               */
  0x75, 0x08,   /*   Report Size (8)                */
  0x81, 0x01,   /*   Input (Const)                  */
  0x95, 0x05,   /*   Report Count (5)               */
  0x75, 0x01,   /*   Report Size (1)                */
  0x05, 0x08,   /*   Usage Page (LEDs)              */
  0x19, 0x01,   /*   Usage Minimum (Num Lock)       */
  0x29, 0x05,   /*   Usage Maximum (Kana)           */
  0x91, 0x02,   /*   Output (Data,Var,Abs)          */
  0x95, 0x01,   /*   Report Count (1)               */
  0x75, 0x03,   /*   Report Size (3)                */
  0x91, 0x01,   /*   Output (Const)                 */
  0x95, 0x06,   /*   Report Count (6)               */
  0x75, 0x08,   /*   Report Size (8)                */
  0x15, 0x00,   /*   Logical Minimum (0)            */
  0x25, 0x65,   /*   Logical Maximum (101)          */
  0x05, 0x07,   /*   Usage Page (Keyboard)          */
  0x19, 0x00,   /*   Usage Minimum (0)              */
  0x29, 0x65,   /*   Usage Maximum (101)            */
  0x81, 0x00,   /*   Input (Data,Ary,Abs)           */
  0xC0          /* End Collection                   */
};

static USBD_HandleTypeDef s_pdev;

/* 配置描述符内的固定偏移（USB 规范布局，共 34 字节） */
#define OFF_CFG_BMATTR        7U
#define OFF_CFG_MAXPOWER      8U
#define OFF_IF_CLASS          14U
#define OFF_IF_SUBCLASS       15U
#define OFF_IF_PROTOCOL       16U
#define OFF_HID_NUMDESC       23U
#define OFF_HID_REPDESC_TYPE  24U
#define OFF_HID_REPDESC_LEN   25U
#define OFF_EP_ADDR           29U
#define OFF_EP_ATTR           30U
#define OFF_EP_MPS            31U
#define OFF_EP_INTERVAL       33U

static void check_config_descriptor(const char *name, uint8_t *cfg, uint16_t len,
                                    uint8_t expected_interval)
{
  CHECK_INT(len, 34, "配置描述符长度");
  if (len != 34U)
  {
    return;
  }

  CHECK_U8(cfg[0], 0x09, "bLength");
  CHECK_U8(cfg[1], USB_DESC_TYPE_CONFIGURATION, "bDescriptorType");
  CHECK_INT(cfg[2] | ((int)cfg[3] << 8), 34, "wTotalLength");
  CHECK_U8(cfg[4], 1, "bNumInterfaces");
  CHECK_U8(cfg[OFF_CFG_BMATTR], 0x80, "bmAttributes（总线供电、无远程唤醒）");
  CHECK_U8(cfg[OFF_CFG_MAXPOWER], 0x32, "bMaxPower（100mA）");

  /* 接口描述符 */
  CHECK_U8(cfg[9], 0x09, "接口描述符 bLength");
  CHECK_U8(cfg[10], USB_DESC_TYPE_INTERFACE, "bDescriptorType=INTERFACE");
  CHECK_U8(cfg[OFF_IF_CLASS], 0x03, "bInterfaceClass=HID");
  CHECK_U8(cfg[OFF_IF_SUBCLASS], 0x01, "bInterfaceSubClass=Boot");
  CHECK_U8(cfg[OFF_IF_PROTOCOL], 0x01, "bInterfaceProtocol=Keyboard");

  /* HID 描述符 */
  CHECK_U8(cfg[18], 0x09, "HID 描述符 bLength");
  CHECK_U8(cfg[19], HID_DESCRIPTOR_TYPE, "bDescriptorType=HID");
  CHECK_U8(cfg[OFF_HID_NUMDESC], 0x01, "bNumDescriptors");
  CHECK_U8(cfg[OFF_HID_REPDESC_TYPE], HID_REPORT_DESC, "报告描述符类型=0x22");
  CHECK_INT(cfg[OFF_HID_REPDESC_LEN] | ((int)cfg[OFF_HID_REPDESC_LEN + 1U] << 8),
            HID_KEYBOARD_REPORT_DESC_SIZE, "wItemLength=63");

  /* 端点描述符 */
  CHECK_U8(cfg[27], 0x07, "端点描述符 bLength");
  CHECK_U8(cfg[28], USB_DESC_TYPE_ENDPOINT, "bDescriptorType=ENDPOINT");
  CHECK_U8(cfg[OFF_EP_ADDR], 0x81, "bEndpointAddress=EP1 IN");
  CHECK_U8(cfg[OFF_EP_ATTR], USBD_EP_TYPE_INTR, "bmAttributes=Interrupt");
  CHECK_INT(cfg[OFF_EP_MPS] | ((int)cfg[OFF_EP_MPS + 1U] << 8), 8, "wMaxPacketSize=8");
  CHECK_U8(cfg[OFF_EP_INTERVAL], expected_interval, "bInterval");

  printf("   [%s] 类=%u 子类=%u 协议=%u EP包长=%u 报告描述符长度=%u\n",
         name,
         (unsigned)cfg[OFF_IF_CLASS],
         (unsigned)cfg[OFF_IF_SUBCLASS],
         (unsigned)cfg[OFF_IF_PROTOCOL],
         (unsigned)(cfg[OFF_EP_MPS] | ((unsigned)cfg[OFF_EP_MPS + 1U] << 8)),
         (unsigned)(cfg[OFF_HID_REPDESC_LEN] | ((unsigned)cfg[OFF_HID_REPDESC_LEN + 1U] << 8)));

  {
    int i;

    printf("   [%s] 原始字节:", name);
    for (i = 0; i < (int)len; i++)
    {
      printf(" %02X", (unsigned)cfg[i]);
    }
    printf("\n");
    printf("   [%s] HID_FS_BINTERVAL=0x%02X HID_HS_BINTERVAL=0x%02X\n",
           name, (unsigned)HID_FS_BINTERVAL, (unsigned)HID_HS_BINTERVAL);
  }
}

static void test_report_descriptor_size(void)
{
  TEST_CASE("报告描述符长度常量 == 63（标准键盘报表描述符）");

  CHECK_INT(HID_KEYBOARD_REPORT_DESC_SIZE, 63, "HID_KEYBOARD_REPORT_DESC_SIZE");
  CHECK_INT(sizeof(k_expected_report_desc), 63, "参考描述符长度");
  CHECK_INT(HID_EPIN_SIZE, 8, "HID_EPIN_SIZE（键盘报表 8 字节）");
}

static void test_config_descriptors(void)
{
  uint8_t *cfg;
  uint16_t len = 0U;

  TEST_CASE("配置描述符：HID 类 / Boot 子类 / 键盘协议 / 总线供电 / 中断 IN 8 字节");

  FakeDesc_Reset();

  /* STM32F103 只跑全速，主机实际使用的就是 FS 这一份（bInterval = HID_FS_BINTERVAL = 10ms）。
     HS 那份仅作一致性检查。 */
  cfg = USBD_HID.GetFSConfigDescriptor(&len);
  check_config_descriptor("FS", cfg, len, HID_FS_BINTERVAL);

  len = 0U;
  cfg = USBD_HID.GetHSConfigDescriptor(&len);
  check_config_descriptor("HS", cfg, len, HID_HS_BINTERVAL);
}

static void test_endpoint_open(void)
{
  uint8_t status;

  TEST_CASE("USBD_HID_Init：打开中断 IN 端点 0x81，最大包长 8");

  FakeDesc_Reset();
  (void)memset(&s_pdev, 0, sizeof(s_pdev));

  status = USBD_HID.Init(&s_pdev, 0U);

  CHECK_INT(status, USBD_OK, "Init 返回值");
  CHECK_INT(FakeDesc_OpenEpCount(), 1, "OpenEP 调用次数");
  CHECK_U8(FakeDesc_OpenEpAddr(), 0x81, "端点地址");
  CHECK_U8(FakeDesc_OpenEpType(), USBD_EP_TYPE_INTR, "端点类型");
  CHECK_INT(FakeDesc_OpenEpMps(), 8, "端点最大包长");
  CHECK(s_pdev.pClassData != NULL, "pClassData 未分配");
}

static void test_get_report_descriptor(void)
{
  USBD_SetupReqTypedef req;
  uint8_t status;

  TEST_CASE("GET_DESCRIPTOR(HID_REPORT_DESC)：主机拿到的 63 字节与标准键盘描述符一致");

  (void)memset(&req, 0, sizeof(req));
  req.bmRequest = 0x81U;              /* 设备到主机 / 标准 / 设备 */
  req.bRequest = USB_REQ_GET_DESCRIPTOR;
  req.wValue = (uint16_t)((uint16_t)HID_REPORT_DESC << 8);
  req.wIndex = 0U;
  req.wLength = HID_KEYBOARD_REPORT_DESC_SIZE;

  s_pdev.dev_state = USBD_STATE_CONFIGURED;
  FakeDesc_Reset();

  status = USBD_HID.Setup(&s_pdev, &req);

  CHECK_INT(status, USBD_OK, "Setup 返回值");
  CHECK_INT(FakeDesc_CtlSendDataCount(), 1, "CtlSendData 调用次数");
  CHECK_INT(FakeDesc_CtlSendDataLen(), 63, "返回长度");
  CHECK_INT(FakeDesc_CtlErrorCount(), 0, "不应 STALL");

  if (FakeDesc_CtlSendDataLen() == 63U)
  {
    int diff = memcmp(FakeDesc_CtlSendDataBuf(), k_expected_report_desc, 63U);

    CHECK(diff == 0, "报告描述符字节不一致（memcmp=%d）", diff);
    if (diff != 0)
    {
      int i;

      for (i = 0; i < 63; i++)
      {
        uint8_t actual = FakeDesc_CtlSendDataBuf()[i];

        if (actual != k_expected_report_desc[i])
        {
          printf("      偏移 %2d: 期望 0x%02X，实际 0x%02X\n",
                 i, (unsigned)k_expected_report_desc[i], (unsigned)actual);
        }
      }
    }
  }
}

static void test_get_hid_descriptor(void)
{
  USBD_SetupReqTypedef req;
  uint8_t status;

  TEST_CASE("GET_DESCRIPTOR(HID)：返回 9 字节 HID 描述符，声明报告描述符长 63");

  (void)memset(&req, 0, sizeof(req));
  req.bmRequest = 0x81U;
  req.bRequest = USB_REQ_GET_DESCRIPTOR;
  req.wValue = (uint16_t)((uint16_t)HID_DESCRIPTOR_TYPE << 8);
  req.wIndex = 0U;
  req.wLength = 9U;

  s_pdev.dev_state = USBD_STATE_CONFIGURED;
  FakeDesc_Reset();

  status = USBD_HID.Setup(&s_pdev, &req);

  CHECK_INT(status, USBD_OK, "Setup 返回值");
  CHECK_INT(FakeDesc_CtlSendDataLen(), 9, "返回长度");
  if (FakeDesc_CtlSendDataLen() == 9U)
  {
    const uint8_t *d = FakeDesc_CtlSendDataBuf();

    CHECK_U8(d[0], 0x09, "bLength");
    CHECK_U8(d[1], HID_DESCRIPTOR_TYPE, "bDescriptorType=HID");
    CHECK_U8(d[5], 0x01, "bNumDescriptors");
    CHECK_U8(d[6], HID_REPORT_DESC, "报告描述符类型");
    CHECK_U8(d[7], HID_KEYBOARD_REPORT_DESC_SIZE, "wItemLength 低字节");
    CHECK_U8(d[8], 0x00, "wItemLength 高字节");
  }
}

static void test_set_report_accepted(void)
{
  USBD_SetupReqTypedef req;
  uint8_t status;

  TEST_CASE("SET_REPORT(LED)：1 字节被接受，超过 1 字节才 STALL");

  (void)memset(&req, 0, sizeof(req));
  req.bmRequest = 0x21U;              /* 主机到设备 / 类 / 接口 */
  req.bRequest = HID_REQ_SET_REPORT;
  req.wValue = 0x0200U;               /* Output 报表 */
  req.wIndex = 0U;
  req.wLength = 1U;

  s_pdev.dev_state = USBD_STATE_CONFIGURED;
  FakeDesc_Reset();

  status = USBD_HID.Setup(&s_pdev, &req);

  CHECK_INT(status, USBD_OK, "SET_REPORT(1 字节) 返回值");
  CHECK_INT(FakeDesc_PrepareRxCount(), 1, "CtlPrepareRx 调用次数");
  CHECK_INT(FakeDesc_PrepareRxLen(), 1, "接收长度");
  CHECK_INT(FakeDesc_CtlErrorCount(), 0, "不应 STALL");

  req.wLength = 2U;
  FakeDesc_Reset();

  status = USBD_HID.Setup(&s_pdev, &req);

  CHECK_INT(status, USBD_FAIL, "SET_REPORT(2 字节) 应失败");
  CHECK_INT(FakeDesc_CtlErrorCount(), 1, "应调用 CtlError");
  CHECK_INT(FakeDesc_PrepareRxCount(), 0, "不应接收超长数据");
}

static void test_is_ready_and_led_state(void)
{
  USBD_HID_HandleTypeDef *hhid;

  TEST_CASE("USBD_HID_IsReady：未配置或端点忙时返回 0");

  (void)memset(&s_pdev, 0, sizeof(s_pdev));
  s_pdev.pClassData = NULL;
  CHECK_INT(USBD_HID_IsReady(&s_pdev), 0, "pClassData 为空时应返回 0");

  (void)USBD_HID.Init(&s_pdev, 0U);
  s_pdev.dev_state = USBD_STATE_DEFAULT;
  CHECK_INT(USBD_HID_IsReady(&s_pdev), 0, "设备未配置时应返回 0");

  s_pdev.dev_state = USBD_STATE_CONFIGURED;
  CHECK_INT(USBD_HID_IsReady(&s_pdev), 1, "配置完成且端点空闲时应返回 1");

  hhid = (USBD_HID_HandleTypeDef *)s_pdev.pClassData;
  hhid->state = HID_BUSY;
  CHECK_INT(USBD_HID_IsReady(&s_pdev), 0, "端点忙时应返回 0");

  hhid->state = HID_IDLE;
  hhid->LedState = 0x02U;   /* CapsLock */
  CHECK_INT(USBD_HID_GetLedState(&s_pdev), 0x02, "读取 LED 状态");
}

int main(void)
{
  printf("=== TinyKey HID 描述符测试（真实 usbd_hid.c） ===\n\n");

  test_report_descriptor_size();
  test_config_descriptors();
  test_endpoint_open();
  test_get_report_descriptor();
  test_get_hid_descriptor();
  test_set_report_accepted();
  test_is_ready_and_led_state();

  return test_summary("HID 描述符");
}
