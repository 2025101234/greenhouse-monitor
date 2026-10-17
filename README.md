# 基于 RT-Thread 的大棚空气温湿度与光照监测终端

> 2026 RT-Thread 嵌入式软件大赛参赛作品
>
> 固件版本 **v1.1.2**　|　开发板 **ArtInChip D12X-DEMO68-V1.2**　|　操作系统 **RT-Thread 4.1.1**　|　GUI **LVGL 8.3.1**

## 项目简介

一个完全离线的温室环境监测终端。使用开发板板载的 480×272 电容触摸屏本地显示，DHT11 采集空气温湿度、BH1750 采集光照强度，超限通过板载蜂鸣器报警。**不依赖 Wi-Fi、不依赖云平台、不需要额外存储卡**，硬件成本仅为两支低价传感器。

### 功能

- **Home**：三通道实时数据卡片 + 各自报警上限 + 系统状态摘要
- **Trends**：温度 / 湿度 / 光照三条曲线分别查看（不共用纵轴），保留最近 60 次采样
- **Report**：三通道平均值、最小值、最大值、报警样本计数
- **Menu**：三个上限阈值加减调整，Apply 后 RAM 立即生效、后台异步落盘
- **报警**：连续 3 次确认 + 2 ℃/2 % RH 回差，避免噪声误报与阈值附近抖动
- **数据有效性**：每路数据独立 `valid_mask`，读不到显示 `--`，绝不显示模拟值

## 硬件

| 项目 | 参数 |
| --- | --- |
| 开发板 | ArtInChip D12X-DEMO68-V1.2 |
| CPU | THead E907，32 位 RISC-V，396 MHz，带 FPU |
| 内存 / 存储 | 8 MB PSRAM / 16 MB SPI NOR Flash |
| 显示 | 480×272 RGB 电容屏，GE 硬件加速，显示旋转 180° |
| 触摸 | GT911，I²C0，复位 PA.10，中断 PA.11，**触摸旋转 0°** |
| 空气温湿度 | DHT11，单总线 GPIO `PC.6`（J8 排孔） |
| 光照 | BH1750，I²C1，SCL `PA.2`(TX1) / SDA `PA.3`(RX1)，地址 `0x23` 或 `0x5c` |
| 蜂鸣器 | 板载 BZ1，`PC.7` / PWM1_B，4 kHz，50 % 占空比 |
| 调试 / 烧录 | 板载 USB-UART0（USB-C），CH343，115200 8N1 |

### 接线要点

1. 传感器必须**共地**，使用板上已核实的 3V3 / GND。
2. 传感器 I²C 走 **`i2c1`**，触摸屏走 **`i2c0`**，两条总线不可混接。
3. J8 是**未焊接通孔**，必须焊排针或可靠固定。手指按压接触会造成时序抖动，是 DHT11 读取失败最常见的原因。
4. 本项目已关闭 SDMC1（TF 卡），把 `PC.6` 释放给 DHT11，**不要随意重新启用**。
5. UART1 未启用，`PA2/PA3` 专供 I²C1，改引脚前必须检查 pinmux。

## 仓库结构

```text
.
├── application/rt-thread/helloworld/greenhouse/   # 应用全部源码
│   ├── greenhouse_app.c     # INIT_APP_EXPORT 启动编排
│   ├── gh_config.h          # 全部硬件配置与功能开关
│   ├── gh_types.h           # 采样 / 阈值 / 状态结构定义
│   ├── gh_sensors.c/.h      # DHT11 位时序 + BH1750 I²C 采样
│   ├── gh_data.c/.h         # 互斥状态快照 / 双消息队列 / 持久化
│   ├── gh_alarm.c/.h        # 连续确认 + 回差 + 蜂鸣器
│   ├── gh_ui.c/.h           # LVGL 四页面 / 曲线 / 统计 / 菜单
│   ├── gh_board.c/.h        # 板型自检 / PWM 蜂鸣器
│   ├── gh_console.c         # MSH 诊断命令
│   └── gh_network.c/.h      # JSON 生成；HTTP 源码保留但默认关闭
└── sdk-patch/               # 相对 Luban-Lite SDK 根目录的改动文件
    ├── target/configs/d12x_demo68-nor_rt-thread_helloworld_defconfig
    ├── target/d12x/demo68-nor/pinmux.c
    ├── target/d12x/demo68-nor/pack/image_cfg.json
    └── packages/artinchip/lvgl-ui/
        ├── lv_conf.h
        ├── lv_demo.c
        ├── lv_driver/lv_port_indev.c
        ├── lv_driver/lv_tpc_run.c
        └── lvgl/env_support/rt-thread/lv_rt_thread_port.c
└── tools/                   # Windows 一键构建脚本（可选）
    ├── build_greenhouse.bat
    └── publish_firmware.ps1
```

`tools/` 下附带了 Windows 下的一键构建脚本（可选）：

- `build_greenhouse.bat`：配置 SDK 环境 → SCons 构建 → 自动调用归档脚本
- `publish_firmware.ps1`：按 `image_cfg.json` 中的真实版本号归档镜像、比对 SHA256、拒绝覆盖同版本不同内容

使用前请把两个脚本放到**与 `luban-lite` 目录同级**的位置，并确认脚本内的 `SDK_ROOT` 指向你的 SDK 目录名。

## 编译

需要 Luban-Lite SDK 及其自带工具链（Windows 下 SDK 内含 Python 与 SCons，无需额外安装）。

```bash
# 1. 获取官方 SDK
git clone https://github.com/artinchip/luban-lite.git
cd luban-lite

# 2. 用 sdk-patch 覆盖对应文件（保持相对路径）
cp -r ../greenhouse-rt-thread/sdk-patch/* .

# 3. 放入应用源码
mkdir -p application/rt-thread/helloworld/greenhouse
cp -r ../greenhouse-rt-thread/application/rt-thread/helloworld/greenhouse/* \
      application/rt-thread/helloworld/greenhouse/

# 4. 应用板级配置并编译
scons --apply-def=d12x_demo68-nor_rt-thread_helloworld_defconfig
scons -j 8
```

产物：

```text
output/d12x_demo68-nor_rt-thread_helloworld/images/d12x_demo68-nor_v1.1.2.img
```

Windows 下也可使用附带的 `build_greenhouse.bat`（配置环境 → SCons 构建 → 自动归档）。

## 烧录

使用 AiBurn v1.5.1，串口方式，无需 TF 卡或 JTAG：

1. 关闭所有占用串口的程序（串口终端等）。
2. USB 数据线接板载 **USB-UART0** 口。
3. 按住 **UBOOT** → 按一下 **RESET** → 松开 UBOOT，进入 BROM。
4. AiBurn 选择实际 COM 口、115200，连接成功后加载 `.img`。
5. 整包烧录**不要**勾选"按分区烧写""强制升级"，不使用"数据擦写"。
6. 等待 100 % / SUCCESS 后重启检查。

> 串口烧录速度约 10.9 KB/s，整包约 17 分钟，慢不等于失败。

## 调试

串口接 USB-UART0，115200 8N1，进入 `aic />` MSH 提示符后执行：

| 命令 | 用途 |
| --- | --- |
| `gh_status` | 采样序号、有效位、报警位、丢弃计数、当前阈值 |
| `gh_sensor_test` | 立即读一次 DHT11 + BH1750，打印各通道有效位 |
| `gh_dht11_test` | 单独走 DHT11 读取路径 |
| `gh_threshold` | 查看阈值 |
| `gh_threshold 350 850 80000` | 设置阈值（35.0 ℃ / 85.0 % RH / 80000 lux） |
| `gh_buzzer_test 500` | 蜂鸣器鸣响 500 ms |
| `gh_alarm_test` | 发布 3 个超限测试样本 |
| `gh_hwinfo` | 打印板型与外设配置 |
| `gh_touch` / `gh_touch on` / `gh_touch off` | 触摸事件计数、坐标打印开关 |
| `gh_touch rotate 0` / `180` | 运行时切换触摸方向（诊断用） |
| `gh_ui_perf` | UI 版本、切页次数、数据更新次数、最大更新耗时 |
| `lvgl_perf` | LVGL 整轮处理最大耗时与超 100 ms 次数 |
| `list_device` / `list_thread` | RT-Thread 外设与线程状态 |

**有效位**：温度 `0x01`、湿度 `0x02`、光照 `0x04`；温湿度有效通常 `0x03`，三项全有效 `0x07`。

## 关键技术点

### 显示旋转与触摸旋转必须解耦

本板显示需要旋转 180° 才能正立，但 GT911 上报的原始坐标**已经**与画面一致。早期版本让触摸方向跟随显示旋转自动翻转，导致点击位置上下颠倒。

```c
/* defconfig */
CONFIG_AICFB_ROTATE_180=y
CONFIG_AIC_FB_ROTATE_DEGREE=180

/* lv_conf.h —— 不再跟随显示旋转 */
#define AIC_LVGL_TOUCH_ROTATION 0
```

### 触摸事件环形队列

LVGL 输入回调只保存"最后坐标 + 状态"。一次按下与抬起落在同一帧内时，LVGL 只能看到一个状态，点击被吞掉。驱动侧引入 **16 深度事件队列**，`input_read` 每次消费一个事件并用 `continue_reading` 告知还有后续；队列满时丢弃最旧事件，保证最新抬起事件不丢失。

### 所有 LVGL 操作收敛到 LVGL 线程

LVGL 不是线程安全的。本项目不做任何跨线程 `lv_async_call()`：`gh_ui_start()` 只标记数据层就绪，建页由 `gh_ui_lvgl_init()` 在 `lv_user_gui_init()` 中调用；数据更新由 LVGL 线程内的 50 ms 定时器主动轮询消息队列。

### 四页面缓存

四页一次性创建，切页只切换 `LV_OBJ_FLAG_HIDDEN`，并按变化刷新。电脑端回归连续 **2000 次切页**，缓存控件稳定保持 **93 个**、不增长，稳定态无堆内存增长，LVGL 内存占用约 35 680 字节。

### UI 回调不写 Flash

Apply 时只在 RAM 生效并释放信号量，由最低优先级线程 `ghsave` 异步落盘，多次 Apply 会合并为一次写，避免阻塞 LVGL 线程。

## 线程模型

| 线程 | 优先级 | 栈 | 职责 |
| --- | --- | --- | --- |
| `ghsensor` | 12 | 4096 | 每 2 s 采样 DHT11 + BH1750 |
| `ghalarm` | 13 | 3072 | 阻塞在报警队列，连续确认与回差判断 |
| GT911 读取 | 18 | — | 中断 + 20 ms 轮询兜底 |
| LVGL | 20 | 32768 | 唯一允许操作 LVGL 对象的线程 |
| `ghnet` | 24 | 6144 | 预留网络线程，当前离线镜像中空转 |
| `ghsave` | 26 | 4096 | 阻塞在信号量，异步写阈值文件 |

> RT-Thread 中数字越小优先级越高。传感器 > 报警 > 触摸/UI > 保存，保证采样不被绘制拖慢、写 Flash 不阻塞交互。

## 验证方法

项目采用三层验证：

**① 编译期检查**：`gh_config.h` 用 `#error` 做配置一致性校验——启用蜂鸣器但未开 PWM1、启用 BH1750 但未开 I2C1、启用 DHT11 但未开 PIN 设备，都会在编译期直接报错。

**② 电脑端自动化回归**（真实 LVGL 源码 + 真实 `gh_ui.c` / `lv_port_indev.c` + RT-Thread 数据层桩，MinGW GCC）：

```text
PASS: 2000 page switches, 93 cached objects, no steady-state heap growth (35680 bytes used).
PASS: idle redraws=0, history survives navigation, light>32767, zero values,
      negative fractions, Apply/Cancel/bounds, stale/partial data.
PASS: queued quick tap triggers real LVGL navigation; rotation and overflow release preserved.
PASS: default rotation=0 with display=180; board log coordinates hit Report/Menu, all tabs work.
```

**③ 板上运行期诊断**：12 条 MSH 命令，可在不接调试器的情况下检查采样有效位、报警状态、消息丢弃计数、触摸事件收发与丢弃、GT911 中断计数、UI 更新耗时、LVGL 单轮耗时、线程与外设列表。

**限制**：电脑端使用软件渲染，不模拟 ArtInChip GE 硬件加速、真实 I²C 时序与 Flash 持久化；测试截图中的曲线为构造的测试数据。实机数据以板上 `gh_status` / `gh_ui_perf` / `lvgl_perf` 输出为准。

## 许可证

应用层代码（`application/` 下 `greenhouse` 目录）采用 **Apache License 2.0**，见 `LICENSE`。

SDK 各组成部分遵循其各自上游许可证：RT-Thread 内核 Apache-2.0，LVGL MIT，LittleFS BSD-3-Clause 等。

## 参考资料

- 官方 D12x 文档：<https://aicdoc.artinchip.com/topics/product/d12x-doc.html>
- 板级文档：<https://aicdoc.artinchip.com/topics/product/d12x-demo-v1.html>
- 原理图：<https://aicdoc.artinchip.com/out/downloads/D12x-DEMO-V1-0.pdf>
- Luban-Lite SDK：<https://github.com/artinchip/luban-lite>
- RT-Thread：<https://www.rt-thread.org>
- LVGL 8.3 文档：<https://docs.lvgl.io/8.3/>
