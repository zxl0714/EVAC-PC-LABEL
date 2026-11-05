# 性能对比说明

[简体中文](PERFORMANCE.md) | [English](PERFORMANCE.en.md) · [返回性能表](../README.md#延迟与性能)

首页数字来自代表场景，延迟是分段估算，CPU 是实测等效逻辑核数。1 核 = 1000 CPU ms/s；测试机器为 i7-13700KF／RTX 4080。

延迟包含等待游戏取样、游戏响应和呈现后段，不含设备输入、显示器扫描及面板响应。

## Xbox 360 / Xenia

移植版均按 0V 测试（虫姬二在游戏内选取），RTX 4080 上 Reflex 已启用。Xenia 分别使用虫姬二 1V、G2 2V、最大往生 3V 和狱门山的原作时序。虫姬二在 Xenia 上也能选原生 0V。[0V 与 Reflex 设置](SETTINGS.md#虫姬二狱门山与-nvidia-reflex)。

估算包含 60 Hz 平均取样等待（8.33 ms）、取样到首张实际位移画面，以及同机历史 Present 到 Windows 显示事件的平均值（移植版独立翻转 0.92 ms，Xenia 桌面合成 11.18 ms）。呈现模式和排队情况会影响最后一项。狱门山移植版 0V 在读取输入的同一轮更新中就能看到位移。

CPU 与延迟分别采集，统计 CPU 时关闭逐帧图像和事件追踪。Reflex 开关对照固定同一场景和 0V 设置。[内部时序和样本数](ANALYSIS.md#v095--xbox-360-低延迟)。

## PS4 / shadPS4

移植版使用 NEO、Vulkan Immediate 和自动启用的 Reflex。平均延迟取 60 Hz 的平均取样等待（8.33 ms），加上游戏读到方向至首张机体位移画面的呈现完成通知。每款游戏按自己的画面与 Vulkan Present ID 配对，完成通知包含驱动及线程唤醒时间。CPU 另在第一关移动、射击时测量，使用正式图形后端。

shadPS4 v0.20.0 使用普通模式和默认 FIFO，沿用同机原有对照。空战之路两边均使用 ARCADE 第一关 VALLEY 的 Silver Sword。[样本与分段耗时](ANALYSIS.md#v095-ps4) · [NEO 与 Reflex 设置](SETTINGS.md#neo-模式)

## PS2 / PCSX2

对照版本为 PCSX2 2.8.2，使用 D3D12、原始分辨率并关闭垂直同步。

RTX 4080 自动启用 Reflex，两作各确认 12 次关内变向。从游戏读到方向到首张位移画面提交返回，ESPGaluda 平均 7.86 ms，铸蔷薇平均 8.29 ms。加上平均取样等待 8.33 ms 和同机 Vulkan Immediate 的历史呈现预算 1.33 ms，合计约 17.53 和 17.96 ms。PCSX2 两作都在第二次更新出现位移，沿用两次 60 Hz 更新与 D3D12 独立翻转的估算，约 43 ms。[测量分解](ANALYSIS.md#v095-ps2)

CPU 在关闭逐帧采集后单独测量。ESPGaluda 移植版为 Arcade 第一关 Ageha，PCSX2 为 BOSS 场景。铸蔷薇两侧均为 Arcade Normal 第一关 PORTTOWN。
