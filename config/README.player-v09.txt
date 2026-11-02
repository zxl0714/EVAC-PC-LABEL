EVAC PC LABEL v0.9.1

开始游玩
运行 EVAC PC LABEL.exe，选择游戏，点击“启动游戏”。
更新时关闭游戏和启动器，解压替换同名文件，保留 user-data/。
Windows x64；360 使用 Direct3D 12，PS4／PS2 使用 Vulkan 1.3。

游戏数据
只需准备想玩的作品，放到对应目录，或在启动器点击“游戏数据…”选择目录。
每个目录中的 README.txt 有完整结构和支持版本。

  game-data/futari/      虫姬二 Ver.1.5；Black Label／Ver.1.01 DLC 可选
  game-data/espgaluda2/  ESPGaluda II Black Label
  game-data/ketsui/      CUSA09062，01.00 本体或完整覆盖 01.02 更新
  game-data/sdoj/        最大往生，Xbox 360 原版
  game-data/daioujou/    CUSA32997，01.00 本体或完整覆盖 01.04 更新
  game-data/esprade/     CUSA15155，01.00 本体完整覆盖 01.04 更新
  game-data/guwange/     XBLA，58410A78，本体 0.0.0.4
  game-data/feveron/     美版 CUSA06735，01.00 本体
  game-data/garegga/     韩版 CUSA10207，01.00 本体
  game-data/espgaluda/   PS2 日版 SLPS-25352，原始 ISO

360 目录直接包含 default.xex，PS4 目录直接包含 eboot.bin，保留全部资源与层级。
PS2 直接放 ISO，文件名不限；首次启动自动准备缓存。例如：
  game-data/espgaluda/Espgaluda (Japan).iso
Ibara 为后续作品，暂未开放。

解包
ZIP／RAR 用常规工具解压。PS2 的 ISO 解压出来后直接放入对应目录。
PS4：打开 tools/Extract-PS4.exe，选择 PKG 与新的输出目录，点击 Extract。
完成后点击 Open game files，将里面的内容复制到对应 game-data 目录。
更新 PKG 单独解包，再把全部内容覆盖到本体目录，替换同名文件。
XBLA：打开 tools/Extract-XBLA.exe，选择内容包并提取，把结果放入 game-data/guwange/。
360 ISO：使用 https://github.com/XboxDev/extract-xiso/releases 提取。
虫姬二 DLC 原始包放置方法见 game-data/futari/README.txt，启动时自动提取。

配置与操作
CN／EN／JP 切换界面语言，页头可修改排行榜昵称。
启动按钮上方选择全屏或窗口分辨率。底部“配置文件”打开 user-data/config.ini。
备份、换目录时保留整个 user-data/，包含配置、存档和录像。

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
PS2 ESPGaluda 使用原作本地成绩与记忆卡。

反馈
退出游戏后点击“上传日志”，选择游玩记录，选填留言并上传。
反馈问题时提供上传成功后显示的报告编号。
源码与构建入口见 SOURCE.txt，组件许可证见 licenses/。
性能对比：https://github.com/zxl0714/EVAC-PC-LABEL#readme

English

Run EVAC PC LABEL.exe, select a game, and click Launch game.
To update, close the games and launcher, replace the program files, and keep user-data/.
Windows x64; Xbox 360 uses Direct3D 12, PS4 and PS2 use Vulkan 1.3.

Game data
Place each game's files in its game-data folder listed above, or choose an external
folder with Game data. Each folder's README.txt lists the structure and supported version.
Xbox 360 folders contain default.xex directly; PS4 folders contain eboot.bin directly.
Keep all resources and subfolders.

For PS2 ESPGaluda, place the original Japanese SLPS-25352 ISO directly in:
  game-data/espgaluda/Espgaluda (Japan).iso
The filename is your choice. First launch prepares the runtime cache automatically.
Ibara is coming soon.

Extraction
Unpack ZIP/RAR normally. PS2 ISO images are ready to use.
PS4: run tools/Extract-PS4.exe, select a PKG and a new output folder, and click Extract.
Use Open game files to locate the files to copy into the game's data folder.
Extract update PKGs separately, then copy all extracted files over the base game.
XBLA: use tools/Extract-XBLA.exe and copy the extracted files into game-data/guwange/.
Xbox 360 ISO: use the extract-xiso Windows release linked above.
For Futari's original DLC packages, follow game-data/futari/README.txt; launch extracts them.

Settings and controls
CN / EN / JP selects the UI language. Edit your leaderboard nickname in the header.
Choose fullscreen or a window size above Launch game. Config opens user-data/config.ini.
Keep the whole user-data/ folder when updating or moving your installation.

Arrow keys move; Enter is Start; Backspace is Select/Touchpad.
Z / X / C / V map to A / B / X / Y on Xbox and Cross / Circle / Square / Triangle on PS4/PS2.
A / S are L1 / R1; Q / W are L2 / R2. SDL controllers and arcade sticks are supported.
ESPGaluda ARCADE defaults: C shot, V Kakusei, X barrier, Z rapid fire. Edit them in the game's BUTTON CONFIG.
Guwange Arrange: arrows move, I/J/K/L aim the shikigami, W summons, X bombs.
F11 or Alt+Enter toggles fullscreen; Alt+F4 exits; F8 marks the log timestamp.

Community
Use the original game's leaderboard and replay menus. Set a nickname in the launcher;
the default is Player. The [community] settings above control the connection.
PS2 ESPGaluda uses the original local scores and memory cards.

Feedback
After closing the game, click Upload logs, select a session, add an optional message,
and upload. Include the report ID when reporting an issue.
Source and build instructions: SOURCE.txt. Component licenses: licenses/.
Performance comparisons: https://github.com/zxl0714/EVAC-PC-LABEL#readme
