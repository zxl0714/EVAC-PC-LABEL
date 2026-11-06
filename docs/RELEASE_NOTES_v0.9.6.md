# EVAC PC LABEL v0.9.6

- 修复部分空战之路数据因 `license.info` 不同而无法启动的问题，并同步调整其他 PS4 游戏的校验。
- 加快游戏识别与启动，更新程序后可继续复用未变化数据的校验结果。
- 修复 PS2 图形后端的资源绑定错误，改善部分 Intel 显卡的兼容性。
- 补充 PS2 启动阶段日志，上传报告时自动记录游戏进程状态，方便定位无法启动的问题。

## English

- Fixed Battle Garegga failing to launch when `license.info` differs between packages. Applied the same verification change to the other PS4 games.
- Faster game detection and startup. Program updates reuse verification results for unchanged game data.
- Fixed a resource-binding error in the PS2 graphics backend, improving compatibility with some Intel GPUs.
- Added PS2 startup milestones and process status to uploaded reports to help diagnose launch failures.
