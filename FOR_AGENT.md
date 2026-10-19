# 开发入口

v0.3 正在开发，发行版本仍为 v0.2。启动器三语言及双语标题已接入；`[collection].language` 为 zh／ja／en，旧配置默认 zh。`--language en --render-preview <新 PNG>` 可不改配置渲染对应语言。实际独立窗口已验证语言切换、昵称草稿保留、保存及日文冷启动读回；PS4 窗口已核对日文名，360 还需重建。大往生当前为独立原生入口可玩基线，合集正常入口和社区适配仍待验收，catalogue 保持 Coming Soon、integrated=false、communitySupported=false。

大往生输入为 CUSA32997／01.04，完整目录 `.local/inputs/daioujou-20261020-r2`，原 ZIP 与全部 61 项 CRC／哈希已核验；r1 为失败提取，不可使用。`games/daioujou/data.json` 记录 60 项运行资源，不含 Project.gp4。共享准备入口 `--prepare-game --game daioujou --root <合集> --data-dir <完整目录>` 校验全部资源并准备六程序，首次生成及缓存命中已通过；不套用绊地狱资源补丁。Build／Test 可选 daioujou，运行整理为两款生成各自的宿主别名和证书，共用 GPU。

PS4 缓存按 `recipe.game` 隔离，保存按内容 Title ID 隔离。合并更新目录使用 `--app-dir <目录> --merged-app on`，不叠加 `--patch-dir`。随包未压缩明文 SELF 只作 RET 初始化合同验证，不执行 Sony libc；bcmp、calloc、sincosf、sprintf_s 等由维护运行时实现。独立入口使用 `--background-log on --collection-config <INI> --collection-game daioujou`。最近检查为 344 项源码、66 项原生、15 项自造录像及双标题 HTTP loopback 2 项通过；此前公共三项 CTest 与 43 项 ELF 准备对照另有证据。

实机证据为 `.local/daioujou-entry-r7` 的 ARCADE／Type-A／Shotia 第一关、自机射击得分 90、死亡复活和暂停，以及 r8 冷启动读回 BGM 69%。CU 短按测试显式使用诊断 120 ms，不代替实体输入验收；测试进程均已关闭。`games/daioujou/community-references.json` 记录本作榜单与资格：每组 0–5 为三人偶×两机体、6 为综合；传输整数为显示分数除十。CUSTOM 原生提交排除，黑版／all-mix 限制也须保留。静态分析资源仅在 `.local/daioujou-resource-inspect-r1`，不进入源码或对外包。下一步是原生游戏联网／录像闭环、正常合集入口和发行，不要借用绊地狱的榜单、角色或录像规则。

社区传输、原生 Score bridge、typed Replay worker 和服务存储都显式按标题选择协议与身份，DaiOuJou 不启用 Ketsui whole-round。`run_community_checks.py` 为自造成绩的双服务 Windows HTTP 检查，不访问公网。r6 保存中的真实 420 分记录在 `.local/daioujou-native-record-r1`，原生／服务端结构和元数据校验均通过；容器为 0x10030，core data_version 为 0x10000，实际 region 为 ARCADE_01000。显式允许随输入附带的四个 core revision，不接受未知 core；未执行原生回放。只读命令为 `--community-replay-profile-check daioujou <附件> [board score]`，不执行快照。

当前无窗口链路检查包含原生 ABI 到真实 HTTP 的双标题成绩、个人榜、录像上传／分块下载，`.local/ps4-community-checks/v03-native-replay-bridge-r2` 四项通过；旧绊地狱原生录像链路的定向回归也通过。底层 `--np-replay-profile-smoke <INI> <新绝对输出目录> <自造附件> <游戏ID>` 只允许 literal HTTP loopback，不执行游戏或原生回放。脚本为它生成 9001 分结构，随后提交 9002，核对旧附件传输钉住原 entry；服务会保留同分拒收的回执行，不能用“有效最高分次数”推断数据库行数。r1 汇总错误已保留为失败现场；不是游戏崩溃或原生链路失败。replay_fixture.py 已纳入 PS4 构建输入收据。

大往生服务已独立部署在 ECS 的 b9a8f4f，使用 `daioujou-current`、18088 loopback、`/var/lib/cave-community/daioujou/` 和共享地址下本作路由。五款合集与两条旧入口健康、49 个空榜、未知标题 404、首次备份完整性通过，旧四款 PID 未变；本地证据在 `.local/daioujou-deploy-b9a8f4f-r1`。服务就绪不是游戏内成绩／录像验收。正常合集运行参数已为两款 PS4 统一使用 configured provider，run.json 记录请求、能力与实际开关；大往生 catalogue 能力仍为 false，不会因参数存在而启用联网。

真实联网开发场景为 `.local/daioujou-network-r1`。早先启动被拒绝、未执行；用户再次明确请求后，同一 Start-Process 正常启动，已完成 ARCADE／Type-A／Shotia 第一关和 1780 分自然局末。独立 loopback 收到 board 0 的原生整数 178，但录像在本地结构／元数据检查被拒绝，游戏显示网络错误，尚未完成闭环。不得用接口 fixture 代替修复后的游戏内验收。当前优先处理下面的帧时间问题，再继续该份录像。

大往生第一关 F8 后 10 秒（8609862441413–8609872441413）完成 422 帧，123 个间隔超过 20 ms，最大 100.777 ms；报告在 `.local/analysis/daioujou-network-r1-gameplay-timing.json`。周期分配日志仍使用同步 printf，122 个相邻记录区间包含该日志，其中 120 个为长帧；其余 300 个区间仅 3 个长帧。独立短采样也在游戏提交线程 300 次样本中观察到 88 次 ZwWriteFile。832717c 将这个日志调用改为 NativeLogf，保留内容、频率及丢弃计数，重建和后台队列／默认计数模式检查通过。

`.local/daioujou-perf-r2` 从上述测试 profile 的副本冷启动，Materials Room → Replay Theater → MY REPLAY 的 1780 分／ARCADE／Type-A／Shotia 录像完整播放至自然返回列表。记录 SHA-256 为 e2b10aa8cf3c573c4744b623543ac4506630fc6479971e10a72c64d7650f24e3。第一关 F8 前／后两个连续 10 秒区间各完成 600 帧，没有超过 20 ms 的间隔，最大分别为 19.943／19.388 ms，报告后缀 early-gameplay-timing／gameplay-timing。这里是同一局的实战与冷回放对照，不是逐帧对齐的双版本微基准；确认这条周期同步写入已移出热路径，不宣称所有卡顿消失。真实本地录像播放已通过，在线附件校验失败仍需单独定位。

公共 TraceAllocation 修复也已整理到 runtime/ps4/ketsui.exe，不代表绊地狱小卡顿已验收。其旧 380 分录像的 1901 帧区间中，598 个相邻完成记录区间有分配日志，仅 1 个长帧；其余 1303 个区间有 4 个长帧，包括 1.633 s 间隙。未出现大往生同样的强关联，不能把这些长帧全部判作日志导致；新宿主同录像短回归仍待执行，相关分析见 docs/ANALYSIS.md。

大往生第二份真实网络现场 `.local/daioujou-network-r2` 的 290 显示分／原生 29 在录像上传校验明确返回 inflate_output_limit。只读解析自带结构定义确认本地记录为 0x65600／数据 0x65000，而 system/config/struct_replay.psb 的网络 Struct 为 0x80000／数据 0x78000；ranking_ctrl 的上传和下载均使用后者并按字段复制。客户端和服务端现按精确网络大小校验，仍要求本作 0x10030／0x10000 和角色／模式／成绩，不接受绊地狱记录或较小本地记录。定义及本地冷回放证据在 community-references.json 分开记录；7 项服务协议与 15 项清单／源码边界检查通过，修正后的实机网络回归待执行。

bfc5808 已重建，16 项原生结构检查和 4 项双标题 HTTP 链路通过。`.local/daioujou-network-r3` 的真实 ARCADE／Type-A／Shotia 局末取得 1240 显示分、board 0 原生 124，附件 26,651 字节已通过网络结构但在元数据阶段返回 replay_mode_mismatch，仍无录像落库。后续诊断同时记录有界的头部数值和预期 board／score，不记录快照或按键正文，不放宽元数据检查。只读资源进一步确认原生 set_header 的 NORMAL=0，网络路径仍使用该标识；不能凭错误名猜测需要接受所有 kind。

当前发行版本为 `v0.2`／`562e019`，四款可启动作品为 futari、espgaluda2、ketsui 和 sdoj，均沿用共享 INI 和各自的正式社区服务。SDOJ 四主模式进关、正式成绩／榜单／录像、离线冷回放、全屏和原生商店购买／保存读回已验证；购买后须退出商店等自动保存再关窗。旧三款沿用已验收的工具链回放，仅补必要集成检查，不展开重复实战。四游戏双包实际解压和对应源码附件验收完成；后续文档不改变冻结包。后文阶段性记录不代表当前能力开关，准确信息见 games/catalogue.json 和验收矩阵前部。

先读 README.md、AGENTS.md、docs/IMPLEMENTATION_PLAN.md 和 docs/TEST_MATRIX.md。v0.1 的发行源码为标签 `v0.1`、提交 `f652df0`；Internal／Public 从这一提交的同一套配套构建产生，代表性流程与实际解压已验收。其后的文档提交补充最终记录，不改变已交付 ZIP 或版本标签。调试朋友的 v0.1 日志时，使用对应提交和符号；制作新版本仍需在新提交重建并重新出包。

合集已接通三款游戏、共享 INI、独立存储和正式 HTTPS 社区服务。v0.1 验证保留 45／6／2 对 360／PS4／启动器程序与 PDB 配对，240 项源码回归、50 项 PS4 原生／配置检查，以及实际选人进关、保存重启读回和成绩／录像流程。虫姬二正式服务上传补测及绊地狱启动器实战、新录像冷启动完整播放已补齐；具体新测与沿用范围见验收矩阵，不重跑全模式、重复进入或极限测试。

用户已完成 G2 同进程重复进退，并接受模式设置现有覆盖，不为这些项目再要求测试或刷分。实体设备、更多角色及长期稳定性仍是扩展覆盖；绊地狱退出清理限制、声音响应和偶发卡顿继续保留。编译器统一与虫姬二／绊地狱帧节拍专项属于下一版本，不回填成 v0.1 已修复。

## 目录与数据边界

| 位置 | 职责 |
| --- | --- |
| common/ | 可由启动器与后端共用的 C++ 清单、UTF-8 INI、路径与只读数据检查 |
| common/data_import.* | 显式复制数据的版本检查、独立暂存、取消、全量校验与最终启用 |
| backends/ps4/、third_party/ps4/ | PS4 独立运行时及提取的开源图形组件；不并入 360 SDK |
| launcher/ | Win32／Direct2D 原生启动器、嵌入式宣传素材与来源清单 |
| games/catalogue.json | 六款游戏的顺序、显示名、后端、接入状态与运行组件要求；SDOJ 已接入，大往生和 ESP Ra.De. 为 Coming Soon |
| games/espgaluda2/data.json | G2 的 155 项路径／大小／SHA-256 元数据，无游戏正文 |
| scripts/generate_catalogue.py | 校验元数据并生成构建目录内的原生清单头文件 |
| config/default.ini | 共用配置模板；不含游戏级显示或输入覆盖 |
| scripts/、tools/ | 构建、回归、源码审计、固定工具及来源 |
| .local/、out/、tools/apps/ | 本地现场、产物、工具安装；不进 Git |
| game-data/、prepared/、user-data/、logs/、cache/ | 数据、准备文件与各游戏写入目录；互相隔离，不进 Git |

配置路径以合集根目录为基准，与启动时的工作目录无关。原始数据可以在外部磁盘或目录联接中；读取时解析真实目标，清单中的子路径不能跳出数据目录。存档、日志、缓存与准备目录不能覆盖游戏数据或通过联接重定向到合集之外。检测工具不创建配置、目录或日志，不执行游戏，也不联网。

## 当前构建与测试

需要 Windows x64、PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK。当前公共库不依赖 ReXGlue 或游戏数据。

源码编译使用较短的英文路径，例如 `E:/CaveDev/CAVE-PC-Collection/source`。固定 CMake 4.2.3 在中文源码路径的编译器隐式链接信息解析中会异常退出；内部开发包先整体解压到英文路径即可构建。玩家运行和外部游戏数据仍支持中文路径。当前已统一 LLVM 23.1.3，CMake 中文源码路径限制仍保留。

```powershell
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
py -3.12 scripts/project.py audit
./out/common/bin/cave_inspect.exe --root .
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
```

工具固定在 tools/packages.lock.json。Prepare-Tools.ps1 可显式使用 `-CacheFrom '<缓存的 apps 目录>'` 加速首次准备，之后编译只读取合集自己的 tools/apps，不依赖旧工程存在。普通构建默认低优先级、两个任务，不启动游戏。Build.ps1 默认构建公共组件、测试和原生启动器，`-LauncherOnly` 构建启动器、只读检测工具及必要公共库；成功后复制启动器 EXE 与 PDB 到根目录，并生成 out/launcher/build-receipt.json。若启动器文件未改变，不重复覆盖运行中的同一文件；需要更新时先退出测试窗口。`-Games espgaluda2 -NativeOnly` 构建无需 ROM 的 G2 平台测试／分析工具。游戏编译入口为 `-Games espgaluda2 -DataDirectory '<受支持的 G2 数据目录>'`；调用本工程的 SDK 重新生成主程序和七个 overlay，再编译、校验配套 PDB 并按白名单整理 runtime。使用 `Test.ps1 -Games espgaluda2` 运行合集公共及 G2 平台回归。

未传数据参数时，build_data.py 使用原生 cave_inspect 解析配置，不另写一套 INI 规则。内部包 source/ 优先使用包根唯一配置与 game-data；普通工程使用自身 config.ini，没有配置时才用 game-data/<id>。显式路径始终优先，坏配置不静默回退；必要时仅自动构建无 ROM 的检测工具。Generate.ps1 保留显式数据参数。

### 本地出包入口

Release.ps1 默认 v0.3，固定包含 futari、espgaluda2、ketsui、sdoj、daioujou；联合 360 收据仍要求 espgaluda2／futari／sdoj，两款 PS4 别名来自同一认证宿主和 GPU。当前大往生尚未开放正常入口和联网，打包预检会停止且不创建输出目录，不要修改能力开关以绕过验收。实际验收完成、最终提交后，使用 `Build.ps1 -Games espgaluda2,futari,sdoj,ketsui,daioujou -Jobs 2` 配套构建，再分别执行 `Release.ps1 -Flavor Internal -Version 0.3` 和 `Release.ps1 -Flavor Public -Version 0.3`。SDOJ／大往生只复制清单中的 114／60 个资源，不递归带入 ISO、工具、音乐补丁或 Project.gp4。包生成后须实际解压并执行下面的 verify。旧 v0.1／v0.2 仍按三款／四款版本档校验，冻结文件不重打。

Internal／Public 使用同一 release_package.py，要求工作树干净，SDK、联合 360、PS4、启动器收据配套，所选版本的全部玩家组件属于当前提交且 runtime 证书与产物哈希相符。后续修改源码／文档会使宿主收据失效，应在最终提交后再构建；打包、上传和实际游玩验收分别记录。

`Release.ps1 -Flavor Internal -Version 0.1` 完整复制三款数据和虫姬二两份 DLC、程序、符号及独立 source/.git；可显式用 DataDirectories 与 FutariDlcDirectory 指定输入。源码 Git 不借用作者 objects，origin 为合集 SSH 地址。`-Flavor Public` 不接受数据包含参数，按运行文件白名单单独组装，不复制数据树、源代码、PDB 或用户现场。两者使用默认 INI、玩家说明、许可证及原生 app-local CRT；仅创建 releases/ 下的新目录，不覆盖旧包。

出包生成 release.json、SHA256SUMS.txt、ZIP 与 ZIP.sha256。`py -3.12 scripts/release_package.py verify --directory '<解压目录>'` 只读复核内容、白名单和内部包的必需数据／源码 Git；不刷新索引、不运行游戏或联网。打包脚本不自动上传或发消息。v0.1 最终 ZIP 的实际解压和轻量验收已完成；后续版本须重新验证，不沿用旧 ZIP 的结果冒充新包通过。

源码附件入口为 `Release-Source.ps1 -BinaryDirectory '<同版本程序包目录>' -OutputDirectory '<releases 下的新目录>'`，底层为 scripts/source_package.py。它先验证已有程序包、干净 HEAD 与 sourceCommit，再复用 source_clone、sdk_sources、ps4_sources 建立独立源码和全部固定依赖，不包含 ROM、生成正文、工具安装或本机现场。`py -3.12 scripts/source_package.py verify --archive '<源码 ZIP>'` 流式复核成员、大小与哈希；正式交付仍需实际解压，核对 Git 和依赖。source-release.json 保存二进制 release.json 的 SHA-256，用于配套追溯。自造 ZIP 测试不代表真正的 v0.3 附件已经生成。

对外程序包不内嵌源码，另备同版本 `CAVE-PC-Collection-v0.1-source.zip`，由包提供者按需提供；不要求接收者先获得私有仓库权限。该附件含独立主仓库、25 个 SDK 与 19 个 PS4 固定依赖源码、清单及构建说明，不含 ROM、生成游戏正文、已安装工具或玩家数据。开发工具通过 Prepare-Tools.ps1 恢复。实际全新英文目录 clone、独立工具／PS4 依赖恢复、单独绊地狱和启动器构建、运行文件整理、配套符号及两组 CTest 已通过。

## 检测语义

默认快速检查只做存在性与大小，不声称哈希已验证。`--programs` 另校验关键程序，`--full` 校验全部资源。报告将数据状态与后端接入状态分开：未迁入的游戏即使资源完整也不是 ready；Coming Soon 不触发运行。程序错误输出 JSON 并返回 2；正常检测返回 0，是否可用以各游戏的 state 为准。

清单是维护源码，原生头文件是生成产物。修改 games 元数据后正常 build，不手改 out/common/include/catalogue_generated.h，也不把 ROM、派生游戏 C++、安装工具、日志、存档或凭据提交。源码审计同时检查 Git 已跟踪和未忽略的新文件；P3 迁入 SDK 时扩展固定子模块检查并保留来源、许可证及双方修复。

## 后续接入

最大往生的源码／输入评估已开始，游戏 ID 预留为 sdoj，不与 daioujou 混用。`scripts/sdoj_inputs.py '<本地解包目录>' --output .local/analysis/sdoj-inputs.json` 只识别本体／TU 组合；games/sdoj/input-references.json 不是完整数据清单，不接入生成、构建或启动器。后续阶段见实施计划 P7。上游应用许可尚未明确，没有导入其 src 或生成游戏正文，也未覆盖公共 SDK。

games/sdoj/data.json 已单独记录经 ISO 内容比对的 114 个光盘本体文件，inputs.json 记录三个 XEX 头；不包含附带工具、系统更新或音乐补丁。Build.ps1／Generate.ps1 支持 sdoj，模块声明在 games/sdoj/recomp/sdoj.toml；CA022100.bin 和 CA022110.bin 各自生成／编译 DLL，不能改成 .xex 名字或合并同基址的映射。共享 CollectionApp 负责宿主，不导入上游配置向导、减速／输入增强或自动更新器。首次正式构建、11 对 PE／PDB 及 runtime 整理已通过，实战仍待验收；清单保持 integrated=false、comingSoon=false，用于检查版本而不开放启动器启动。

本体主程序在 recomp/default-functions.toml 声明 63 个已审阅入口。初始化转发、比较函数和条件尾调用均已在真实重启中越过，随后在音频设备初始化后的浮点函数 0x8217F850 缺注册处退出。已结合接口指针表集中审阅另外 21 个含浮点、存储或虚调用的入口，再由正常生成器翻译，待实际验证；不改原指令或跳过调用。callback_inventory.py 的 codePointerRuns 用于定位带已注册入口的连续指针组，但异常表也会有相似结构，不能自动视为 vtable。只读 CFG 类别仍拒绝直接存储等指令；它只是保守审阅工具，不是函数全集。

communitySupported 是每游戏的运行时能力，与共享 INI 的 enabled 偏好分开；SDOJ 暂为 false，其他已接入作品默认 true。用户现要求 SDOJ 社区／Live 也纳入 v0.2，需先查明本作原生榜单、成绩与录像协议再接独立服务，不借用 G2 的 view 或数据格式。未支持时不创建通信 provider，run.json 分别记录请求、支持和实际开启状态，宿主保留离线 profile 和共享昵称。不为此在默认 INI 增加联网覆盖。同步检查本体商店和隐藏内容，音乐补丁继续暂缓。
免费且通过游玩解锁的内容按用户要求保留原生进度，由玩家自行解锁；不要赠送金币、默认全开或改存档标志。重点确认原生解锁与保存路径工作正常。
SDOJ 原生协议已从本体调用和实际 Xbox 360／Shot 结算交叉确认：普通个人榜单共 39 个 view，六个原生属性；总分为 20000004，时间为 20000002，不能沿用 G2 的属性编号。录像使用 304 字节头、01304250 magic、+92 总长度和 +8 加权 BE16 校验；原始时间低 DWORD 在前，整个 signed64 属性必须保留，不按 G2 的 FILETIME 解释。原生顺序为成绩、录像、仅 10008011 的大小确认；最后一步不是新成绩。团队评分的额外 view 未适配，不套用个人榜。
读取列号也不能直接取写入 property 的低 16 位：8212CB40–8212CBCC 的原生读取器要求列 1 为原位形时间（64 位），列 2／4／5／6 分别为角色／关卡／续关／选项（32 位），分数取 row.rating。错误地将时间放在列 2，会显示 1601 年日期，并在下载后的角色预览中越界。514f079 已修复并完成真实旧成绩读回、正确日期、下载后保存及新进程离线完整回放至自然返回；本机 9,286 分的原生联网闭环通过。正式 HTTPS 游玩、商店购买保存、启动器和发行仍待验收，不提前开放正式能力。
共享的有界 native_service worker 负责 HTTP、昵称 token、成绩引用及原字节录像传输；G2 和 SDOJ 分别提供 schema／view／元数据校验。`tests/run_native_score_replay.py --game sdoj --program <sdoj_community_tests.exe>` 和 espgaluda2 对应入口只使用自造数据、临时 SQLite 和随机 loopback 端口，不能向正式服务写分。`sdoj_community_tests --inspect-replay <文件>` 只读检查本地录像。实际游戏验证可显式传 `--sdoj_community_test_endpoint=http://127.0.0.1:<端口>/cave/api/v1`；默认空，不改变公开能力标记，日志明确为待验收的开发 provider。上线验收前 communitySupported 仍为 false。
c3d0bbe 的 306 项源码和 18 组原生 CTest（含两款 loopback）通过；22cc51d 的宿主／符号及 runtime 整理通过。实机测试的自动启动命令被工具策略拒绝，没有启动游戏，也没有改走其他通道。现场服务已停止、根 INI 临时方向别名撤回；后续先处理该启动限制，再做原生提交／下载实战，不把已通过的传输 fixture 记为联网验收。旧三款实战受影响回归及四游戏发行仍待完成。
SDOJ 服务已从 af116e0 源码独立部署到 `/opt/cave-community/releases/af116e0`，`sdoj-current` 指向该目录；18087 仅监听 loopback，数据库／备份位于 `/var/lib/cave-community/sdoj/`。服务器 Python 3.10 的 35 项服务／备份／部署测试、四款合集和两条旧入口的 HTTPS 健康、SDOJ 空榜和未知游戏 404、首次备份完整性通过。G2 `current` 仍为 60f9d3d，旧三款 PID 未改变，不重启或搬迁其他游戏。`sdoj_community_tests --check-endpoint <HTTPS API 根>` 是只读传输检查，`--probe` 只用于独立 loopback 自造数据；两者都不是实际游戏验收。
0619bd6 无 ROM 重建后，两款原生只读 HTTPS 检查和 6 组 community CTest 通过，完整源码检查为 309 项。服务已就绪，但自动游戏启动限制仍未解除；不换通道重试被拒绝的启动，不把服务健康或原生传输 helper 当成实机通过。仍缺原生游戏提交／下载及冷启动回放、商店购买保存、启动器 SDOJ 实际启动、旧三款定向实战与四游戏包验收；不能只完成服务器就标记 v0.2。
本体已审阅五个静态 Xbox API 入口，见 community-overrides.toml／community/guest_hooks.cpp；SDOJ 原调用包含同步 stats write、排名估计和录像 IO，9a9b68d 已为公共桥接增加空 OVERLAPPED 支持，同步调用在客体调用线程有界等待，不占共享 IO 调度队列。新 community_sync 原生用例通过同步读写／枚举／估计／录像、错误传播、旧异步路径及同时取消两项同步请求后的迟到回调检查，不执行游戏或访问网络。296 项源码检查、11 项 SDK 和 14 项宿主增量构建、12 对 PE／PDB 及五组原生 CTest 通过。未完成游戏专属属性／录像格式和服务前，communitySupported 继续为 false。测试用的根 INI 小键盘方向别名已撤回，原哈希 0c58f876 已恢复。

81fd6ce 已实际运行到 Xbox 360／Shot 玩家第一关并创建 SYSTEM.DAT；原默认渲染拒绝 UI 的 kInvalidTexture 描述符，因此通过命令行对照确认 gpu_allow_invalid_fetch_constants=true 恢复标题和菜单。此兼容开关现由 SDOJ 的 OnGamePreSetup 设置，共享宿主只提供钩子，不把游戏条件写进公共平台；后续需验证无额外参数重启、模式切换及存档／录像。音频有非静音输出，不等于声音响应验收。data.json 的 forbiddenFiles 禁止本体目录出现 default.xexp、CA022100.binp、CA022110.binp；代码生成和原生启动检查均拒绝，不删除或改写原文件。

d7c4058 已无额外 GPU 参数重启读入 SYSTEM.DAT，并经 Arcade HD／Type-C／Shot／Auto Bomb OFF 进入第一关；随后局末附近缺注册入口 0x880943E8 退出。两个 overlay 现各有独立 *-functions.toml：CA022100 的 0x880943E8 和 CA022110 的 0x880952F0 都是 4 字节跳转，转向已有正常实现；源码修复待实际回归。callback_inventory.py 也报告代码构造的地址，但其中可能是异常继续点而非独立函数，不能把候选批量作为已审阅入口。

后续 749cd9b 实际回归已越过该崩溃点：Arcade HD 自然结算、保存槽位 1、返回模式待机及主菜单通过，REPLAY_XB01.DAT 为 79,328 字节。随后 Ver.1.5 自然结束／返回、Novice 玩家第一关通过；冷启动完整回放和设置读回尚未验收。输入操作的截图在按键后可能仍是过渡帧，先重新观察，不盲目重复确认以免启动下一局。当前 UI 预览已按六张卡片自适应，SDOJ 方形标题采用 contain，其他作品保持 cover。

v0.2 必须完成最大往生可玩接入，不能仅发布工具链和 UI 改进。本体的静态兼容检查使用 `scripts/sdoj_analyze.py --data-dir '<本地解包目录>' --output .local/analysis/sdoj-census`；先 NativeOnly 构建原生检查工具，过程不需要 G2 ROM。实现共用 backends/xbox360/diagnostics/inspect_xex.cpp，保留 espgaluda2_inspect.exe 名称；SDOJ 的两个 .bin 按 XEX 处理，检查重叠基址装卸载。它不应用 TU、不执行客体或证明可玩。音乐补丁按用户要求暂不处理。

代码生成的 TOML 路径现按 UTF-8 转换，包含入口、game_root、模块、includes 和输出；Windows 原生流读取声明，避免系统代码页解释。实际原中文目录、中文／emoji 输出及 include 的生成结果，与英文路径试验的 76 个 C++／头文件逐字节相同。原生 CTest codegen_paths 另用自造无效 XEX 检查中文输入／include 的路径走到静态装载器后按预期拒绝，不执行客体，也不是游戏生成通过。含非 BMP 字符的数据根在额外开发试验仍失败，不将现有结果泛化为所有 Unicode 路径。

静态模块、回调、日志和 G2 社区边界的检查方法见 [分析与诊断工具](docs/ANALYSIS.md)，其中列出原 66 项 Python 回归的迁移对应。工具显式读取数据目录，报告及解压区段只写新的 .local 路径；检查结果不代替实际进关和声音验收。

G2 以 ESPGaluda2-PC 的 ba85ac0 为迁移起点。维护 SDK 在 third_party/rexglue，平台代码在 backends/xbox360，游戏适配、模块声明和工具在 games/espgaluda2；公共 HTTP 在 common/community。tests/fixtures/g2-default.ini 只是旧解析器的回归样本，不是第二份玩家配置。docs/SOURCE_ORIGINS.json 记录来源，docs/provenance/sdk-import-baseline.json 保存历史 SDK 导入身份与文件哈希；两者仅供追溯，依赖恢复不读取这些历史记录。tools/sdk-source.lock.json 只固定当前第三方依赖，工具目录用法见 tools/README.md。

Xbox 360 输入库为 `cave_xbox360_input`，在 backends/xbox360/CMakeLists.txt 定义，使用 `cave::xbox360` 命名空间及公共 `cave::Settings`，不依赖 G2 配置适配器。新 360 游戏先按游戏 ID 解析同一 INI，再把解析结果交给该库；不要复制键盘或 SDL 驱动。现有输入回归同时检查 G2 与虫姬二覆盖字段的隔离，实体设备仍需真实验证。

SDK 依赖通过 `py -3.12 scripts/sdk_sources.py restore` 恢复，再执行 `./scripts/Build-Sdk.ps1 -Jobs 2`。可显式加 `--cache-from '<已有相同布局的源码仓库>'` 加速依赖复制；新 Git 对象不借用旧目录，origin 保持官方地址。共 22 个顶层固定依赖和 3 个嵌套依赖。SDK 已在合集目录独立编译，三个二进制／PDB 对校验通过。构建收据绑定源文件、工具、依赖、生成正文及二进制和 PDB 身份；未成功构建时不会产生成功收据。宿主与模式模块仍需真实运行验收，不要以 SDK 编译成功代替游戏验收。

完整工作在新合集目录进行，旧 G2、虫姬二和绊地狱原目录保持不动。一份 INI 供启动器和游戏共用，公共偏好由适配层应用；不要另写一份 G2 配置，也不要迁入虫姬二的图形设置向导。联网默认开启，昵称 Player；已有文件显式设置的 enabled=false 仍有效，不覆盖旧配置。正式 API 的服务、兼容路由和备份见 server/README.md。

当前有 229 项 Python／CLI 回归。PS4 的六对程序／PDB、49 项原生／配置检查、35 项镜像准备检查、94 项资源准备检查及 5 项真实 GPU 自制场景检查通过；双游戏 360 的 18 组 CTest 另行保持回归，不混计成游戏进关。`espgaluda2_community_service_tests --check-endpoint https://xiaolu.life/cave/api/v1` 只读检查正式 HTTPS 榜单，不提交成绩；`--probe` 仍只接受隔离的 loopback 服务。需要联网游玩回归时使用测试昵称与原游戏流程，不用 probe 向正式服务造分。

### 虫姬二接入

来源固定为 7dced597；game-data、旧宿主和图形设置向导没有迁入源码。games/futari 中 audio 保留作用域音频等待与只读追踪，compat 保留模式装卸载适配，community 保留原生协议、录像及待传逻辑，diagnostics 保留存储钩子与诊断，recomp 保存主程序和八个 overlay 的声明。`community_client.h` 仅适配公共 HTTP 类型；不要恢复另一份通信 worker。服务协议源码在 server/games/futari.py，不因源码迁入就重新部署旧服务。

`scripts/futari/local_service.py` 为真实游戏提供独立 loopback 现场，方法见 docs/ANALYSIS.md。新社区 journal 绑定游戏和原端点，改昵称／build 后仍使用原请求正文；端点变更、其他游戏及没有 origin 的旧 journal 保留而不自动发送，不能通过补填当前 endpoint 猜测迁移。该保护不改变线上协议。日志分析的 guestAudio 与输出队列分开，保留追踪作用域、有效玩法探针及丢弃计数；已有证据只有 Black Label 死亡片段，没有道具拾取或扬声器发声时刻证据。

```powershell
./scripts/Generate.ps1 -Game futari -DataDirectory '<受支持的数据目录>'
./scripts/Build.ps1 -Games futari -NativeOnly -Jobs 2
./scripts/Test.ps1 -Games futari
./scripts/Build.ps1 -Games espgaluda2,futari -NativeOnly -Jobs 2
./scripts/Test.ps1 -Games espgaluda2,futari
```

生成器支持嵌套的 media/Module 路径，逐个隔离生成 overlay，保留每个模块自己的 includes 和 31 个主程序回调。只生成到 ignored generated/futari，不读取旧工程的生成正文。完整构建使用 `./scripts/Build.ps1 -Games futari -DataDirectory '<本体数据目录>' -Jobs 2`；同时构建两款时可传 `-Games espgaluda2,futari -DataDirectories @{espgaluda2='<G2 数据>';futari='<虫姬二本体>'}`。单游戏输出在 out/<id>，同时构建输出在 out/xbox360；构建、生成收据与 runtime 整理均校验选择的游戏集合。native 构建无需 ROM，不能代替实际游玩。原生联网回归使用 tests/futari/run_native_community.py 创建的临时 loopback 服务和临时数据，包含损坏录像及重试故障注入，不读取旧存档或向正式服务提交。公网接口传入此测试程序会被拒绝。

两款宿主继承 backends/xbox360/host.* 的 CollectionApp，共用启动合同、INI、路径与 profile 锁、输入、全屏、计时和诊断；各自 host.cpp 只接入社区与游戏特有生命周期。元数据头按游戏独立生成。崩溃 writer 为同一份平台源码，但保留 espgaluda2_crash_writer.exe／futari_crash_writer.exe 两个文件名，避免单独构建另一款时破坏已配套的文件哈希。schema=2 的报告记录真实 game 和 build，不能把 G2 身份写入虫姬二报告。

虫姬二的 `[game.futari].dlc_dir` 指向保留原内容结构的独立 DLC 根，缺失时允许启动本体。ContentManager 在启动客体线程前注册只读 Marketplace 内容源，DLC 不复制到 profile；SaveGame 仍写合集 profile 下的 `<XUID>/435A07D7/00000001/`。创建、头部／缩略图改写、安装及删除 DLC 均拒绝，正常关闭挂载不受影响。content_source 原生回归只使用自造临时数据；实际 DLC 识别、保存及重启读回分别见验收矩阵。

`games/futari/data.json` 的 optionalContent 单独记录 Black Label 和 Ver1.01 的完整包及头部元数据，不并入本体必需文件。InspectContent 按包返回状态；只有 Full 的 verified 结果才加入 SDK 的明确包白名单，空白名单不能退回全目录枚举，直接 Open／Resolve／Exists 也受同一限制。缺少或损坏一个包不阻塞可用的另一个包及本体。源根允许外部目录别名，包内附加文件及链接被拒绝。未配置 dlc_dir 时使用所选数据目录下的 dlc；显式空值关闭附加内容。cave_inspect 的 optionalContent 与 run.json 记录状态、检查数量及原因，快速检查不声称已校验哈希。

真实本体 52 项及 DLC 两包 7 项全量校验通过。缺少或显式关闭 DLC 时，宿主使用 prepared/futari/no-content 的空白名单只读视图，不回退读取旧 profile 中的 Marketplace 包；显式关闭分支已实际验证 Black Label 不可选、Ver1.01 不出现、本体仍可进关。包部分损坏的隔离仍以自造数据回归为当前证据，尚未做真实游戏菜单对照。

公共键盘对完整未采样短按按实际客体 VBlank 保留一个采样帧，同帧重复 GetState 不提前消耗；已采样的长按仍立即松开。SDK 的 guest_input_frame.h 只提供实际 VBlank 推进的原子 epoch，不使用 CPU 频率或固定毫秒延长。输入测试包含同帧千次读取、次帧清除、触发器、立即松开和失焦／面板清理。input_diagnostics=true 仅用于有界事件诊断，不改变采样策略。

直接测试运行时可启动 `runtime/xbox360/espgaluda2.exe --collection_root=<合集根> --game_data_root=<数据目录> --user_data_root=<合集内独立 profile> --game_id=espgaluda2 --launch_contract=1`。数据路径参数是临时覆盖，不写回 INI。共享显示／声音／输入及社区值按公共配置、当前游戏覆盖解析；同一 profile 有进程间排他锁，退出自动释放。日志在 logs/espgaluda2。computer-use 的方向键目前被 SDL 收为小键盘键，自动测试可临时在本地 INI 为四方向增加 Numpad8/2/4/6 别名；结束后恢复，不更改发行模板，也不据此声称实体键盘／手柄已全部实测。

根启动器支持 `--root <合集根> --game espgaluda2 --profile <测试 ID> --data-dir <临时数据目录>`；不指定 game 时打开列表。默认根只取 EXE 所在目录，开发目录内的 out/common/bin 副本需显式传 root。启动前按清单全校验 ROM，并检查 `runtime/games/espgaluda2/runtime-manifest.ini` 记录的配套程序 SHA-256；更新运行文件后执行完整游戏构建／stage，不手改此清单。游戏运行时持有合集级进程锁，profile 另有写入锁。启动器不结束游戏进程，游戏退出后列表仍在。`tests/launch_test.cpp` 仅执行自制辅助程序，覆盖参数转义、中文空格路径、正常／非零／259 退出码与跨进程互斥，不代替真实游戏测试。

界面设计稿在 docs/launcher-design.html，素材来源与哈希在 launcher/artwork.json。`--render-preview <新 PNG 路径>` 输出同一原生绘制器的视觉预览，安装状态明确为示例，不作为实际安装证据。绘制仅在界面改变时进行；窗口使用 Direct2D DC 目标，避免已观测到的 HWND GPU 呈现白屏。宣传素材不属于项目自产代码许可证，正式发布前需核对其分发范围。

“游戏数据…”中的复制操作只写默认 game-data/<id>，拒绝非空目标和输出目录联接。源目录根可以是可解析的别名，内部文件／子目录不能包含链接；复制完整目录（包括额外文件、零字节文件和空目录），逐文件核对内容并复核支持的版本。只有新暂存目录通过全部校验后才启用并更新 config.ini。取消或失败只清理本次随机命名暂存目录；已经验证但配置更新失败的数据保留，并明确报告。窗口关闭会等待复制取消清理，不杀掉后台线程。合集内选择写成相对路径，外部选择保持绝对路径。tests/test_data_import.py 用真实 Windows junction 和自造 main.bin 数据检验边界，不要求 ROM。

启动器五款横幅的视觉方案与自审已完成；首版为三款可玩作品与两款 Coming Soon。Internal／Public 从同一次已验证构建输出，范围与发布门槛见实施计划；出包不自动上传。

v0.1 的最终 Internal 群文件、版本通知及实际启动器截图交付已完成，证据单独保存在本地；不要再次上传或发送。后续交付仍需用户明确指定范围和目标，构建／出包不自动发布，实际操作遵循工具要求。

### 启动器快捷设置与选择

开发分支使用唯一的底部启动按钮，横幅只选择作品。快速检测仅针对所选游戏，完整校验仍在 PrepareLaunch；切换选择发生于检测中时，完成旧请求后刷新最新选择，不用另一款游戏的结果启用按钮。

共享快捷设置调用 ResolveSharedSettings／SaveSharedPreferences，不能把当前游戏的覆盖当成共享默认。common/config_update.* 提供按字段保留文本的编辑及带原文比较的原子替换，数据目录选择也复用这一实现。未来新增可编辑项继续沿用公共解析与这一份 INI。昵称仍按 1–64 UTF-8 字节校验；游戏级覆盖不删除，选中时提示。

启动器自审预览为 --render-preview；实际全屏选项／昵称保存和进程重开读回、Tab 焦点、Coming Soon 与无 ROM 选择状态已经通过 CU。具体范围见验收矩阵，不能把这些界面检查写成新的三款实战验收。

### 绊地狱接入

版本范围：v0.1 已交付。开发工具锁已统一为版本化 LLVM 23.1.3，通过 toolchain.py 为两边提供各自驱动和相同来源的符号／链接工具；Prepare-Tools.ps1 的 -Ps4 为兼容别名。SDK／宿主／GPU／模块及启动器完整重建、符号和三款代表性旧录像回放已通过，不更改已冻结 v0.1 ZIP 和标签。虫姬二与绊地狱已有短区间帧时间基线；绊地狱常态诊断热路径改为有界后台写入，已做同份录像对照，具体结果见验收矩阵。G2 的约 19.1 ms 仍是特定短测的最大出帧间隔，不是输入延迟或统一硬门槛。

固定来源 9501378a 已按 `scripts/import_ketsui_sources.py` 迁入：运行时在 backends/ps4，提取的 GCN／驻留 GPU 组件和许可证在 third_party/ps4；原生 ELF／时序／接口工具在 scripts/ps4，游戏资源读取覆盖在 scripts/ketsui。PS4 CMake 使用合集路径、本地 clang-cl 23、独立依赖及优化构建；旧 launcher 和发行脚本不迁入，游戏已接通公共启动合同并开放启动器入口。

```powershell
./scripts/Prepare-Tools.ps1 -Ps4
py -3.12 scripts/ps4_sources.py restore
py -3.12 scripts/ps4_sources.py verify
./scripts/Build.ps1 -Games ketsui -NativeOnly -Jobs 2
./scripts/Test.ps1 -Games ketsui
py -3.12 scripts/ps4_build_receipt.py verify
py -3.12 -m unittest discover -s tests -p test_ketsui_python.py -v
py -3.12 scripts/ps4/analyze_timing.py '<已有 stdout.jsonl 或现场目录>' --output .local/analysis/ketsui-timing.json
```

19 个依赖从上游恢复到 .deps/ps4，可选缓存用法见 tools/README.md。输出在 out/ps4-native 和 out/ps4-gpu，out/ps4/build-receipt.json 记录输入、工具及六对 PE／PDB 的 GUID 和 age；其中 KetsuiResourcePrepareCheck 仅用于自造数据对照，不进入玩家程序包。修改提取的上游文件时，在对应 upstream-files.json 的 localChanges 明确记录维护哈希；383 项当前文件在构建前后核对。`Build.ps1 -Games ketsui -Jobs 2` 无需 ROM，完成后由 stage_ps4.py 校验收据并整理 runtime/ps4/ketsui.exe、GPU DLL、符号和运行文件证书，不启动游戏。PS4 的 NativeOnly 同样只构建运行时；原始数据在首次启动时原生准备。

三份已适配的 Python 套件通过 `tests/test_ketsui_python.py` 纳入 Source 检查；`scripts/ps4/run_native_checks.py` 在校验配套构建后运行 36 项机制、15 项真实配置文件和 2 项终止日志检查，共 53 项，结果及逐项输出保存到新的 .local/ps4-native-checks 目录。GPU 的五项 CTest 另用自制资源检查绘制、缓存及窗口呈现，含垂直同步开启／关闭。其余导入测试和二进制自制媒体 fixture 的扩展按实际需要单独记录来源／哈希。

PS4 与启动器、360 后端编译同一份 common 配置库，不重新实现 INI 规则。`backends/ps4/collection_config.*` 将共用按钮位置映射为 PS4 按钮：Select 为 Touchpad，Start 为 Options，肩键、扳机与摇杆按下均保留。SDL 开关与径向死区、PC 总音量、启动全屏、显示同步和清单中的游戏标题在客体运行前应用；PC 音量与游戏内逐通道音量叠乘。两条 Vulkan 呈现路径都按请求选择 FIFO／Immediate／Mailbox，客体显示时钟不变；GPU API 现为版本 5，宿主与 DLL 必须配套重建。

只读检查当前配置可用 `./out/ps4-native/KetsuiNativeProbe.exe --collection-config-check ./config.ini`，不启动 SDL、音频、GPU 或社区 worker，不写配置。开发执行入口接受 `--collection-config <同一份 config.ini>`，与旧 `--input-config` 互斥；旧参数仅保留给机制诊断，不是第二份玩家配置。公共 `/cave/api/v1` 自动映射到 `/cave/api/v1/games/ketsui`，也兼容既有 Ketsui 专属地址；其他游戏路径被拒绝。真实 ARCADE 进关与正式成绩／录像上传、启动器和重启检查的范围分别记录在验收矩阵中，不把自造检查或演示画面当作玩家进关。

`games/ketsui/data.json` 保存 CUSA09062 本体及 1.02 补丁的 59 个必需文件、大小和 SHA-256；零字节的 CUSA09062.complete 提取标记不是必需资源。本体与补丁保持原目录结构，启动前检查数据及 runtime 文件证书。可重新只读盘点：`py -3.12 scripts/ps4_inventory.py --game-data '<数据根>' --output .local/analysis/<新文件>.json`。

ELF 镜像准备的原生实现位于 `backends/ps4/loader/elf_prepare.*`，按既有装载合同处理段、导入／导出、重定位、弱符号及 libc 的 RET 验证，不执行原始程序。诊断入口为 `KetsuiNativeProbe.exe --prepare-elf <输入 ELF 绝对路径> --root <合集根绝对路径> --output <新输出绝对路径> [--module] [--libc <libc 绝对路径>]`。输出仅允许在 .local 或 prepared/ketsui 下的新文件，拒绝链接、数据目录重叠、ADS 和相对组件，不覆盖现有文件。无需 Python；Public 包实际解压／首次使用仍属于发布验收，不能只凭单项准备结果关闭。

`loader/prepare_game.cpp` 使用 prepared_cache.* 的不可变代和 active.json 指针。配方绑定全部受支持数据、准备选项及准备 EXE 的 SHA-256；缓存命中时复核文件清单、大小和内容哈希，失败则保留旧现场并新建一代，不回退运行不匹配的旧镜像。整套预检可用 `runtime/ps4/ketsui.exe --prepare-game --root <合集绝对路径> [--data-dir <数据绝对路径>]`，不执行客体，不创建 profile 或日志。

`collection_launch.*` 接受公共五参数合同，使用 GameSession 和 profile 写入锁，自动准备后调用既有原生执行路径。日志在 logs/ketsui/run-UTC-PID，包含 run.json、config.snapshot.ini、preparation.json、stdout.jsonl 与 stderr.txt；两条日志允许运行中读取。正常关窗的现有退出码 27 不改成 0：仅在呈现清理完成后写 exit.ini，启动器用 PID、进程创建时间、原因及状态核对后恢复列表，不误报崩溃；其他非零退出仍警告。未完成的客体终止清理继续作为已知限制。

合集执行客体前启动 `native_async_log.*`。GPU 提交、宿主慢操作及 NGS2／WASAPI 热路径使用完整记录的非阻塞投递；`diagnostics/async_log_queue.h` 固定容量，磁盘慢写不占用游戏线程或音频图锁。`native_async_log` 累计损失／写入失败和最后排空状态不可省略。终止路径必须在 `_lock_file(stdout)` 之前停止并排空队列，再直接输出最终诊断；锁独立异常处理器不等待此 worker。独立机制命令保留同步默认行为。分析范围及固定录像路径见 docs/ANALYSIS.md。

CU 的毫秒短按无法触发街机投币时，可仅为本次启动器进程设置环境变量 `CAVE_DIAGNOSTIC_INPUT_TAP_MS=120`，桥接既有原生按键延长诊断；允许 0–500，未设置或 0 时完全不启用。五参数合同及玩家 INI 不变，`run.json` 的 `diagnosticInputTapMs` 明确记录取值；启动后恢复开发 shell 的环境变量，不用于声称默认物理输入已测。`--collection-input-self-test` 无 ROM／窗口地检查解析边界，实际游戏仍必须由 CU 操作投币、选机与进关。

`scripts/ps4/run_prepare_checks.py` 通过 27 组自造 ELF 和 8 组真实路径检查，使用 Python 实现逐字节对照；由 Test.ps1 的 Ketsui 分支一并执行。`py -3.12 scripts/ketsui/verify_preparation.py --game-data '<数据根>'` 另在新的 .local 现场对照实际 eboot／两个 PRX，并做无客体入口的装载校验。生成文件含派生游戏正文，只留本地；Python 工具是开发侧对照实现，不是 Public 包的玩家启动依赖。实际玩家进关、保存与录像结果另见验收矩阵。

原生资源准备位于 `backends/ps4/content/resource_prepare.*`，入口为 `KetsuiNativeProbe.exe --prepare-fixes <数据根> --root <合集根绝对路径> --output <新 .ktfix 绝对路径> [--community-replay] [--community-offline]`；offline 自动包含 replay。它从已验证的本地 eboot 读取种子，解析 MDF／PSB 并生成只读覆盖，保留 BGM 自动保存、录像批量提交和局末本地采集修复，不改写归档或发送成绩。合集运行时还需配套启用原生 rounds producer，不能只打开资源脚本开关。

`run_resource_checks.py` 运行 82 项自造数据对照和 12 项输出／参数边界检查；检查程序与报告不属于 Public 运行链。`verify_preparation.py` 同时对照实际三种覆盖组合的完整文件及元数据，再用 ContentView 装载验证，并核对全部 59 个原始文件前后不变。Python 仍只作为开发 oracle，玩家侧资源准备调用原生代码。
