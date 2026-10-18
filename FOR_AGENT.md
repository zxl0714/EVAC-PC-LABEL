# 开发入口

先读 README.md、AGENTS.md、docs/IMPLEMENTATION_PLAN.md 和 docs/TEST_MATRIX.md。P0 与 G2 的 P1 基线已收尾，P2 公共底座已通过；P3 的独立 G2 构建、共享配置、原生启动器与正式联网已接通。当前 P4 已迁入虫姬二维护适配、生成声明及机制回归，完整宿主和八个模式 DLL 已编译，继续实际本体／DLC 游玩回归；绊地狱仍在其后。

用户已经完成同进程重复进退，并接受模式设置现有覆盖，不为这两项重新要求测试或刷分。G2 的维护源码、SDK 和七个模式模块已在合集独立构建并整理到 runtime。原生启动器的真实进关、本地保存和跨进程完整回放、共享 INI 覆盖、完整数据复制和异常退出提示／再启动通过。六种 PLAY 模式均已在合集选人进关并卸载返回。原 Python 专项检查已适配到合集，分析工具不再依赖旧工程。正式 HTTPS 服务已部署；游戏实际成绩上传、榜单读回、网络录像下载落盘及冷启动完整回放通过。实体输入、扩展覆盖及音画回归继续跟踪，不把短测写成整个 P3 通过。当前不打 v0.1 发布标签。

## 目录与数据边界

| 位置 | 职责 |
| --- | --- |
| common/ | 可由启动器与后端共用的 C++ 清单、UTF-8 INI、路径与只读数据检查 |
| common/data_import.* | 显式复制数据的版本检查、独立暂存、取消、全量校验与最终启用 |
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

G2 以 ESPGaluda2-PC 的 ba85ac0 为迁移起点。维护 SDK 在 third_party/rexglue，平台代码在 backends/xbox360，游戏适配、模块声明和工具在 games/espgaluda2；公共 HTTP 在 common/community。tests/fixtures/g2-default.ini 只是旧解析器的回归样本，不是第二份玩家配置。tools/references.json 记录来源，tools/sdk-source.lock.json 保留 SDK 的上游与虫姬二来源和固定依赖版本。

Xbox 360 输入库为 `cave_xbox360_input`，在 backends/xbox360/CMakeLists.txt 定义，使用 `cave::xbox360` 命名空间及公共 `cave::Settings`，不依赖 G2 配置适配器。新 360 游戏先按游戏 ID 解析同一 INI，再把解析结果交给该库；不要复制键盘或 SDL 驱动。现有输入回归同时检查 G2 与虫姬二覆盖字段的隔离，实体设备仍需真实验证。

SDK 依赖通过 `py -3.12 scripts/sdk_sources.py restore` 恢复，再执行 `./scripts/Build-Sdk.ps1 -Jobs 2`。可显式加 `--cache-from '<已有相同布局的源码仓库>'` 加速依赖复制；新 Git 对象不借用旧目录，origin 保持官方地址。共 22 个顶层固定依赖和 3 个嵌套依赖。SDK 已在合集目录独立编译，三个二进制／PDB 对校验通过。构建收据绑定源文件、工具、依赖、生成正文及二进制和 PDB 身份；未成功构建时不会产生成功收据。宿主与模式模块仍需真实运行验收，不要以 SDK 编译成功代替游戏验收。

完整工作在新合集目录进行，旧 G2、虫姬二和绊地狱原目录保持不动。一份 INI 供启动器和游戏共用，公共偏好由适配层应用；不要另写一份 G2 配置，也不要迁入虫姬二的图形设置向导。联网默认开启，昵称 Player；已有文件显式设置的 enabled=false 仍有效，不覆盖旧配置。正式 API 的服务、兼容路由和备份见 server/README.md。

当前有 141 项 Python／CLI 回归；双游戏 native 构建的 18 组 CTest 全部通过，包含新增的只读内容源隔离和同帧键盘采样回归。历史单游戏计数见验收矩阵，不以之代替新增测试。`espgaluda2_community_service_tests --check-endpoint https://xiaolu.life/cave/api/v1` 只读检查正式 HTTPS 榜单，不提交成绩；`--probe` 仍只接受隔离的 loopback 服务。需要联网游玩回归时使用测试昵称与原游戏流程，不用 probe 向正式服务造分。

### 虫姬二接入

来源固定为 7dced597；game-data、旧宿主和图形设置向导没有迁入源码。games/futari 中 audio 保留作用域音频等待与只读追踪，compat 保留模式装卸载适配，community 保留原生协议、录像及待传逻辑，diagnostics 保留存储钩子与诊断，recomp 保存主程序和八个 overlay 的声明。`community_client.h` 仅适配公共 HTTP 类型；不要恢复另一份通信 worker。服务协议源码在 server/games/futari.py，不因源码迁入就重新部署旧服务。

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

公共键盘对完整未采样短按按实际客体 VBlank 保留一个采样帧，同帧重复 GetState 不提前消耗；已采样的长按仍立即松开。SDK 的 guest_input_frame.h 只提供实际 VBlank 推进的原子 epoch，不使用 CPU 频率或固定毫秒延长。输入测试包含同帧千次读取、次帧清除、触发器、立即松开和失焦／面板清理。input_diagnostics=true 仅用于有界事件诊断，不改变采样策略。

直接测试运行时可启动 `runtime/xbox360/espgaluda2.exe --collection_root=<合集根> --game_data_root=<数据目录> --user_data_root=<合集内独立 profile> --game_id=espgaluda2 --launch_contract=1`。数据路径参数是临时覆盖，不写回 INI。共享显示／声音／输入及社区值按公共配置、当前游戏覆盖解析；同一 profile 有进程间排他锁，退出自动释放。日志在 logs/espgaluda2。computer-use 的方向键目前被 SDL 收为小键盘键，自动测试可临时在本地 INI 为四方向增加 Numpad8/2/4/6 别名；结束后恢复，不更改发行模板，也不据此声称实体键盘／手柄已全部实测。

根启动器支持 `--root <合集根> --game espgaluda2 --profile <测试 ID> --data-dir <临时数据目录>`；不指定 game 时打开列表。默认根只取 EXE 所在目录，开发目录内的 out/common/bin 副本需显式传 root。启动前按清单全校验 ROM，并检查 `runtime/games/espgaluda2/runtime-manifest.ini` 记录的配套程序 SHA-256；更新运行文件后执行完整游戏构建／stage，不手改此清单。游戏运行时持有合集级进程锁，profile 另有写入锁。启动器不结束游戏进程，游戏退出后列表仍在。`tests/launch_test.cpp` 仅执行自制辅助程序，覆盖参数转义、中文空格路径、正常／非零／259 退出码与跨进程互斥，不代替真实游戏测试。

界面设计稿在 docs/launcher-design.html，素材来源与哈希在 launcher/artwork.json。`--render-preview <新 PNG 路径>` 输出同一原生绘制器的视觉预览，安装状态明确为示例，不作为实际安装证据。绘制仅在界面改变时进行；窗口使用 Direct2D DC 目标，避免已观测到的 HWND GPU 呈现白屏。宣传素材不属于项目自产代码许可证，正式发布前需核对其分发范围。

“游戏数据…”中的复制操作只写默认 game-data/<id>，拒绝非空目标和输出目录联接。源目录根可以是可解析的别名，内部文件／子目录不能包含链接；复制完整目录（包括额外文件、零字节文件和空目录），逐文件核对内容并复核支持的版本。只有新暂存目录通过全部校验后才启用并更新 config.ini。取消或失败只清理本次随机命名暂存目录；已经验证但配置更新失败的数据保留，并明确报告。窗口关闭会等待复制取消清理，不杀掉后台线程。合集内选择写成相对路径，外部选择保持绝对路径。tests/test_data_import.py 用真实 Windows junction 和自造 main.bin 数据检验边界，不要求 ROM。

启动器先完成五款横幅的视觉方案与自审，不等待用户评审。合集运行与双游戏 SDK、PS4 后端的接入顺序仍按计划推进；首版为三款可玩作品与两款 Coming Soon。最后按同一次已验证构建输出 Internal／Public 两种包，范围与发布门槛见实施计划；出包不自动上传。
