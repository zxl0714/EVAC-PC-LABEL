CAVE PC Collection

运行环境
Windows 64 位。虫姬二和 ESPGaluda II 使用 D3D12，绊地狱使用 Vulkan；
请安装显卡厂商提供的正式驱动。所需的 C++ 运行库已随程序附带。

使用方法
完整解压后，双击根目录 CAVE-PC-Collection.exe，选择游戏即可。
启动游戏.bat 是备用入口。不要在压缩包内直接运行，也不要只复制一个 EXE。

游戏数据
内部完整版已经附带虫姬二、ESPGaluda II 和绊地狱的数据与支持的附加内容。
对外程序包不带数据：可从启动器选择已有数据目录，或放到以下位置。
只安装一款游戏即可使用，缺少其他游戏不影响游玩。

  game-data/futari/       default.xex 等虫姬二本体文件
  game-data/futari/dlc/   Black Label 与 Ver1.01，保留原来的内容目录结构
  game-data/espgaluda2/   default.xex、各模式 XEX 和 res/DISKDATA 等完整数据
  game-data/ketsui/       CUSA09062-app 与 CUSA09062-patch（1.02）两个目录

绊地狱首次启动会在 prepared/ketsui 自动准备运行镜像；原始数据不会被改写。
怒首领蜂大往生和 ESP Ra.De. 暂为 Coming Soon。

配置与操作
启动器右上角“配置文件”打开根目录 config.ini，修改并保存后重启游戏生效。
各游戏共用显示、音量、键盘和手柄设置；游戏内设置仍由各游戏保存。
默认键盘：方向键移动；Z/X/C/V 对应手柄下/右/左/上方按钮；Enter 开始；
Backspace 对应 Select/Touchpad；A/S 为 L1/R1，Q/W 为 L2/R2。
具体射击、炸弹、投币和菜单确认仍以各游戏内的按钮设置为准。
F11 或 Alt+Enter 切换全屏，Alt+F4 关闭游戏；F8 在日志中加入时间标记。
手柄自动检测，常用 PS/Xbox/街机摇杆可通过 SDL 接入；可在游戏内调整动作。

联网
默认开启，无需注册。昵称默认为 Player，可在 [community] 修改 nickname；
enabled = false 可关闭联网。默认服务为 https://xiaolu.life/cave/api/v1。
联网会发送昵称、成绩和适用的录像，三款游戏的榜单分别管理。

存档与更新
存档、录像和待传记录在 user-data/<游戏>/，不要随意删除。
更新程序时保留自己的 config.ini 和 user-data；不要用旧缓存覆盖新缓存。

遇到问题
记下游戏、模式、角色、发生步骤和大致时间；可以在出现异常后按 F8 做标记。
从启动器“日志”找到 logs/<游戏>/ 最近一次 run-* 文件夹，关闭游戏后将整个
文件夹压缩发给提供本包的人，附上 release.json。不要只发最后一行错误。
绊地狱日志为 stdout.jsonl/stderr.txt；360 游戏保留各自的程序与崩溃报告。
需要复现保存/录像问题时，先保留原文件，再按说明提供相应槽位的副本。
日志可能包含昵称、机器信息和本地路径，发送前可先查看。

已知范围
绊地狱主动关窗后会返回启动器，但完整的客体退出清理仍未完成，日志会记录。
虫姬二与绊地狱的帧节拍专项优化安排在后续版本；有偶发卡顿请附日志反馈。

开发
内部完整版的 source 是独立 Git 仓库，从 source/FOR_AGENT.md 开始即可。
对外包不带源码。组件许可和对应源码信息见 licenses 与 SOURCE.txt。
