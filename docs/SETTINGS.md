# 设置与操作

[简体中文](SETTINGS.md) | [English](SETTINGS.en.md) · [返回首页](../README.md)

## 配置与操作

启动按钮上方可选择全屏或 1280×720、1600×900、1920×1080、2560×1440、3840×2160 窗口分辨率，重启后恢复上次选择。排行榜昵称在页头右侧修改；右上角 `CN / EN / JP` 切换启动器语言。保存昵称时，如有游戏字库无法显示的字符，会提示受影响游戏及实际传入的名字。游戏本身的语言按原作提供。

底部“配置文件”打开 `user-data/config.ini`，可编辑音量、键盘、手柄和联网选项。保存后在启动器点“刷新”，再启动游戏生效。完整选项见[默认配置模板](../config/default.ini)。

`[display]` 的 `vsync = false` 使用低延迟呈现；游戏仍按原作节拍运行，可能出现画面撕裂。需要垂直同步时设为 `true`。升级保留已有配置，原来为 `true` 的设置可在这里修改。

| 默认键盘操作 | 功能 |
| --- | --- |
| 方向键 | 方向输入 |
| Z / X / C / V | 手柄下／右／左／上方按钮 |
| Enter / Backspace | Start / Select（PS4 为 Touchpad） |
| A / S、Q / W | L1 / R1、L2 / R2 |
| F11 或 Alt+Enter | 切换全屏 |
| Alt+F4 | 关闭游戏 |
| F8 | 在日志中标记当前时间，方便反馈问题 |

排行榜玩家卡片可点击 `CLOSE` 或按 Enter／Esc 关闭。

手柄主按键对照：

| 方位 | Xbox 360 | PS4 | 键盘 |
| --- | --- | --- | --- |
| 下／South | A | × | Z |
| 右／East | B | ○ | X |
| 左／West | X | □ | C |
| 上／North | Y | △ | V |

射击、炸弹、换人等具体动作由各游戏内的按键设置决定。手柄和摇杆通过 SDL 接入，可使用其支持的 XInput、PlayStation 等设备。


空战之路默认：`V` 投币，`Enter` 开始，`C / S` 射击，`Z / A` 武器，`X` 阵型，`Backspace` 暂停菜单。快速存档按游戏模式分别保存。

狱门山的 **Arrange Xbox 360** 自动射击：方向键／左摇杆移动角色，`I / K / J / L`（上／下／左／右）或右摇杆移动式神，`W / RT` 召唤，`X / B` 炸弹。式神方向可在 `[game.guwange.keyboard]` 中通过 `right_stick_up/down/left/right` 调整；其他游戏的键位不受影响。

备份或换目录时保留整个 `user-data/`，其中包含配置、存档和录像。修改游戏内设置后，等待保存完成再关闭。

## NEO 模式

PS4 游戏默认启用 NEO（PS4 Pro）模式，使用 1080p 合成输出。弹铳的首张响应画面比普通模式提前约一帧，绊地狱、大往生和 ESP Ra.De. 的同场景延迟基本不变，GPU 开销略增。

关闭 NEO：打开启动器底部“配置文件”，在 `user-data/config.ini` 已有的对应游戏段落中添加或修改 `neo=false`，然后重启游戏。例如关闭绊地狱：

```ini
[game.ketsui]
neo = false
```

大往生、ESP Ra.De.、弹铳、空战之路分别使用 `[game.daioujou]`、`[game.esprade]`、`[game.feveron]`、`[game.garegga]`。设回 `neo=true` 可重新开启。

## 最大往生 Frame Delay

在 `user-data/config.ini` 的 `[game.sdoj]` 段设置：

```ini
[game.sdoj]
data_dir = game-data/sdoj
frame_delay = 0
```

| 值 | 档位 |
| --- | --- |
| 0 | 提交画面后等待下一次 VBlank（默认） |
| 1 | 低延迟 |
| 2 | 平衡 |
| 3 | 原始路径 |

这是游戏内部的帧调度档位；实际按键到画面的总延迟还受手柄和显示链路影响。已有明确设置会保留，修改后重启游戏生效。

## G2 Frame Delay

在 `user-data/config.ini` 设置：

```ini
[game.espgaluda2]
frame_delay = 0
```

默认 `0` 将等待移到提交画面之后，`1` 为低延迟路径，`2` 为原作时序。已有显式设置保持不变；修改后重启游戏生效。

## AMD Radeon Anti-Lag 2

Xbox 360、PS4 和 PS2 游戏共用一个开关。在 `user-data/config.ini` 的 `[display]` 下写入 `amd_anti_lag = true` 并重启游戏。设为 `false` 即可关闭。未填写时，标准程序包默认关闭，文件名带 `-amd-anti-lag2` 的测试包默认开启。已有配置中的显式选择优先。

运行时按实际渲染显卡和驱动能力启用。运行日志里的 `AMD_ANTI_LAG` 记录探测结果。NVIDIA 显卡继续使用独立的 `reflex` 设置。

## 虫姬二、狱门山与 NVIDIA Reflex

虫姬二在游戏内依次选 **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing)**，选 **EXIT** 保存。如果已经显示 **Screen Setting (Maniac)**，直接进入 **Extra Setting**。狱门山默认使用 `0V`，在 `[game.guwange]` 下配置 `frame_delay`，普通、Blue 和 Arrange 单人模式可减少两帧输入延迟；设为 `2` 可恢复原作时序。

Xbox 360、PS4 和 PS2 游戏默认在支持的 NVIDIA 显卡上自动启用 Reflex。要关闭，在 `[display]` 下设置 `reflex = off`，重启游戏后生效。设回 `auto` 恢复自动探测。

## 社区排行榜

联网默认开启，无须注册。在启动器设置昵称后，通过游戏内的排行榜和录像界面使用社区服务。各作品使用独立榜单，上榜资格沿用游戏规则。

同一游戏内，相同昵称会归为同一社区身份，首次游玩请把默认的 `Player` 改成自己的昵称，并固定使用。服务会检查提交格式及录像完整性，但不验证昵称归属。最大往生团队评分和大往生 CUSTOM 成绩提交暂未接入。狱门山支持普通／Blue／Arrange 排行榜和录像；在线合作未接入，本地双人尚未验收。

提交到社区的昵称、成绩及分享的录像可被其他玩家查看；未分享的本地录像不会自动公开。

~~~ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
~~~

将 `enabled` 改为 `false` 可离线游玩。

## 检查更新

启动器会在后台检查 GitHub 正式发布，每个新版本只自动提醒一次；游玩中不弹窗。提醒中可打开发布页查看说明和下载。

自动检查的成功结果缓存 24 小时。要关闭自动检查，在 `user-data/config.ini` 设置：

```ini
[collection]
check_for_updates = false
```

此选项与社区排行榜联网开关独立。提醒记录保存在 `user-data/update-check.json`，更新文件由玩家自行下载和替换。

## 更新与备份

更新时先关闭游戏和启动器，再解压到原目录替换同名文件。程序包不包含 `user-data/`，已有配置、存档和录像保留。

- `user-data/`：配置、各游戏的存档、录像等。备份或换目录时，保留整个文件夹。
- `game-data/`：自行准备的游戏数据。换目录时可一并转移，也可重新选择原数据目录。
- `logs/`：运行日志。保留遇到问题的那次记录即可。

数据校验缓存位于 `user-data/cache/`。保留它可减少更新后的启动等待；文件变化时会自动重新校验。

旧版根目录的 `config.ini` 会自动迁入 `user-data/config.ini`；新位置已有配置时优先使用它。相对数据路径以合集根目录为起点。

## 日志与启动问题

退出游戏后点击“上传日志”，默认选择最近一局，也可以切换其他记录。在留言中写下模式、角色和问题场景，再点击“上传”。成功后会显示报告编号，反馈时附上这个编号；“查看本地日志”可打开对应记录。

游戏无法启动时也可上传日志。若运行组件不可用，点击“详情”查看具体文件和处理方法。数据缺失或版本不支持时，查看[数据指南](GAME_DATA.md#无法识别时怎么检查)。

PS4 作品提示 Vulkan 版本过低时，更新显卡驱动并重启 Windows。AMD 显卡可从[官方驱动支持页](https://www.amd.com/en/support/download/drivers.html)选择对应型号。
