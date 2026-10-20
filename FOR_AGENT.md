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
| config/default.ini | 唯一玩家 INI 的默认模板 |
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

配置路径始终相对合集根目录，不依赖工作目录。共用 fullscreen、vsync、音量、键盘、SDL 手柄、昵称和联网选项；游戏覆盖优先，但默认模板不为每个游戏重复一套通用设置。

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

最大往生的只读探针默认关闭，可在独立测试根的宿主命令行显式开启：

- --input_diagnostics=true --sdoj_input_trace=true：宿主键沿、原始按钮、两套玩法掩码生成及复制。
- --sdoj_render_trace=true：逻辑复制、原生缓冲交换与帧等待。
- --sdoj_audio_trace=true：模块命令、引擎队列及播放调用。

这些记录不修改游戏行为。输入复制／缓冲交换的时间不是对应画面的 scanout，音频队列时间不是扬声器时刻。DISPLAY_DIAG 的成功返回若统计全零仍不可用。两项上游降延迟补丁尚未接入；地址版本映射和已有观测见 ANALYSIS.md，不照搬 TU 偏移。

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
