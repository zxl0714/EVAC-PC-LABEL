# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

将 CAVE 的经典弹幕射击游戏带到 Windows PC。

EVAC PC LABEL 是一个非官方的 PC 移植合集，原名 CAVE PC Collection。通过同一个启动器选择游戏，用一份配置管理显示、声音、键盘、手柄和社区昵称。只需准备想玩的作品，无须安装整个合集的游戏数据。

目前已接入五款作品，并提供存档、录像和社区排行榜。各游戏使用专门适配的 PC 运行时，无须另外安装 Xbox 360 或 PS4 模拟器。

[开始游玩](#开始游玩) · [游戏数据指南](docs/GAME_DATA.md) · [更新记录](docs/CHANGELOG.md) · [开发与构建](FOR_AGENT.md)

## 支持的游戏

| 游戏 | 常用简称 | 支持的数据版本 |
| --- | --- | --- |
| 虫姫さまふたり Ver.1.5 | 虫姬二／Futari | Xbox 360；可选 Black Label、Ver.1.01 DLC |
| エスプガルーダII ブラックレーベル | G2／Espgaluda II | Xbox 360 |
| ケツイ Deathtiny ～絆地獄たち～ | 绊地狱／Ketsui | PS4，CUSA09062，本体与 1.02 更新的双目录数据 |
| 怒首領蜂最大往生 | 最大往生／SDOJ | Xbox 360，原版光盘数据 |
| 怒首領蜂 大往生 臨廻転生 | 大往生／DaiOuJou | PS4，CUSA32997，01.04 完整合并数据 |

エスプレイドΨ（ESP Ra.De.）计划后续加入，启动器中标为 Coming Soon。

同名游戏在不同平台、地区和更新版本上的数据可能不同，请按[游戏数据指南](docs/GAME_DATA.md)准备。大往生与最大往生是两款不同的作品。

## 开始游玩

### 运行环境

使用 Windows x64，并安装显卡厂商的正式驱动。虫姬二、G2、最大往生需要 Direct3D 12；绊地狱、大往生需要 Vulkan。程序运行库随玩家包附带。

### 首次使用

1. 将完整的 **Public 程序包**解压到一个可写文件夹，保留包内全部文件。游戏数据需自行准备；GitHub 的源码 ZIP 用于开发，不是玩家程序包。
2. 运行根目录的 `CAVE-PC-Collection.exe`，备用入口为 `启动游戏.bat`。
3. 选中想玩的游戏，点击底部“游戏数据…”→“选择数据目录…”，选择已经解包的游戏文件夹。也可以按下表直接放入合集。
4. 点击“启动游戏”。首次启动会校验数据；PS4 作品还会自动准备运行文件，请等待完成。

当前发布的程序仍使用 `CAVE-PC-Collection.exe` 这一名称。EVAC PC LABEL 的程序与发行包更名安排在 v0.4，旧版本可继续使用。

### 游戏数据放在哪里

最省空间的方式是通过“选择数据目录…”使用现有文件夹，原文件不会被移动或修改。希望集中存放时，可选择“复制到合集…”复制到默认位置；该位置须为空。

| 游戏 | 默认目录 | 在所选目录中应直接看到 |
| --- | --- | --- |
| 虫姬二 | `game-data/futari/` | `default.xex`、`media/` 等完整资源 |
| G2 | `game-data/espgaluda2/` | `default.xex`、`dat*.xex` 等完整资源 |
| 绊地狱 | `game-data/ketsui/` | `CUSA09062-app/` 与 `CUSA09062-patch/` |
| 最大往生 | `game-data/sdoj/` | `default.xex`、`CA022100.bin`、`CA022110.bin` 等完整资源 |
| 大往生 | `game-data/daioujou/` | `eboot.bin`、各 `.prx` 及完整资源 |

默认目录可自行创建，只放需要的游戏即可。这里的文件名用于判断应选哪一层，其他资源也须完整保留；启动器会检查文件内容，而非仅凭文件夹名识别。

ISO／PKG 需先提取为游戏文件；启动器接收的是解包目录。普通 ZIP 若包含已经提取的游戏文件，解压后选择实际数据根目录，避免多套一层文件夹。

绊地狱在 **v0.3.2 仍需要上述双目录与 1.02 更新**，暂不能只放本体或把补丁覆盖合并。后续单目录方案见[实施计划](docs/IMPLEMENTATION_PLAN.md)。各作品的完整目录示例、DLC 放法和识别失败处理见[游戏数据指南](docs/GAME_DATA.md)。

## 配置与操作

启动器页头可以修改全屏和排行榜昵称；右上角 `CN / EN / JP` 切换启动器语言。游戏本身的语言按原作提供。

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

射击、炸弹、换人等具体动作由各游戏内的按键设置决定。手柄和摇杆通过 SDL 接入，可使用其支持的 XInput、PlayStation 等设备。

## 社区排行榜

联网默认开启，无须注册。先在启动器将 `Player` 改成便于辨认的昵称，再通过游戏内的排行榜和录像界面使用；昵称会显示在社区榜单中。

这是合集的社区服务，不需要登录 Xbox Live 或 PlayStation Network。各作品使用独立榜单，上榜资格和适用录像流程沿用游戏规则。

~~~ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
~~~

将 `enabled` 改为 `false` 可离线游玩。最大往生本体中的金币与商店解锁按原作游玩进度进行。

## 更新与备份

当前维护版本为 **v0.3.2**，修复大往生 AC-CHALLENGE 排行榜，并补齐绊地狱 CUSTOM 综合／Type-A／Type-B 榜与对应录像功能。

[下载 v0.3.2 覆盖更新包](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip) · [SHA-256 校验文件](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip.sha256)

这个链接是供**已有 v0.3／v0.3.1／较早 v0.3.2 安装**使用的更新包，不是首次安装的完整程序包。关闭游戏和启动器，将 ZIP 内容直接解压到原合集目录，替换同名文件即可。更新包含此前 v0.3.1 的改动，详情见[更新记录](docs/CHANGELOG.md)。

- `user-data/`：配置、各游戏的存档、录像等。备份或换目录时，保留整个文件夹。
- `game-data/`：自行准备的游戏数据。覆盖更新时保留；换目录安装时可一并转移，也可重新选择原数据目录。
- `logs/`：运行日志。保留遇到问题的那次记录即可。

旧版根目录的 `config.ini` 会在首次启动时自动迁入 `user-data/config.ini`；新位置已有配置时直接使用它。配置中的相对数据路径始终以合集根目录为起点。

## 遇到问题

如果提示数据缺失或版本不支持，先检查[数据指南](docs/GAME_DATA.md#无法识别时怎么检查)中的目录层级和版本要求。不要通过改文件名或删校验清单来解决版本不匹配。

反馈崩溃、卡顿、音效或联网问题时，请提供：

- 合集版本、游戏名称、模式与角色。
- 从哪里进入、做了什么、实际发生什么；如方便可附截图或短视频。
- `logs/<游戏 ID>/` 下对应的完整 `run-*` 文件夹，压缩后随反馈附上。

问题发生时可按 F8 标记时间，再关闭游戏收集日志。`release.json` 记录程序版本，定位构建有疑问时可一并附上。公开分享日志前检查其中的本机路径等个人信息；原始游戏数据不需要随反馈上传。

## 参与开发

工程共享启动器、配置和公共组件，分别维护 Xbox 360 与 PS4 后端以及各游戏适配。源码仓库不包含游戏数据、生成的游戏程序正文或玩家存档。

从 [FOR_AGENT.md](FOR_AGENT.md) 开始，可查阅环境准备、编译、运行和打包命令。提交改动前阅读 [AGENTS.md](AGENTS.md)；工作安排见[实施计划](docs/IMPLEMENTATION_PLAN.md)，已验证范围见[验收矩阵](docs/TEST_MATRIX.md)。问题反馈与代码贡献都请注明具体游戏和程序版本。

## 来源与许可

项目使用 ReXGlue／Xenia 相关组件、shadPS4 派生图形组件、SDL 等开源工作。维护源码的来源见[来源清单](docs/SOURCE_ORIGINS.json)，组件声明见 [ReXGlue 许可证](third_party/rexglue/LICENSE)与 [PS4 第三方说明](third_party/ps4/THIRD_PARTY_NOTICES.txt)，横幅素材来源见[素材清单](launcher/artwork.json)。各组件保留各自的许可证。

发行包附带 `licenses/` 和 `SOURCE.txt`，后者说明如何取得该程序版本对应的维护源码与构建说明。公开程序包与源码附件均不附带游戏数据。
