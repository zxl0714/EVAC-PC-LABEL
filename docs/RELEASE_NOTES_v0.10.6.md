# EVAC PC LABEL v0.10.6

## English

- All four PS2 games now support CHD disc images. [#36](https://github.com/zxl0714/EVAC-PC-LABEL/issues/36)
- Improved Death Label startup in DoDonPachi DaiOuJou. On compatible Windows systems, the game switches to DXGI when it detects RivaTuner Statistics Server (RTSS), avoiding the Vulkan display calls seen in crash reports.
- Fixed a stutter when first entering a stage in Ibara and reduced audio dropouts after brief stalls in PS2 games. [#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- The launcher now recognizes PS4 game dumps patched only to lower the required system software version.
- Fixed a startup crash in Mushihimesama Futari when playing on Linux with game data on a Windows drive.
- Big Picture now uses the bindings saved under **Controls → All games**. You can use your configured Start button to launch the selected game. [#35](https://github.com/zxl0714/EVAC-PC-LABEL/issues/35)
- Esc opens the in-game pause menu in DonPachi Recomp and DoDonPachi Recomp. [#37](https://github.com/zxl0714/EVAC-PC-LABEL/pull/37)
- The launcher now shows new releases more reliably and can restart an interrupted update when the server cannot resume the download. [#38](https://github.com/zxl0714/EVAC-PC-LABEL/pull/38)
- If a game cannot start because a runtime file does not match the release, the launcher now tells you which file to restore from the program ZIP. [#39](https://github.com/zxl0714/EVAC-PC-LABEL/issues/39)

## 简体中文

- 四款 PS2 游戏现已支持 CHD 光盘镜像。[#36](https://github.com/zxl0714/EVAC-PC-LABEL/issues/36)
- 改善《大往生》Death Label 的启动稳定性。在支持的 Windows 设备上，检测到 RivaTuner Statistics Server（RTSS）时会自动改用 DXGI 显示，绕开玩家日志中发生闪退的 Vulkan 调用。
- 修复《铸蔷薇》首次进关时的卡顿，减少 PS2 游戏卡顿后的断音。[#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- PS4 游戏打过只降低系统版本要求的补丁后，启动器也能正常识别。
- 修复在 Linux 上从 Windows 磁盘加载《虫姬二》时的启动闪退。
- 大屏模式现在会使用“输入设置”→“所有游戏”里保存的按键。按自己设置的 Start 键即可启动选中的游戏。[#35](https://github.com/zxl0714/EVAC-PC-LABEL/issues/35)
- 《首领蜂》和《怒首领蜂》的 Recomp 版现可按 Esc 打开游戏内暂停菜单。[#37](https://github.com/zxl0714/EVAC-PC-LABEL/pull/37)
- 修复启动器有时不提示新版本的问题。更新下载中断后，如果服务器无法续传，启动器会重新下载。[#38](https://github.com/zxl0714/EVAC-PC-LABEL/pull/38)
- 游戏因运行文件版本不匹配而无法启动时，启动器会提示需要从程序包中恢复哪个文件。[#39](https://github.com/zxl0714/EVAC-PC-LABEL/issues/39)
