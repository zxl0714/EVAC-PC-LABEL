# EVAC PC LABEL v0.9.5

- ESPGaluda II、最大往生和狱门山默认使用 0V。虫姬二保留游戏内选择，在 Frame Delay 菜单可选 **0V (Tearing)**；原作新档默认 **1V (Fast)**。
- 为了进一步压低从按键到画面响应的延迟，全部 11 款游戏在兼容的 NVIDIA 显卡上自动启用 Reflex。Radeon 玩家可在 `user-data/config.ini` 的 `[display]` 下设置 `amd_anti_lag = true` 试用 Anti-Lag 2。标准包默认关闭，AMD 测试包默认开启。
- 我们手头没有 AMD 显卡，Anti-Lag 2 的兼容性和实际收益还需要实机验证。欢迎 Radeon 玩家用熟悉的关卡对比开关前后的手感和流畅度，通过启动器的“上传日志”告诉我们显卡型号、驱动版本和测试结果。
- PS4 游戏提前提交已绘制的画面，缩短输入到呈现的等待。
- ESPGaluda 和铸蔷薇在绘制完成后提前呈现画面，缩短操作到画面响应的时间。
- 摇杆和手柄在游戏读取按键时刷新状态，减少已到达的输入错过当前帧的情况。

要恢复原作时序，在 `user-data/config.ini` 的对应区段设置 `frame_delay` 后重启游戏。已有配置优先。

| 游戏 | 区段 | 原作设置 |
| --- | --- | --- |
| ESPGaluda II | `[game.espgaluda2]` | `frame_delay = 2` |
| 最大往生 | `[game.sdoj]` | `frame_delay = 3` |
| 狱门山 | `[game.guwange]` | `frame_delay = original` |

延迟为平均估算值（ms），CPU 为等效逻辑核。Xbox 360 移植版使用 0V，Xenia 使用原作档位。

| 游戏 | 移植版延迟 | Xenia 延迟 | 移植版 CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| 虫姬二 | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| 最大往生 | 13 | 69 | 0.37 | 3.48 |
| 狱门山 | 13 | 66 | 0.29 | 3.13 |

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

## English

- ESPGaluda II, SaiDaiOuJou and Guwange now default to 0V. In Futari, choose **0V (Tearing)** in the in-game Frame Delay menu. A new Futari save starts at **1V (Fast)**.
- To further reduce the time from a button press to visible movement, all 11 games automatically enable Reflex on supported NVIDIA GPUs. Radeon players can try Anti-Lag 2 by setting `amd_anti_lag = true` under `[display]` in `user-data/config.ini`. It defaults to off in the standard package and on in the AMD test package.
- We don't have a Radeon GPU to verify compatibility or measure the latency benefit. If you have one, try a familiar stage with Anti-Lag 2 on and off, then use **Upload logs** in the launcher to share your GPU model, driver version and how the controls and frame pacing felt.
- PS4 games submit frames earlier to reduce presentation latency.
- ESPGaluda and Ibara present completed frames earlier so movement appears sooner after input.
- Xbox 360, PS4 and PS2 titles refresh gamepad and arcade-stick input when the game reads it, so an input already available to the driver can reach the current frame.

To restore the original timing, set `frame_delay` in the corresponding section of `user-data/config.ini` and restart the game. Existing settings take precedence.

| Game | Section | Original setting |
| --- | --- | --- |
| ESPGaluda II | `[game.espgaluda2]` | `frame_delay = 2` |
| SaiDaiOuJou | `[game.sdoj]` | `frame_delay = 3` |
| Guwange | `[game.guwange]` | `frame_delay = original` |

Estimated average latency in ms and CPU usage in equivalent logical cores. The Xbox 360 ports use 0V. Xenia uses the original game settings.

| Game | Port latency | Xenia latency | Port CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| DoDonPachi SaiDaiOuJou | 13 | 69 | 0.37 | 3.48 |
| Guwange | 13 | 66 | 0.29 | 3.13 |

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
