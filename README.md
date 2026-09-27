# TinyKey — STM32F103 4×4 矩阵键盘 USB HID 键盘

TinyKey 是一个小而有据可查的嵌入式开源项目：把一块 4×4 带二极管的矩阵键盘做成即插即用的标准 USB 键盘，按下第 N 个键，电脑输入 a~p

MCU：STM32F103C8T6 ｜ 固件：STM32CubeMX + HAL ｜ USB：Device FS，HID 键盘类

## 硬件连接

| 信号 | 引脚 | 配置 |
| --- | --- | --- |
| row1 ~ row4 | PA0 / PA1 / PA2 / PA3 | 输入 + **内部下拉** |
| column1 ~ column4 | PA4 / PA5 / PA6 / PA7 | 推挽输出，空闲为低 |
| USB DM / DP | PA11 / PA12 | USB Device FS |
| SWDIO / SWCLK | PA13 / PA14 | 调试口 |

每个按键串联一只二极管，实际接线为：

```
行线 → 按键 → 二极管阴极 → 二极管阳极 → 列线
```

二极管只能从列线流向行线，因此扫描极性是 **逐列拉高、行线读到高电平表示按下**，行线必须用内部下拉保持默认低。
（注意：这与「行线上拉 + 逐列拉低」的常见方案相反，那种接法在本硬件上永远读不到按键。）

极性集中在一个宏里：`Core/Inc/keyboard.h` 的 `KB_ROW_ACTIVE_HIGH`（当前为 `1`）。若二极管方向相反，改成 `0` 即可，
`Core/Src/keyboard_port.c` 会据此自动把行线改配为内部上拉、列线改为逐列拉低。

## 键位映射

按 `row * 4 + col` 行优先排列，HID 键码 `0x04`~`0x13`：

| | col0 | col1 | col2 | col3 |
| --- | --- | --- | --- | --- |
| **row0** | `a` | `b` | `c` | `d` |
| **row1** | `e` | `f` | `g` | `h` |
| **row2** | `i` | `j` | `k` | `l` |
| **row3** | `m` | `n` | `o` | `p` |

标准 6KRO：同时按下超过 6 键时只上报前 6 个。不做长按连发（字符重复交给主机）。

## 目录结构

```
Core/Inc/keyboard.h          键盘模块配置与接口（含扫描极性宏）
Core/Src/keyboard.c          纯逻辑：扫描时序、四态消抖、键码映射、6KRO 报表、收敛式上报
Core/Src/keyboard_port.c     移植层：GPIO 上下拉/列驱动、微秒延时、毫秒时钟、USB 上报
Core/Src/main.c              仅在 USER CODE 区挂接 KB_Init() / KB_Task()
USB_DEVICE/                  CubeMX 生成的 USB 设备层（usb_device.h 内补了 hUsbDeviceFS 声明）
Middlewares/.../Class/HID/    ST HID 类驱动，已改为键盘报告描述符（改动点均有 [TinyKey] 注释）
tests/                       宿主机单元/集成测试（MinGW gcc）
docs/plans/                  设计文档：方案取舍、算法、验证方案、构建排障记录
```

## 构建

两条路径都可用：

- **Keil MDK（uVision）**：打开 `MDK-ARM/TinyKey.uvprojx`，直接 Build。内存映射 `IROM 0x08000000/0x10000`、`IRAM 0x20000000/0x5000` 已在工程中配好。
- **VSCode + EIDE**：打开 `MDK-ARM/TinyKey.code-workspace`，执行 Rebuild。注意 `.eide/eide.yml` 的存储区必须填实际地址（本项目已填好），否则链接会报 `L6406E: No space in execution regions`。

用到的工具链缓存 `MDK-ARM/.pack/`、`MDK-ARM/.cmsis/` 未入库，克隆后由 EIDE / Keil 自动重新获取。

## 宿主机测试

逻辑、移植层与 USB 描述符都可以在电脑上验证，无需硬件：

```powershell
pwsh -File tests/run_tests.ps1
```

三组测试共 373 项检查：扫描时序、消抖（含抖动）、16 键映射、多键合并、6KRO 截断、
USB 端点忙时报表不丢不重、二极管防鬼键（假 HAL 内置带二极管的 4×4 矩阵电气模型）、
以及键盘报告描述符的逐字节校验。详见 `tests/README.md`。

## 已知限制

- 标准 6KRO，不支持全键无冲（NKRO）
- 16 个键固定映射为 `a`~`p`，无修饰键 / 多媒体键 / 宏
- 硬件行为（二极管方向、USB 枚举、按键手感）需在实机上确认

完整设计说明、方案对比与 CubeMX 重新生成工程后的补丁清单见
[docs/plans/2026-09-23-tinykey-keyboard-design.md](docs/plans/2026-09-23-tinykey-keyboard-design.md)。
