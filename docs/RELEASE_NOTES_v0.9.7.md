# EVAC PC LABEL v0.9.7

- 启动器新增封面网格布局。
- 启动器记住窗口大小，下次打开时恢复。
- 修复大往生因模块内存地址分配失败而在加载时退出的问题。

想用回横幅布局，在 `user-data/config.ini` 的 `[collection]` 下设置：

```ini
[collection]
launcher_layout = banner
```

保存后重启启动器。

## English

- Added a cover grid layout to the launcher.
- The launcher now remembers its window size between sessions.
- Fixed a module memory address allocation error that could cause DaiOuJou to exit during loading.

To use the original banner layout, set the following under `[collection]` in `user-data/config.ini`:

```ini
[collection]
launcher_layout = banner
```

Save the file and restart the launcher.
