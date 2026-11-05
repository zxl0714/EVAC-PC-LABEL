# EVAC PC LABEL v0.9.5

## 更新内容

- **Xbox 360 的 0V。** ESPGaluda II、最大往生和狱门山默认使用 0V，已有配置优先。狱门山的普通、Blue 和 Arrange 单人模式都能在读到方向的那一帧显示机体移动。
- **虫姬二沿用游戏内设置。** 新存档默认 **1V(Fast)**。在 **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay** 选择 **0V(Tearing)**，然后选 **EXIT** 保存。
- **手柄和摇杆。** 三个平台都在游戏读键时刷新 SDL 输入。驱动已经收到的方向或按键，即使还没被窗口线程处理，也能赶上这一次读键。
- **PS4 出帧。** GPU 的画面复制命令一提交，窗口线程就开始准备输出，GPU 在实际读取这张图之前完成同步。绊地狱从游戏读到方向，到提交第一张机体移动画面，平均从 **5.27 ms 降到 3.97 ms**。
- **PS2 读键与出帧。** ESPGaluda 和铸蔷薇把一帧中可用的等待时间移到读键前，让更晚到达的输入也能赶上当前帧。关闭垂直同步时，ESPGaluda 在绘图同步点交出画面，铸蔷薇在整幅画面的合成命令提交后交出游戏选定的显示页。两作保持原有的 60 Hz 节奏，并在所有显卡上统一用着色器将画面等比绘制到窗口。
- **NVIDIA Reflex 与 AMD Anti-Lag 2。** 为进一步缩短输入到画面响应的等待，全部 11 款游戏都会在兼容的 NVIDIA 显卡上自动启用 Reflex。AMD Anti-Lag 2 默认关闭，可在 `user-data/config.ini` 的 `[display]` 下设置 `amd_anti_lag = true` 试用。我们缺少 Radeon 实机，欢迎 AMD 玩家用熟悉的关卡对比开关前后的手感和流畅度，通过启动器的「上传日志」反馈显卡型号、驱动版本和结果。
- **PS2 崩溃日志。** 「上传日志」会随崩溃报告附上异常线程可还原的调用栈、最近的绘图步骤，以及 GPU 最后确认完成的输出帧。

### 恢复原作时序

在 `user-data/config.ini` 中修改对应游戏的数值，重启游戏生效。

```ini
[game.espgaluda2]
frame_delay = 2

[game.sdoj]
frame_delay = 3

[game.guwange]
frame_delay = 2
```

### 输入延迟与 CPU 占用

测试机为 i7-13700KF／RTX 4080。延迟是从输入到达电脑到 Windows 显示事件的平均估算值（ms），CPU 是实测的等效逻辑核数。Xbox 360 移植版使用 0V，Xenia 使用原作档位。

| 游戏 | 移植版延迟 | Xenia 延迟 | 移植版 CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| 虫姬二 | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| 最大往生 | 13 | 69 | 0.37 | 3.48 |
| 狱门山 | 13 | 66 | 0.29 | 3.13 |

**虫姬二的 10 ms 是怎么算的？** 以 0V、Reflex 开启时向左拨杆为例。游戏每 16.67 ms 读一次方向。如果输入恰好在读键前到达，这一帧就能响应；如果刚错过，就要等将近一帧。算下来平均等 **8.33 ms**。游戏读到方向后，到提交第一张机体向左移动的画面，实测平均 **0.78 ms**；从提交到 Windows 记录这张画面已显示，按同机此前测量估算约 **0.92 ms**。三段相加约 **10 ms**。摇杆本身的上报时间，以及显示器扫描和像素响应的时间另计。

**PS4**

| 游戏 | 移植版延迟 | shadPS4 延迟 | 移植版 CPU | shadPS4 CPU |
| --- | ---: | ---: | ---: | ---: |
| 绊地狱 | 16 | 55 | 0.32 | 0.62 |
| 大往生 | 15 | 55 | 0.30 | 0.34 |
| ESP Ra.De. | 20 | 46 | 0.43 | 0.62 |
| 弹铳 | 17 | 46 | 0.36 | 0.66 |
| 空战之路 | 16 | 46 | 0.35 | 0.62 |

**PS2**

| 游戏 | 移植版延迟 | PCSX2 延迟 | 移植版 CPU | PCSX2 CPU |
| --- | ---: | ---: | ---: | ---: |
| ESPGaluda | 18 | 43 | 0.28 | 0.25 |
| 铸蔷薇 | 18 | 43 | 0.38 | 0.27 |

PS2 数值取自本版统一窗口缩放方式前的测量。ESPGaluda 的 CPU 样本分别取自移植版第一关和 PCSX2 的 Boss 场景。

## English

- **Xbox 360 frame delay.** ESPGaluda II, SaiDaiOuJou and Guwange default to 0V. Existing settings take precedence. In Guwange’s Arcade, Blue and Arrange single-player modes, the ship can move in the same frame that reads the stick.
- **Futari uses its in-game setting.** New saves start at **1V(Fast)**. Go to **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay**, select **0V(Tearing)**, then choose **EXIT** to save.
- **Gamepads and arcade sticks.** All three runtimes refresh SDL input when the game reads the controls. A button or direction that the driver has already received can reach the current frame even while the window thread is waiting to process the event.
- **PS4 presentation.** The window thread starts preparing the next frame as soon as the GPU copy is submitted. The GPU synchronizes the copy when the image is needed. In Ketsui, the average time from reading the stick to submitting the first frame showing movement fell from **5.27 ms to 3.97 ms**.
- **PS2 input and presentation.** ESPGaluda and Ibara move spare time in each frame ahead of the input read so that later inputs can still affect that frame. With VSync off, ESPGaluda hands over the image at its drawing sync point. Ibara hands over the game’s selected display buffer once the commands composing the full image have been submitted. Both retain their original 60 Hz timing and now use a shader on every GPU to scale the image to the window while preserving its aspect ratio.
- **NVIDIA Reflex and AMD Anti-Lag 2.** To further reduce input-to-display latency, all 11 games enable Reflex automatically on supported NVIDIA GPUs. AMD Anti-Lag 2 is opt-in. Set `amd_anti_lag = true` under `[display]` in `user-data/config.ini` to try it. We don’t have a Radeon GPU for testing. If you do, try a familiar stage with Anti-Lag 2 on and off, then use **Upload logs** to share your GPU, driver version and how the controls and frame pacing felt.
- **PS2 crash reports.** **Upload logs** includes the available call stack of the faulting thread, recent rendering steps and the last output frame the GPU confirmed complete.

### Restore original timing

Change the value for your game in `user-data/config.ini`, then restart the game.

```ini
[game.espgaluda2]
frame_delay = 2

[game.sdoj]
frame_delay = 3

[game.guwange]
frame_delay = 2
```

### Input latency and CPU usage

Test PC: i7-13700KF and RTX 4080. Latency is the estimated average from input reaching the PC to the Windows display event, in milliseconds. CPU usage is measured in equivalent logical cores. The Xbox 360 ports use 0V and Xenia uses the original settings.

| Game | Port latency | Xenia latency | Port CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| DoDonPachi SaiDaiOuJou | 13 | 69 | 0.37 | 3.48 |
| Guwange | 13 | 66 | 0.29 | 3.13 |

**Where do Futari’s 10 ms come from?** Take a leftward stick input at 0V with Reflex on. The game reads the stick every 16.67 ms. An input that arrives just before a read can move the ship in that frame. One that just misses the read waits nearly a frame, giving an average wait of **8.33 ms**. From the read to submitting the first frame showing the ship moving left took **0.78 ms** on average. An earlier measurement on the same PC puts submission to the Windows display event at about **0.92 ms**. Together that gives about **10 ms**. Controller reporting, monitor scanout and pixel response take additional time.

**PS4**

| Game | Port latency | shadPS4 latency | Port CPU | shadPS4 CPU |
| --- | ---: | ---: | ---: | ---: |
| Ketsui | 16 | 55 | 0.32 | 0.62 |
| DaiOuJou | 15 | 55 | 0.30 | 0.34 |
| ESP Ra.De. | 20 | 46 | 0.43 | 0.62 |
| Dangun Feveron | 17 | 46 | 0.36 | 0.66 |
| Battle Garegga | 16 | 46 | 0.35 | 0.62 |

**PS2**

| Game | Port latency | PCSX2 latency | Port CPU | PCSX2 CPU |
| --- | ---: | ---: | ---: | ---: |
| ESPGaluda | 18 | 43 | 0.28 | 0.25 |
| Ibara | 18 | 43 | 0.38 | 0.27 |

The PS2 figures were measured before this release’s change to window scaling. ESPGaluda CPU samples cover Stage 1 in the port and a boss scene in PCSX2.
