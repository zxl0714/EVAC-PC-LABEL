# 开发入口

先读 README.md、AGENTS.md、docs/IMPLEMENTATION_PLAN.md 和 docs/TEST_MATRIX.md。P0 与 G2 的 P1 基线已收尾，P2 公共底座已通过；P3 的独立 G2 构建、共享配置、原生启动器与正式联网已接通。P4 双游戏完整构建及虫姬二主要本地闭环已有验证，专项回归继续跟踪；P5 的 PS4 宿主、驻留 GPU 和共享 INI 适配已在新目录编译并通过机制检查，下一步接通准备数据和合集启动合同。

干净联合构建 6b47555 的 out/xbox360 有 45 对程序／PDB，153 项源码回归和 18 组 CTest 通过，虫姬二 integrated=true。启动器实际启动 Black Label／Xbox 360 两次进关并分别卸载返回，F11 全屏及 Alt+Enter 切回窗口、正常退出后恢复启动器均通过。显式关闭 DLC 的另一进程读回既有系统存档，本体 Ver1.5／Arcade／Original／Palm Normal 选人、存活玩家第一关、暂停及卸载返回通过。八模块已有入口记录，本体／DLC 冷启动录像回放和 G2 新宿主回归见验收矩阵；其余模块重复进入按用户要求暂缓，不阻塞当前迁移推进，也不标成通过。本机隔离服务的真实成绩／录像上传、榜单读回及下载保存已通过；6b47555 的 run-20261019T013724950Z-76988 将下载槽 No.4 冷启动完整播放并自然返回，容器哈希不变、退出码 0。正式 HTTPS、声音响应及实体输入继续验证。Ver1.01 Xbox 360 尚缺存活玩家截图证据，不以 PLAYER1 开场及续关画面替代。

用户已经完成同进程重复进退，并接受模式设置现有覆盖，不为这两项重新要求测试或刷分。G2 的维护源码、SDK 和七个模式模块已在合集独立构建并整理到 runtime。原生启动器的真实进关、本地保存和跨进程完整回放、共享 INI 覆盖、完整数据复制和异常退出提示／再启动通过。六种 PLAY 模式均已在合集选人进关并卸载返回。原 Python 专项检查已适配到合集，分析工具不再依赖旧工程。正式 HTTPS 服务已部署；游戏实际成绩上传、榜单读回、网络录像下载落盘及冷启动完整回放通过。实体输入、扩展覆盖及音画回归继续跟踪，不把短测写成整个 P3 通过。当前不打 v0.1 发布标签。

## 目录与数据边界

| 位置 | 职责 |
| --- | --- |
| common/ | 可由启动器与后端共用的 C++ 清单、UTF-8 INI、路径与只读数据检查 |
| common/data_import.* | 显式复制数据的版本检查、独立暂存、取消、全量校验与最终启用 |
| backends/ps4/、third_party/ps4/ | 正在接入的 PS4 独立运行时及提取的开源图形组件；不并入 360 SDK |
| launcher/ | Win32／Direct2D 原生启动器、嵌入式宣传素材与来源清单 |
| games/catalogue.json | 五款游戏的顺序、显示名、后端、接入状态与运行组件要求 |
| games/espgaluda2/data.json | G2 的 155 项路径／大小／SHA-256 元数据，无游戏正文 |
| scripts/generate_catalogue.py | 校验元数据并生成构建目录内的原生清单头文件 |
| config/default.ini | 共用配置模板；不含游戏级显示或输入覆盖 |
| scripts/、tools/ | 构建、回归、源码审计、固定工具及来源 |
| .local/、out/、tools/apps/ | 本地现场、产物、工具安装；不进 Git |
| game-data/、prepared/、user-data/、logs/、cache/ | 数据、准备文件与各游戏写入目录；互相隔离，不进 Git |

配置路径以合集根目录为基准，与启动时的工作目录无关。原始数据可以在外部磁盘或目录联接中；读取时解析真实目标，清单中的子路径不能跳出数据目录。存档、日志、缓存与准备目录不能覆盖游戏数据或通过联接重定向到合集之外。检测工具不创建配置、目录或日志，不执行游戏，也不联网。

## 当前构建与测试

需要 Windows x64、PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK。当前公共库不依赖 ReXGlue 或游戏数据。

```powershell
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
py -3.12 scripts/project.py audit
./out/common/bin/cave_inspect.exe --root .
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
```

工具固定在 tools/packages.lock.json。Prepare-Tools.ps1 可显式使用 `-CacheFrom '<缓存的 apps 目录>'` 加速首次准备，之后编译只读取合集自己的 tools/apps，不依赖旧工程存在。普通构建默认低优先级、两个任务，不启动游戏。Build.ps1 默认构建公共组件、测试和原生启动器，`-LauncherOnly` 只编译启动器及必要公共库；成功后复制 EXE 与 PDB 到根目录。若启动器文件未改变，不重复覆盖运行中的同一文件；需要更新时先退出测试窗口。`-Games espgaluda2 -NativeOnly` 构建无需 ROM 的 G2 平台测试／分析工具。游戏编译入口为 `-Games espgaluda2 -DataDirectory '<受支持的 G2 数据目录>'`；调用本工程的 SDK 重新生成主程序和七个 overlay，再编译、校验配套 PDB 并按白名单整理 runtime。使用 `Test.ps1 -Games espgaluda2` 运行合集公共及 G2 平台回归。Release.ps1 尚未实现。

## 检测语义

默认快速检查只做存在性与大小，不声称哈希已验证。`--programs` 另校验关键程序，`--full` 校验全部资源。报告将数据状态与后端接入状态分开：未迁入的游戏即使资源完整也不是 ready；Coming Soon 不触发运行。程序错误输出 JSON 并返回 2；正常检测返回 0，是否可用以各游戏的 state 为准。

清单是维护源码，原生头文件是生成产物。修改 games 元数据后正常 build，不手改 out/common/include/catalogue_generated.h，也不把 ROM、派生游戏 C++、安装工具、日志、存档或凭据提交。源码审计同时检查 Git 已跟踪和未忽略的新文件；P3 迁入 SDK 时扩展固定子模块检查并保留来源、许可证及双方修复。

## 后续接入

静态模块、回调、日志和 G2 社区边界的检查方法见 [分析与诊断工具](docs/ANALYSIS.md)，其中列出原 66 项 Python 回归的迁移对应。工具显式读取数据目录，报告及解压区段只写新的 .local 路径；检查结果不代替实际进关和声音验收。

G2 以 ESPGaluda2-PC 的 ba85ac0 为迁移起点。维护 SDK 在 third_party/rexglue，平台代码在 backends/xbox360，游戏适配、模块声明和工具在 games/espgaluda2；公共 HTTP 在 common/community。tests/fixtures/g2-default.ini 只是旧解析器的回归样本，不是第二份玩家配置。docs/SOURCE_ORIGINS.json 记录来源，docs/provenance/sdk-import-baseline.json 保存历史 SDK 导入身份与文件哈希；两者仅供追溯，依赖恢复不读取这些历史记录。tools/sdk-source.lock.json 只固定当前第三方依赖，工具目录用法见 tools/README.md。

Xbox 360 输入库为 `cave_xbox360_input`，在 backends/xbox360/CMakeLists.txt 定义，使用 `cave::xbox360` 命名空间及公共 `cave::Settings`，不依赖 G2 配置适配器。新 360 游戏先按游戏 ID 解析同一 INI，再把解析结果交给该库；不要复制键盘或 SDL 驱动。现有输入回归同时检查 G2 与虫姬二覆盖字段的隔离，实体设备仍需真实验证。

SDK 依赖通过 `py -3.12 scripts/sdk_sources.py restore` 恢复，再执行 `./scripts/Build-Sdk.ps1 -Jobs 2`。可显式加 `--cache-from '<已有相同布局的源码仓库>'` 加速依赖复制；新 Git 对象不借用旧目录，origin 保持官方地址。共 22 个顶层固定依赖和 3 个嵌套依赖。SDK 已在合集目录独立编译，三个二进制／PDB 对校验通过。构建收据绑定源文件、工具、依赖、生成正文及二进制和 PDB 身份；未成功构建时不会产生成功收据。宿主与模式模块仍需真实运行验收，不要以 SDK 编译成功代替游戏验收。

完整工作在新合集目录进行，旧 G2、虫姬二和绊地狱原目录保持不动。一份 INI 供启动器和游戏共用，公共偏好由适配层应用；不要另写一份 G2 配置，也不要迁入虫姬二的图形设置向导。联网默认开启，昵称 Player；已有文件显式设置的 enabled=false 仍有效，不覆盖旧配置。正式 API 的服务、兼容路由和备份见 server/README.md。

当前有 222 项 Python／CLI 回归。PS4 的五对程序／PDB、49 项原生／配置检查及 5 项真实 GPU 自制场景检查通过；双游戏 360 的 18 组 CTest 另行保持回归，不混计成游戏进关。`espgaluda2_community_service_tests --check-endpoint https://xiaolu.life/cave/api/v1` 只读检查正式 HTTPS 榜单，不提交成绩；`--probe` 仍只接受隔离的 loopback 服务。需要联网游玩回归时使用测试昵称与原游戏流程，不用 probe 向正式服务造分。

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

启动器先完成五款横幅的视觉方案与自审，不等待用户评审。合集运行与双游戏 SDK、PS4 后端的接入顺序仍按计划推进；首版为三款可玩作品与两款 Coming Soon。最后按同一次已验证构建输出 Internal／Public 两种包，范围与发布门槛见实施计划；出包不自动上传。

### 绊地狱接入

固定来源 9501378a 已按 `scripts/import_ketsui_sources.py` 迁入：运行时在 backends/ps4，提取的 GCN／驻留 GPU 组件和许可证在 third_party/ps4；原生 ELF／时序／接口工具在 scripts/ps4，游戏资源读取覆盖在 scripts/ketsui。PS4 CMake 已改为合集独立路径，使用本地 clang-cl 23、独立依赖及优化构建；旧 launcher 和发行脚本不迁入，游戏入口仍为 integrated=false。

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

19 个依赖从上游恢复到 .deps/ps4，可选缓存用法见 tools/README.md。输出在 out/ps4-native 和 out/ps4-gpu，out/ps4/build-receipt.json 记录输入、工具及五对 PE／PDB 的 GUID 和 age。修改提取的上游文件时，在对应 upstream-files.json 的 localChanges 明确记录维护哈希；383 项当前文件在构建前后核对。NativeOnly 不要求 ROM，也不启动游戏；省略该选项的 Ketsui 完整准备流程仍待接通。

三份已适配的 Python 套件通过 `tests/test_ketsui_python.py` 纳入 Source 检查；`scripts/ps4/run_native_checks.py` 在校验配套构建后运行 34 项机制和 15 项真实配置文件检查，结果及逐项输出保存到新的 .local/ps4-native-checks 目录。GPU 的五项 CTest 另用自制资源检查绘制、缓存及窗口呈现，含垂直同步开启／关闭。其余导入测试将在保存、联网与媒体接线时逐项适配；二进制自制媒体 fixture 尚未导入，恢复时单独记录来源／哈希。

PS4 与启动器、360 后端编译同一份 common 配置库，不重新实现 INI 规则。`backends/ps4/collection_config.*` 将共用按钮位置映射为 PS4 按钮：Select 为 Touchpad，Start 为 Options，肩键、扳机与摇杆按下均保留。SDL 开关与径向死区、PC 总音量、启动全屏、显示同步和清单中的游戏标题在客体运行前应用；PC 音量与游戏内逐通道音量叠乘。两条 Vulkan 呈现路径都按请求选择 FIFO／Immediate／Mailbox，客体显示时钟不变；GPU API 现为版本 5，宿主与 DLL 必须配套重建。

只读检查当前配置可用 `./out/ps4-native/KetsuiNativeProbe.exe --collection-config-check ./config.ini`，不启动 SDL、音频、GPU 或社区 worker，不写配置。开发执行入口接受 `--collection-config <同一份 config.ini>`，与旧 `--input-config` 互斥；旧参数仅保留给机制诊断，不是第二份玩家配置。公共 `/cave/api/v1` 自动映射到 `/cave/api/v1/games/ketsui`，也兼容既有 Ketsui 专属地址；其他游戏路径被拒绝。此次通过的是配置解析、原生应用和自造场景，不是合集真实进关或联网游玩验收；启动合同、数据准备、常态日志分级与真实流程仍待接入。
