# EVAC PC LABEL v0.9.4

- 新增 PS2《铸蔷薇》，可游玩 Arcade、Arrange 和练习模式，也可浏览画廊。
- 修复《虫姬二》在 Arcade 模式使用 Reco（Normal）打到二面时的一处崩溃。
- PS2 游戏改用 SDL3 输出音频，改善 44.1 kHz 设备的播放兼容性，减少关卡加载后声音滞后的情况。
- 四款 Xbox 360 游戏在部分 AMD Radeon 核显（设备 ID `1002:1638`）上优先使用 Vulkan。初始化失败时自动切回 Direct3D 12。

## English

- Added the PS2 version of Ibara, including Arcade, Arrange, Practice, and Gallery.
- Fixed a Stage 2 crash when playing Reco (Normal) in Mushihimesama Futari Arcade mode.
- PS2 games now use SDL3 audio, improving playback on 44.1 kHz devices and reducing audio lag after stage loads.
- All four Xbox 360 games now prefer Vulkan on AMD Radeon integrated GPUs with device ID `1002:1638`, with automatic Direct3D 12 fallback if initialization fails.
