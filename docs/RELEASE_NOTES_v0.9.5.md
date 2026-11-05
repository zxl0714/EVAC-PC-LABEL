# EVAC PC LABEL v0.9.5

- ESPGaluda II、最大往生和狱门山新增可选 0V。虫姬二可在游戏内的 Frame Delay 设置中选择 0V。
- Xbox 360、PS4 和 PS2 游戏在兼容的 NVIDIA 显卡上自动启用 Reflex。Radeon 玩家可在 `[display]` 下设置 `amd_anti_lag = true` 试用 Anti-Lag 2。
- PS4 游戏提前提交已绘制的画面，缩短输入到呈现的等待。
- ESPGaluda 和铸蔷薇在绘制完成后提前呈现画面，缩短操作到画面响应的时间。
- 摇杆和手柄在游戏读取按键时刷新状态，减少已到达的输入错过当前帧的情况。

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

- ESPGaluda II, SaiDaiOuJou and Guwange now offer 0V. Futari already includes it in the in-game Frame Delay settings.
- Xbox 360, PS4 and PS2 titles automatically enable Reflex on supported NVIDIA GPUs. Radeon users can try Anti-Lag 2 by setting `amd_anti_lag = true` under `[display]`.
- PS4 games submit frames earlier to reduce presentation latency.
- ESPGaluda and Ibara present completed frames earlier so movement appears sooner after input.
- Xbox 360, PS4 and PS2 titles refresh gamepad and arcade-stick input when the game reads it, so an input already available to the driver can reach the current frame.

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
