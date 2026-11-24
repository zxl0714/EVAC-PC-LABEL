# EVAC PC LABEL v0.10.6

## English

- All four PS2 games can now play directly from CHD. Choose **Game data… → Choose disc image…** and select your CHD. The PS4 edition of DoDonPachi DaiOuJou can also use the PS2 CHD for Death Label. [#36](https://github.com/zxl0714/EVAC-PC-LABEL/issues/36)
- Fixed a graphics synchronization error before Death Label displays its first game frame in DoDonPachi DaiOuJou.
- Fixed a stall when Ibara first loads a stage. PS2 audio now keeps samples that can still be played after a brief interruption. [#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- All five PS4 games now recognize supported dumps with a patch that only changes the required firmware version.
- Fixed a crash in Mushihimesama Futari when Proton reads game data from a Windows-mounted drive.
- Big Picture now uses the bindings saved under **Controls → All games**. You can use your configured Start button to launch the selected game. [#35](https://github.com/zxl0714/EVAC-PC-LABEL/issues/35)
- Esc opens the in-game pause menu in DonPachi Recomp and DoDonPachi Recomp. [#37](https://github.com/zxl0714/EVAC-PC-LABEL/pull/37)
- The launcher now shows new releases more reliably and can restart an interrupted update when the server cannot resume the download. [#38](https://github.com/zxl0714/EVAC-PC-LABEL/pull/38)
- If a runtime file fails verification at startup, the launcher now shows its installed location and how to restore it from the program ZIP. [#39](https://github.com/zxl0714/EVAC-PC-LABEL/issues/39)

## 简体中文

- 四款 PS2 游戏现可直接读取 CHD。在“游戏数据…”→“选择光盘镜像…”里选中 CHD 即可。从 PS4 版《大往生》进入 Death Label 也能使用 PS2 的 CHD。[#36](https://github.com/zxl0714/EVAC-PC-LABEL/issues/36)
- 修复《大往生》Death Label 显示首张游戏画面前的一处图形同步错误。
- 修复《铸蔷薇》首次进关时的卡顿。PS2 游戏遇到短暂卡顿后会保留仍可播放的音频，减少断音。[#34](https://github.com/zxl0714/EVAC-PC-LABEL/issues/34)
- 五款 PS4 游戏均可识别只修改了固件版本要求的已支持游戏数据。
- 修复在 Proton 下从 Windows 挂载盘读取游戏数据时《虫姬二》可能闪退的问题。
- 大屏模式现在会使用“输入设置”→“所有游戏”里保存的按键。按自己设置的 Start 键即可启动选中的游戏。[#35](https://github.com/zxl0714/EVAC-PC-LABEL/issues/35)
- 《首领蜂》和《怒首领蜂》的 Recomp 版现可按 Esc 打开游戏内暂停菜单。[#37](https://github.com/zxl0714/EVAC-PC-LABEL/pull/37)
- 修复启动器有时不提示新版本的问题。更新下载中断后，如果服务器无法续传，启动器会重新下载。[#38](https://github.com/zxl0714/EVAC-PC-LABEL/pull/38)
- 启动时如果运行文件校验失败，启动器会显示该文件的实际位置，并提示如何用程序包恢复。[#39](https://github.com/zxl0714/EVAC-PC-LABEL/issues/39)
