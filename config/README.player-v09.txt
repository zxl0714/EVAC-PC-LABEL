EVAC PC LABEL v0.9.9

开始游玩
运行 EVAC PC LABEL.exe，选择游戏，点击“启动游戏”。
更新时关闭游戏和启动器，解压替换同名文件，保留 user-data/。
Windows x64。Xbox 360 自动选择 Direct3D 12 或 Vulkan，PS4／PS2 使用 Vulkan 1.3。

游戏数据
只需准备想玩的作品，放到对应目录，或在启动器点击“游戏数据…”选择目录。
每个目录中的 README.txt 有完整结构和支持版本。

  game-data/futari/      虫姬二 Ver.1.5；Black Label／Ver.1.01 DLC 可选
  game-data/espgaluda2/  ESPGaluda II Black Label
  game-data/ketsui/      CUSA09062，01.00 本体或完整覆盖 01.02 更新
  game-data/sdoj/        最大往生，Xbox 360 原版
  game-data/daioujou/    CUSA32997，01.00 本体或完整覆盖 01.04 更新
  game-data/esprade/     CUSA15155，01.00 本体或完整覆盖 01.04 更新
  game-data/guwange/     XBLA，58410A78，本体 0.0.0.4
  game-data/feveron/     美版 CUSA06735，01.00 本体
  game-data/garegga/     韩版 CUSA10207，01.00 本体
  game-data/espgaluda/   PS2 日版 SLPS-25352，原始 ISO
  game-data/ibara/       铸蔷薇，PS2 日版 SLPM-66301，原始 ISO

360 目录直接包含 default.xex，PS4 目录直接包含 eboot.bin，保留全部资源与层级。
PS2 直接放 ISO，文件名不限；首次启动自动准备缓存。例如：
  game-data/espgaluda/Espgaluda (Japan).iso

解包
ZIP／RAR 用常规工具解压。PS2 的 ISO 解压出来后直接放入对应目录。
PS4：打开 tools/Extract-PS4.exe，选择 PKG 与新的输出目录，点击 Extract。
完成后点击 Open game files，将里面的内容复制到对应 game-data 目录。
更新 PKG 单独解包，再把全部内容覆盖到本体目录，替换同名文件。
XBLA：打开 tools/Extract-XBLA.exe，选择内容包并提取，把结果放入 game-data/guwange/。
360 ISO：打开 tools/Extract-Xbox360.exe，Browse… 选择 ISO，Extract 开始提取，
完成后用 Open game files 打开目录，将内容放入对应的 game-data 游戏目录。
虫姬二 DLC 原始包放置方法见 game-data/futari/README.txt，启动时自动提取。

大往生 X Arrange 音乐
用 tools/Extract-Xbox360.exe 解开 Xbox 360 版 Black Label Extra 的 ISO。
把 dat/sound/ 中的 ddo_bgm_SoundBank.xsb 和 ddo_bgm_WaveBank.xwb
复制到 game-data/daioujou/x-arrange/，启动后在 BGM SET 选择 X Arrange(Xbox 360)。

配置与操作
CN／EN／JP 切换界面语言，页头可修改排行榜昵称。
启动器关闭时会记住窗口大小。拖动卡片调整顺序，右键隐藏游戏，眼睛图标显示隐藏项。
启动按钮上方选择全屏或窗口分辨率。底部“配置文件”打开 user-data/config.ini。
底部“输入设置”可修改键盘、手柄和摇杆绑定。选择所有游戏或当前游戏，双击一项后按键，保存后下次启动生效。
备份、换目录时保留整个 user-data/，包含配置、存档和录像。

ESPGaluda II、最大往生和狱门山默认使用 0V。要恢复原作时序，在对应区段修改 frame_delay 后重启：

[game.espgaluda2]
frame_delay = 2

[game.sdoj]
frame_delay = 3

[game.guwange]
frame_delay = 2

虫姬二新档默认 1V(Fast)。要开启 0V，在游戏内选 Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing)。
低延迟显示使用 [display] 下的 vsync = false 和 reflex = auto。兼容的 NVIDIA 显卡会自动启用 Reflex。PS4 默认使用 NEO 模式。
Radeon 玩家可在 [display] 下设 amd_anti_lag = true 试用 Anti-Lag 2，标准包默认关闭。

  键盘          Xbox 360   PS4／PS2
  Z             A          ×
  X             B          ○
  C             X          □
  V             Y          △
方向键移动，Enter 为 Start，Backspace 为 Select／Touchpad。
A／S 为 L1／R1，Q／W 为 L2／R2。支持 SDL 手柄与摇杆。
ESPGaluda ARCADE 默认 C 射击，V 觉圣，X 护罩，Z 连射；按键可在游戏的 BUTTON CONFIG 中调整。
狱门山 Arrange：方向键移动，I/J/K/L 控制式神，W 召唤，X 炸弹。
F11／Alt+Enter 切换全屏；Alt+F4 退出；F8 标记日志时间。

联网
支持的游戏从原菜单进入社区排行榜和录像，默认昵称 Player。
在启动器中改成自己的昵称并固定使用；配置位于 [community]：
  enabled = true
  endpoint = https://xiaolu.life/cave/api/v1
  nickname = Player
PS2 游戏使用原作本地成绩与记忆卡。

反馈
退出游戏后点击“上传日志”，选择游玩记录，选填留言并上传。
反馈问题时提供上传成功后显示的报告编号。
游戏无法启动时也可以上传日志；提示运行组件不可用时，点击“详情”查看具体文件和处理方法。
源码与构建入口见 SOURCE.txt，组件许可证见 licenses/。
性能对比：https://github.com/zxl0714/EVAC-PC-LABEL#readme

English

Drag cards to reorder games; right-click to hide them. Use the eye icon to show hidden games.
For DaiOuJou X Arrange music, extract the Xbox 360 Black Label Extra ISO with
tools/Extract-Xbox360.exe. Copy ddo_bgm_SoundBank.xsb and ddo_bgm_WaveBank.xwb
from dat/sound/ to game-data/daioujou/x-arrange/. Launch the game and choose
X Arrange(Xbox 360) in BGM SET.

Run EVAC PC LABEL.exe, select a game, and click Launch game.
To update, close the games and launcher, replace the program files, and keep user-data/.
Windows x64. Xbox 360 selects Direct3D 12 or Vulkan automatically. PS4 and PS2 use Vulkan 1.3.

Game data
Place each game's files in its game-data folder listed above, or choose an external
folder with Game data. Each folder's README.txt lists the structure and supported version.
Xbox 360 folders contain default.xex directly; PS4 folders contain eboot.bin directly.
Keep all resources and subfolders.

Place each original Japanese PS2 ISO in its folder:
  game-data/espgaluda/Espgaluda (Japan).iso
  game-data/ibara/Ibara (Japan).iso
Use SLPS-25352 for ESPGaluda and SLPM-66301 for Ibara.
The filename is your choice. First launch prepares the runtime cache automatically.

Extraction
Unpack ZIP/RAR normally. PS2 ISO images are ready to use.
PS4: run tools/Extract-PS4.exe, select a PKG and a new output folder, and click Extract.
Use Open game files to locate the files to copy into the game's data folder.
Extract update PKGs separately, then copy all extracted files over the base game.
XBLA: use tools/Extract-XBLA.exe and copy the extracted files into game-data/guwange/.
Xbox 360 ISO: open tools/Extract-Xbox360.exe, Browse… to the ISO, then Extract.
Use Open game files and copy the contents into the corresponding game-data folder.
For Futari's original DLC packages, follow game-data/futari/README.txt; launch extracts them.

Settings and controls
CN / EN / JP selects the UI language. Edit your leaderboard nickname in the header.
The launcher remembers its window size when closed.
Choose fullscreen or a window size above Launch game. Config opens user-data/config.ini.
Open Controls to remap your keyboard, gamepad or arcade stick. Choose a scope, double-click a binding and press an input. Changes apply the next time you start a game.
Keep the whole user-data/ folder when updating or moving your installation.

ESPGaluda II, SaiDaiOuJou and Guwange default to 0V. To restore original timing, change frame_delay in the matching section and restart:

[game.espgaluda2]
frame_delay = 2

[game.sdoj]
frame_delay = 3

[game.guwange]
frame_delay = 2

Futari starts at 1V(Fast). For 0V, select Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing).
For low latency, use vsync = false and reflex = auto under [display]. Supported NVIDIA GPUs enable Reflex automatically. PS4 games default to NEO mode.
Radeon users can try Anti-Lag 2 by setting amd_anti_lag = true under [display]. The standard package defaults to off.

Arrow keys move; Enter is Start; Backspace is Select/Touchpad.
Z / X / C / V map to A / B / X / Y on Xbox and Cross / Circle / Square / Triangle on PS4/PS2.
A / S are L1 / R1; Q / W are L2 / R2. SDL controllers and arcade sticks are supported.
ESPGaluda ARCADE defaults: C shot, V Kakusei, X barrier, Z rapid fire. Edit them in the game's BUTTON CONFIG.
Guwange Arrange: arrows move, I/J/K/L aim the shikigami, W summons, X bombs.
F11 or Alt+Enter toggles fullscreen; Alt+F4 exits; F8 marks the log timestamp.

Community
Use the original game's leaderboard and replay menus. Set a nickname in the launcher;
the default is Player. The [community] settings above control the connection.
PS2 games use their original local scores and memory cards.

Feedback
After closing the game, click Upload logs, select a session, add an optional message,
and upload. Include the report ID when reporting an issue.
Startup failures can also be uploaded. Click Details for missing or inaccessible runtime files and recovery steps.
Source and build instructions: SOURCE.txt. Component licenses: licenses/.
Performance comparisons: https://github.com/zxl0714/EVAC-PC-LABEL#readme
