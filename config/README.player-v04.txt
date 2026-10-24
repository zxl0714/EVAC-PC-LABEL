EVAC PC LABEL v0.4

开始游玩
运行根目录 EVAC PC LABEL.exe，选中游戏，点击“启动游戏”。
启动游戏.bat 是备用入口。需要 Windows 64 位及显卡厂商提供的正式驱动。
虫姬二、ESPGaluda II、最大往生使用 D3D12，绊地狱和大往生使用 Vulkan。

配置与存档
共用配置位于 user-data/config.ini，由启动器自动创建。
配置、各游戏存档和录像都在 user-data，备份时保留整个文件夹。
旧版根目录的 config.ini 会自动迁入；新位置已有配置时优先使用它。
配置中的相对路径仍以合集根目录为起点，例如 game-data/futari/。

右上角点击 CN / EN / JP 切换语言，带下划线的文字表示当前语言。
页头可改全屏和排行榜昵称，点击“保存”；启动游戏前也会保存快捷设置。
底部“配置文件”打开 user-data/config.ini，可修改显示、音量、按键和联网。
外部编辑保存后点击“刷新”，下次启动游戏生效。游戏内设置通过各游戏菜单保存。

游戏数据
通过底部“游戏数据…”选择自己的数据目录，或使用默认目录：

  game-data/futari/       虫姬二本体，default.xex 位于根目录
  game-data/futari/dlc/   可选 Black Label 和 Ver1.01，保留内容目录结构
  game-data/espgaluda2/   default.xex、dat*.xex 位于根目录，保留 res/ 等资源
  game-data/ketsui/       CUSA09062／01.00 本体或覆盖 01.02 的完整数据，根含 eboot.bin
  game-data/sdoj/         光盘本体，含 default.xex、CA022100.bin、CA022110.bin
  game-data/daioujou/     CUSA32997／01.04 合并数据，eboot.bin 位于根目录

先完整解包：ISO／PKG 要提取为游戏文件；ZIP 解压后若仍是 ISO／PKG，
还需继续提取。压缩包名称不影响识别，改扩展名不能代替解包。
将解包目录里面的全部内容放进对应目录，不要多套一层，也不要只复制上面
列出的入口文件。保留其余资源、原文件名和内部层级。例如：
  正确：game-data/futari/default.xex，以及同层的 media/ 等完整资源
  多套一层：game-data/futari/解压目录/default.xex
也可通过“选择数据目录…”直接选中实际含 default.xex／eboot.bin 的那层。
放好后点击“刷新”；此前选过外部路径时，需重新选择新的数据位置。

只安装一款游戏也能使用；ESP Ra.De. 暂未接入。
虫姬二不装 DLC 也能玩 Ver.1.5 本体；dlc/ 留空或不存在均可，无须改配置。
Black Label 和 Ver1.01 才需要对应 DLC，内容包目录和配套头文件需完整保留。
game-data 下各目录的 README.txt 介绍具体放法。
绊地狱先将本体目录里的内容放入 game-data/ketsui/；可选的 01.02 更新也把
目录里的内容覆盖到同一位置。不要再套 CUSA09062-app／CUSA09062-patch 两层。
保留原始本体和更新文件；想退回 01.00 时重新提取本体到一个新目录。
“复制到合集…”接受空目录或仅含放置说明的目录，不覆盖已有游戏数据。
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

最大往生 Frame Delay
默认采用原始三帧路径，可在 user-data/config.ini 的 [game.sdoj] 段中
设置 frame_delay = 3。1V／2V 为实验性低延迟选项。
已有配置明确指定 1 或 2 时，需要手动改为 3 才会使用原始路径。
保存后重新启动最大往生。不支持 0。
这是游戏消费输入至画面提交的软件档位，显示器和设备延迟另计。

开发
根据 SOURCE.txt 中的 GitHub 地址取得对应版本源码，从仓库根目录 FOR_AGENT.md
开始准备依赖和编译。编译使用较短英文目录。
组件许可证见 licenses/。
