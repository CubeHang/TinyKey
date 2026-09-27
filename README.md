# TinyKey — STM32F103 4×4 矩阵键盘 USB HID 键盘

TinyKey 是一个小而有据可查的嵌入式开源项目：把一块 4×4 带二极管的矩阵键盘做成即插即用的标准 USB 键盘，按下第 N 个键，电脑输入 a~p

| | |
| --- | --- |
| MCU | STM32F103C8T6（Cortex-M3，72 MHz，64 KB Flash / 20 KB RAM） |
| 固件 | STM32CubeMX 6.17 + STM32Cube FW_F1 V1.8.7（HAL 库） |
| USB | Device FS（12 Mbps），HID 键盘类，免驱 |
| 构建 | Keil MDK-ARM（uVision / AC5 / AC6）或 VSCode + EIDE |
| 测试 | 宿主机 MinGW gcc，373 项检查，无需硬件 |

---

## 1. 功能概览

**已实现**

| 功能 | 说明 |
| --- | --- |
| 4×4 矩阵扫描 | 每 1 ms 一轮，逐列驱动、读行线，5 µs 电平建立延时 |
| 硬件防鬼键 | 每键串联二极管，多键同按不会误判出未按下的键 |
| 按键消抖 | 每键独立四态状态机，连续 10 次采样一致才改变状态 |
| 标准 USB 键盘 | 63 字节标准键盘报告描述符，8 字节输入报表，主机免驱识别 |
| 6KRO | 最多同时上报 6 个键码，符合标准键盘协议 |
| 多键合并 | 多个键位的变化合并进同一份报表，不产生中间态抖动 |
| 可靠上报 | 端点忙时不丢包也不重发，规避 ST 库静默丢包问题 |
| 按键 LED | 接收主机下发的 SET_REPORT（NumLock/CapsLock 等），可用 API 读取 |
| 极性可配置 | 二极管方向反过来时改一个宏即可，无需改代码逻辑 |
| 可移植 | 逻辑层与硬件层分离，换 MCU 只需实现 7 个移植层函数 |

**明确不做**（按项目范围裁剪，非缺陷）

- 全键无冲（NKRO）—— 标准 6KRO
- 修饰键（Ctrl/Shift/Alt/GUI）、多媒体键、宏
- 长按连发 —— 字符重复交给操作系统的按键重复机制
- 键盘 LED 物理指示灯 —— 状态已接收，但没有外接 LED

---

## 2. 硬件

### 2.1 引脚连接

| 信号 | STM32 引脚 | GPIO 配置 | 说明 |
| --- | --- | --- | --- |
| row1 ~ row4 | PA0 / PA1 / PA2 / PA3 | 输入 + 内部**下拉** | 空闲为低 |
| column1 ~ column4 | PA4 / PA5 / PA6 / PA7 | 推挽输出 | 空闲为低 |
| USB DM / DP | PA11 / PA12 | USB Device FS | 板上需 1.5 kΩ 上拉（芯片内置） |
| SWDIO / SWCLK | PA13 / PA14 | 调试口 | 保留 |
| HSE 晶振 | PD0 / PD1 | 8 MHz | USB 需要精确时钟，必须外接 |

引脚定义集中在 `Core/Inc/main.h`（`R1_Pin`~`R4_Pin`、`C1_Pin`~`C4_Pin`），由 CubeMX 生成。

### 2.2 二极管方向与扫描极性（关键）

每个按键串联一只二极管，实际接线为：

```
行线 ── 按键 ── 二极管阴极 ──▶|── 二极管阳极 ── 列线
```

二极管符号是「阳极 →|— 阴极」，电流只能从阳极流向阴极，**在本接线下电流只能从列线流向行线**。这决定了扫描极性：

| | 本项目（阳极在列线侧） | 常见方案（阳极在行线侧） |
| --- | --- | --- |
| 行线 | 内部**下拉**，空闲低 | 内部上拉，空闲高 |
| 列线 | 逐列**拉高**，其余低 | 逐列拉低，其余高 |
| 判定 | 行读到**高** = 按下 | 行读到**低** = 按下 |
| 空闲态 | 所有列输出**低** | 所有列输出高 |

> ⚠️ 如果按常见方案（行线上拉 + 逐列拉低）驱动本硬件，二极管永远反偏截止，**所有按键都读不到**。

**电气裕量**：列拉高且按键按下时，行线节点电位为「3.3 V 经二极管压降 + 40 kΩ 内部下拉」，
约 **2.7 V**，高于 STM32F103 的 VIH(min) = 2.0 V，判定可靠。行线节点 RC 约 1 µs，5 µs 建立延时足够。

**极性开关**：`Core/Inc/keyboard.h` 的 `KB_ROW_ACTIVE_HIGH`

- `1`（默认）：阳极在列线侧，下拉输入 + 逐列拉高
- `0`：阳极在行线侧，上拉输入 + 逐列拉低

改成 `0` 后，`Core/Src/keyboard_port.c` 会自动把行线重配为内部上拉、列线改为逐列拉低，扫描判定同步反向，无需改其他代码。

### 2.3 电路要点

- 二极管：任意小信号开关管即可（1N4148、1N4148W、BAT54 等），正向电流仅约 70 µA
- 无需外部上拉/下拉电阻，使用 STM32 内部上下拉（约 40 kΩ）
- USB 总线供电，整机电流远低于配置描述符声明的 100 mA

---

## 3. 工作原理

### 3.1 矩阵扫描

一轮扫描（每 1 ms 执行一次）：

1. 所有列输出非导通电平（低）—— 空闲态，所有二极管反偏，行线全部保持低，不会误触发
2. 对 `col = 0 .. 3` 依次执行：
   - 把该列驱动到导通电平（高），其余列保持低
   - 延时 5 µs 等待电平建立
   - 读取 4 根行线：**行线为高 → 该行与当前列的交叉键按下**
3. 扫描结束，所有列恢复低

伪代码：

```c
for (col = 0; col < 4; col++) {
    KB_Port_SelectColumn(col);        /* 只让这一列具备导通条件 */
    KB_Port_DelayUs(5);
    uint8_t rows = KB_Port_ReadRows();  /* bit0~3 = row0~3 */
    for (row = 0; row < 4; row++)
        kb_update_key(row * 4 + col, (rows >> row) & 1);  /* 高 = 按下 */
}
KB_Port_ReleaseColumns();
```

### 3.2 为什么二极管能防鬼键

无二极管的矩阵在同时按下 L 形三个键（如 r0c0、r0c1、r1c0）时，电流会经其他按键形成串扰回路，让 MCU 误判出并未按下的 r1c1。

本硬件中，只有**被驱动到导通电平的那一列**，其交叉键的二极管才可能正偏；其余列的二极管阳极处于低电平，反向截止，串扰路径被物理切断。

以经典鬼键组合为例（按下 r0c0、r0c1、r1c0，扫描 c1 列）：即便 r0 被 r0c1 抬高，也无法经 r0c0 反向流回 c0 列（c0 为低，二极管反偏），因此 r1 保持低，**不会**误判 r1c1。该行为由宿主机测试用带二极管的矩阵电气模型验证。

### 3.3 消抖状态机

机械按键在按下/松开瞬间有 10~20 ms 的电平抖动。每个按键维护独立状态机，连续 10 次（10 ms）采样一致才确认状态变化：

| 当前状态 | 本次采样 | 动作 |
| --- | --- | --- |
| 空闲 | 按下 | → 按下消抖，计数清零 |
| 空闲 | 未按下 | 保持空闲 |
| 按下消抖 | 按下 | 计数 +1；达到 10 → **按下保持**，置位按键位图 |
| 按下消抖 | 未按下 | → 空闲（判为抖动，丢弃） |
| 按下保持 | 未按下 | → 释放消抖，计数清零 |
| 按下保持 | 按下 | 保持 |
| 释放消抖 | 未按下 | 计数 +1；达到 10 → **空闲**，清除按键位图 |
| 释放消抖 | 按下 | → 按下保持（判为抖动，恢复原状态） |

抖动导致电平反复时会退回原稳定态，因此只会产生一次干净的状态跳变。

### 3.4 报表生成与「收敛式上报」

由 16 位稳定按键位图生成 8 字节报表：按 `key_index` 升序把 HID 键码填入 6 个槽位，超过 6 个键则截断。

上报采用**收敛式**策略，这是本项目一个容易被忽略但很重要的设计：

```c
KB_BuildReport(稳定位图, desired);
if (memcmp(desired, 已发送报表) != 0) {
    if (KB_Port_SendReport(desired))       /* 端点忙时返回 false */
        memcpy(已发送报表, desired);        /* 只有成功才记账 */
}
```

ST 库的 `USBD_HID_SendReport()` 在 IN 端点忙时**仍然返回 `USBD_OK`，但报表被静默丢弃** —— 这是这类项目「偶尔漏字符」的常见根因。本实现先查询端点状态，失败就不更新「已发送报表」，下个周期带着同一份期望状态重试，因此：

- **不丢**：状态变化不会因为端点忙而消失
- **不重**：只有确认发出后才记账，同一份报表不会重复发送
- **自动合并**：按键变化快于 USB 轮询时，中间态被合并，最终态一定送达

### 3.5 时序与响应

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 扫描周期 | 1 ms | `KB_SCAN_PERIOD_MS` |
| 列建立延时 | 5 µs | `KB_COL_SETTLE_US` |
| 消抖窗口 | 10 ms | `KB_DEBOUNCE_MS` |
| 最坏响应时间 | ≈ 11 ms | 扫描周期 + 消抖窗口，落在机械按键 10~20 ms 的合理范围 |
| USB 轮询间隔 | 10 ms | 端点描述符 `bInterval = 0x0A` |
| 单轮扫描耗时 | 数十 µs | 4 列 × (驱动 + 5 µs + 4 次读引脚)，主循环占用极低 |

---

## 4. USB HID 实现

### 4.1 设备描述

| 项目 | 值 |
| --- | --- |
| VID / PID | `0x0483` / `0x572B`（沿用 ST 示例，HID 键盘用系统自带驱动，无需 .inf） |
| 产品名 | `TinyKey Keyboard` |
| 制造商字符串 | `STMicroelectronics` |
| 设备类 | 接口级 HID：`bInterfaceClass = 0x03`、`bInterfaceSubClass = 0x01`（Boot）、`bInterfaceProtocol = 0x01`（Keyboard） |
| 供电属性 | `bmAttributes = 0x80`（总线供电，不支持远程唤醒） |
| 最大功耗 | 100 mA |

设备管理器 / `lsusb` 里应识别为 **TinyKey Keyboard**，接口协议为键盘。

### 4.2 端点

| 端点 | 地址 | 类型 | 最大包长 | 轮询间隔 |
| --- | --- | --- | --- | --- |
| HID IN | `0x81` | 中断 | 8 字节 | 10 ms |

只有 IN 端点；LED 状态经 EP0 控制传输的 SET_REPORT 下发。

### 4.3 输入报表格式（8 字节）

| 字节 | 内容 |
| --- | --- |
| 0 | 修饰键位图（bit0~7 = LCtrl/LShift/LAlt/LGUI/RCtrl/RShift/RAlt/RGUI）—— 本项目恒为 `0x00` |
| 1 | 保留，固定 `0x00` |
| 2~7 | 最多 6 个 HID 键码，空槽填 `0x00` |

示例（按下第 1 个键）：

```
按下： 00 00 04 00 00 00 00 00     ← 0x04 即键盘 'a'
松开： 00 00 00 00 00 00 00 00
```

### 4.4 报告描述符

63 字节标准键盘报告描述符，声明：

- 8 位修饰键输入 + 8 位保留输入
- 1 字节 LED 输出报表（NumLock / CapsLock / ScrollLock / Compose / Kana）
- 6 字节键码数组输入（Usage 0~101）

长度宏为 `HID_KEYBOARD_REPORT_DESC_SIZE`（`usbd_hid.h`），描述符本体在
`Middlewares/ST/STM32_USB_Device_Library/Class/HID/Src/usbd_hid.c` 的 `HID_KEYBOARD_ReportDesc[]`。
宿主机测试会把它与独立书写的标准键盘描述符逐字节比对。

### 4.5 按键 LED

主机在 NumLock / CapsLock 等状态变化时下发 `SET_REPORT(Output)`，固件已接收（不 STALL），状态存于 HID 句柄：

```c
uint32_t leds = USBD_HID_GetLedState(&hUsbDeviceFS);
/* bit0 NumLock / bit1 CapsLock / bit2 ScrollLock / bit3 Compose / bit4 Kana */
```

若日后外接指示灯，直接读该值驱动 GPIO 即可。

### 4.6 对 ST 原始库的改动清单

本项目基于 CubeMX 生成的「HID 类」工程，ST 默认模板是**摇杆鼠标**。以下改动散布在库文件中，均带 `[TinyKey]` 注释：

| 文件 | 改动 |
| --- | --- |
| `Class/HID/Inc/usbd_hid.h` | `HID_EPIN_SIZE` `0x04` → `0x08`；`HID_MOUSE_REPORT_DESC_SIZE 74` → `HID_KEYBOARD_REPORT_DESC_SIZE 63`；句柄新增 `LedState`；新增 `USBD_HID_IsReady()` / `USBD_HID_GetLedState()` 声明 |
| `Class/HID/Src/usbd_hid.c` | 三份配置描述符：`bmAttributes 0xE0` → `0x80`、`nInterfaceProtocol 0x02` → `0x01`、报告描述符长度宏改名；鼠标描述符数组 → 键盘描述符；`USBD_HID_Setup()` 新增 `HID_REQ_SET_REPORT` 分支；`Init` 初始化 `LedState`；新增 `USBD_HID_IsReady()` / `USBD_HID_GetLedState()` |
| `USB_DEVICE/App/usbd_desc.c` | 产品名字符串 → `TinyKey Keyboard` |
| `USB_DEVICE/App/usb_device.h` | 补 `extern USBD_HandleTypeDef hUsbDeviceFS;`（CubeMX 生成的本文件漏了它，缺了会编译不过） |

> ⚠️ 前两项位于 **Middlewares 库文件**中，**重新用 CubeMX 生成工程会被覆盖回鼠标**。补回步骤见 §14 与设计文档 §8。

---

## 5. 键位映射

索引 = `row * 4 + col`，HID 键码 `0x04`~`0x13` 对应 `a`~`p`：

| 按键 | 行 | 列 | 引脚交叉点 | 字符 | HID 键码 |
| --- | --- | --- | --- | --- | --- |
| Key1 | 0 | 0 | PA0 × PA4 | `a` | `0x04` |
| Key2 | 0 | 1 | PA0 × PA5 | `b` | `0x05` |
| Key3 | 0 | 2 | PA0 × PA6 | `c` | `0x06` |
| Key4 | 0 | 3 | PA0 × PA7 | `d` | `0x07` |
| Key5 | 1 | 0 | PA1 × PA4 | `e` | `0x08` |
| Key6 | 1 | 1 | PA1 × PA5 | `f` | `0x09` |
| Key7 | 1 | 2 | PA1 × PA6 | `g` | `0x0A` |
| Key8 | 1 | 3 | PA1 × PA7 | `h` | `0x0B` |
| Key9 | 2 | 0 | PA2 × PA4 | `i` | `0x0C` |
| Key10 | 2 | 1 | PA2 × PA5 | `j` | `0x0D` |
| Key11 | 2 | 2 | PA2 × PA6 | `k` | `0x0E` |
| Key12 | 2 | 3 | PA2 × PA7 | `l` | `0x0F` |
| Key13 | 3 | 0 | PA3 × PA4 | `m` | `0x10` |
| Key14 | 3 | 1 | PA3 × PA5 | `n` | `0x11` |
| Key15 | 3 | 2 | PA3 × PA6 | `o` | `0x12` |
| Key16 | 3 | 3 | PA3 × PA7 | `p` | `0x13` |

矩阵视图：

| | col0 (PA4) | col1 (PA5) | col2 (PA6) | col3 (PA7) |
| --- | --- | --- | --- | --- |
| **row0 (PA0)** | `a` | `b` | `c` | `d` |
| **row1 (PA1)** | `e` | `f` | `g` | `h` |
| **row2 (PA2)** | `i` | `j` | `k` | `l` |
| **row3 (PA3)** | `m` | `n` | `o` | `p` |

改键位只需编辑 `Core/Src/keyboard.c` 里的 `KB_KeyMap[]`（每个键一行，四个字段：行、列、HID 键码、调试用字符）。

---

## 6. 代码结构

```
TinyKey/
├── Core/
│   ├── Inc/
│   │   ├── keyboard.h            键盘模块配置宏 + 全部接口（含极性开关）
│   │   ├── main.h                CubeMX 生成的引脚定义
│   │   └── ...
│   └── Src/
│       ├── keyboard.c            ★ 逻辑层（纯 C，无 HAL/USB 依赖，可在 PC 上直接测试）
│       ├── keyboard_port.c       ★ 移植层（GPIO、微秒延时、毫秒时钟、USB 上报）
│       ├── main.c                仅在两处 USER CODE 区挂接 KB_Init() / KB_Task()
│       ├── gpio.c                行线下拉输入、列线推挽输出（CubeMX）
│       └── ...
├── USB_DEVICE/
│   ├── App/usb_device.c          USB 设备初始化，注册 HID 类
│   ├── App/usb_device.h          补了 hUsbDeviceFS 的 extern 声明
│   ├── App/usbd_desc.c           设备/字符串描述符（产品名 TinyKey Keyboard）
│   └── Target/usbd_conf.[ch]     USB 底层配置（PMA、静态内存分配、bInterval）
├── Middlewares/ST/STM32_USB_Device_Library/
│   ├── Core/                     ST USB 设备协议栈（未改动）
│   └── Class/HID/                ★ HID 类驱动，已由鼠标改为键盘
├── Drivers/                      STM32F1 HAL + CMSIS（CubeMX 生成，未改动）
├── MDK-ARM/                      Keil 工程、EIDE 配置、启动文件
├── tests/                        ★ 宿主机测试（MinGW gcc），见 tests/README.md
├── docs/plans/                   ★ 设计文档
├── TinyKey.ioc                   CubeMX 工程配置
└── README.md
```

分层的核心目的是**可测试性**：

```
main.c
  └─ KB_Init() / KB_Task()            Core/Src/keyboard.c       逻辑层
        └─ KB_Port_Init / SelectColumn / ReleaseColumns
           ReadRows / DelayUs / Millis / SendReport
                                      Core/Src/keyboard_port.c  移植层
              ├─ HAL_GPIO_*  ──▶  PA0~PA7
              └─ USBD_HID_*  ──▶  USB_DEVICE + Middlewares HID 类
```

逻辑层不包含任何 HAL/USB 头文件，所有硬件操作经移植层接口完成。因此可以在电脑上用 gcc 编译
`keyboard.c` 并配合假移植层跑完整测试，无需硬件。

---

## 7. 配置项

全部可调参数集中在 `Core/Inc/keyboard.h`：

| 宏 | 默认值 | 含义 | 何时调整 |
| --- | --- | --- | --- |
| `KB_ROW_ACTIVE_HIGH` | `1` | 扫描极性，由二极管方向决定 | 二极管阳极接行线时改为 `0` |
| `KB_SCAN_PERIOD_MS` | `1` | 扫描周期（ms） | 主循环负载高时放宽 |
| `KB_DEBOUNCE_MS` | `10` | 消抖窗口（ms） | 按键抖动大则加大，追求低延迟则减小 |
| `KB_COL_SETTLE_US` | `5` | 列电平建立延时（µs） | 接线长、容性大时加大 |
| `KB_ROWS` / `KB_COLS` | `4` / `4` | 矩阵规模 | 换用其他规模矩阵 |
| `KB_MAX_KEYS` | `6` | 同时上报键数上限 | 标准键盘协议固定 6，不建议改 |

---

## 8. 接口说明

### 8.1 逻辑层 API（`keyboard.c`）

```c
void             KB_Init(void);                             /* 初始化状态机 + 移植层 */
void             KB_Task(void);                             /* 主循环周期调用，内部按 1ms 节流 */
uint16_t         KB_GetStableMask(void);                    /* 稳定按键位图，bit = row*4+col */
void             KB_BuildReport(uint16_t mask, uint8_t report[8]);  /* 位图 → 8 字节报表 */
const KB_KeyDef *KB_KeyAt(uint16_t key_index);              /* 按键定义查询，越界返回 NULL */
extern const KB_KeyDef KB_KeyMap[16];                       /* 键码映射表 */
```

`KB_Task()` 非阻塞：未到扫描时刻会立即返回，不影响 USB 中断响应。

### 8.2 移植层接口契约（移植到其他 MCU 只需实现这 7 个）

```c
void     KB_Port_Init(void);                              /* 配置 GPIO 与延时资源 */
void     KB_Port_SelectColumn(uint8_t col);               /* 第 col 列到导通电平，其余列非导通 */
void     KB_Port_ReleaseColumns(void);                    /* 所有列回到非导通电平 */
uint8_t  KB_Port_ReadRows(void);                          /* bit0~3 = row0~3，1 = 高电平 */
void     KB_Port_DelayUs(uint32_t us);                    /* 微秒级忙等 */
uint32_t KB_Port_Millis(void);                            /* 毫秒时钟 */
bool     KB_Port_SendReport(uint8_t report[8]);           /* true = 已提交；false = 端点忙，稍后重试 */
```

**移植步骤**

1. 实现上述 7 个函数（参考 `Core/Src/keyboard_port.c`，约 120 行）
2. 修改 `KB_ROW_ACTIVE_HIGH` 匹配新硬件的二极管方向
3. 按新引脚改 `KB_Port_*` 里的端口/引脚数组
4. 若引脚不再全在同一个 GPIO 端口，把 `KB_GPIO_PORT` 拆成行/列两个端口宏

---

## 9. 构建

### 9.1 Keil MDK-ARM（uVision）

打开 `MDK-ARM/TinyKey.uvprojx`，直接 Build。内存映射已配置：

```
IROM 0x08000000 / 0x10000（64 KB）
IRAM 0x20000000 / 0x5000 （20 KB）
```

### 9.2 VSCode + EIDE

打开 `MDK-ARM/TinyKey.code-workspace`，执行 **Rebuild**。

> ⚠️ `.eide/eide.yml` 中 AC5/AC6 的 `storageLayout` **必须填写实际地址**。
> 若留空（默认 `size: ""`、`startAddr: ""`），EIDE 会生成全零的 scatter 文件，链接必然报
> `L6406E: No space in execution regions` 与 `L6407E: Sections ... could not fit`。
> 本项目已填好；若发现 `MDK-ARM/build/TinyKey/TinyKey.sct` 又变成一串 `0x0000000000`，
> 就是配置被 EIDE 覆盖了，重新填写或在 EIDE 项目属性的内存布局里设置。

### 9.3 资源占用（实测）

| 工具链 | Flash (RO + RW) | RAM (RW + ZI) | Flash 余量 | RAM 余量 |
| --- | --- | --- | --- | --- |
| AC5（armcc，-O3） | 33720 B（32.9 KB） | 4056 B（4.0 KB） | 49% | 80% |
| AC6（armclang，-O0） | 28312 B（27.6 KB） | 4056 B（4.0 KB） | 57% | 80% |

两种工具链都实测编译 + 链接通过，无需裁剪 HAL 模块。

### 9.4 工具链缓存

`MDK-ARM/.pack/`（Keil STM32F1xx_DFP 器件包，77 MB）与 `MDK-ARM/.cmsis/`（CMSIS 头文件缓存，4 MB）
未入库，克隆后由 EIDE / Keil 自动重新获取。仓库实际入库体积约 3.9 MB。

---

## 10. 测试

逻辑、移植层与 USB 描述符都能在电脑上验证，**不需要硬件**：

```powershell
pwsh -File tests/run_tests.ps1
```

三组测试共 **373 项检查**：

| 测试 | 链接的真实源码 | 覆盖内容 |
| --- | --- | --- |
| `kb_logic_test`（193 项） | `Core/Src/keyboard.c` | 空闲不误触发、抖动抑制、16 键映射、多键合并、6KRO 截断、端点忙时不丢不重、列扫描时序、防鬼键 |
| `kb_port_test`（100 项） | `keyboard.c` + `keyboard_port.c` | 引脚映射、扫描极性、GPIO 列驱动/行读取、端到端出字符、端到端防鬼键、端到端端点忙重试 |
| `hid_desc_test`（80 项） | 真实 `usbd_hid.c` | 配置描述符字节（HID 类 / Boot 子类 / 键盘协议 / 总线供电 / 中断 IN 8 字节）、`GET_DESCRIPTOR` 返回的 63 字节报告描述符逐字节比对、`SET_REPORT` 行为、`USBD_HID_IsReady` 状态机 |

测试替身位于 `tests/shims/`，其中 `fake_hal.c` 内置了**带二极管的 4×4 矩阵电气模型**：按「列驱动状态 + 实际配置的上下拉 + 按键集合」实时算出行线电平。若极性配错，相关用例会直接失败，而不是在板子上表现为「按键没反应」。详见 `tests/README.md`。

**测试覆盖不到、需上板确认**：二极管实际方向、USB 枚举、真实 RC 建立时间与按键手感。

---

## 11. 烧录与上板验证

1. 编译（Keil 或 EIDE），得到 `MDK-ARM/build/TinyKey/TinyKey.hex`
2. 用 ST-Link / OpenOCD / J-Link 下载
3. 验证清单：
   - 设备管理器（或 `lsusb`）出现 **TinyKey Keyboard**，类别为键盘
   - 打开记事本，按 Key1 应输入 `a`，Key16 应输入 `p`
   - 快速连按不丢字符、不多字符
   - 同时按 3~4 个键（同列/同行/交叉）不应出现未按下的字符
4. 若某个键无反应：量该行/该列二极管方向与焊接，再确认该交叉点的行列编号与 §5 表格一致

---

## 12. 故障排查

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| 电脑识别为鼠标 / 摇杆 | 烧的是旧固件，或 HID 描述符被 CubeMX 覆盖回鼠标 | 重新编译烧录；按 §4.6 与 §14 补回描述符改动 |
| 设备完全无反应 | USB 时钟不是精确 48 MHz（HSE 未接或 PLL 配置错） | 确认 PD0/PD1 接 8 MHz 晶振，`SystemClock_Config` 中 USB 预分频为 1.5 |
| 所有按键都无反应 | 扫描极性与二极管方向不匹配 | 按 §2.2 切换 `KB_ROW_ACTIVE_HIGH` |
| 个别按键无反应 / 串键 | 该交叉点二极管方向焊反或虚焊；行列接线与表格不符 | 量二极管方向；核对行列编号 |
| 偶发漏字符 / 重复字符 | 上报未做端点忙判断（旧实现） | 已由收敛式上报解决；确认 `KB_Port_SendReport` 检查了 `USBD_HID_IsReady` |
| 同一个键连续输入多个字符 | 消抖窗口太小 | 加大 `KB_DEBOUNCE_MS` |
| 按键响应迟钝 | 消抖窗口太大 | 减小 `KB_DEBOUNCE_MS` |
| 链接报 `L6406E: No space in execution regions` | EIDE 存储区尺寸为空，scatter 全零 | 见 §9.2 |
| 编辑器报 `无法打开源文件 stdint.h` | IntelliSense 配置失效（与编译无关） | `MDK-ARM/.vscode/c_cpp_properties.json` 的 `compilerPath` 指向本机 Keil 安装路径；按需修改后 Reload Window |

---

## 13. 已知限制与可扩展方向

**限制**

- 标准 6KRO，16 键全按只上报前 6 个
- 无修饰键、多媒体键、宏、层切换
- 无长按连发（由操作系统按键重复负责）
- 硬件行为尚未在实机上验证（作者无板）

**可扩展方向**

- NKRO：把报告描述符的键码部分改为位图（如 16 位或 32 位），报表长度相应调整，`KB_BuildReport` 改为置位
- 修饰键：把 `KB_ROW_ACTIVE_HIGH` 这类矩阵位置映射到 HID 修饰键位（报表 byte0）
- 媒体键：增加一个 Usage Page 0x0C 的报表 ID
- 按键 LED 指示灯：读 `USBD_HID_GetLedState()` 驱动 GPIO
- 长按连发：在状态机中增加「按下保持」计时

---

## 14. 维护注意：CubeMX 重新生成工程后

以下改动会被 CubeMX 覆盖，需按此清单补回（约 5 分钟）：

1. `Class/HID/Inc/usbd_hid.h`：`HID_EPIN_SIZE` → `0x08`；描述符长度宏改名与改值；句柄加 `LedState`；加两个新函数声明
2. `Class/HID/Src/usbd_hid.c`：三份配置描述符的 `bmAttributes` → `0x80`、`nInterfaceProtocol` → `0x01`、描述符长度宏；鼠标描述符 → 键盘描述符；`Setup` 加 `HID_REQ_SET_REPORT`；补两个新函数
3. `Core/Inc/keyboard.h`、`Core/Src/keyboard.c`、`Core/Src/keyboard_port.c` 会被移出 Keil 工程 → 重新加入 `Application/User/Core` 分组（源文件本身不会删除）
4. `usbd_desc.c` 产品名会被改回 `STM32 Human interface`
5. `main.c`、`usb_device.h` 的改动位于 `USER CODE` 保护区，会保留
6. `.ioc` 中的 GPIO 上下拉已同步修改，正常重新生成会保持正确

完整排查过程与更多细节见 [docs/plans/2026-09-23-tinykey-keyboard-design.md](docs/plans/2026-09-23-tinykey-keyboard-design.md)。

---

## 15. 许可证

本仓库尚未指定开源许可证（默认保留所有权利）。若希望他人复用，建议补充 MIT 或 Apache-2.0 等许可证文件。
