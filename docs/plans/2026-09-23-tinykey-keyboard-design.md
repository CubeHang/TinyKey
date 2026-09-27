# TinyKey：STM32F103 4×4 矩阵键盘 → USB HID 键盘 设计文档

日期：2026-09-23
工程：`HAL_Project/TinyKey`（STM32F103C8T6，CubeMX 6.17 + HAL F1 1.8.7，MDK-ARM/Keil + EIDE 双构建配置）

## 1. 目标

按下 4×4 矩阵键盘的第 N 个按键时，向电脑输入对应字符（Key1→`a`，Key2→`b`，… Key16→`p`）。
扫描方案与键位映射遵循用户提供的《STM32 4×4 矩阵键盘扫描方案技术文档》。

## 2. 硬件与引脚（已确认）

| 信号 | 引脚 | 配置 |
| --- | --- | --- |
| row1~row4 | PA0 / PA1 / PA2 / PA3 | 输入 + 内部上拉 |
| column1~column4 | PA4 / PA5 / PA6 / PA7 | 推挽输出，默认高 |
| USB DM / DP | PA11 / PA12 | USB Device FS |
| SWDIO / SWCLK | PA13 / PA14 | 保留调试 |

> 用户文档里列线写的是 `PA5/PA6/PA7/PB0`，与本工程 CubeMX 实际配置（`PA4~PA7`）不一致。
> 已与用户确认：**以工程实际配置 `PA4~PA7` 为准**（CubeMX 已配好并标注 `C1~C4`，无需改动 `.ioc`/`gpio.c`/`main.h`）。

### 2.1 二极管方向与扫描极性（重要修正）

**实际接线（用户确认）：** 行线 → 按键 → 二极管阴极 → 二极管阳极 → 列线。

二极管符号为「阳极 →|— 阴极」，电流只能从阳极流向阴极，因此在本接线下**电流只能从列线流向行线**。这直接决定扫描极性，并且**推翻了用户原文档的「行线上拉输入 + 逐列拉低」**：

| | 原文档（阳极在行线侧才成立） | 本工程实际（阳极在列线侧） |
| --- | --- | --- |
| 行线 | 上拉输入，空闲高 | **下拉输入，空闲低** |
| 列线 | 逐列**拉低**，其余高 | 逐列**拉高**，其余低 |
| 判定 | 行读到**低** = 按下 | 行读到**高** = 按下 |
| 空闲态 | 所有列输出高 | 所有列输出**低** |

原因：若按原文档把列拉低，二极管阳极（列）= 0V、阴极（行）被上拉到 3.3V，二极管反向截止，行线永远读不到按下。

电气裕量：列拉高且按键按下时，行线节点为「3.3V 经二极管 + 40kΩ 内部下拉」，约 2.7V，高于 STM32F103 的 VIH(min)=2.0V，判定可靠。行线节点 RC ≈ 1µs，5µs 建立延时足够。

该极性在 `keyboard.h` 中用**一个宏** `KB_ROW_ACTIVE_HIGH` 表示（默认 1）。`keyboard_port.c` 会按该宏自行配置行线上下拉与列线极性，因此**不依赖 `gpio.c` 的默认值**；若二极管方向相反，只需把该宏改成 0。

防鬼键性质不变：只有被驱动到导通电平的那一列，其交叉键按下才能改变行线电平；其余列二极管反偏截止，串扰回路不存在，**软件无需额外判定**。

## 3. 关键决策：HID 类改造方案

工程现状是 CubeMX 以「HID 类」生成的，`usbd_hid.c` 里内嵌的是 ST 默认的**摇杆鼠标**描述符：

| 项目 | 现状（鼠标） | 需要（键盘） |
| --- | --- | --- |
| `HID_EPIN_SIZE` | `0x04` | `0x08` |
| 报告描述符 | 74 字节鼠标 | 63 字节标准键盘 |
| `bInterfaceProtocol` | `0x02` (mouse) | `0x01` (keyboard) |
| `bmAttributes` | `0xE0`（自供电） | `0x80`（总线供电，无远程唤醒） |

**采用方案 A**：直接修改 `Middlewares/.../Class/HID/` 下的 `usbd_hid.c` / `usbd_hid.h`。

- 优点：改动最小，与 ST 官方库和常见 STM32 键盘资料一一对应。
- 代价：**重新用 CubeMX 生成工程会把这些改动覆盖回鼠标**。补丁清单见 §8。

被否决的方案：
- 方案 B（把键盘 HID 类私有化到 `USB_DEVICE/Class/`）：CubeMX 覆盖风险只剩 `usb_device.c` 一行，但要维护约 700 行 ST 库代码副本。
- 方案 C（换回 CustomHID 类）：描述符在 App 层 `USER CODE` 保护区最安全，但 CustomHID 中间件文件已从工程中删除，迁移成本最高。

## 4. 文件清单

| 文件 | 动作 | 职责 |
| --- | --- | --- |
| `Middlewares/.../Class/HID/Inc/usbd_hid.h` | 改 | EP 大小、键盘描述符长度、句柄新增 `LedState`、新增就绪查询接口 |
| `Middlewares/.../Class/HID/Src/usbd_hid.c` | 改 | 63 字节键盘报告描述符、接口协议/供电属性、接受 `SET_REPORT`(LED)、新增 `USBD_HID_IsReady` |
| `Core/Inc/keyboard.h` | 新增 | 配置宏、键码表类型、逻辑层与移植层接口契约 |
| `Core/Src/keyboard.c` | 新增 | **纯 C 逻辑**：扫描时序、四态消抖、键码映射、6KRO 报表生成、收敛式发送（不依赖 HAL，可在 PC 上测试） |
| `Core/Src/keyboard_port.c` | 新增 | **移植层**：GPIO 列驱动/行读取、微秒延时、毫秒时钟、USB 上报 |
| `Core/Src/main.c` | 改 | 仅在 `USER CODE` 区加 `KB_Init()` / `KB_Task()` |
| `Core/Src/gpio.c` | 改 | 行线改内部下拉、列线默认输出低（与 §2.1 极性一致，保证上电安全态） |
| `TinyKey.ioc` | 改 | 同步 `GPIO_PuPd=GPIO_PULLDOWN`（PA0~PA3）、`PinState=GPIO_PIN_RESET`（PA4~PA7） |
| `USB_DEVICE/App/usb_device.h` | 改 | 补 `extern USBD_HandleTypeDef hUsbDeviceFS;`（CubeMX 生成的本文件漏了它，`keyboard_port.c` 必须用到） |
| `USB_DEVICE/App/usbd_desc.c` | 改 | 产品名改为 `TinyKey Keyboard`（可选，便于设备管理器识别） |
| `MDK-ARM/TinyKey.uvprojx` | 改 | 把两个新 `.c` 加进 `Application/User/Core` 分组 |
| `MDK-ARM/.eide/eide.yml` | 改 | 删除已不存在的 CustomHID 文件引用、补上 `Class/HID` 文件与头文件路径、加入新文件 |
| `tests/` | 新增 | 宿主机 gcc 测试（假 HAL / 假 USB）+ 一键构建脚本 |

拆成「纯逻辑 + 移植层」的原因：本机没有 ARM 编译器（无 armcc/armclang），但有 MinGW gcc。
拆开后可以在 PC 上真编译、真运行扫描/消抖/报表全套逻辑，而不是只能"烧进去试试看"。

## 5. 算法

### 5.1 扫描（每 1 ms 一轮）

对齐用户文档 4.2 节，但极性按 §2.1 修正（逐列**拉高**）：

1. 所有列输出低（空闲态，所有二极管反偏，行线被内部下拉保持低，无误触发）
2. 对 `col = 0..3`：目标列拉高、其余列保持低 → 延时 5 µs 等电平稳定 → 读 4 根行线：
   - 行线为高 → 该行与当前列的交叉键按下
   - 行线为低 → 未按下
3. 扫描结束所有列恢复低

### 5.2 消抖（每键独立四态机，对齐文档 5.2 节）

```
空闲 ──检测到低电平──▶ 按下消抖 ──连续10次采样为低──▶ 按下保持
  ▲                        │                              │
  └──连续10次采样为高── 释放消抖 ◀──检测到高电平──────────┘
```

- 消抖窗口 `KB_DEBOUNCE_MS = 10`（10 次 1 ms 采样一致才认账，落在文档建议的 10~20 ms 内）
- 抖动期电平反复时计数清零、回到原稳定态，不会产生毛刺事件

### 5.3 报表生成与发送

- 16 位稳定态位图 → 标准 8 字节键盘报表 `[修饰键, 保留, 键码×6]`
- 键码查表：`key_index = row*4 + col` → HID `0x04~0x13`（`a~p`），与用户文档表格一致
- **收敛式发送**：维护「期望报表」与「已发送报表」，两者不同且 IN 端点空闲时才发送，成功后才更新「已发送」。
  这样规避了 ST 库 `USBD_HID_SendReport()` 在端点忙时**静默丢包**的问题（这是这类项目"偶尔漏字符"的常见根因）。
  按键变化比 USB 轮询快时，中间态被合并、最终态一定送达。

### 5.4 明确的取舍（YAGNI）

- 标准 6KRO：同时按下超过 6 个键时只上报前 6 个（含修饰键位，但本键盘无修饰键）
- 不做长按连发：角色重复交给电脑的按键重复机制
- 不做 NKRO、不做宏、不做多媒体键

## 6. 接口契约

### 逻辑层（`keyboard.c`，纯 C）

```c
void            KB_Init(void);
void            KB_Task(void);                                   /* 主循环周期调用，内部自做 1ms 节流 */
uint16_t        KB_GetStableMask(void);                          /* bit = row*4+col，1 = 稳定按下 */
void            KB_BuildReport(uint16_t mask, uint8_t report[8]); /* 位图 → 8 字节报表 */
const KB_KeyDef *KB_KeyAt(uint16_t key_index);
```

### 移植层（必须实现，`keyboard_port.c` 提供目标实现，宿主机测试提供桩）

```c
void     KB_Port_Init(void);
void     KB_Port_SelectColumn(uint8_t col);   /* 拉低第 col 列，其余列拉高 */
void     KB_Port_ReleaseColumns(void);        /* 所有列拉高（空闲态） */
uint8_t  KB_Port_ReadRows(void);              /* bit0..3 = row0..3，1 = 高电平 */
void     KB_Port_DelayUs(uint32_t us);
uint32_t KB_Port_Millis(void);
bool     KB_Port_SendReport(uint8_t report[8]); /* false = 端点忙，调用方下个周期重试 */
```

## 7. 验证方案

无 ARM 工具链，因此用 MinGW gcc 在宿主机上做两级测试：

**A. 纯逻辑测试**（`tests/kb_logic_test.c` + 真 `keyboard.c` + 假移植层）

| 用例 | 期望 |
| --- | --- |
| 空闲 | 不产生任何报表 |
| 按键带抖动（电平反复 3 ms） | 消抖结束后只产生 1 次 `{0,0,0x04,0,0,0,0,0}` |
| 松开 | 产生全 0 报表 |
| 任意行列交叉键 | 上报键码与文档表格一一对应（16 键全覆盖） |
| 双键同按 | 一次报表包含两个键码，升序 |
| 8 键同按 | 只上报前 6 个 |
| 端点忙 | 报表既不丢失也不重复，端点空闲后恰好补发一次 |
| 列扫描时序 | 4 列按 0→3 依次被拉低，且每次只有一列为低 |

**B. 移植层 + 描述符测试**（`tests/kb_port_test.c` + 真 `keyboard_port.c` + 假 HAL/USB；`tests/hid_desc_test.c` + 真 `usbd_hid.c`）

| 用例 | 期望 |
| --- | --- |
| 假 GPIO 矩阵 → 真 `keyboard_port.c` | 列驱动/行读取语义与真实 HAL 一致，端到端产出正确报表 |
| 配置描述符 | `bInterfaceClass=3`、`bInterfaceProtocol=1`、`bmAttributes=0x80` |
| HID 描述符 | `wItemLength = 63` |
| 端点描述符 | `wMaxPacketSize = 8` |
| `GET_DESCRIPTOR(HID_REPORT_DESC)` | 返回的 63 字节与标准键盘描述符逐字节一致 |

**硬件侧仍需用户验证**：二极管方向、行列接线、USB 枚举后设备管理器出现 `TinyKey Keyboard`、记事本敲键出字符。

## 8. CubeMX 重新生成工程后的补丁清单

若日后重新运行 CubeMX 生成代码，以下改动会被覆盖，需按此清单补回（约 5 分钟）：

1. `usbd_hid.h`：`HID_EPIN_SIZE` 改 `0x08`；`HID_MOUSE_REPORT_DESC_SIZE` 改 `HID_KEYBOARD_REPORT_DESC_SIZE 63`；句柄加 `LedState`；加 `USBD_HID_IsReady`/`USBD_HID_GetLedState` 声明
2. `usbd_hid.c`：三份配置描述符的 `bmAttributes` 改 `0x80`、`nInterfaceProtocol` 改 `0x01`；报告描述符长度宏改名；鼠标描述符数组换成键盘描述符；`Setup` 里加 `HID_REQ_SET_REPORT` 分支；追加 `USBD_HID_IsReady`/`USBD_HID_GetLedState`
3. `Core/Src/keyboard.c`、`Core/Src/keyboard_port.c`、`Core/Inc/keyboard.h` 会被移出 Keil 工程 → 重新加入 `Application/User/Core` 分组
4. `main.c` 的 `USER CODE` 区调用会保留（CubeMX 保护），无需处理
5. `usb_device.h` 的 `extern USBD_HandleTypeDef hUsbDeviceFS;` 在 `USER CODE` 区，会保留
6. `usbd_desc.c` 产品名字符串会被改回 → 视需要重改
7. `gpio.c`/`.ioc` 的上下拉与默认电平会按 `.ioc` 恢复；`.ioc` 已同步修改，所以正常重新生成会保持正确

## 9. 构建排障记录（2026-09-23 实测）

现象是「工程无法识别 `stdint.h`」。实测结论：**这不是源码或编译器问题，且不是真正的编译错误**。

证据：

1. 用工程实际使用的 `D:\Keil5\ARM\ARMCC\bin\armcc.exe`（AC5）按 `build/TinyKey/compile_commands.json` 里的原样参数编译 `keyboard.c` / `main.c` / `keyboard_port.c`，**退出码全部为 0**。
2. 用 AC6 的 `armclang.exe`（`--target=arm-arm-none-eabi -mcpu=cortex-m3`）同样编译通过。
3. 两个工具链的 `include` 目录里都存在 `stdint.h`（`ARMCC\include\stdint.h`、`ARMCLANG\include\stdint.h`）。
4. `build/TinyKey/compiler.log` 里**没有任何 stdint 报错**，真正失败在**链接**阶段：
   `L6406E: No space in execution regions` × 190 余条 + `L6407E: Sections of aggregate size 0x41c4 bytes could not fit`。

### 9.1 真正的构建阻塞：EIDE 存储区尺寸为空

`build/TinyKey/TinyKey.sct`（EIDE 生成）里所有区域都是零尺寸：

```
LR_IROM1 0x0000000000 0x0000000000 { ER_IROM1 0x0000000000 0x0000000000 { ... } ... }
```

原因是 `MDK-ARM/.eide/eide.yml` 中 AC5 的 `storageLayout` 把勾选区域的 `startAddr`/`size` 写成了空字符串
（`size: ""` / `startAddr: ""`），覆盖了 EIDE 从设备推导出的正确值（`builder.params` 里 `ram=20480 rom=65536` 其实是对的）。

修复：

| 位置 | 修改 |
| --- | --- |
| `eide.yml` → `deviceName` | `null` → `STM32F103C8` |
| `eide.yml` → AC5 `storageLayout.RAM` 勾选项 | `0x20000000` / `0x5000` |
| `eide.yml` → AC5 `storageLayout.ROM` 启动项 | `0x08000000` / `0x10000` |
| `eide.yml` → AC5 多出的第二个勾选 IROM | 取消勾选（原为 0x0 尺寸，多余） |
| `eide.yml` → AC6 `scatterFilePath` | 占位符 `<YOUR_SCATTER_FILE>.sct` → `""` |

修复后用 armcc + armlink 实际链接验证（AC5 目标文件）：

| | 占用 | 容量 | 余量 |
| --- | --- | --- | --- |
| Flash (RO + RW) | 33720 B | 65536 B | 51% |
| RAM (RW + ZI) | 4056 B | 20480 B | 80% |

又用 AC6 全量重编 + 链接复核：28/28 个 C 文件编译成功，链接退出码 0，
ROM 28312 B / RAM 4056 B，同样宽裕。

### 9.2 `stdint.h` 报错的真实出处：IntelliSense

`MDK-ARM/.vscode/c_cpp_properties.json` 是陈旧配置：

- `includePath` 仍指向**已删除**的 `Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Inc`（应为 `Class/HID/Inc`）
- `compilerPath` 指向 EIDE 自带的 `arm-none-eabi-gcc`，而 `intelliSenseMode` 是 `gcc-arm`，同时 `compileCommands` 指向由 **armcc** 生成的 `compile_commands.json`（其中是 `--no_depend_system_headers`、相对 `-I` 路径）

在这种混合配置下，C/C++ 扩展无法推导出编译器的系统头文件搜索路径，于是编辑器报「无法打开源文件 stdint.h」。

修复：`CustomHID/Inc` → `Class/HID/Inc`，`compilerPath` 改指向实际在用的
`D:/Keil5/ARM/ARMCLANG/bin/armclang.exe`（clang 系，可被扩展查询到系统头路径），
并把 `D:/Keil5/ARM/ARMCLANG/include` 与 `D:/Keil5/ARM/ARMCC/include` 显式加入 `includePath` 作为兜底。

### 9.3 三条构建路径的现状

| 路径 | 状态 |
| --- | --- |
| Keil uVision（`.uvprojx`，AC5） | 内存映射本来就正确：`<Cpu>IRAM(0x20000000-0x20004FFF) IROM(0x8000000-0x800FFFF)</Cpu>` |
| EIDE + AC5 | 已修复（§9.1） |
| EIDE + AC6 | 布局本就正确，已实测全量编译链接通过 |

另：`build/TinyKey/.obj` 下残留了 CustomHID 时代的 `usbd_customhid.o`、`usbd_custom_hid_if.o`
（对应源文件已删除）。它们不在工程文件列表里、不会被链接，但手工链接时会因属性冲突报
`L6242E`，已清理。

## 10. Review Checklist

- [ ] 扫描极性：行线内部下拉、逐列驱动到导通电平（高）、行高=按下 — 由 `tests/kb_port_test.c` 的极性用例与假矩阵模型验证
- [ ] HID 类：报告描述符为 63 字节标准键盘描述符，`HID_EPIN_SIZE=8`，`bInterfaceProtocol=1` — 由 `tests/hid_desc_test.c` 逐字节校验
- [ ] 数据流：GPIO 矩阵 → 消抖稳定态位图 → 8 字节报表 → `USBD_HID_SendReport` 全链路可达 — 由 `tests/kb_port_test.c` 端到端验证
- [ ] 错误处理：IN 端点忙时报表不丢失、不重复，端点空闲后补发 — 由「端点忙」用例验证
- [ ] 质量门：`KB_SCAN_PERIOD_MS + KB_DEBOUNCE_MS` 决定的最坏响应时间 ≤ 11 ms，符合文档 10~20 ms 消抖要求
- [ ] 边界情况：16 键全覆盖映射正确；>6 键同按按 6KRO 截断且不越界写 `report[8]`
- [ ] 构建：`TinyKey.uvprojx` 与 `.eide/eide.yml` 均包含新文件、无失效路径引用
- [ ] 构建：`eide.yml` 存储区已填实际地址（§9.1），AC5/AC6 均能编出固件且容量宽裕
- [ ] 宿主机测试：`pwsh -File tests/run_tests.ps1` 全绿（当前 373 项检查 0 失败）
