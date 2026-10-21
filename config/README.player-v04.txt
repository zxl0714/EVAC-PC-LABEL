CAVE PC Collection v0.4 开发版

开始游玩
运行根目录 CAVE-PC-Collection.exe，选中游戏，点击“启动游戏”。
启动游戏.bat 是备用入口。需要 Windows 64 位及显卡厂商提供的正式驱动。
虫姬二、ESPGaluda II、最大往生使用 D3D12，绊地狱和大往生使用 Vulkan。

配置与更新
共用配置位于 user-data/config.ini。更新时保留 user-data 文件夹，即可保留
配置、各游戏存档和录像。首次启动会将旧版根目录的 config.ini 原样迁入；
已有 user-data/config.ini 时优先使用它，不会用旧文件覆盖。
首次从 v0.3 换目录升级时，先把旧根目录 config.ini 放入旧 user-data，再将
整个 user-data 转移到新版本；在原目录更新则由首次启动自动完成迁移。
配置中的相对路径仍以合集根目录为起点，例如 game-data/futari/。

右上角点击 CN / EN / JP 切换语言，带下划线的文字表示当前语言。
页头可改全屏和排行榜昵称，点击“保存”；启动游戏前也会保存快捷设置。
底部“配置文件”打开 user-data/config.ini，可修改显示、音量、按键和联网。
外部编辑保存后点击“刷新”，下次启动游戏生效。游戏内设置通过各游戏菜单保存。

游戏数据
通过底部“游戏数据…”选择自己的数据目录，或使用默认目录：

  game-data/futari/       虫姬二本体，default.xex 位于根目录
  game-data/futari/dlc/   Black Label 和 Ver1.01，保留内容目录结构
  game-data/espgaluda2/   完整解包数据，含模式 XEX 和 res/DISKDATA
  game-data/ketsui/       CUSA09062-app、CUSA09062-patch（1.02）两个目录
  game-data/sdoj/         光盘本体，含 default.xex、CA022100.bin、CA022110.bin
  game-data/daioujou/     CUSA32997／01.04 合并数据，eboot.bin 位于根目录

只安装一款游戏也能使用；ESP Ra.De. 暂未接入。虫姬二支持上述两份 DLC。
大往生需要完整合并更新数据；最大往生使用原版光盘数据，音乐补丁暂未接入。
两款 PS4 作品首次启动会自动准备运行镜像，游戏数据仍保留在原位置。

默认按键
方向键移动；Z／X／C／V 对应手柄下／右／左／上方按钮；Enter 开始；
Backspace 为 Select／Touchpad；A／S 为 L1／R1；Q／W 为 L2／R2。
投币、确认、射击和炸弹的具体动作，以各游戏内的按键设置为准。
F11／Alt+Enter 切换全屏，Alt+F4 关窗，F8 在日志中加入时间标记。
手柄由 SDL 自动识别，可在配置中调整键盘绑定和摇杆死区。

联网、存档与反馈
联网默认开启，昵称默认 Player，可在启动器修改。共享 [community] 配置中，
enabled = false 可关闭联网；endpoint 默认 https://xiaolu.life/cave/api/v1。
五款游戏使用各自的榜单，通过游戏界面提交成绩、看榜和上传／下载适用录像。
最大往生团队评分和大往生 CUSTOM 成绩提交暂未接入。
存档与录像保存在 user-data/<游戏>/；修改游戏设置或商店购买后，等待自动保存
完成再关窗。关闭联网仍可使用本地存档和已有本地录像。

问题发生时按 F8，记下游戏、模式、角色和操作步骤。底部“日志”打开日志目录，
关闭游戏后将 logs/<游戏>/ 最新的完整 run-* 文件夹压缩发给提供程序的人。
PS4 退出清理的既有限制仍会记入日志，受控关窗可返回启动器。

最大往生 Frame Delay
在 user-data/config.ini 已有的 [game.sdoj] 段内添加 frame_delay = 2，
保存并重新启动最大往生即可选择两帧档。也可填 1 选择一帧低延迟档，
或填 3 恢复原始三帧路径。游戏速度和原作减速保持不变，不支持 0。
这是游戏消费输入至画面提交的软件档位，显示器和设备延迟另计。
当前开发版缺少此键时仍采用 3；最终默认值待普通输入验收后调整。

开发
从 source/FOR_AGENT.md 或仓库根目录 FOR_AGENT.md 开始。编译使用较短英文目录。
Frame Delay 的实现、测试与待完成项见 docs/SDOJ_LATENCY_PLAN.md。
源码、构建和验证记录在仓库中维护；许可证与素材来源随项目保存。
