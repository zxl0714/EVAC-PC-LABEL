# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

将 CAVE 的经典弹幕射击游戏带到 Windows PC。

EVAC PC LABEL 是 CAVE 游戏的非官方 PC 移植合集，提供统一启动器、共享配置、存档、录像和社区排行榜。游戏数据按需安装。

[开始游玩](#开始游玩) · [游戏数据指南](docs/GAME_DATA.md) · [更新记录](docs/CHANGELOG.md) · [开发与构建](FOR_AGENT.md)

下载入口见“下载与备份”。

## 延迟与性能

更低的 CPU 开销，以及 G2、最大往生的 1V 低延迟选项。360 代表场景的延迟估算与 CPU 实测对比：

| 游戏 | 移植版延迟/ms | Xenia 延迟/ms | 移植版 CPU/核 | Xenia CPU/核 |
| --- | ---: | ---: | ---: | ---: |
| 虫姬二 | 26 | 36 | 0.08 | 1.26 |
| ESPGaluda II | 26 | 51 | 0.38 | 1.37 |
| 最大往生 | 26 | 69 | 0.43 | 3.46 |

移植版三款均按 1V；G2 需[启用实验性 Frame Delay](#g2-实验性-frame-delay)，默认仍为 2V。Xenia 分别保留原作的 1V、2V、3V。

延迟由游戏内测量、8.3ms 平均采样等待和同机历史呈现数据估算，输出路径按移植版独立翻转、Xenia 桌面合成；不含设备输入及显示器扫描、面板响应。CPU 为 i7-13700KF／RTX 4080 回放场景的等效逻辑核数，1 核 = 1000 CPU ms/s。[比较方法与数据](docs/ANALYSIS.md#360-统一条件比较)。

## 开始游玩

### 运行环境

使用 Windows x64，并安装显卡厂商的正式驱动。虫姬二、G2、最大往生、狱门山需要 Direct3D 12；绊地狱、大往生、ESP Ra.De.、弹铳需要 Vulkan。程序运行库随玩家包附带。

### 运行

1. 将程序包解压到一个可写文件夹，游戏数据需自行准备。
2. 运行根目录的 `EVAC PC LABEL.exe`，备用入口为 `Launch.bat`。
3. 选中想玩的游戏，点击底部“游戏数据…”→“选择数据目录…”，选择已经解包的游戏文件夹。也可以按下表直接放入合集。
4. 点击“启动游戏”。首次启动会校验数据；PS4 作品还会自动准备运行文件，请等待完成。

## 游戏数据

只需准备想玩的游戏：

| 游戏 | 常用简称 | 支持的数据版本 |
| --- | --- | --- |
| 虫姫さまふたり Ver.1.5 | 虫姬二／Futari | Xbox 360；可选 Black Label、Ver.1.01 DLC |
| エスプガルーダII ブラックレーベル | G2／Espgaluda II | Xbox 360 |
| ケツイ Deathtiny ～絆地獄たち～ | 绊地狱／Ketsui | PS4，CUSA09062，01.00 本体或覆盖 01.02 更新后的完整单目录 |
| 怒首領蜂最大往生 | 最大往生／SDOJ | Xbox 360，原版光盘数据 |
| 怒首領蜂 大往生 臨廻転生 | 大往生／DaiOuJou | PS4，CUSA32997，01.00 本体或 01.04 完整合并数据 |
| エスプレイドΨ | ESP Ra.De. Psi | PS4，CUSA15155，01.00 本体覆盖 01.04 更新后的完整数据 |
| ぐわんげ | 狱门山／Guwange | Xbox 360 XBLA，58410A78，本体 0.0.0.4 |
| 弾銃フィーバロン | 弹铳／Dangun Feveron | PS4，美版 CUSA06735，01.00 本体 |

### ISO／PKG／XBLA 怎么解包

- **Xbox 360 ISO**：下载 [extract-xiso 的 Windows Release](https://github.com/XboxDev/extract-xiso/releases)，使用 `extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"`。提取后应能直接看到 `default.xex`。
- **PS4 PKG**：双击程序包中的 `tools/Extract-PS4.exe`，选择本体 PKG 和新输出目录，点击“Extract”。完成后点击“Open game files”，把里面的全部内容复制到对应的 `game-data/<游戏>/`。更新 PKG 用同一个工具另行解包，再把提取的全部内容覆盖进本体目录，替换同名文件。

保留整个 `tools/` 文件夹，解包依赖已随包提供。工具会提取游戏资源和 `sce_sys` 元数据；“Open game files”直接进入 `uroot/`，复制其中内容即可。详细步骤及脚本高级用法见[解包说明](docs/GAME_DATA.md#iso-和-pkg-解包)。

**狱门山 XBLA**：先解压 ZIP／RAR，双击 `tools/Extract-XBLA.exe`。点击 **Select package...**，选择其中的内容包（通常是没有扩展名的长文件名），再选择输出位置。完成后点击 **Open game files**，将里面的全部内容复制到 `game-data/guwange/`，让 `default.xex` 直接位于该目录。使用本体 0.0.0.4，不叠加 Title Update。

### 游戏数据放在哪里

`game-data/` 下有以下目录，各附一份中英双语 `README.txt`。“选择数据目录…”可直接使用现有文件夹，不改动原文件；“复制到合集…”会复制到默认位置，目标须为空或仅含这份说明。

| 游戏 | 默认目录 | 在所选目录中应直接看到 |
| --- | --- | --- |
| 虫姬二 | `game-data/futari/` | `default.xex`、`media/` 等完整资源 |
| G2 | `game-data/espgaluda2/` | `default.xex`、`dat*.xex` 等完整资源 |
| 绊地狱 | `game-data/ketsui/` | `eboot.bin`、各 `.prx` 及完整资源 |
| 最大往生 | `game-data/sdoj/` | `default.xex`、`CA022100.bin`、`CA022110.bin` 等完整资源 |
| 大往生 | `game-data/daioujou/` | `eboot.bin`、各 `.prx` 及完整资源 |
| ESP Ra.De. | `game-data/esprade/` | `eboot.bin`、各 `.prx` 及完整资源 |
| 狱门山 | `game-data/guwange/` | `default.xex`、`module/`、`game_res/`、`res/` 等完整资源 |
| 弹铳 | `game-data/feveron/` | `eboot.bin`、各 `.prx` 及完整资源 |

只向需要的游戏目录放入完整文件，保留内部层级；其他目录留空即可。绊地狱可直接使用本体；安装 01.02 时，将更新目录内的内容覆盖到同一游戏根目录，详见[数据指南](docs/GAME_DATA.md)。

ISO／PKG 需先提取为游戏文件；ZIP 解压后选择实际游戏根目录。压缩包叫什么名字不影响识别，也不能靠改扩展名把 ISO／PKG 变成可用数据。

复制的是解包目录**里面的全部内容**，不是把外层文件夹再套进去。例如虫姬二应为 `game-data/futari/default.xex`，而不是 `game-data/futari/解压目录/default.xex`。表中的文件名只是帮助确认层级，不是只需复制这几个文件；保留其余资源、原文件名和内部目录结构。

虫姬二不安装 DLC 也可游玩 Ver.1.5 本体。`game-data/futari/dlc/` 可以留空或不存在，无须改配置；只有 Black Label／Ver.1.01 需要对应 DLC。默认数据目录名按表使用；选择外部数据目录时，外层文件夹可自行命名。

虫姬二 DLC 将原始文件放入 `game-data/futari/dlc/0000000000000000/435A07D7/00000002/`，保留文件名，启动游戏时自动解包。已有 v0.7.2 的解包目录保持原样即可。完整目录见[游戏数据指南](docs/GAME_DATA.md#虫姫さまふたり-ver15futari)，启动器“详情”可查看两项 DLC 的状态。

## 配置与操作

“启动时全屏”位于启动按钮上方，排行榜昵称在页头右侧修改；右上角 `CN / EN / JP` 切换启动器语言。保存昵称时，如有游戏字库无法显示的字符，会提示受影响游戏及实际传入的名字。游戏本身的语言按原作提供。

底部“配置文件”打开 `user-data/config.ini`，可编辑音量、键盘、手柄和联网选项。保存后在启动器点“刷新”，再启动游戏生效。完整选项见[默认配置模板](config/default.ini)。

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

狱门山的 **Arrange Xbox 360** 自动射击：方向键／左摇杆移动角色，`I / K / J / L`（上／下／左／右）或右摇杆移动式神，`W / RT` 召唤，`X / B` 炸弹。式神方向可在 `[game.guwange.keyboard]` 中通过 `right_stick_up/down/left/right` 调整；其他游戏的键位不受影响。

备份或换目录时保留整个 `user-data/`，其中包含配置、存档和录像。修改游戏内设置后，等待保存完成再关闭。

## 检查更新

启动器会在后台检查 GitHub 正式发布，每个新版本只自动提醒一次；游玩中不弹窗。提醒中可打开发布页查看说明和下载。

自动检查的成功结果缓存 24 小时。要关闭自动检查，在 `user-data/config.ini` 设置：

```ini
[collection]
check_for_updates = false
```

此选项与社区排行榜联网开关独立。提醒记录保存在 `user-data/update-check.json`，更新文件由玩家自行下载和替换。

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

## 最大往生 Frame Delay

在 `user-data/config.ini` 的 `[game.sdoj]` 段设置：

```ini
[game.sdoj]
data_dir = game-data/sdoj
frame_delay = 1
```

| 值 | 档位 |
| --- | --- |
| 1 | 低延迟，默认值 |
| 2 | 平衡 |
| 3 | 原始路径 |

这是游戏内部的帧调度档位；实际按键到画面的总延迟还受手柄和显示链路影响。已有明确设置会保留，修改后重启游戏生效。

## G2 实验性 Frame Delay

在 `user-data/config.ini` 设置：

```ini
[game.espgaluda2]
frame_delay = 1
```

`2` 为默认的原始路径，`1` 为实验性低延迟路径。修改后重启游戏生效；设回 `2` 即可恢复原始路径。

## 下载与备份

Windows x64。更新内容见[更新记录](docs/CHANGELOG.md)。

- [下载最新程序包与 SHA-256](https://github.com/zxl0714/EVAC-PC-LABEL/releases/latest)。不含游戏数据。
- 参与开发：[GitHub 仓库](https://github.com/zxl0714/EVAC-PC-LABEL)。程序对应的源码提交见包内 `SOURCE.txt`，依赖恢复和构建步骤见 [FOR_AGENT.md](FOR_AGENT.md)。

更新时先关闭游戏和启动器，再解压到原目录替换同名文件。程序包不包含 `user-data/`，已有配置、存档和录像保留。

- `user-data/`：配置、各游戏的存档、录像等。备份或换目录时，保留整个文件夹。
- `game-data/`：自行准备的游戏数据。换目录时可一并转移，也可重新选择原数据目录。
- `logs/`：运行日志。保留遇到问题的那次记录即可。

旧版根目录的 `config.ini` 会自动迁入 `user-data/config.ini`；新位置已有配置时优先使用它。相对数据路径以合集根目录为起点。

## 遇到问题

数据缺失或版本不支持时，检查[数据指南](docs/GAME_DATA.md#无法识别时怎么检查)中的目录层级和版本要求。

反馈崩溃、卡顿、音效或联网问题时，请提供：

- 合集版本（见 `release.json`）、游戏名称、模式与角色。
- 从哪里进入、做了什么、实际发生什么；如方便可附截图或短视频。
- 对应 `logs/<游戏 ID>/run-*/` 中的文本日志，例如 `game.log`、`stdout.jsonl`、`stderr.txt`、`startup-error.txt`，以及运行信息 `run.json`；按实际存在的文件提供。

问题发生时可按 F8 标记时间，退出游戏后复制需要的日志，保留原始记录。公开提交前将副本中的本机用户名、私人目录、昵称和自定义服务地址替换为占位文字。

不要把整个 `run-*` 文件夹直接传到公开 Issue：其中的 `config.ini` 是配置快照，`.dmp` 和 `*-module.bin` 可能包含内存及原游戏代码。需要这些材料时，维护者会另行联系；游戏数据、存档和录像也不作为默认公开附件。

## 参与开发

环境准备、编译、运行和打包见 [FOR_AGENT.md](FOR_AGENT.md)。提交改动前阅读 [AGENTS.md](AGENTS.md)；后续工作见[实施计划](docs/IMPLEMENTATION_PLAN.md)。

## 来源与许可

项目使用 ReXGlue／Xenia 相关组件、shadPS4 派生图形组件、SDL 等开源工作。维护源码的来源见[来源清单](docs/SOURCE_ORIGINS.json)，组件声明见 [ReXGlue 许可证](third_party/rexglue/LICENSE)与 [PS4 第三方说明](third_party/ps4/THIRD_PARTY_NOTICES.txt)，横幅素材来源见[素材清单](launcher/artwork.json)。各组件保留各自的许可证。

发行包的 `licenses/` 包含许可证，`SOURCE.txt` 提供对应源码与构建说明的获取方式。
