# 命令行指南

[简体中文](CLI.md) · [English](CLI.en.md)

## 直接启动游戏

准备好游戏数据后，在合集目录用 PowerShell 运行。

```powershell
& '.\EVAC PC LABEL.exe' --game futari
```

换个游戏 ID 就能启动其他游戏，数据和存档与启动器共用。

命令提示符（CMD）的写法如下。

```bat
"EVAC PC LABEL.exe" --game futari
```

## 启动器大屏模式

点击启动器底部的 **大屏模式**，或在游戏库中按 `F11`。大屏里可选择 **桌面模式** 或再按 `F11` 返回。启动器会记住退出时的模式。

普通窗口和大屏都可用手柄十字键、左摇杆或街机摇杆选游戏，按确认键启动。大屏里按返回键打开退出菜单。

从大屏启动游戏后，长按 `Esc` 或同时按住 `Start` 和十字键 `↑` 约一秒，即可退出游戏并返回大屏。这两项可在 `user-data/config.ini` 的 `[collection]` 中修改。

```ini
[collection]
big_picture_exit_key = Escape
big_picture_exit_buttons = Start + Up
```

键盘可改用 `F12` 等按键。手柄组合使用游戏的按键绑定，一次在同一设备上按住至少两个键，例如 `Start + Select`。街机摇杆可到启动器的「输入设置」绑定对应按键。任一项设为 `none` 即可关闭。

机台可以用以下命令直接打开大屏。

```powershell
& '.\EVAC PC LABEL.exe' --big-picture
```

## 游戏 ID

| 平台 | 游戏 | ID |
| --- | --- | --- |
| Xbox 360 | 虫姬二 | `futari` |
| Xbox 360 | ESPGaluda II | `espgaluda2` |
| Xbox 360 | 怒首领蜂最大往生 | `sdoj` |
| Xbox 360 | 狱门山 | `guwange` |
| PS4 | 绊地狱 | `ketsui` |
| PS4 | 怒首领蜂大往生 | `daioujou` |
| PS4 | ESP Ra.De. Ψ | `esprade` |
| PS4 | 弹铳 | `feveron` |
| PS4 | 空战之路 | `garegga` |
| PS2 | ESPGaluda | `espgaluda` |
| PS2 | 铸蔷薇 | `ibara` |
| PS2 | 大往生 (PS2) | `daioujou-ps2` |
| 街机 | 首领蜂 | `donpachi` |

## 可选参数

| 参数 | 何时使用 |
| --- | --- |
| `--data-dir <目录>` | 临时改用其他数据目录。平时使用启动器选好的目录，或默认的 `game-data/<id>/`。相对路径从合集目录计算。 |
| `--profile <名称>` | 使用独立存档，路径为 `user-data/<id>/<名称>/`，默认名称为 `default`。名称可用英文字母、数字、下划线和连字符，最长 64 个字符。 |
| `--root <目录>` | 使用另一个合集安装目录。默认使用 EXE 所在目录。 |

按需把这些参数加到 `--game <id>` 后面，带空格的路径加引号。PS2 的数据目录是放有 ISO 的文件夹。[各平台目录示例](GAME_DATA.md)

```powershell
& '.\EVAC PC LABEL.exe' --game ketsui --data-dir 'D:\Game Data\Ketsui' --profile practice
```

从脚本或前端调用时，EXE 会等待游戏退出并返回游戏进程的退出码。启动器报错时会弹窗并返回 `2`。
