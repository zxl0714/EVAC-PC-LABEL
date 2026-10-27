EVAC PC LABEL v0.7.6

本版增加 PS4 图形解包工具、大往生 01.00 联网适配及弹铳 Coming Soon 卡片。
启动器默认高度不超过屏幕可用区域的 90%，游戏列表滚动显示。
当前为本地预览包：大往生 01.00 联网已在本机验证，线上服务适配待部署。
更新时关闭游戏和启动器，解压到原目录替换同名文件；保留 user-data/。

开始游玩 ---------------------------------------------------------------------
运行 EVAC PC LABEL.exe，选择游戏，点击“启动游戏”。备用入口为 Launch.bat。
使用 Windows 64 位及显卡厂商驱动。Xbox 360 作品使用 Direct3D 12，PS4 作品
使用 Vulkan。运行库已随包附带。

游戏数据 ---------------------------------------------------------------------
仅安装想玩的游戏即可。在底部“游戏数据…”选择已解包目录，或放到默认位置：
  game-data/futari/      虫姬二 Ver.1.5；Black Label／Ver.1.01 DLC 可选
  game-data/espgaluda2/  ESPGaluda II Black Label
  game-data/ketsui/      CUSA09062，01.00 本体或覆盖 01.02 更新后的完整数据
  game-data/sdoj/        最大往生，Xbox 360 原版光盘数据
  game-data/daioujou/    CUSA32997，01.00 本体或覆盖 01.04 更新
  game-data/esprade/     CUSA15155，01.00 本体覆盖 01.04 更新后的完整数据
  game-data/guwange/     XBLA，Title ID 58410A78，本体 0.0.0.4，无 Title Update

所选目录应直接包含 default.xex（360）或 eboot.bin（PS4），保留全部资源
和内部层级，不只复制入口文件，也不再套一层文件夹。复制完成后点“刷新”。
首次启动会校验数据；PS4 作品还会自动准备运行文件，请等待完成。
虫姬二不装 DLC 也可玩 Ver.1.5；可选完整 DLC 放到 game-data/futari/dlc/。
保持原有内容树，已有 dlc/00000002/<包目录>/ 也能识别。
Black Label 需要 spa.bin 和完整 Rom/。
完整目录、提取命令见 game-data/futari/README.txt；选中本作点“详情”可查看状态。
默认 [game.futari] dlc_dir = auto 跟随本体目录下的 dlc/；原有显式路径会保留。
狱门山要保留 module/ 下的四个原始 DLL，它们是 Xbox 游戏数据；不要混入
.xexp／.dllp 更新文件。各目录的 README.txt 有对应要求。

解包 -------------------------------------------------------------------------
ZIP／RAR 先用常规工具解压。如果里面已经有 default.xex／eboot.bin 和全部
资源，直接选择那一层。如果仍是 ISO、PKG 或一个 XBLA 容器，继续提取。
PS4 使用下面的图形工具；Xbox 360 的命令在 PowerShell 中运行，替换为实际路径。

Xbox 360 ISO：下载并完整解压 extract-xiso 的 Windows Release：
https://github.com/XboxDev/extract-xiso/releases
  .\extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"
完成后选择 D:\GameExtract\futari；G2、最大往生同理。

Xbox 360 XBLA：若狱门山解压后得到一个很长文件名、没有扩展名的文件，
它是内容容器。在合集根目录使用随包脚本（Windows PowerShell 5.1 即可）：
  .\tools\Extract-Xbla.ps1 -InputPackage "D:\Games\原始容器文件名" -OutputDir "D:\GameExtract\guwange"
输出目录的上一层须已存在。脚本检查文件链与数据块校验，不改动原始容器。
完成后直接选择 D:\GameExtract\guwange，或把里面全部内容复制到默认目录。
不要把整个 XBLA 容器改名为 default.xex。此工具支持本作使用的小型
LIVE／PIRS 单哈希表副本布局；不是 ISO、CON 存档或 Title Update 应用工具。

虫姬二 DLC：将原始文件按 game-data/futari/README.txt 放好，保留文件名，
启动游戏时自动解包。已有 v0.7.2 解包目录保持原样即可。

PS4 PKG：双击 tools/Extract-PS4.exe，选择本体 PKG 和新输出目录，点击
“Extract”。完成后点击“Open game files”，将里面的全部内容复制到对应的
game-data/<游戏>/。更新 PKG 用同一工具另行解包，再将提取的全部内容覆盖
到本体目录，替换同名文件、保留本体独有资源。原 PKG 不会改动。
保留整个 tools/ 文件夹，所需组件已随包提供。输出目录中的 uroot/ 是实际
游戏文件目录；不要把 uroot 文件夹本身套进 game-data。详细用法见 tools/README.txt。
PKG passcode 通常保留默认值；它与 ZIP／RAR 的解压密码不同。

配置与操作 -------------------------------------------------------------------
右上角 CN / EN / JP 切换启动器语言，页头修改排行榜昵称，启动按钮上方设置
全屏。底部“配置文件”打开 user-data/config.ini，修改后点“刷新”并重启游戏。
备份或换目录时保留整个 user-data/，其中包含配置、存档和录像。
昵称含游戏字库不支持的字时，保存前会显示受影响游戏与实际传入的名字。
启动器后台检查 GitHub 正式发布，每个新版只自动提醒一次，游玩中不弹窗。
提醒中可打开发布页查看和下载新版本。成功结果缓存 24 小时；要关闭自动
检查，在 [collection] 中设置 check_for_updates = false。此开关独立于排行榜联网。
提醒记录在 user-data/update-check.json；下载后自行解压替换程序文件。

方向键移动；Enter 为 Start；Backspace 为 Select／Touchpad；
A／S 为 L1／R1，Q／W 为 L2／R2。主按键对照：
  方位          Xbox 360   PS4   键盘
  下／South     A          ×     Z
  右／East      B          ○     X
  左／West      X          □     C
  上／North     Y          △     V
具体射击、炸弹、投币等以各游戏设置为准。SDL 接入兼容手柄和摇杆。
狱门山 Arrange：方向键移动角色，I/K/J/L 对应右摇杆，W 召唤式神，X 炸弹。
可在 [game.guwange.keyboard] 修改 right_stick_up/down/left/right。
F11／Alt+Enter 切换全屏；Alt+F4 关闭；F8 在日志中标记当前时间。
排行榜玩家卡片可点击 CLOSE 或按 Enter／Esc 关闭。
修改游戏内设置后，等待保存完成再退出。

联网 -------------------------------------------------------------------------
通过原游戏菜单使用社区排行榜和录像，无须注册。默认配置：
  [community]
  enabled = true
  endpoint = https://xiaolu.life/cave/api/v1
  nickname = Player
enabled = false 为离线游玩。同一游戏中，相同昵称代表同一社区身份，首次游玩
请将 Player 改成自己的昵称并固定使用。服务检查格式与录像完整性，不验证昵称归属。
提交的昵称、成绩和分享的录像对其他玩家可见，未分享的本地录像不会自动公开。
狱门山在线合作、最大往生团队评分、大往生 CUSTOM 成绩提交暂未接入。
狱门山本地双人尚未验收；本版支持三种单人玩法。

最大往生 Frame Delay：在 [game.sdoj] 下设置 frame_delay = 1。
1 为默认低延迟档，2 为平衡档，3 为原始路径；已有明确设置会保留。
这是游戏内部调度档位，总输入延迟还受设备和显示链路影响；重启后生效。

反馈 -------------------------------------------------------------------------
异常时可按 F8；退出后在 logs/<游戏>/run-*/ 中复制实际存在的文本日志
（game.log、stdout.jsonl、stderr.txt、startup-error.txt）及 run.json，
附上版本、模式、角色和复现步骤。底部“日志”可打开目录，原始记录请保留。
公开反馈前，替换日志副本中的用户名、私人路径、昵称和自定义服务地址。
不要公开上传完整 run-*：config.ini 是配置快照，.dmp 和 *-module.bin
可能包含内存与原游戏代码。需要这些材料时由维护者另行联系。
源码提交与构建入口见 SOURCE.txt，组件许可证见 licenses/。

English ======================================================================

v0.7.6 adds a graphical PS4 extractor, DaiOuJou 01.00 community support and a
Dangun Feveron Coming Soon card. The initial launcher height is capped at 90%
of the screen work area, with a scrollable game list.
This local preview includes the 01.00 client changes; the matching online service
update is pending deployment. To update, close games and launcher, extract over
the existing folder, and keep user-data/.

Run EVAC PC LABEL.exe, select a game and click Launch game. Launch.bat is the
alternate entry. Windows x64 and graphics-vendor drivers are required.
Xbox 360 titles use Direct3D 12; PS4 titles use Vulkan. Runtime DLLs are included.

Install only the games you want. Use Game data to choose an extracted folder,
or use the game-data paths listed above. Place default.xex (360) or eboot.bin
(PS4) directly inside, preserving all files and subfolders. Each game's folder
includes a bilingual README.txt with placement instructions. Click Refresh.
Futari Ver.1.5 works without DLC; its optional content goes in futari/dlc/.
Keep the original content tree; dlc/00000002/<package-folder>/ also works. Black Label
needs spa.bin and all three Rom/ files. See game-data/futari/README.txt for the
complete tree and commands. Details shows the actual search path and status.
The default [game.futari] dlc_dir = auto follows the base game's dlc/ folder.
Existing explicit paths are preserved; set auto to follow the selected base.
Guwange uses XBLA base 0.0.0.4, Title ID 58410A78, without Title Update files.
Keep all four original DLLs in module/; they are Xbox game data.

Extract ZIP/RAR first. If it contains an ISO, PKG or XBLA container rather than
game files, use the appropriate command above with your own paths:
- ISO: extract-xiso, from the official Windows release linked above.
- XBLA: the included tools/Extract-Xbla.ps1, with -InputPackage and -OutputDir.
  Windows PowerShell 5.1 is sufficient. Choose a new output folder under an
  existing parent. Files, chains and data-block hashes are checked; the input
  is unchanged. Select the output folder directly. Do not rename a container
  to default.xex. The helper supports this game's small LIVE/PIRS single-table
  layout, not ISO, CON saves or Title Update application.
- PS4: double-click tools/Extract-PS4.exe, select a PKG and a new output folder,
  then click Extract. Click Open game files when finished and copy all contents
  into game-data/<game>/. Extract updates separately and overlay all their game
  files onto the base, keeping base-only files. Keep the entire tools/ folder;
  dependencies are included. See tools/README.txt for the supported versions.
  The PKG passcode normally stays at its default of 32 zeros; ZIP/RAR passwords
  are separate. Original PKGs remain unchanged.
- Futari DLC: place the original files as shown in game-data/futari/README.txt,
  keep their filenames, then start the game. Extraction is automatic on first
  use. Existing v0.7.2 extracted folders work in place.

Keep the entire user-data/ folder when backing up or moving the collection.
It contains config.ini, saves and replays. Config opens the shared INI; click
Refresh after external edits and restart the game. CN / EN / JP changes the
launcher language. Nickname and Start fullscreen are available in the launcher.
Unsupported nickname characters trigger a warning before saving.
The launcher checks stable GitHub releases in the background and automatically
announces each new version once, never during gameplay. Open the release page
from the notification for notes and downloads. Successful checks are cached
for 24 hours. Set [collection] check_for_updates = false to disable auto checks;
community networking is independent.
Reminder history lives in user-data/update-check.json. Download and replace
program files yourself when updating.

Arrows: direction. Z/X/C/V: Xbox A/B/X/Y or PS4 Cross/Circle/Square/Triangle.
Enter: Start. Backspace: Select/Touchpad. A/S: L1/R1. Q/W: L2/R2.
Leaderboard player cards close with CLOSE, Enter or Esc.
Shot, bomb and coin follow in-game settings. SDL handles compatible controllers.
Guwange Arrange uses arrows for the character, I/K/J/L for the right stick,
W to summon Shikigami and X for bombs. Right-stick keys can be changed under
[game.guwange.keyboard] as right_stick_up/down/left/right.
F11 or Alt+Enter: fullscreen. Alt+F4: close. F8: mark the log timeline.
Wait for in-game saving to finish before closing.

Community is enabled by default, with nickname Player and endpoint
https://xiaolu.life/cave/api/v1. No registration is needed. Set enabled=false
under [community] for offline play. Use native ranking/replay menus. The same
nickname represents the same identity within each game; replace Player with
your own distinctive name and keep it. Formats and replay integrity are checked,
not ownership of nicknames. Submitted nicknames, scores, and shared replays are
visible to other players; unshared local replays are not automatically public.
Guwange online co-op, SDOJ team scores and DaiOuJou CUSTOM submissions are not
integrated. Guwange supports the three single-player modes; local two-player
play has not been validated. In [game.sdoj], frame_delay=1 is low latency (default), 2 balanced,
3 original. This is an internal scheduling policy, not total device-to-display
latency. Existing explicit preferences are preserved. Restart after changes.

For issues, press F8 and close the game. Copy the relevant text logs (game.log,
stdout.jsonl, stderr.txt, startup-error.txt) and run.json from logs/<game>/run-*/,
including only files that exist. Add version, mode, character and steps.
Keep the originals. Before posting publicly, replace usernames, private paths,
nicknames and custom server addresses in the copies with placeholders.
Do not publicly attach the entire run-* folder: config.ini is a configuration
snapshot; .dmp and *-module.bin may contain memory and original game code.
A maintainer will arrange a separate transfer if these files are needed.
See SOURCE.txt for source/build information and licenses/ for component licenses.
