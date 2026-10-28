# 更新记录

## v0.8.0

- 新增《弾銃フィーバロン / Dangun Feveron》，支持 ARCADE、SUPER EASY、FEVER、CUSTOM，以及存档、录像和社区排行榜。
- 修复虫姬二 Black Label 和 ESPGaluda II 下载录像后作者显示为自己的问题，重启或修改昵称后仍保留原作者。
- 修复 ESPGaluda II 启动后首次下载录像失败、需要重试的问题。
- 降低虫姬二、G2、最大往生和狱门山的 CPU 开销，优化定时器、渲染任务和原生完成等待。
- G2 新增并默认启用 1V 低延迟路径，可将 `frame_delay` 设为 `2` 使用原始路径；已有显式设置保留。
- 新增 `tools/Extract-XBLA.exe`，通过窗口选择内容包并提取狱门山游戏文件。

### 延迟与性能

360 代表场景的延迟估算与 CPU 实测对比：

| 游戏 | 移植版延迟/ms | Xenia 延迟/ms | 移植版 CPU/核 | Xenia CPU/核 |
| --- | ---: | ---: | ---: | ---: |
| 虫姬二 | 26 | 36 | 0.08 | 1.26 |
| ESPGaluda II | 26 | 51 | 0.38 | 1.37 |
| 最大往生 | 26 | 69 | 0.43 | 3.46 |

移植版三款均使用默认 1V；Xenia 分别为原作 1V、2V、3V。延迟计入 8.3ms 平均采样等待及同机历史呈现后段，按移植版独立翻转、Xenia 桌面合成估算，不含设备输入和显示器扫描／响应。CPU 为 i7-13700KF／RTX 4080 回放场景的等效逻辑核数，1 核 = 1000 CPU ms/s。[比较方法与数据](ANALYSIS.md#360-统一条件比较)。

### English

- Added Dangun Feveron: ARCADE, SUPER EASY, FEVER, CUSTOM, saves, replays and community leaderboards.
- Fixed downloaded replay authors being replaced with your own nickname in Futari Black Label and ESPGaluda II, including after restarting or changing your nickname.
- Fixed ESPGaluda II's first replay download after startup requiring a retry.
- Reduced CPU work in Futari, G2, SaiDaiOuJou and Guwange through timer, renderer and completion-wait improvements.
- Added G2's 1V low-latency path and enabled it by default. Set `frame_delay=2` for the original path; existing explicit settings are preserved.
- Added `tools/Extract-XBLA.exe` for extracting Guwange content packages through a file picker.

### Latency and performance

Estimated latency and measured CPU usage in representative Xbox 360 scenes:

| Game | Port latency/ms | Xenia latency/ms | Port CPU/cores | Xenia CPU/cores |
| --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | 26 | 36 | 0.08 | 1.26 |
| ESPGaluda II | 26 | 51 | 0.38 | 1.37 |
| DoDonPachi SaiDaiOuJou | 26 | 69 | 0.43 | 3.46 |

All three ports use their default 1V setting; Xenia retains the original 1V, 2V and 3V settings, respectively. Latency includes an average 8.3 ms sampling wait and historical same-PC presentation data, assuming independent flip for the ports and desktop composition for Xenia. Device input, display scanout and panel response are excluded. CPU figures are equivalent logical cores from replay workloads on an i7-13700KF / RTX 4080; one core equals 1,000 CPU ms/s. [Method and data (Chinese)](ANALYSIS.md#360-统一条件比较).

## v0.7.6

- 新增 PS4 图形解包工具，可直接提取本体和更新 PKG。
- 大往生 1.00 增加社区排行榜和录像上传下载支持。
- 启动器增加《弾銃フィーバロン / Dangun Feveron》Coming Soon 卡片。
- 调整启动器窗口高度，适配较小屏幕，游戏列表可滚动。
- 更新中英文游戏数据放置指南。

## v0.7.5

- 修复大往生排行榜“查看玩家资料”导致的退出，显示玩家昵称和 CLOSE 按钮，支持 Enter／Esc 关闭。
- 绊地狱、大往生和 ESP Ra.De. 增加 Vulkan 兼容呈现路径，支持缺少 EXT／KHR maintenance1 的驱动环境。
- 启动日志提前记录显卡型号、驱动版本、Vulkan 版本和呈现能力，便于定位启动失败。

## v0.7.4

- 修复部分显卡驱动下绊地狱、大往生、ESP Ra.De. 启动退出的问题，兼容 Vulkan maintenance1 的 EXT 和 KHR 扩展。
- 绊地狱支持明文 SELF 格式的 01.00 本体备份。
- 大往生增加 01.00 本体离线游玩；社区排行榜和联网录像继续使用 01.04 数据。
- 修正开发工具 Ninja 的文件校验值，解决新环境准备失败。

## v0.7.3

- 修复虫姬二 Black Label／Ver.1.01 DLC 识别，支持现有内容树和常见的 `dlc/00000002/<包目录>/` 放法。
- DLC 默认跟随所选本体的 `dlc/` 目录，保留显式路径设置；启动器“详情”显示查找位置和各项 DLC 状态。
- 虫姬二原始 DLC 文件在启动时自动解包，已有 v0.7.2 目录保持可用；同步中英文放置说明。
- 启动器后台检查 GitHub 正式发布，每个新版本只提醒一次，可从提醒打开发布页；游玩中不弹窗。

## v0.7.2

- 整理中英文使用说明，统一下载版本和对应源码、依赖与构建入口。
- 补充随包的 PS4 第三方组件来源及许可声明。
- 区分公开反馈的文本日志和按需提供的崩溃附件，补充社区昵称及录像分享说明。
- 游戏功能与 v0.7.1 修订版一致，保留玩家卡片 CLOSE、狱门山退出及最大往生 Team Play 修复。

## v0.7.1

- Xbox 360 排行榜玩家卡片使用 PLAYER CARD／CLOSE 标签，支持 Enter／Esc 关闭。
- 调整昵称输入框的文字垂直对齐。
- 游戏数据放置说明统一命名为 `README.txt`，同步中英文使用说明。
- 缩小游戏列表滚动和悬停的重绘范围，避免昵称及全屏控件闪烁。
- 修复狱门山“Return to Game Library”留下后台进程、启动器持续显示运行中的问题。
- 修复最大往生进入 Team Play 时的初始化崩溃。

## v0.7

- 加入《ぐわんげ／Guwange（狱门山）》XBLA 本体，支持普通、Blue 和 Arrange Xbox 360 单人玩法、存档及录像。
- 接入狱门山社区排行榜和录像上传下载，沿用合集昵称、联网开关和服务地址。
- 为 Arrange 增加键盘式神方向映射，支持与角色移动同时输入；手柄沿用右摇杆操作。
- 增加 XBLA 内容包提取脚本和中英文放置说明。
- 修复 Xbox 360 模块导出入口重复分配，卸载模块时清除旧入口，改善多模块游戏的资源管理。

## v0.6.1

- 修复 ESP Ra.De.「いろりの部屋」房屋模式崩溃。
- 修复 G2 三面中 BOSS 相关的遗漏回调，补齐 Xbox 360、Arcade 及其他模式的对应入口。
- 包含 v0.6 包修订的页头间距调整和重复启动文件清理。
- 重写中英文入门说明，增加 ISO／PKG 解包步骤、PS4 提取辅助脚本和键盘／手柄按键对照。

## v0.6 包修订（2026-10-24）

- 移除包内重复的旧名称启动器及对应调试符号，只保留 EVAC PC LABEL 入口。
- 去掉排行榜昵称下方的常驻说明及空行，收紧页头间距。

## v0.6

- 加入《エスプレイドΨ》，接入统一启动器、存档、社区排行榜与录像上传下载。
- 修复 ESP Ra.De. 下载录像列表残留加载面板，以及正常关窗被启动器误报异常退出的问题。
- 改进三款 PS4 游戏的画面交接与呈现同步，减少窗口等待对游戏出帧的影响；优化着色器读取，降低 ESP Ra.De. 绘图指令处理开销。
- 最大往生 Frame Delay 默认改为 1V 低延迟档，保留 2V／3V 可选及已有明确设置。
- 全屏选项移到启动按钮上方，排行榜昵称移至页头右侧。
- 保存昵称时提示游戏字库缺字及显示名截短，可返回修改或保留原昵称。
- BAT 备用启动入口改为 `Launch.bat`。

## v0.4

- 启动器与游戏窗口统一为 EVAC PC LABEL，默认启动文件为 `EVAC PC LABEL.exe`。
- 记住上次关闭时选择的游戏；重复打开启动器时唤起现有窗口。
- 收紧语言选项间距，调整底部操作对齐，移除常驻键盘提示。
- 完整包预建游戏数据目录并附放置说明；绊地狱改用一个根目录，支持本体和可选覆盖更新。
- 绊地狱本体版接入成绩与录像上传下载，保留原作的录像版本兼容判断。
- 修复 G2 Xbox 360 Novice 中遗漏原生回调导致的一处崩溃。
- 修复最大往生低延迟档位的减速统计交接，以及训练重入时统计清零引起的崩溃。
- 最大往生 Frame Delay 默认恢复为 3V 原始路径。已有显式档位保留，1V／2V 作为手动测试选项；旧配置若指定了 1 或 2，可手动改为 `frame_delay = 3`。

## v0.3.2

- 修复大往生打开 AC-CHALLENGE RANKING 时的网络错误，支持挑战累计积分提交、翻页和查看本人排名。挑战榜使用原作奖牌积分，不附带录像。
- 补齐绊地狱 CUSTOM 综合、Type-A、Type-B 榜的成绩提交、读榜和对应录像上传下载；录像保留原始设置，上榜资格沿用游戏规则。
- 更新包包含 v0.3.1 的全部改动，可直接覆盖已有 v0.3、v0.3.1 或较早的 v0.3.2 安装。

包含两项排行榜更新的对应源码标签为 `v0.3.2-custom`。较早的 `v0.3.2` 标签只含大往生 AC-CHALLENGE 修复。

## v0.3.1

- 配置集中到 `user-data/config.ini`；首次启动自动迁移旧根目录配置，保留已有设置。
- 启动器使用 CN / EN / JP 文字语言栏，将配置、日志等工具统一放在底部，并更新最大往生横幅。
- 增加 EVAC PC LABEL 标识和配套字标图标。
- 最大往生增加可配置的 Frame Delay，缺省使用 1V；在 `[game.sdoj]` 中设 `frame_delay = 2` 可选兼容档，设 `3` 可选原始路径。已有显式设置继续保留。

Frame Delay 表示游戏软件链路的策略，不等同于设备输入到屏幕发光的实测总延迟。图标与缺省 1V 已包含在当前 v0.3.2 中。

## v0.3

- 加入《怒首領蜂 大往生 臨廻転生》，接入统一启动器、配置、存档、日志及社区成绩／排行榜／录像功能。
- 优化绊地狱与大往生的线程交接和画面输出，改善游玩及回放卡顿。
- 启动器支持中文、日文、英文；游戏名称采用官方日文主标题和英文副标题。

## v0.2

- 加入《怒首領蜂最大往生》及其社区成绩、排行榜和录像功能。
- 接入统一启动入口、配置与独立存储，保留原作游玩赚金币和商店解锁流程。

## v0.1

- 首次整合虫姬二、Espgaluda II 和绊地狱，提供统一启动器、共享配置及各游戏独立的存档、录像与日志。

下一版本的工作安排见[实施计划](IMPLEMENTATION_PLAN.md)。
