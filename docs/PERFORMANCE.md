# 性能对比说明

[简体中文](PERFORMANCE.md) | [English](PERFORMANCE.en.md) · [返回性能表](../README.md#延迟与性能)

首页数字来自代表场景，延迟是分段估算，CPU 是实测等效逻辑核数。1 核 = 1000 CPU ms/s；测试机器为 i7-13700KF／RTX 4080。

延迟包含等待游戏取样、游戏响应和呈现后段，不含设备输入、显示器扫描及面板响应。

## Xbox 360 / Xenia

移植版三款均使用默认 1V；Xenia 分别保留原作的 1V、2V、3V。G2 可通过[设置指南](SETTINGS.md#g2-frame-delay)切回 2V。

延迟由游戏内测量、8.3ms 平均采样等待和同机历史呈现数据估算，输出路径按移植版独立翻转、Xenia 桌面合成；不含设备输入及显示器扫描、面板响应。CPU 为 i7-13700KF／RTX 4080 回放场景的等效逻辑核数，1 核 = 1000 CPU ms/s。[比较方法与数据](ANALYSIS.md#360-统一条件比较)。

## PS4 / shadPS4

移植版默认启用 NEO（PS4 Pro）模式与低延迟呈现；shadPS4 v0.20.0 为普通模式、默认 FIFO。延迟估算包含平均采样等待与同机呈现预算，CPU 为第一关短场景的实测等效核数。[比较方法与数据](ANALYSIS.md#v082-neo-对照)。

NEO 的设置和关闭方法见[设置指南](SETTINGS.md#neo-模式)。PS4 游戏按 CPU 能力自动兼容 BMI1、LZCNT 和 MOVBE 指令。

空战之路两边均使用 ARCADE 第一关 VALLEY 的 Silver Sword。连续变向时，从游戏读到方向到提交首张可见位移画面，移植版平均 5.70 ms，shadPS4 平均 33.09 ms。[测量记录](ANALYSIS.md#v094-garegga-and-ibara)

## PS2 / PCSX2

对照版本为 PCSX2 2.8.2，使用 D3D12、原始分辨率并关闭垂直同步。

ESPGaluda 的平均取样等待为 8.33 ms，移植版响应实测 16.87 ms，PCSX2 按已确认的两次游戏更新推算；呈现后段分别复用同机 Vulkan IMMEDIATE 小基准与 D3D12 独立翻转记录。移植版 CPU 使用 v0.9.1 第一关短场景的已有采样，PCSX2 使用此前的 BOSS 场景。移植版出帧间隔 P95 17.32 ms，最大 17.93 ms。[方法与数据](ANALYSIS.md#v090-ps2-latency-estimate)。

铸蔷薇两边均为 ARCADE NORMAL 第一关 PORTTOWN。移植版把帧内空闲时间移到读键之前，读到方向至首张机体位移画面平均 22.02 ms（12 次变向）。PCSX2 从同一存档分别保持中立和按右，第二次更新出现位移，按 60 Hz 折算为 33.33 ms。计入平均取样等待和呈现后段，两侧总延迟期望约为 32 和 43 ms。[测量记录](ANALYSIS.md#v094-garegga-and-ibara)
