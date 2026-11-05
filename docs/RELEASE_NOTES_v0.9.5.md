# EVAC PC LABEL v0.9.5

- ESPGaluda II、最大往生和狱门山新增可选 0V。虫姬二可在游戏内的 Frame Delay 设置中选择 0V。
- Xbox 360 游戏新增 NVIDIA Reflex 支持，在兼容的 NVIDIA 显卡上自动启用。
- 摇杆和手柄在游戏读取按键时刷新状态，减少已到达的输入错过当前帧的情况。

延迟为平均估算值（ms），CPU 为等效逻辑核。移植版使用 0V，Reflex 自动开启。Xenia 使用原作档位。

| 游戏 | 移植版延迟 | Xenia 延迟 | 移植版 CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| 虫姬二 | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| 最大往生 | 13 | 69 | 0.37 | 3.48 |
| 狱门山 | 13 | 66 | 0.29 | 3.13 |

## English

- ESPGaluda II, SaiDaiOuJou and Guwange now offer 0V. Futari already includes it in the in-game Frame Delay settings.
- Xbox 360 titles automatically enable NVIDIA Reflex on supported NVIDIA GPUs.
- Xbox 360 titles refresh gamepad and arcade-stick input when the game reads it, so an input already available to the driver can reach the current frame.

Estimated average latency in ms and CPU usage in equivalent logical cores. The ports use 0V with Reflex enabled. Xenia uses the original game settings.

| Game | Port latency | Xenia latency | Port CPU | Xenia CPU |
| --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | 10 | 36 | 0.07 | 1.28 |
| ESPGaluda II | 12 | 51 | 0.35 | 1.43 |
| DoDonPachi SaiDaiOuJou | 13 | 69 | 0.37 | 3.48 |
| Guwange | 13 | 66 | 0.29 | 3.13 |
