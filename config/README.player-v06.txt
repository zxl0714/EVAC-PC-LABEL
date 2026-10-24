EVAC PC LABEL v0.6.1

本次更新 / What's new
修复 ESP Ra.De.「いろりの部屋」房屋模式崩溃。
修复 G2 三面中 BOSS 相关的遗漏回调，补齐各模式对应入口。
整理启动器页头布局，移除旧名称的重复 EXE／PDB。
Fixed the ESP Ra.De. room-mode crash and missing G2 stage-3 callbacks across
modes; tightened the launcher header and removed
duplicate executables and symbols under the old launcher name.

开始游玩 ---------------------------------------------------------------------
运行 EVAC PC LABEL.exe，选中游戏，点击“启动游戏”。备用入口为 Launch.bat。
需要 Windows 64 位及显卡厂商的正式驱动。虫姬二、ESPGaluda II、最大往生
使用 Direct3D 12；绊地狱、大往生、ESP Ra.De. 使用 Vulkan。

游戏数据 ---------------------------------------------------------------------
在启动器底部点击“游戏数据…”选择已解包目录，或放入以下默认位置：
  game-data/futari/      Xbox 360 虫姬二本体；可选 DLC 放在其 dlc/ 下
  game-data/espgaluda2/  Xbox 360 ESPGaluda II
  game-data/ketsui/      CUSA09062，01.00 本体或覆盖 01.02 更新后的完整目录
  game-data/sdoj/        Xbox 360 最大往生原版光盘数据
  game-data/daioujou/    CUSA32997，覆盖 01.04 更新后的完整目录
  game-data/esprade/     CUSA15155，01.00 本体覆盖 01.04 更新后的完整目录

只需安装想玩的游戏。ISO／PKG 先提取为游戏文件；ZIP 解压后若仍为 ISO／PKG，
还需继续提取。default.xex（360）或 eboot.bin（PS4）应直接位于所选目录，
保留全部资源和内部层级，不要只复制入口文件，也不要再套一层文件夹。
PS4 更新的内容覆盖到本体目录中；各目录的 README.txt 提供具体要求。
首次启动会校验数据，PS4 作品还会自动准备运行文件，请等待完成。
虫姬二不装 DLC 也能玩 Ver.1.5；Black Label／Ver.1.01 需要对应完整 DLC。

ISO／PKG 解包

Xbox 360 ISO 使用 extract-xiso，下载 Windows Release 后完整解压：
https://github.com/XboxDev/extract-xiso/releases
在工具目录打开 PowerShell，替换输入文件和新输出目录：
  .\extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"
完成后在输出目录直接看到 default.xex；原 ISO 不变。

PS4 PKG 使用 LibOrbisPkg v0.2 的 PkgTool-0.2.231.zip，完整解压并保留 DLL：
https://github.com/maxton/LibOrbisPkg/releases/tag/v0.2
在合集目录打开 PowerShell，使用包内工具，替换以下示例路径：
  .\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\base.pkg" -OutputDir "D:\GameExtract\base"
  .\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\update.pkg" -OutputDir "D:\GameExtract\update"
两个输出目录须尚不存在。脚本提取游戏资源及 sce_sys 元数据，实际游戏文件
在各自的 uroot/ 下。先把 base/uroot/ 里面全部内容复制到对应 game-data 目录，
再用 update/uroot/ 内部全部内容覆盖同名文件；没有更新时只做本体这一步。
不要把 uroot 本身再套一层，也不要用更新目录替代整个本体目录。
默认使用常见备份包的 32 个零 passcode；其他 passcode 用 -Passcode 指定。
ZIP／RAR 的解压密码不是 PKG passcode；零售加密 PKG 需先准备可提取的备份。

配置、存档与操作 -------------------------------------------------------------
user-data/config.ini 由启动器自动创建。备份或换目录时保留整个 user-data/，
其中包含配置、存档与录像。旧版根目录配置会自动迁入；已有新配置优先使用。
右上角 CN / EN / JP 切换启动器语言；昵称在页头修改，全屏选项在启动按钮上方。
底部“配置文件”可修改音量、键盘、手柄和联网；外部编辑后点“刷新”，重启游戏生效。
昵称包含原作字库无法显示的字符时，保存前会提示受影响游戏和实际传入的名字。

方向键移动；Enter 为 Start；Backspace 为 Select／Touchpad；
A／S 为 L1／R1，Q／W 为 L2／R2。

  方位          Xbox 360   PS4   键盘
  下／South     A          ×     Z
  右／East      B          ○     X
  左／West      X          □     C
  上／North     Y          △     V

投币、确认、射击、炸弹等动作以游戏内设置为准。SDL 自动识别兼容手柄与摇杆。
F11／Alt+Enter 切换全屏，Alt+F4 关闭游戏，F8 标记日志时间。
修改游戏设置后，等待游戏保存完成再关闭。

联网 -------------------------------------------------------------------------
联网默认开启，无须注册；昵称默认 Player，在启动器中修改。
[community] 中 endpoint 默认 https://xiaolu.life/cave/api/v1，enabled = false
可关闭联网。通过各游戏的原生菜单使用社区排行榜及支持的录像功能。
最大往生团队评分和大往生 CUSTOM 成绩提交暂未接入。
同一游戏内，相同昵称会归为同一社区身份，建议固定使用自己的昵称。
服务检查提交格式及录像完整性，但不验证昵称归属。

  [community]
  enabled = true
  endpoint = https://xiaolu.life/cave/api/v1
  nickname = Player

最大往生 Frame Delay ---------------------------------------------------------
在 user-data/config.ini 的 [game.sdoj] 段配置：
  [game.sdoj]
  data_dir = game-data/sdoj
  frame_delay = 1

1 为低延迟档（默认），2 为平衡档，3 为原始路径。
这是游戏内部的帧调度档位；按键到画面的总延迟还受手柄和显示链路影响。
已有明确设置会保留，修改后重启游戏生效。

问题反馈与源码 ---------------------------------------------------------------
遇到异常可按 F8，退出后把 logs/<游戏>/ 对应完整 run-* 文件夹压缩，
附上版本、游戏、模式、角色和复现步骤。底部“日志”可打开日志目录。
源码版本与构建入口见 SOURCE.txt，许可证见 licenses/。

English ======================================================================

Getting started

Run EVAC PC LABEL.exe, select a game and click Launch game. Launch.bat is the
alternate entry. Windows x64 and current graphics-vendor drivers are required.
Xbox 360 titles use Direct3D 12; PS4 titles use Vulkan.

Use Game data to select an extracted game folder, or use the game-data folders
listed above. Install only the games you want. Extract ISO/PKG containers first.
Place default.xex (360) or eboot.bin (PS4) directly in the selected folder,
together with all resources and their original directory structure.
Merge PS4 updates over the base files in one directory. Ketsui supports 01.00
or merged 01.02; DaiOuJou requires merged 01.04; ESP Ra.De. requires the
CUSA15155 01.00 base merged with 01.04. Each folder includes a placement guide.
Futari Ver.1.5 works without DLC; Black Label and Ver.1.01 require their DLC.
First launch verifies the data and prepares PS4 runtime files automatically.

Extracting ISO / PKG files

Xbox 360 ISO: download and unpack a Windows Release build of extract-xiso:
https://github.com/XboxDev/extract-xiso/releases
Open PowerShell in the tool folder and replace these example paths:
  .\extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"
The output should contain default.xex directly. The original ISO is unchanged.

PS4 PKG: download PkgTool-0.2.231.zip from LibOrbisPkg v0.2 and keep its DLLs:
https://github.com/maxton/LibOrbisPkg/releases/tag/v0.2
Open PowerShell in the collection folder and use the included helper:
  .\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\base.pkg" -OutputDir "D:\GameExtract\base"
  .\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\update.pkg" -OutputDir "D:\GameExtract\update"
Output folders must not already exist. The helper extracts game files and
sce_sys metadata into each output's uroot/. Copy all contents INSIDE base/uroot/
to the game's data folder, then overlay all contents of update/uroot/, replacing
matching files. Keep base-only resources; skip the update step if not needed.
The script defaults to the common backup passcode of 32 zeros; use -Passcode
for a different one. ZIP/RAR passwords are separate. Encrypted retail PKGs
require an extractable backup first.

Config, saves and controls

Keep the entire user-data folder when backing up or moving the collection.
It contains config.ini, saves and replays. The launcher creates missing
configuration and migrates older root-level config.ini when needed.
CN / EN / JP changes launcher language. Edit your nickname in the header;
Start fullscreen is above Launch game. Config opens the shared INI. After
external edits, click Refresh and restart the game. Unsupported nickname
characters trigger a warning showing affected games and native display names.

Default keys: arrows for direction;
Enter for Start; Backspace for Select/Touchpad; A/S for L1/R1; Q/W for L2/R2.

  Position   Xbox 360   PS4   Keyboard
  South      A          ×     Z
  East       B          ○     X
  West       X          □     C
  North      Y          △     V

Coin, confirm, shot and bomb assignments follow each game's own settings.
F11 or Alt+Enter toggles fullscreen; Alt+F4 closes; F8 marks the log timeline.
SDL handles compatible controllers and arcade sticks. Wait for saving to
finish before closing after changing in-game settings.

Online features

Community features are enabled by default, with nickname Player and endpoint
https://xiaolu.life/cave/api/v1. No registration is required. Set enabled=false
under [community] for offline play. Use each game's native ranking and replay
menus. SDOJ team scores and DaiOuJou CUSTOM score submissions are not integrated.
Within each game, the same nickname represents the same community identity.
The service checks submission formats and replay integrity, but does not
authenticate nickname ownership. Choose a distinctive name and keep using it.

SDOJ Frame Delay

Set the following in user-data/config.ini:
  [game.sdoj]
  data_dir = game-data/sdoj
  frame_delay = 1

1 is low latency (default), 2 is balanced, and 3 is the original path. These are
internal scheduling policies; total input-to-display latency also depends on
the controller and display chain. Existing explicit settings are preserved.
Restart the game after changing the value.

Issues and source

For issues, press F8, close the game and share the complete relevant run-*
folder under logs/<game>, plus the version, mode, character and reproduction
steps. SOURCE.txt identifies the source revision and build instructions;
licenses/ contains component licenses.
