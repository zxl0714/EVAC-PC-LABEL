# CAVE PC Collection

CAVE 游戏的 Windows PC 移植合集。一个启动器、一份 config.ini，按需安装自己拥有的游戏数据，各作品分别保存存档、录像和日志。

## v0.3.1 覆盖更新

v0.3.1 以程序更新包交付：关闭游戏和启动器，将 ZIP 内容直接解压到原 v0.3
目录，替换同名文件即可。继续使用原游戏数据、DLC、存档和录像；更新包不携带
配置文件，首次启动会将旧根目录 config.ini 原样迁入 user-data/config.ini。

本次包含 CN / EN / JP 文字语言栏、底部工具入口、新的最大往生横幅与配置目录
迁移。最大往生可在 `[game.sdoj]` 中设置 `frame_delay = 1 / 2 / 3`（填写一个整数），
默认采用 1V 低延迟档，也可选择 2V 兼容档或 3V 原始路径；已有显式配置不变。
覆盖步骤见配套 README.txt。

开发出包命令为 `./scripts/Release.ps1 -Flavor Update -Version 0.3.1`。
压缩包根目录直接包含启动器和 runtime，不需要再移动外层文件夹。对应维护源码
按包内 SOURCE.txt 单独提供，旧 v0.3 发行文件保留原样。
默认 1V 修订版的源码标签为 `v0.3.1-1v`。

## v0.3

本版加入《怒首領蜂 大往生 臨廻転生》，目前支持五款游戏：

| 游戏 | 游戏 ID |
| --- | --- |
| 虫姫さまふたり Ver.1.5 | futari |
| エスプガルーダII ブラックレーベル | espgaluda2 |
| ケツイ Deathtiny ～絆地獄たち～ | ketsui |
| 怒首領蜂最大往生 | sdoj |
| 怒首領蜂 大往生 臨廻転生 | daioujou |

エスプレイドΨ（ESP Ra.De.）继续列在后续支持名单中。

本次主要更新：

- 大往生接入统一启动器、配置、存档和日志，支持主要游玩模式与 CUSTOM 设置保存。
- 大往生成绩、排行榜和录像接入社区服务，使用合集昵称，无需注册。
- 优化绊地狱、大往生的任务交接、线程调度和画面输出，改善游玩与回放中的卡顿。
- 启动器支持中文、日文和英文；游戏统一采用日文主标题、英文副标题。

发行源码固定为标签 **v0.3**、提交 **b8807ca**。Internal、Public 和对应源码附件均来自这次配套构建。详细开发与验收记录见 [验收矩阵](docs/TEST_MATRIX.md)；v0.1、v0.2 的标签和压缩包保留原样。

## 开始游玩

从 v0.3.1 起，配置集中到 `user-data/config.ini`，右上角直接点击
`CN / EN / JP` 切换语言。配置、日志、数据和刷新入口统一位于底部。

完整解压后，运行根目录 CAVE-PC-Collection.exe，选择横幅，再点击底部的“启动游戏”。启动游戏.bat 是备用入口。

启动器页头可以修改全屏和排行榜昵称，右上角可切换语言。“配置文件”打开唯一的 config.ini，其他显示、音量、键盘和手柄选项在这里编辑；外部编辑保存后点“刷新”。

F11／Alt+Enter 切换全屏，Alt+F4 关窗，F8 给当前日志加入时间标记。手柄由 SDL 识别，默认键盘与具体配置项见程序包的 README.txt 和 [配置模板](config/default.ini)。

需要 Windows x64 和显卡厂商的正式驱动。虫姬二、G2、最大往生使用 D3D12；绊地狱和大往生使用 Vulkan。运行库随程序包附带。

## 游戏数据

只安装一款游戏也可以使用，缺少其他作品的数据不影响启动器。默认目录如下，也可通过启动器的“游戏数据…”选择现有目录：

| 游戏 ID | 默认位置与布局 |
| --- | --- |
| futari | game-data/futari/，default.xex 位于根目录 |
| espgaluda2 | game-data/espgaluda2/，保留完整解包结构 |
| ketsui | game-data/ketsui/，含 CUSA09062-app、CUSA09062-patch（1.02） |
| sdoj | game-data/sdoj/，光盘本体的 default.xex、CA022100.bin、CA022110.bin 等资源 |
| daioujou | game-data/daioujou/，CUSA32997／01.04 完整合并数据，eboot.bin 位于根目录 |

虫姬二的 Black Label／Ver1.01 放在 game-data/futari/dlc/，保留内容目录结构，也可在 [game.futari] 中指定 dlc_dir。大往生需要完整合并数据；最大往生使用受支持的原版光盘数据，音乐补丁暂未接入。

两款 PS4 作品首次启动会在 prepared/ 下自动准备运行镜像。原始游戏数据只读，准备文件和各作品的写入目录分别管理。

## 联网、存档与反馈

联网默认开启，昵称默认为 Player，可在启动器修改。在 config.ini 的 [community] 中关闭 enabled 即可离线游玩；默认 endpoint 为 https://xiaolu.life/cave/api/v1。

五款游戏各自使用独立榜单，通过原生界面提交成绩、看榜及上传／下载适用录像。最大往生保留游玩赚金币、商店购买的原生解锁方式。团队评分及各模式的排名资格按 [玩家说明](config/README.player-v03.txt) 列出的支持范围使用。

存档、录像等保存在 user-data/<游戏>/。从 v0.3.1 起，更新时只需保留 user-data，配置也在其中；首次启动会将旧版根目录 config.ini 原样迁入，已有新位置配置时优先使用新配置。配置里的相对数据路径仍相对合集根目录。修改游戏内设置或在最大往生商店购买后，退出相应页面并等自动保存完成再关窗。

首次从 v0.3 换目录升级时，先把旧根目录的 config.ini 放进旧 user-data，再整体转移到新版本；在原目录更新则由首次启动自动迁移。

反馈问题时请附游戏、模式、角色和操作步骤，并将 logs/<游戏>/ 最新的 run-* 文件夹压缩发送。F8 可标记问题发生时间，release.json 可帮助对应程序版本。

## 开发

工程共享启动器、配置和公共组件；Xbox 360 与 PS4 后端分别维护，各游戏保留自己的适配与数据清单。仓库不保存 ROM、生成的游戏程序正文、安装工具、玩家数据或凭据。

从 [FOR_AGENT.md](FOR_AGENT.md) 开始，目录职责与步骤见 [实施计划](docs/IMPLEMENTATION_PLAN.md)，源码来源见 [迁移基线](docs/BASELINE.md)。

开发环境为 Windows x64、PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK。工具脚本恢复固定 LLVM 23.1.3 等依赖。源码建议使用较短的英文目录；玩家包和游戏数据可放中文路径。

~~~powershell
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
./out/common/bin/cave_inspect.exe --root .
~~~

以上构建公共组件和启动器，不需要 ROM。Xbox 360 作品先执行 scripts/Build-Sdk.ps1，再按 FOR_AGENT.md 提供数据、生成并编译指定游戏。检测已有数据可使用：

~~~powershell
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
~~~

## 打包与协作

提供三种文件：

- Internal：启动器、五款完整受支持数据及虫姬二 DLC、程序、符号和独立 source/.git。
- Public：启动器、运行程序、默认配置、玩家说明和许可证；用户自行提供游戏数据。
- Source：与程序包配套的独立源码 Git、固定 SDK／PS4 依赖和构建说明，不含 ROM，可按需提供。

~~~powershell
./scripts/Release.ps1 -Flavor Internal -Version 0.3
./scripts/Release.ps1 -Flavor Public -Version 0.3
./scripts/Release-Source.ps1 -BinaryDirectory '<已生成的程序包目录>' -OutputDirectory '<releases 下的新目录>'
~~~

程序包的 release.json 记录版本、源码提交和文件哈希；源码附件另外绑定对应程序包。构建与打包只生成本地产物，上传和消息通知是单独步骤。发行流程采用一次公共认证、每个最终 ZIP 一次实际解压验收；冻结后的相同文件复用验收记录，具体规则见 FOR_AGENT.md。
