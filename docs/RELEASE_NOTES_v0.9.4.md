# EVAC PC LABEL v0.9.4

- 新增 PS2《铸蔷薇》，支持 Arcade、Arrange、练习模式和画廊。平均输入延迟估算约 32 ms，同机 PCSX2 约 43 ms。
- 修复《虫姬二》在 Arcade 模式使用 Reco（Normal）时的多处关内崩溃。
- 修复《ESPGaluda》在部分音频设备上加载关卡后声音滞后的问题。PS2 游戏现可兼容更多播放设备。
- 修复《绊地狱》回收纹理时可能触发的一处崩溃。
- 部分 AMD Radeon 核显运行 Xbox 360 游戏时可能在启动后崩溃。四款 Xbox 360 游戏现已在这些设备上优先使用 Vulkan，以避开这处 Direct3D 12 故障。

## English

- Added the PS2 version of Ibara with Arcade, Arrange, Practice, and Gallery. Estimated average input latency was about 32 ms for the port and 43 ms for PCSX2 on the same PC.
- Fixed several in-stage crashes when playing Reco (Normal) in Mushihimesama Futari Arcade mode.
- Fixed an issue where ESPGaluda's audio could lag after a stage loaded on some audio devices. PS2 games now support a wider range of playback devices.
- Fixed a crash that could occur when Ketsui freed textures.
- Xbox 360 games could crash shortly after launch on some AMD Radeon integrated GPUs. All four titles now prefer Vulkan on those devices to avoid the Direct3D 12 failure.
