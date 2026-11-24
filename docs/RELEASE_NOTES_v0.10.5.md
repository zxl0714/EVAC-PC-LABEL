# EVAC PC LABEL v0.10.5

## English

- Added CapsLock4284’s DoDonPachi Recomp to the launcher. Download the Windows release and extract it to `external/DoDonPachi-Recomp/`, then select your World or Japanese arcade ROM ZIP under **Game data**. With a merged ZIP, you can switch regions in the game menu. [Download DoDonPachi Recomp](https://github.com/CapsLock4284/DoDonPachi-Recomp/releases/latest) · [Setup guide](GAME_DATA.en.md#dodonpachi) · [#32](https://github.com/zxl0714/EVAC-PC-LABEL/issues/32)
- Fixed a regression in Ibara that delayed the picture during gameplay. [#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- Death Label now skips the RivaTuner Statistics Server (RTSS) overlay when launched from the PS4 version of DoDonPachi DaiOuJou, to avoid a reported crash on entry.
- This release brings SteamOS compatibility updates to all 15 games in the collection. Mushihimesama Futari now recognizes the original Black Label and Ver.1.01 DLC packages. We also fixed a loading-screen hang in Ketsui on some Vulkan devices.
- Fixed a startup crash in the PS4 version of DoDonPachi DaiOuJou on older CPUs without AVX. All five PS4 games use the same compatibility path automatically.
- PS2 games can now use a software Vulkan driver such as Mesa llvmpipe when a hardware GPU is unavailable.

## 简体中文

- 启动器接入 CapsLock4284 的《怒首领蜂》DoDonPachi Recomp。先下载作者发布的 Windows 版，解压到 `external/DoDonPachi-Recomp/`，再到「游戏数据」里选择街机国际版或日版 ROM ZIP。使用合并包时，可以在游戏菜单里切换地区。[下载 DoDonPachi Recomp](https://github.com/CapsLock4284/DoDonPachi-Recomp/releases/latest) · [数据放置说明](GAME_DATA.md#dodonpachi) · [#32](https://github.com/zxl0714/EVAC-PC-LABEL/issues/32)
- 修复《铸蔷薇》关内画面响应变慢的问题。[#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- 从 PS4 版《大往生》进入 Death Label 时会跳过 RTSS 屏显层，以避开玩家遇到的进场闪退。
- 本次 SteamOS 适配覆盖合集全部 15 款游戏。《虫姬二》现可识别 Black Label 和 Ver.1.01 原始 DLC 包，《绊地狱》在部分 Vulkan 设备上卡在加载画面的问题也已修复。
- 修复 PS4 版《大往生》在缺少 AVX 的旧 CPU 上启动时崩溃的问题。五款 PS4 游戏都会自动启用这项兼容处理。
- PS2 游戏现在可以在没有硬件 GPU 的环境下使用 Mesa llvmpipe 等 Vulkan 软件驱动运行。
