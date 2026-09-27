# TinyKey 宿主机测试

目标工程用 Keil MDK（armcc/armclang）编译，本机没有 ARM 工具链，因此这里用 **MinGW gcc**
把**与目标完全相同的源码**在电脑上编译运行，在烧录前把逻辑、移植层和 USB 描述符都验证一遍。

## 运行

```powershell
pwsh -File tests/run_tests.ps1
```

全绿时退出码为 0，任一用例失败为 1。产物在 `tests/build/`。

## 三组测试

| 测试 | 链接的真实源码 | 验证内容 |
| --- | --- | --- |
| `kb_logic_test` | `Core/Src/keyboard.c` | 空闲不误触发、抖动抑制、16 键映射、多键合并、6KRO 截断、端点忙时不丢不重、扫描时序、二极管防鬼键 |
| `kb_port_test` | `keyboard.c` + `keyboard_port.c` | 引脚映射、扫描极性、GPIO 列驱动/行读取、端到端出字符、端到端防鬼键、端到端端点忙重试 |
| `hid_desc_test` | `Middlewares/.../Class/HID/Src/usbd_hid.c` | 配置描述符字节（HID 类 / Boot 子类 / 键盘协议 / 总线供电 / 中断 IN 8 字节）、`GET_DESCRIPTOR` 返回的 63 字节报告描述符逐字节比对、`SET_REPORT`(LED) 行为、`USBD_HID_IsReady` 状态机 |

## 测试替身（`tests/shims/`）

| 文件 | 作用 |
| --- | --- |
| `stm32f1xx.h` / `stm32f1xx_hal.h` | 最小 HAL 替身。命名与真实头文件相同但**不加入目标的 include 路径**，只在宿主机测试里顶替真实 HAL |
| `fake_hal.c` | 假 GPIO + **带二极管的 4x4 矩阵电气模型**：按「列驱动状态 + 配置的上下拉 + 按键集合」实时算出行线电平；每次读引脚记录导通列号，并标记「同时导通多列」 |
| `fake_usb_report.c` | 假 HID 上报通道。刻意复现 ST 库「端点忙时仍返回 OK 但静默丢包」的行为，用来验证收敛式上报确实不丢不重 |
| `fake_usb_desc.c` | 捕获型 USB 库桩：抓取 `USBD_CtlSendData` / `USBD_LL_OpenEP` / `USBD_CtlPrepareRx` 的入参 |
| `test_util.h` / `test_util.c` | 断言与统计 |

真实 `main.h`（`Core/Inc/main.h`）**没有**被替身覆盖，因此测试校验的就是工程里真实的引脚宏
（R1~R4 = PA0~PA3、C1~C4 = PA4~PA7）。

## 端口/极性说明

假 HAL 的矩阵模型默认按 `KB_ROW_ACTIVE_HIGH` 解释二极管方向，并检查移植层**实际配置的**
行线上下拉是否与之一致（`FakeGpio_PullMismatch()`）。若把行线配成上拉、或改了极性宏却忘了
同步，相关用例会直接失败，而不是在板子上表现为"按键没反应"。

## 局限

宿主机测试覆盖不到的部分需要在真机上确认：

- 二极管的实际方向与焊接
- USB 线序、枚举后设备管理器是否出现 `TinyKey Keyboard`
- 真实 RC 建立时间与按键手感（必要时调 `KB_COL_SETTLE_US`、`KB_DEBOUNCE_MS`）
