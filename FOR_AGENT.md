# 开发入口

这是同一启动器下的多游戏工程。当前发行版为 **v0.3**，源码固定在标签 v0.3／b8807ca；五款游戏已经接入。源码来自本仓库维护代码与固定开源依赖，不包含 ROM 或生成的游戏程序正文。

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

游戏 ID 为 futari、espgaluda2、ketsui、sdoj、daioujou。最大往生是 sdoj，大往生是 daioujou。ESP Ra.De. 暂未接入。

原始数据只读。写入目录与原数据隔离，不借用旧项目的工具安装、构建产物或个人 profile。

## 准备环境

使用 Windows x64、PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK。源码放较短的英文目录，例如 E:/CaveDev/CAVE-PC-Collection；固定 CMake 的中文源码路径问题尚在，玩家包和游戏数据可使用中文路径。

~~~powershell
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
~~~

公共组件和启动器不需要 ROM 或先构建游戏 SDK。工具版本锁在 tools/packages.lock.json，统一使用 LLVM 23.1.3；两个后端保留各自的驱动形式和 ABI 参数。

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

对应源码附件已经包含固定 SDK／PS4 依赖。可用 Prepare-Tools.ps1 的 -CacheFrom 和 ps4_sources.py 的 --cache-from 显式复用本机缓存；缓存只加速恢复，最终源码和 Git 对象仍自包含。

## 配置与数据

开发版配置位于 `user-data/config.ini`；`EnsureConfig` 首次启动原样迁移旧根目录文件，新位置已存在时优先使用它。只读工具通过 `ConfigPath`／`project.config_path` 查找，不执行迁移。配置内的相对路径始终相对合集根目录，不依赖工作目录或 INI 所在目录。共用 fullscreen、vsync、音量、键盘、SDL 手柄、昵称和联网选项；游戏覆盖优先，但默认模板不为每个游戏重复一套通用设置。

运行时只读同一份 config.ini；source/ 位于内部包内时，构建的数据解析优先使用包根的同一配置和 game-data。显式数据参数优先，坏配置不静默回退。

数据布局和版本见 README 与各 games/<id>/data.json。检查现有目录：

~~~powershell
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
~~~

快速检查只查存在性和大小；--full 校验资源 SHA-256，--programs 校验运行程序。检测工具只读、不运行游戏或联网。Coming Soon 与数据是否齐全是不同状态。

最大往生使用受支持的光盘本体，不混入 default.xexp、.binp 或音乐补丁。两款 PS4 作品首次运行由原生准备链生成自己的 prepared/<id>/ 缓存，不修改原数据；大往生为 CUSA32997／01.04 合并目录，绊地狱为本体加 1.02 补丁的双目录。

## 编译与运行

单独编译游戏，显式数据目录可省略以使用配置中的路径：

~~~powershell
./scripts/Build.ps1 -Games espgaluda2 -DataDirectory 'D:/Games/G2' -Jobs 2
./scripts/Build.ps1 -Games @('espgaluda2','futari','sdoj','ketsui','daioujou') -Jobs 2
./scripts/Test.ps1 -Scope Native -Games @('espgaluda2','futari','sdoj','ketsui','daioujou')
~~~

Xbox 360 的 -NativeOnly 构建无 ROM 的机制／分析工具，不代替游戏生成。PS4 宿主本身无须 ROM 即可编译，玩家数据在启动时准备。默认两个任务、低优先级，构建不自动启动游戏。

输出包含独立宿主、模块和 GPU 组件，不追求把所有内容塞进一个 EXE。Build.ps1 完成符号配套和 runtime 整理；不要从旧工程手工拼 DLL，也不要只复制单个 EXE。

正式游戏使用启动器，开发可选择独立 profile：

~~~powershell
./CAVE-PC-Collection.exe --game espgaluda2 --profile test
~~~

GUI 和命令行共用 PrepareLaunch 的完整检查。两款 PS4 运行程序为 runtime/ps4/ketsui.exe、daioujou.exe，共用配套 GPU；GPU API 当前为 7，宿主和 DLL 必须来自同次认证构建。

收据检查入口：

~~~powershell
py -3.12 scripts/build_receipt.py verify-host --games espgaluda2,futari,sdoj
py -3.12 scripts/ps4_build_receipt.py verify
py -3.12 scripts/launcher_receipt.py verify
~~~

联合 360 收据在 out/xbox360/bin/build-receipt.json，PS4 与启动器分别在 out/ps4/、out/launcher/。收据绑定源码、工具、依赖、程序和 PDB；改动后按影响范围重建。重现发行版时从对应标签建立独立 checkout，保留已冻结包。

## 排查与验证

朋友反馈先取 release.json、游戏／模式／角色、步骤、F8 时间标记和完整 run-* 日志。360 记录 game.log、配置快照、运行身份及崩溃材料；PS4 记录 run.json、stdout.jsonl、stderr.txt 和准备收据。按源码提交和程序哈希选择配套符号。

源码测试、机制 fixture、实际游戏和端到端表现各回答不同问题。优先复用未受影响的可靠结果，新增检查针对改动；实际启动和输入操作使用 computer-use。独立测试局的重启和原生确认已获授权。

修改启动器交互后运行 `./scripts/Test-Launcher.ps1 -Jobs 2`。它无需 ROM，在隐藏的临时窗口中走生产鼠标／键盘消息处理、控件与 INI 保存，并启动独立进程检查三语言冷读回。配置／日志的 shell-open 请求在边界截获，不打开外部应用或向桌面注入输入；排版另用 `--render-preview` 与真实窗口截图检查。

最大往生的只读探针默认关闭，可在独立测试根的宿主命令行显式开启：

- --input_diagnostics=true --sdoj_input_trace=true：宿主键沿、原始按钮、两套玩法掩码生成及复制。
- --sdoj_render_trace=true：逻辑复制、原生缓冲交换与帧等待。
- --sdoj_audio_trace=true：模块命令、引擎队列及播放调用。
- --sdoj_latency_trace=true --output_latency_trace=true：复制、完成缓冲、发布、原生绘图／帧结束 FIFO、原生回调等待、物理 swap 包及同图像的 Present CPU 返回。需要查看方向附近的命令内容时另加 --sdoj_latency_content=true；快照只留独立测试日志，不进入发行包。

这些记录不修改游戏行为。输入复制／缓冲交换的时间不是对应画面的 scanout，音频队列时间不是扬声器时刻。DISPLAY_DIAG 的成功返回若统计全零仍不可用。两项上游降延迟补丁尚未接入；地址版本映射和已有观测见 ANALYSIS.md，不照搬 TU 偏移。

最大往生软件延迟的当前结论见 ANALYSIS.md 的“原生渲染队列与最终输出”：约三帧主要在原作发布／读取顺序和原生帧等待，而不是短暂的最终提交。离线复查使用 `py -3.12 scripts/sdoj_output.py '<run 目录>' --output '<新的报告路径>'`；内容候选使用同参数形式的 scripts/sdoj_content.py。报告记录全部输入日志 SHA-256，拒绝缺失来源和含歧义的关联；需要自机存活的实际画面来限定方向沿语义。不要为重算报告重复启动游戏或把 Present 返回称为物理出光。

正在实现 [最大往生 Frame Delay 改进](docs/SDOJ_LATENCY_PLAN.md)：3V 原路径 → 2V 稳定路径 → 1V 低延迟路径，不包含 0V。1V 参考虫姬二“玩法完成后再交给渲染器”的交接原则，保留最终节拍、原作减速和录像语义。玩家配置为 `[game.sdoj].frame_delay = 3 / 2 / 1`（实际填写一个整数），内部策略称 original／balanced／low；开发宿主接受三档、缺省仍为 3。两模块进关／返回、旧录像、新录像冷读回、逐逻辑帧和输出来源链均已有实际结果；关闭细粒度延迟／音频追踪后的节奏与队列对照也已完成，保留逻辑观察器以界定关卡。普通键盘短按、存活自机移动、失焦恢复及同段音画观察已按用户要求暂缓至 v0.4 待办，当前不启动游戏或重复请求手动操作。短测通过后确认发布默认 1V，保留 2V／3V，再核对配置和冷启动读回；本次只更新计划，不改默认。不要重跑已接受的完整录像或扩展全模式。完整身份、初始化类别与比较边界见 TEST_MATRIX。

生产实现分为原生输入前段、提前请求、完整结果交接、有界等待／排空、资源重试中途等待，以及保留原减速公式所读统计年龄的 RenderMetrics；新增地图只在分配时初始化。源码机制测试为 sdoj_frame_delay、sdoj_logic_trace 和 sdoj_tilemap；运行 `./scripts/Test-SdojPrefix.ps1` 可用本地生成物隔离验证两套原生输入前段（需要已安装 SDK，不打开窗口或读取实体设备）。来源分析另列 first_result_* 排除暂停重复画面的年龄。不改全局 SDL、VSync 或音频等待，本任务不出包。

无法投递桌面输入时，可在独立离线测试根显式加 `--sdoj_test_pad_file="<已存在的绝对路径>"`。此选项默认空且只在启动时读取，要求 `[community] enabled=false`；它给真实 InputSystem 增加玩家 1 的逻辑 Xbox 按钮源，不注入桌面事件或改写客体内存，不能作为实体键盘／摇杆验收。文件只放一条命令，替换内容时递增正整数序号：`1 press 1000 8` 表示 A 按 8 个 guest frame，按钮是十六进制、时长是十进制 1–600；Start=`10`、B=`2000`、X=`4000`、上／下／左／右=`1/2/4/8`，可按位合并。旧序号忽略，按钮按时自动释放，已确认的脉冲不会被后续命令截断。`2 quit` 通过 UI 线程调用正常的游戏停机与关窗流程；不能直接结束 UI 循环而留下玩法线程。`SDOJ_TEST_PAD` 日志记录命令确认与按下／释放，实际操作仍应结合画面和游戏状态检查。

固定录像的行为对照使用默认关闭的 `--sdoj_logic_trace=true`，与细粒度 latency trace 的预算独立。每个完成点记录复制前 live 输入端口、完整命令缓冲 SHA-256、原生等待深度、核心状态及结束请求，最多每模块加载代 32768 条；录像输入在此复制点之后解码，不能将端口全零解释为录像没有操作。正常模块另记录绘图统计所属的已完成批次，以及减速公式实际读取的操作数，用于定位当前 Arcade HD 的等待深度差异。所有记录只读客体，不改减速公式。`sdoj_logic_trace` CTest 使用自造内存验证观察器；`Test-SdojPrefix.ps1` 使用生产 GuestCall 验证完整上下文和浮点状态恢复。

选择两次相同录像中的明确起始逻辑帧后运行：

```powershell
py -3.12 scripts/sdoj_logic_compare.py <reference-run> <candidate-run> --module 2 --reference-generation <N> --candidate-generation <N> --reference-first <N> --candidate-first <N> --frames <N> --output <new-report.json>
```

窗口必须连续、无缺帧／重复／无效记录；工具不自动对齐或隐藏命令差异，报告绑定原日志哈希。输入和原始命令摘要相同不单独证明像素、音频、完整内存或物理出光一致；实际相同录像的选择、局末状态和延迟另行观察。正确性 trace 与无细粒度诊断的节奏／音画短测分开。

PS4 性能先用无窗口 bench 筛选，再合并有价值的改动做短固定录像回归：

~~~powershell
py -3.12 scripts/ps4/run_offline_bench.py --output .local/offline-bench/<新目录>
~~~

包含绘图、14／16 任务、连续／60 Hz 派发、预热和重复，--compare 可比较已有 report.json。不要同时编译或运行干扰任务。正常宿主采用继承 CPU 可运行范围，保留六个任务线程和必要同步；pinned 仅用于离线对照。根因与已接受的游戏结果见 docs/PS4_SYNCHRONIZATION.md。

源码、生成声明和 SDK 修复放维护目录，不手改 generated/。提取的上游 PS4 改动须在对应 upstream-files.json 的 localChanges 记录维护哈希；保留来源与许可证。提交前运行相应用例及 scripts/project.py audit。

## 联网

五款游戏共用 [community] 配置，默认 Player、enabled=true、endpoint=https://xiaolu.life/cave/api/v1，各自连接独立 namespace。客户端保留原作的成绩资格、模式、录像版本和格式；不借用另一款的 view ID 或回放合同。

大往生、绊地狱使用各自 PS4 规则，G2／最大往生由公共有界 worker 传输。最大往生保留原生商店进度；大往生 CUSTOM 可游玩与能否上榜分开处理。服务测试的自造成绩只发 loopback，不向正式服务写 fixture。

## 出包与交付

v0.3.1 的覆盖更新使用 `./scripts/Release.ps1 -Flavor Update -Version 0.3.1`。
ZIP 直接包含根目录程序与 runtime，无外层目录，不携带 config.ini、user-data、
游戏数据、准备镜像、缓存、日志、源码或符号。先关闭程序，直接解压到 v0.3
目录替换同名文件；首次启动自动迁移旧配置。发布前只做一次实际覆盖验收，
核对配置、存档／录像哨兵保持不变及配套程序身份。对应源码仍可单独出包。
开发根的启动器正在运行时，使用 `Build.ps1 -SkipLauncherStage` 保留窗口，
Update 包直接取收据绑定的 out/ 程序，仍检查源文件、工具及配套 PDB 身份。

先提交发行源码并配套构建，再执行：

~~~powershell
./scripts/Release.ps1 -Flavor Internal -Version 0.3
./scripts/Release.ps1 -Flavor Public -Version 0.3
./scripts/Release-Source.ps1 -BinaryDirectory '<已生成的程序包目录>' -OutputDirectory '<releases 下的新目录>'
~~~

Internal 需要全部支持数据和虫姬二两份 DLC，可用 -DataDirectories 与 -FutariDlcDirectory 指定目录。最大往生／大往生按 114／60 项资源清单复制，排除 ISO、工具、音乐补丁和 Project.gp4。Public 按运行白名单组装，源码附件不含 ROM，并绑定程序包的 release.json。

每次创建 releases/ 下的新输出目录，保留旧发行。一次公共构建认证之后，每个最终 ZIP 只在新目录解压验收一次：

~~~powershell
py -3.12 scripts/release_package.py verify --directory '<实际解压后的程序包根>'
py -3.12 scripts/source_package.py verify --archive '<对应源码 ZIP>'
~~~

源码附件还检查实际解压的独立 Git 与固定依赖。记录 ZIP SHA-256、源码提交、步骤和结果；相同冻结文件复用成功记录。两个程序包的公共运行文件比较哈希，选一份做小而完整的启动回归；Internal 另外检查数据、DLC、源码和实际启动器，不重复五款实战或整套环境恢复。

上传前核对本地产物身份，上传后检查远端大小、状态和公网校验文件，不重新下载整包或解压。仅内容变化／失败的部分重验；后续文档和交付记录单独提交，不因它们重打冻结包。

更新说明面向玩家：先写新增内容和体验改善，影响使用的具体问题给出办法，详细测试边界留在开发文档。截图取实际发行启动器并排除其他窗口、账号和本地路径。构建与打包不自动上传或发消息；实际交付按用户指定目标和当时工具要求执行。
