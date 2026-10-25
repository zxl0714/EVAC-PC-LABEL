# EVAC PC LABEL 开发入口

EVAC PC LABEL 使用统一启动器和公共配置，按游戏隔离适配、存档、录像及日志。
当前产品版本为 v0.7.3；发行状态与实际验收范围见实施计划和验收矩阵。

已接入七款游戏，Guwange 的三种单人玩法、保存及正式社区短流程已验证。源码来自本仓库维护代码与固定开源依赖，不包含 ROM 或生成的游戏程序正文。程序包对应的提交见包内 SOURCE.txt。

先读 [README](README.md) 和 [AGENTS.md](AGENTS.md)。实现计划、实际覆盖和技术分析分别在 [实施计划](docs/IMPLEMENTATION_PLAN.md)、[验收矩阵](docs/TEST_MATRIX.md) 与 [分析入口](docs/ANALYSIS.md)。调查过程留在这些文档中，本文件介绍如何接手开发。

## 目录

| 目录或文件 | 用途 |
| --- | --- |
| common/ | 清单、UTF-8 INI、路径、数据检查、导入及公共启动合同 |
| launcher/ | Win32／Direct2D 启动器、翻译和内嵌横幅 |
| backends/xbox360/ | Xbox 360 公共宿主、输入、音频、诊断和社区桥接 |
| backends/ps4/ | PS4 装载、原生服务、内容读取、输入和社区适配 |
| games/<id>/ | 每游戏清单、代码生成声明、专属修复与诊断 |
| third_party/rexglue/ | 维护的 Xbox 360 SDK 源码及固定子模块 |
| third_party/ps4/ | 提取的图形组件、来源及许可证 |
| scripts/、tools/ | 构建、整理、检查、打包、固定工具与依赖锁 |
| config/default.ini | user-data/config.ini 的默认模板 |
| game-data/<id>/ | 玩家提供的数据，不进 Git |
| generated/、out/、runtime/ | 生成正文、构建产物、配套运行文件，不进 Git |
| prepared/、user-data/、logs/、cache/ | 独立的准备镜像、存档录像、日志和缓存 |
| .local/、releases/ | 开发现场、证据与发行输出，不进 Git |

游戏 ID 为 futari、espgaluda2、ketsui、sdoj、daioujou、esprade、guwange。最大往生是 sdoj，大往生是 daioujou。ESP Ra.De. 使用 CUSA15155 本体覆盖 01.04 更新后的单目录，共用 PS4 运行时。

### DLC 与启动器更新检查

虫姬二的 `games/futari/data.json` 只声明真实 DLC 资源，不将 `.header` 当作输入。
`common/catalogue.cpp` 有界识别现有内容树及常见缺层目录，报告每项资源的实际来源；
`backends/xbox360/diagnostics/content_prepare.cpp` 将已完整验证的资源复制到
`prepared/futari/content/<recipe-sha256>/`，用 SDK 内容 API 生成元数据后只读挂载。
缓存按资源清单及原作内容名称分组，验证后复用，损坏缓存重建；原始游戏／DLC 不写入。
`optionalContent.nativeName` 是原作枚举时匹配的内容名称，与启动器显示名称分开维护。
`dlc_dir = auto` 跟随所选本体，显式路径和空值禁用语义保留。
`common/stfs.*` 原生读取 LIVE/PIRS DLC 容器；Quick 只遍历结构，Full 校验数据块与
manifest 资源哈希。`ContentInspection.container` 记录原始文件，准备层自动提取到
上述缓存，验证后原子发布；不启动 PowerShell。旧解包目录沿用同一份缓存配方。
`scripts/Extract-Dlc.ps1` 保留为独立工具，玩家正常安装无需运行它。

`launcher/update_check.*` 独立于游戏运行时及社区服务。后台查询固定 GitHub Release
API，状态位于 `user-data/update-check.json`；成功检查缓存 24 小时，失败退避一小时。
只有实际显示的提醒才记入 `notified`，游戏运行／窗口隐藏期间延后。网络线程仅持有
结果对象，不引用窗口或路径，不阻塞关闭。UI 使用原生窗口，版本来自
`scripts/product.py` 生成的 `artwork.h`，与 EXE 版本资源保持一致。
机制检查：`cave_common_tests`、`content_prepare`、`cave_update_tests`、
`tests/test_xbla_extract.py` 和 `scripts/Test-Launcher.ps1`。

Guwange 的 `module/*.dll` 是 Xbox 360 XEX2 游戏模块，不能作为 Windows DLL 直接加载；生成器分别将四个同址模式模块转换为独立原生库。本体与 TU1 的程序地址不同，以 `games/guwange/inputs.json` 和 `data.json` 的实际哈希为准。

Guwange 的 Xbox 360 Arrange 自动射击，方向键移动角色，右摇杆移动式神、RT 召唤式神、B 炸弹；键盘对应方向键、I/K/J/L、W、X。`[game.guwange.keyboard]` 的 `right_stick_up/down/left/right` 可改式神方向，支持多个键或 `none`，与其他绑定重复时报错。此默认仅在 Guwange 生效，其他作品键位不变；SDL 手柄保留真实右摇杆。

本作社区适配位于 `games/guwange/community/`，服务端为 `server/games/guwange.py`，使用独立数据库和 `/games/guwange` API。原生 view 1–4／5–8／9–12 分别对应普通／Blue／Arrange，每组为综合及三角色榜；保存原生录像字节，不复用其他作品的录像格式。机制测试为 `guwange_replay`、`guwange_community`、`guwange_community_loopback`。

正式 Guwange 服务已部署，使用独立 `guwange-current`、18090 端口、数据库及每日快照。正常合集入口已完成 Arrange 5336 分的 HTTPS 提交、读榜、录像上传下载、落盘和完整回放；部署模板及增量更新步骤见 `server/README.md`。

Guwange 的单条 XUID 枚举用于结算前读取个人最佳成绩：适配为显式用户查询，新昵称返回 rank=0／rating=0 的本地记录。不能返回空行指针，也不能把 around-user 的第一名当成该玩家的成绩；公开榜与多条周边查询不补这条记录。保持原游戏的成绩比较和上榜资格，不通过预填服务端成绩规避空榜。

正常启动沿用共享 `[community]` 配置。开发协议检查可用 `py -3.12 -m server.games.guwange --database '<独立测试目录>/community.sqlite3' --port 0` 建立本机服务，再给独立测试宿主传入 `--guwange_community_test_endpoint=http://127.0.0.1:<服务输出端口>/cave/api/v1`；该开发入口要求关闭正常社区配置，仅接受 loopback，不能与正常 provider 同时启用。实际资格、提交与录像流程由游戏驱动，不向正式榜单提交自造记录。

v0.7 以单人玩法为验收范围。SDK 当前只提供 user 0 的登录身份；SDL 的多设备输入不等于第二个已登录用户。菜单里的 1P／2P Side Start 是起始侧选择，不能据此宣称本地双人已通过。合作模块已生成但在线合作未接入；第二用户身份、设备归属及原生加入流程留作后续独立工作。

当前数据合同固定为 XBLA 本体 0.0.0.4，不应用 TU1。`scripts/Extract-Xbla.ps1` 在 Windows PowerShell 5.1 下提取已展开 ZIP 内的 LIVE／PIRS 容器，输出目录须新建、父目录已存在。实际本体的 29 个输出文件与 `games/guwange/data.json` 全部一致；这项提取校验不是游戏运行验收。格式边界与命令见 `docs/GAME_DATA.md`，自造容器回归为 `tests/test_xbla_extract.py`。

原始数据只读。写入目录与原数据隔离，不借用旧项目的工具安装、构建产物或个人 profile。

## 准备环境

使用 Windows x64、PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK。源码放较短的英文目录，例如 E:/CaveDev/EVAC-PC-LABEL；固定 CMake 的中文源码路径问题尚在，玩家包和游戏数据可使用中文路径。

~~~powershell
git clone https://github.com/zxl0714/EVAC-PC-LABEL.git
cd EVAC-PC-LABEL
git submodule update --init -- third_party/rexglue/thirdparty/inja
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
~~~

公共组件和启动器不需要 ROM 或先构建游戏 SDK；上述子模块命令恢复更新检查使用的 JSON 头文件。工具版本锁在 tools/packages.lock.json，统一使用 LLVM 23.1.3；两个后端保留各自的驱动形式和 ABI 参数。

新 clone 编译 Xbox 360 前恢复固定依赖并构建 SDK：

~~~powershell
py -3.12 scripts/sdk_sources.py restore
./scripts/Build-Sdk.ps1 -Jobs 2
~~~

PS4 作品恢复另一套依赖，不需要上述 Xbox 360 SDK：

~~~powershell
py -3.12 scripts/ps4_sources.py restore
py -3.12 scripts/ps4_sources.py verify
~~~

从 GitHub 取得与程序包 SOURCE.txt 对应的提交，再按上述命令恢复固定 SDK／PS4 依赖。可用 Prepare-Tools.ps1 的 -CacheFrom 和 ps4_sources.py 的 --cache-from 显式复用本机缓存；缓存只加速恢复，最终源码和 Git 对象仍自包含。

## 配置与数据

开发版配置位于 `user-data/config.ini`；`EnsureConfig` 首次启动原样迁移旧根目录文件，新位置已存在时优先使用它。只读工具通过 `ConfigPath`／`project.config_path` 查找，不执行迁移。配置内的相对路径始终相对合集根目录，不依赖工作目录或 INI 所在目录。共用 fullscreen、vsync、音量、键盘、SDL 手柄、昵称和联网选项；游戏覆盖优先，但默认模板不为每个游戏重复一套通用设置。

运行时只读同一份 config.ini。开发时可通过配置或显式参数指定现有游戏数据目录，无须另复制一份 ROM；显式参数优先，坏配置不静默回退。

数据布局和版本见 README 与各 games/<id>/data.json。检查现有目录：

~~~powershell
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
~~~

快速检查查存在性和大小；有多个数据版本时也会核对根程序 SHA-256，以选定精确清单。--full 校验全部资源 SHA-256，--programs 校验运行程序。检测工具只读、不运行游戏或联网。Coming Soon 与数据是否齐全是不同状态。

最大往生使用受支持的光盘本体，不混入 default.xexp、.binp 或音乐补丁。三款 PS4 作品首次运行由原生准备链生成自己的 prepared/<id>/ 缓存，不修改原数据；大往生为 CUSA32997／01.04 合并目录，ESP Ra.De. 为 CUSA15155／01.04 合并目录。绊地狱使用 CUSA09062／01.00 本体或本体覆盖 01.02 后的单目录，直接包含 eboot.bin；不接受旧双目录。检测 JSON 的 dataVariant／dataVersion／dataLayout 标明实际识别结果，缓存和 Internal 组包绑定同一版本清单。实际游玩验收见 TEST_MATRIX。

## 编译与运行

单独编译游戏，显式数据目录可省略以使用配置中的路径：

~~~powershell
./scripts/Build.ps1 -Games espgaluda2 -DataDirectory 'D:/Games/G2' -Jobs 2
./scripts/Build.ps1 -Games @('espgaluda2','futari','sdoj','guwange','ketsui','daioujou','esprade') -Jobs 2
./scripts/Test.ps1 -Scope Native -Games @('espgaluda2','futari','sdoj','guwange','ketsui','daioujou','esprade')
~~~

Xbox 360 的 -NativeOnly 构建无 ROM 的机制／分析工具，不代替游戏生成。PS4 宿主本身无须 ROM 即可编译，玩家数据在启动时准备。默认两个任务、低优先级，构建不自动启动游戏。

输出包含独立宿主、模块和 GPU 组件，不追求把所有内容塞进一个 EXE。Build.ps1 完成符号配套和 runtime 整理；不要从旧工程手工拼 DLL，也不要只复制单个 EXE。

正式游戏使用启动器，开发可选择独立 profile：

~~~powershell
& './EVAC PC LABEL.exe' --game espgaluda2 --profile test
& './EVAC PC LABEL.exe' --select ketsui
~~~

`--game` 直接启动游戏；`--select` 只指定首次打开 GUI 时选中的作品。GUI 正常关闭时在 `[collection].selected_game` 保存选中 ID，下次打开恢复；昵称草稿不会因此自动保存。同一安装目录再次打开只唤起原窗口，不用新参数改变原选择或重复启动游戏，不同目录各自运行。

GUI 和命令行共用 PrepareLaunch 的完整检查。PS4 运行程序共用配套 GPU；开发分支 GPU API 当前为 13，宿主和 DLL 必须来自同次认证构建。

收据检查入口：

~~~powershell
py -3.12 scripts/build_receipt.py verify-host --games espgaluda2,futari,sdoj,guwange
py -3.12 scripts/ps4_build_receipt.py verify
py -3.12 scripts/launcher_receipt.py verify
~~~

联合 360 收据在 out/xbox360/bin/build-receipt.json，PS4 与启动器分别在 out/ps4/、out/launcher/。收据绑定源码、工具、依赖、程序和 PDB；改动后按影响范围重建。重现发行版时从对应标签建立独立 checkout，保留已冻结包。

## 排查与验证

反馈先取 release.json、游戏／模式／角色、步骤、F8 时间标记和脱敏文本日志。360 记录 game.log、配置快照、运行身份及崩溃材料；PS4 记录 run.json、stdout.jsonl、stderr.txt 和准备收据。公开 Issue 不收完整 run-*、配置快照、.dmp 或 *-module.bin；需要完整现场时另行约定私下传输，原始模块快照不进入 Git。按源码提交和程序哈希选择配套符号。

源码测试、机制 fixture、实际游戏和端到端表现各回答不同问题。优先复用未受影响的可靠结果，新增检查针对改动；实际启动和输入操作使用 computer-use。独立测试局的重启和原生确认已获授权。

修改启动器交互后运行 `./scripts/Test-Launcher.ps1 -Jobs 2`。它无需 ROM，在隐藏的临时窗口中走生产鼠标／键盘消息处理、控件与 INI 保存，并启动独立进程检查三语言冷读回。配置／日志的 shell-open 请求在边界截获，不打开外部应用或向桌面注入输入；排版另用 `--render-preview` 与真实窗口截图检查。

修改 PS4 呈现同步后，先运行 `ctest --test-dir out/ps4-gpu --output-on-failure`。
有 Khronos 验证层时，再运行 `out/ps4-gpu/KetsuiGpuRuntimeSmoke.exe --validation --presentation`；
可通过当前进程的 `VK_LAYER_PATH` 指向验证层 manifest 所在目录，无须全局注册。
普通 CTest 默认不开验证层；输出的 `validation=false` 不能算验证层通过。
独立图像读回 API 供组件检查像素与生命周期，正常游戏不调用它。
`out/ps4-native/KetsuiNativeProbe.exe --indexed-draw-self-test out/ps4-gpu/KetsuiGpuRuntime.dll --validation`
检查 GNM → PM4 → Vulkan 索引绘图的实际像素、16/32 位格式和内容更新；需要上述验证层。
不带 `--validation` 的版本加入 `scripts/ps4/run_native_checks.py`，无需 ROM。
`--shared-present-queue` 可检查不具备独立呈现队列时的兼容路径。
诊断会话可显式设置 `EVAC_PS4_TIMING_TRACE=1`，再用
`py -3.12 scripts/ps4/analyze_capture.py <stdout.jsonl> --output <新报告.json>`
分别检查游戏交帧与窗口提交。`consecutive_interval` 只统计连续序号，
`interval` 包含日志序号缺口；缺口本身不是显示丢帧证据。两者均不是物理扫描时间。

G2 的 Novice 遗漏回调在 `recomp/dat163-functions.toml` 声明。重新生成后可运行 `./scripts/Test-G2Callback.ps1`，隔离编译真实生成的转发入口、检查注册和调用 ABI；测试只在 `.local/analysis/g2-callback-fixture/` 产生派生代码，不运行模式或替代模式内短测。

最大往生的只读探针默认关闭，可在独立测试根的宿主命令行显式开启：

- --input_diagnostics=true --sdoj_input_trace=true：宿主键沿、原始按钮、两套玩法掩码生成及复制。
- --sdoj_render_trace=true：逻辑复制、原生缓冲交换与帧等待。
- --sdoj_audio_trace=true：模块命令、引擎队列及播放调用。
- --sdoj_latency_trace=true --output_latency_trace=true：复制、完成缓冲、发布、原生绘图／帧结束 FIFO、原生回调等待、物理 swap 包及同图像的 Present CPU 返回。需要查看方向附近的命令内容时另加 --sdoj_latency_content=true；快照只留独立测试日志，不进入发行包。

这些记录不修改游戏行为。输入复制／缓冲交换的时间不是对应画面的 scanout，音频队列时间不是扬声器时刻。DISPLAY_DIAG 的成功返回若统计全零仍不可用。两项上游降延迟补丁尚未接入；地址版本映射和已有观测见 ANALYSIS.md，不照搬 TU 偏移。

最大往生软件延迟的当前结论见 ANALYSIS.md 的“原生渲染队列与最终输出”：约三帧主要在原作发布／读取顺序和原生帧等待，而不是短暂的最终提交。离线复查使用 `py -3.12 scripts/sdoj_output.py '<run 目录>' --output '<新的报告路径>'`；内容候选使用同参数形式的 scripts/sdoj_content.py。报告记录全部输入日志 SHA-256，拒绝缺失来源和含歧义的关联；需要自机存活的实际画面来限定方向沿语义。不要为重算报告重复启动游戏或把 Present 返回称为物理出光。

[最大往生 Frame Delay 改进](docs/SDOJ_LATENCY_PLAN.md)提供 3V 原路径、2V balanced 与 1V low，不包含 0V。玩家配置为 `[game.sdoj].frame_delay = 3 / 2 / 1`（实际填写一个整数）；v0.6 按玩家实战反馈默认采用 1V，保留已有显式档位。减速统计与训练重入崩溃已修复；两模块、录像、输出来源链、持续输入和音画节奏的实际检查范围见 TEST_MATRIX。默认值修改只需针对配置缺省、模板、迁移和冷启动读回验证，不重跑完整模式或重打冻结发行包。

生产实现分为原生输入前段、提前请求、完整结果交接、有界等待／排空、资源重试中途等待，以及保留原减速公式所读统计年龄的 RenderMetrics；新增地图只在分配时初始化。源码机制测试为 sdoj_frame_delay、sdoj_logic_trace 和 sdoj_tilemap；运行 `./scripts/Test-SdojPrefix.ps1` 可用本地生成物隔离验证两套原生输入前段（需要已安装 SDK，不打开窗口或读取实体设备）。来源分析另列 first_result_* 排除暂停重复画面的年龄。不改全局 SDL、VSync 或音频等待。

无法投递桌面输入时，可在独立离线测试根显式加 `--sdoj_test_pad_file="<已存在的绝对路径>"`。此选项默认空且只在启动时读取，要求 `[community] enabled=false`；它给真实 InputSystem 增加玩家 1 的逻辑 Xbox 按钮源，不注入桌面事件或改写客体内存，不能作为实体键盘／摇杆验收。文件只放一条命令，替换内容时递增正整数序号：`1 press 1000 8` 表示 A 按 8 个 guest frame，按钮是十六进制、时长是十进制 1–600；Start=`10`、B=`2000`、X=`4000`、上／下／左／右=`1/2/4/8`，可按位合并。旧序号忽略，按钮按时自动释放，已确认的脉冲不会被后续命令截断。`2 quit` 通过 UI 线程调用正常的游戏停机与关窗流程；不能直接结束 UI 循环而留下玩法线程。`SDOJ_TEST_PAD` 日志记录命令确认与按下／释放，实际操作仍应结合画面和游戏状态检查。

固定录像的行为对照使用默认关闭的 `--sdoj_logic_trace=true`，与细粒度 latency trace 的预算独立。每个完成点记录复制前 live 输入端口、完整命令缓冲 SHA-256、原生等待深度、核心状态及结束请求，最多每模块加载代 32768 条；录像输入在此复制点之后解码，不能将端口全零解释为录像没有操作。正常模块另记录绘图统计所属的已完成批次，以及减速公式实际读取的操作数，用于定位当前 Arcade HD 的等待深度差异。所有记录只读客体，不改减速公式。`sdoj_logic_trace` CTest 使用自造内存验证观察器；`Test-SdojPrefix.ps1` 使用生产 GuestCall 验证完整上下文和浮点状态恢复。

选择两次相同录像中的明确起始逻辑帧后运行：

```powershell
py -3.12 scripts/sdoj_logic_compare.py <reference-run> <candidate-run> --module 2 --reference-generation <N> --candidate-generation <N> --reference-first <N> --candidate-first <N> --frames <N> --output <new-report.json>
```

窗口必须连续、无缺帧／重复／无效记录；工具不自动对齐或隐藏命令差异，报告绑定原日志哈希。输入和原始命令摘要相同不单独证明像素、音频、完整内存或物理出光一致；实际相同录像的选择、局末状态和延迟另行观察。正确性 trace 与无细粒度诊断的节奏／音画短测分开。

PS4 性能先用无窗口 bench 筛选，再合并有价值的改动做短固定录像回归：

实际游玩卡顿的详细采集可在启动器子进程环境设置 `EVAC_PS4_TIMING_TRACE=1`，
正常使用 `--game ketsui` 启动。该开关不写入玩家配置；新增帧节拍目标／通知、
GPU 工作线程交接、500 微秒以上宿主操作、250 微秒以上绘图／资源访问和 GPU
批次时间戳。沿用有界异步日志，溢出／写盘异常计入 `native_async_log`；GPU 查询
不增加提交或等待。GPU 时间跨度包含依赖和队列空隙，不等于独占执行时间。
Windows WPR 在当前普通权限下返回 0xc5585011，未启用系统调度／实际呈现采集；
不得把这些内部时刻称为实际屏幕显示。F8 标记可回看前 5 秒。
`scripts/ps4/analyze_capture.py <stdout.jsonl> --output <新报告.json>` 汇总最长帧
及标记前后窗口；基础全局帧／音频统计仍用 `analyze_timing.py`。

~~~powershell
py -3.12 scripts/ps4/run_offline_bench.py --output .local/offline-bench/<新目录>
~~~

包含绘图、14／16 任务、连续／60 Hz 派发、预热和重复，--compare 可比较已有 report.json。不要同时编译或运行干扰任务。正常宿主采用继承 CPU 可运行范围，保留六个任务线程和必要同步；pinned 仅用于离线对照。根因与已接受的游戏结果见 docs/PS4_SYNCHRONIZATION.md。

源码、生成声明和 SDK 修复放维护目录，不手改 generated/。提取的上游 PS4 改动须在对应 upstream-files.json 的 localChanges 记录维护哈希；保留来源与许可证。提交前运行相应用例及 scripts/project.py audit。

## 联网

六款游戏共用 [community] 配置，默认 Player、enabled=true、endpoint=https://xiaolu.life/cave/api/v1，各自连接独立 namespace。客户端保留原作的成绩资格、模式、录像版本和格式；不借用另一款的 view ID 或回放合同。

大往生、绊地狱、ESP Ra.De. 使用各自 PS4 规则，G2／最大往生由公共有界 worker 传输。最大往生保留原生商店进度；大往生 CUSTOM 可游玩与能否上榜分开处理。ESP 的 ARCADE／SUPER EASY／ARCADE PLUS／CUSTOM 各有综合及角色榜，OSARAI／いろりの部屋为进度榜。服务测试的自造成绩只发 loopback，不向正式服务写 fixture。

## 出包与交付

两种包共用对应版本的完整运行组件和数据放置说明，ZIP 根目录直接包含启动器：

| 文件名 | 附加内容 |
| --- | --- |
| `EVAC-PC-LABEL-v<版本>-win-x64.zip` | 无游戏数据、源码或符号 |
| `EVAC-PC-LABEL-v<版本>-win-x64-internal.zip` | 按参数选择游戏数据、DLC 和符号，不含源码 |

两种包均不携带 config.ini、user-data、准备镜像、缓存或日志。启动器使用内嵌
默认配置，已有用户文件保留。SOURCE.txt 指向配套源码提交和构建入口；对外发布前
确认接收者能取得该提交及固定依赖，仓库开源操作单独执行。

启动器的 Logo／ICO 在 `launcher/branding/`，来源与哈希由 `launcher/branding.json`
维护，准备说明见该目录的 README。`generate_artwork.py` 将完整透明 Logo 和多尺寸
ICO 嵌入程序，构建显式跟踪资源变更；不要求玩家另放图片。源码审计仅放行清单内
通过结构／哈希核对的三个品牌资源，不扩大为任意二进制例外。

开发根的启动器正在运行时，使用 `Build.ps1 -SkipLauncherStage` 保留窗口，
打包直接取收据绑定的 out/ 程序，检查源文件、工具及配套 PDB 身份。

先提交发行源码并配套构建，再执行：

~~~powershell
./scripts/Release.ps1 -Version 0.7
./scripts/Release.ps1 -Flavor Internal -Version 0.7 -DataGames guwange
./scripts/Release.ps1 -Flavor Internal -Version 0.7 -DataGames all -FutariContent all -Symbols
~~~

默认 Flavor 为 Public。Internal 的 -DataGames 接受游戏 ID 数组或 all；
-FutariContent 接受 black-label、ver101 或 all，须同时选择 futari；
-Symbols 带入匹配 PDB。未指定的内容不包含，选择的内容必须完整。
可用 -DataDirectories 指定数据路径，只有路径而未指定 -DataGames 时选择这些游戏；
-FutariDlcDirectory 指定所选 DLC 的源目录。所有数据按精确清单复制，绊地狱绑定
检测到的 base／update-102 版本，不收录目录中多余的 ISO、工具和补丁包。

启动入口只保留 `EVAC PC LABEL.exe`，选用符号时配套 `EVAC PC LABEL.pdb`。
新包不再复制旧名称的 EXE／PDB；ZIP 覆盖解压不会删除旧目录中已有的文件。

清单 format=3 明确记录 dataGames、futariContent、symbolsIncluded、sourceCommit
和平台。Public 的 game-data 仅允许 `config/game-data/*.txt` 对应的说明；
Internal 另允许所选数据清单。历史 format=1／2 验证保留，新入口不再生成 Update 或
源码附件。冻结文件不改名、不覆盖。

v0.7 profile 包含七款运行组件、七份放置说明及 PS4／XBLA 两个提取脚本；
内部包可用 `-Version 0.7 -DataGames guwange` 仅选狱门山数据，或 `all`
选择全部作品。打包前须完成所选运行组件、启动器及 SDK 的同提交构建认证。
旧版本继续按原游戏集与六份说明验证，不因新增 Guwange 改写历史清单。

每次创建 releases/ 下的新输出目录，保留旧发行。一次公共构建认证之后，每个最终 ZIP 只在新目录解压验收一次：

~~~powershell
py -3.12 scripts/release_package.py verify --directory '<实际解压后的程序包根>'
~~~

记录 ZIP SHA-256、源码提交、步骤和结果，相同冻结文件复用成功记录。在同一
临时目录补充配置、存档和录像哨兵，验证覆盖后保留。verify 命令针对干净解压的
发行载荷，不用于带玩家新增文件的安装目录。两种包的公共程序比较哈希，Internal
另查所选数据、DLC 和符号，不重复已验收的实战或整套环境恢复。

上传前核对本地产物身份，上传后检查远端大小、状态和公网校验文件，不重新下载整包或解压。仅内容变化／失败的部分重验；后续文档和交付记录单独提交，不因它们重打冻结包。

更新说明面向玩家：先写新增内容和体验改善，影响使用的具体问题给出办法，详细测试边界留在开发文档。截图取实际发行启动器并排除其他窗口、账号和本地路径。构建与打包不自动上传或发消息；实际交付按用户指定目标和当时工具要求执行。
