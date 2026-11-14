# EVAC PC LABEL v0.9.10

- 绊地狱新增 **X Arrange (Xbox 360)** 曲集。在 `game-data/ketsui/x-arrange/` 放入 Xbox 360 版的曲目后，可在 **BGM SET** 选择。[放置说明](GAME_DATA.md#ketsui)
- 大往生（01.00／01.04）和绊地狱（01.00／01.03）新增对日版 PlayStation Store 导出数据及对应 FPKG 解包目录的支持。[PR #18](https://github.com/zxl0714/EVAC-PC-LABEL/pull/18)
- 改善 PS4 游戏数据识别兼容性，支持缺少或更换系统封面、背景图的导出数据。
- 修复虫姬二切换存储设备时的崩溃。
- 修复 PS4 游戏窗口缩放时可能崩溃的问题。

## English

- Added **X Arrange (Xbox 360)** music for Ketsui. Place the Xbox 360 tracks in `game-data/ketsui/x-arrange/` and select the set in **BGM SET**. [File placement](GAME_DATA.en.md#ketsui)
- Added support for Japanese PlayStation Store dumps and their extracted FPKG layouts for DaiOuJou (01.00 and 01.04) and Ketsui (01.00 and 01.03). [PR #18](https://github.com/zxl0714/EVAC-PC-LABEL/pull/18)
- Improved PS4 game-data recognition for exports with missing or different system cover and background art.
- Fixed a crash when selecting a storage device in Mushihimesama Futari.
- Fixed a crash that could occur while resizing a PS4 game window.

![X Arrange (Xbox 360) in the Ketsui BGM SET menu](images/ketsui-x-arrange.jpg)
