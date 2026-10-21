# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

将 CAVE 的经典弹幕射击游戏带到 Windows PC。

EVAC PC LABEL 是 CAVE 游戏的非官方 PC 移植合集，提供统一启动器、共享配置、存档、录像和社区排行榜。游戏数据按需安装。

[开始游玩](#开始游玩) · [游戏数据指南](docs/GAME_DATA.md) · [更新记录](docs/CHANGELOG.md) · [开发与构建](FOR_AGENT.md)

本分支面向 v0.4；下文说明对应当前开发代码。已发布的 v0.3.2 下载见“更新与备份”。

## 支持的游戏

| 游戏 | 常用简称 | 支持的数据版本 |
| --- | --- | --- |
| 虫姫さまふたり Ver.1.5 | 虫姬二／Futari | Xbox 360；可选 Black Label、Ver.1.01 DLC |
| エスプガルーダII ブラックレーベル | G2／Espgaluda II | Xbox 360 |
| ケツイ Deathtiny ～絆地獄たち～ | 绊地狱／Ketsui | PS4，CUSA09062，01.00 本体或覆盖 01.02 更新后的完整单目录 |
| 怒首領蜂最大往生 | 最大往生／SDOJ | Xbox 360，原版光盘数据 |
| 怒首領蜂 大往生 臨廻転生 | 大往生／DaiOuJou | PS4，CUSA32997，01.04 完整合并数据 |

エスプレイドΨ（ESP Ra.De.）计划后续加入，启动器中标为 Coming Soon。

## 开始游玩

### 运行环境

使用 Windows x64，并安装显卡厂商的正式驱动。虫姬二、G2、最大往生需要 Direct3D 12；绊地狱、大往生需要 Vulkan。程序运行库随玩家包附带。

### 首次使用

1. 将完整程序包解压到一个可写文件夹，游戏数据需自行准备。
2. 运行根目录的 `EVAC PC LABEL.exe`，备用入口为 `启动游戏.bat`。
3. 选中想玩的游戏，点击底部“游戏数据…”→“选择数据目录…”，选择已经解包的游戏文件夹。也可以按下表直接放入合集。
4. 点击“启动游戏”。首次启动会校验数据；PS4 作品还会自动准备运行文件，请等待完成。

### 游戏数据放在哪里

完整程序包已创建下列目录，各附一份 `放置说明.txt`。“选择数据目录…”可直接使用现有文件夹，不改动原文件；“复制到合集…”会复制到默认位置，目标须为空或仅含放置说明。

| 游戏 | 默认目录 | 在所选目录中应直接看到 |
| --- | --- | --- |
| 虫姬二 | `game-data/futari/` | `default.xex`、`media/` 等完整资源 |
| G2 | `game-data/espgaluda2/` | `default.xex`、`dat*.xex` 等完整资源 |
| 绊地狱 | `game-data/ketsui/` | `eboot.bin`、各 `.prx` 及完整资源 |
| 最大往生 | `game-data/sdoj/` | `default.xex`、`CA022100.bin`、`CA022110.bin` 等完整资源 |
| 大往生 | `game-data/daioujou/` | `eboot.bin`、各 `.prx` 及完整资源 |

只向需要的游戏目录放入完整文件，保留内部层级；其他目录留空即可。绊地狱可直接使用本体；安装 01.02 时，将更新目录内的内容覆盖到同一游戏根目录，详见[数据指南](docs/GAME_DATA.md)。

ISO／PKG 需先提取为游戏文件；ZIP 解压后选择实际游戏根目录。

完整目录示例、DLC 放法和识别问题见[游戏数据指南](docs/GAME_DATA.md)。

## 配置与操作

启动器页头可以修改全屏和排行榜昵称；右上角 `CN / EN / JP` 切换启动器语言。游戏本身的语言按原作提供。

底部“配置文件”打开 `user-data/config.ini`，可编辑音量、键盘、手柄和联网选项。保存后在启动器点“刷新”，再启动游戏生效。完整选项见[默认配置模板](config/default.ini)。

最大往生默认 `frame_delay = 3`，使用原始三帧路径；已有 1V／2V 设置会保留，可在 `[game.sdoj]` 中手动改为 3。1V／2V 为手动测试档位。

| 默认键盘操作 | 功能 |
| --- | --- |
| 方向键 | 方向输入 |
| Z / X / C / V | 手柄下／右／左／上方按钮 |
| Enter / Backspace | Start / Select（PS4 为 Touchpad） |
| A / S、Q / W | L1 / R1、L2 / R2 |
| F11 或 Alt+Enter | 切换全屏 |
| Alt+F4 | 关闭游戏 |
| F8 | 在日志中标记当前时间，方便反馈问题 |

射击、炸弹、换人等具体动作由各游戏内的按键设置决定。手柄和摇杆通过 SDL 接入，可使用其支持的 XInput、PlayStation 等设备。

## 社区排行榜

联网默认开启，无须注册。在启动器设置昵称后，通过游戏内的排行榜和录像界面使用社区服务。各作品使用独立榜单，上榜资格沿用游戏规则。

~~~ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
~~~

将 `enabled` 改为 `false` 可离线游玩。

## 更新与备份

已发布版本：**v0.3.2**。更新内容见[更新记录](docs/CHANGELOG.md)。

[下载 v0.3.2 覆盖更新包](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip) · [SHA-256 校验文件](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip.sha256)

覆盖更新包适用于已有 v0.3／v0.3.1／较早 v0.3.2 安装。关闭游戏和启动器，将 ZIP 解压到原合集目录并替换同名文件即可。首次安装需使用完整程序包。

v0.3.2 的启动文件仍为 `CAVE-PC-Collection.exe`，绊地狱仍使用其原有双目录。迁移到 v0.4 时按本页的新布局放置数据；v0.4 覆盖包会一并更新旧文件名入口，已有快捷方式仍打开新版启动器。v0.4 尚未发布。

- `user-data/`：配置、各游戏的存档、录像等。备份或换目录时，保留整个文件夹。
- `game-data/`：自行准备的游戏数据。覆盖更新时保留；换目录安装时可一并转移，也可重新选择原数据目录。
- `logs/`：运行日志。保留遇到问题的那次记录即可。

旧版根目录的 `config.ini` 会自动迁入 `user-data/config.ini`；新位置已有配置时优先使用它。相对数据路径以合集根目录为起点。

## 遇到问题

数据缺失或版本不支持时，检查[数据指南](docs/GAME_DATA.md#无法识别时怎么检查)中的目录层级和版本要求。

反馈崩溃、卡顿、音效或联网问题时，请提供：

- 合集版本（见 `release.json`）、游戏名称、模式与角色。
- 从哪里进入、做了什么、实际发生什么；如方便可附截图或短视频。
- `logs/<游戏 ID>/` 下对应的完整 `run-*` 文件夹，压缩后随反馈附上。

问题发生时可按 F8 标记时间，退出游戏后打包日志。公开分享前检查其中的个人信息。

## 参与开发

环境准备、编译、运行和打包见 [FOR_AGENT.md](FOR_AGENT.md)。提交改动前阅读 [AGENTS.md](AGENTS.md)；后续工作见[实施计划](docs/IMPLEMENTATION_PLAN.md)。

## 来源与许可

项目使用 ReXGlue／Xenia 相关组件、shadPS4 派生图形组件、SDL 等开源工作。维护源码的来源见[来源清单](docs/SOURCE_ORIGINS.json)，组件声明见 [ReXGlue 许可证](third_party/rexglue/LICENSE)与 [PS4 第三方说明](third_party/ps4/THIRD_PARTY_NOTICES.txt)，横幅素材来源见[素材清单](launcher/artwork.json)。各组件保留各自的许可证。

发行包的 `licenses/` 包含许可证，`SOURCE.txt` 提供对应源码与构建说明的获取方式。
