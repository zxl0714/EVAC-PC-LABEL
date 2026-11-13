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
