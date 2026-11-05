# EVAC PC LABEL v0.9.5

- **Xbox 360 输入延迟优化。** ESPGaluda II、最大往生和狱门山默认使用 0V，机体能在游戏读到方向的那一帧移动。
- **虫姬二如何获得最低延迟。** 在游戏内依次选择 **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing)**。
- **输入设备响应更快。** 三个平台都在游戏读键时刷新手柄和摇杆状态，让刚上报的按键和方向赶上当前帧。
- **PS4 输入延迟优化。** 五款游戏都能在 GPU 复制画面的同时准备显示。以绊地狱为例，从读到方向到提交首张位移画面，平均从 **5.27 ms 降到 3.97 ms**。
- **PS2 输入延迟优化。** ESPGaluda 和铸蔷薇在绘图前更晚读键，画面就绪后更早提交，平均延迟均约 **18 ms**。
- **PS2 显卡兼容性。** 针对 Intel Arc B390 启动 ESPGaluda 和铸蔷薇时出现的崩溃，两款游戏都改用着色器缩放画面。如果仍然崩溃，建议更新 Intel 显卡驱动后重试，Vulkan 驱动会随之更新。[Intel 驱动下载](https://www.intel.com/content/www/us/en/download/785597/intel-arc-graphics-windows.html)
- **NVIDIA Reflex。** 全部 11 款游戏默认在支持的 NVIDIA 显卡上启用 Reflex，缩短输入到画面响应的等待。
- **AMD Anti-Lag 2。** 我们缺少 Radeon 实机验证稳定性，因此默认关闭。可在 `user-data/config.ini` 的 `[display]` 下设 `amd_anti_lag = true`，重启游戏后试用。欢迎通过启动器的「上传日志」反馈显卡型号、驱动版本和手感。
- **PS2 崩溃日志。** 遇到崩溃时点击「上传日志」，报告会附上调用栈和崩溃前的绘图步骤。

[如何恢复 Xbox 360 游戏的原作时序](https://github.com/zxl0714/EVAC-PC-LABEL/blob/v0.9.5/config/README.player-v09.txt#L42-L53)

## 最新的输入延迟与 CPU 占用

**表中移植版的最低延迟配置**（`user-data/config.ini`）

- **全部游戏**在 `[display]` 下使用 `reflex = auto`、`vsync = false`。
- **Xbox 360** 的虫姬二在游戏内选 0V。ESPGaluda II、最大往生、狱门山分别在 `[game.espgaluda2]`、`[game.sdoj]`、`[game.guwange]` 下设 `frame_delay = 0`。
- **PS4** 五款游戏各自的配置段使用 `neo = true`（默认）。**PS2** 两款游戏使用上述全局设置即可。

测试机为 i7-13700KF／RTX 4080。延迟为平均估算值（ms），CPU 为等效逻辑核数。Xenia 使用原作档位。[测试方法](https://github.com/zxl0714/EVAC-PC-LABEL/blob/v0.9.5/docs/PERFORMANCE.md)

| 游戏 | 对照模拟器 | 移植版延迟 | 模拟器延迟 | 移植版 CPU | 模拟器 CPU |
| --- | --- | ---: | ---: | ---: | ---: |
| 虫姬二 | Xenia | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | Xenia | 12 | 51 | 0.35 | 1.43 |
| 最大往生 | Xenia | 13 | 69 | 0.37 | 3.48 |
| 狱门山 | Xenia | 13 | 66 | 0.29 | 3.13 |
| 绊地狱 | shadPS4 | 16 | 55 | 0.32 | 0.62 |
| 大往生 | shadPS4 | 15 | 55 | 0.30 | 0.34 |
| ESP Ra.De. | shadPS4 | 20 | 46 | 0.43 | 0.62 |
| 弹铳 | shadPS4 | 17 | 46 | 0.36 | 0.66 |
| 空战之路 | shadPS4 | 16 | 46 | 0.35 | 0.62 |
| ESPGaluda | PCSX2 | 18 | 43 | 0.28 | 0.25 |
| 铸蔷薇 | PCSX2 | 18 | 43 | 0.38 | 0.27 |

**虫姬二的 10 ms** ≈ 平均等待读键 **8.33 ms**（60 Hz 下的半帧）＋提交首张位移画面 **0.78 ms**＋提交到 Windows 显示事件约 **0.92 ms**。设备上报和显示器响应另计。

## English

- **Xbox 360 input latency.** ESPGaluda II, SaiDaiOuJou and Guwange default to 0V. The ship can move in the same frame that the game reads the stick.
- **Lowest latency in Futari.** In the game, select **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing)**.
- **Faster controller response.** All three runtimes refresh gamepad and arcade-stick input when the game reads the controls, so a newly reported button press or direction can reach the current frame.
- **PS4 input latency.** All five games can prepare the next frame for display while the GPU copies the image. In Ketsui, the average time from reading the stick to submitting the first frame showing movement fell from **5.27 ms to 3.97 ms**.
- **PS2 input latency.** ESPGaluda and Ibara read input closer to drawing and submit the finished image sooner. Estimated latency averages about **18 ms** for both games.
- **PS2 graphics compatibility.** To address startup crashes reported on Intel Arc B390, ESPGaluda and Ibara now use a shader to scale the frame to the window. If either game still crashes, update your Intel graphics driver and try again. The update includes the Vulkan driver. [Intel driver download](https://www.intel.com/content/www/us/en/download/785597/intel-arc-graphics-windows.html)
- **NVIDIA Reflex.** All 11 games enable Reflex by default on supported NVIDIA GPUs to reduce input-to-display latency.
- **AMD Anti-Lag 2.** We don’t have Radeon hardware to verify stability, so Anti-Lag 2 is off by default. To try it, set `amd_anti_lag = true` under `[display]` in `user-data/config.ini` and restart the game. Use **Upload logs** to share your GPU model, driver version and how it felt.
- **PS2 crash reports.** After a crash, use **Upload logs** to send the call stack and the rendering steps leading up to it.

[Restore original Xbox 360 timing](https://github.com/zxl0714/EVAC-PC-LABEL/blob/v0.9.5/config/README.player-v09.txt#L116-L127)

## Latest input latency and CPU usage

**Lowest-latency settings used for the ports** in `user-data/config.ini`

- **All games** use `reflex = auto` and `vsync = false` under `[display]`.
- **Xbox 360** games use 0V. Select it in Futari’s in-game menu. Set `frame_delay = 0` under `[game.espgaluda2]`, `[game.sdoj]` and `[game.guwange]` for the other three games.
- **PS4** games use `neo = true` in each game’s section (the default). Both **PS2** games use the global settings above.

Measured on an i7-13700KF and RTX 4080. Latency is an estimated average in milliseconds. CPU usage is in equivalent logical cores. Xenia uses the original timing settings. [Test methods](https://github.com/zxl0714/EVAC-PC-LABEL/blob/v0.9.5/docs/PERFORMANCE.en.md)

| Game | Emulator | Port latency | Emulator latency | Port CPU | Emulator CPU |
| --- | --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | Xenia | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | Xenia | 12 | 51 | 0.35 | 1.43 |
| DoDonPachi SaiDaiOuJou | Xenia | 13 | 69 | 0.37 | 3.48 |
| Guwange | Xenia | 13 | 66 | 0.29 | 3.13 |
| Ketsui | shadPS4 | 16 | 55 | 0.32 | 0.62 |
| DaiOuJou | shadPS4 | 15 | 55 | 0.30 | 0.34 |
| ESP Ra.De. | shadPS4 | 20 | 46 | 0.43 | 0.62 |
| Dangun Feveron | shadPS4 | 17 | 46 | 0.36 | 0.66 |
| Battle Garegga | shadPS4 | 16 | 46 | 0.35 | 0.62 |
| ESPGaluda | PCSX2 | 18 | 43 | 0.28 | 0.25 |
| Ibara | PCSX2 | 18 | 43 | 0.38 | 0.27 |

**Futari’s 10 ms** ≈ **8.33 ms** waiting for the next input read on average (half a frame at 60 Hz) + **0.78 ms** to submit the first frame showing movement + about **0.92 ms** to the Windows display event. Device reporting and monitor response take additional time.
