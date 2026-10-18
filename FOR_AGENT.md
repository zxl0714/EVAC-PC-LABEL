# 开发入口

先读 README.md、AGENTS.md、docs/IMPLEMENTATION_PLAN.md 和 docs/TEST_MATRIX.md。P0 与 G2 的 P1 基线已收尾；P2 的公共清单、路径、INI 和无窗口检测已建立。下一步是 P3：迁入 G2 的维护 SDK／宿主与游戏适配，接通共享配置，并按视觉方案实现原生启动器。

用户已经完成同进程重复进退，并接受模式设置现有覆盖，不为这两项重新要求测试或刷分。G2 的维护源码、SDK 和七个模式模块已在合集独立构建并整理到 runtime。原生启动器已实际启动 Black Label／Seseri 第一关并保存新录像，重启后完整回放返回列表；共享 INI 覆盖、完整数据复制和异常退出提示／再启动通过。其他模式和原 G2 Python 专项测试迁移仍需完成，不能把单模式进关写成整个 P3 通过。当前不打 v0.1 发布标签。

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

G2 以 ESPGaluda2-PC 的 ba85ac0 为迁移起点。维护 SDK 在 third_party/rexglue，平台代码在 backends/xbox360，游戏适配、模块声明和工具在 games/espgaluda2；公共 HTTP 在 common/community。tests/fixtures/g2-default.ini 只是旧解析器的回归样本，不是第二份玩家配置。tools/references.json 记录来源，tools/sdk-source.lock.json 保留 SDK 的上游与虫姬二来源和固定依赖版本。

SDK 依赖通过 `py -3.12 scripts/sdk_sources.py restore` 恢复，再执行 `./scripts/Build-Sdk.ps1 -Jobs 2`。可显式加 `--cache-from '<已有相同布局的源码仓库>'` 加速依赖复制；新 Git 对象不借用旧目录，origin 保持官方地址。共 22 个顶层固定依赖和 3 个嵌套依赖。SDK 已在合集目录独立编译，三个二进制／PDB 对校验通过。构建收据绑定源文件、工具、依赖、生成正文及二进制和 PDB 身份；未成功构建时不会产生成功收据。宿主与模式模块仍需真实运行验收，不要以 SDK 编译成功代替游戏验收。

完整工作在新合集目录进行，旧 G2、虫姬二和绊地狱原目录保持不动。一份 INI 最终供启动器和游戏共用，公共偏好由适配层应用；不要另写一份 G2 配置，也不要迁入虫姬二的图形设置向导。当前模板联网关闭，正式合集 API 部署后再开启发行默认值。

直接测试运行时可启动 `runtime/xbox360/espgaluda2.exe --collection_root=<合集根> --game_data_root=<数据目录> --user_data_root=<合集内独立 profile> --game_id=espgaluda2 --launch_contract=1`。数据路径参数是临时覆盖，不写回 INI。共享显示／声音／输入及社区值按公共配置、当前游戏覆盖解析；同一 profile 有进程间排他锁，退出自动释放。日志在 logs/espgaluda2。computer-use 的方向键目前被 SDL 收为小键盘键，自动测试可临时在本地 INI 为四方向增加 Numpad8/2/4/6 别名；结束后恢复，不更改发行模板，也不据此声称实体键盘／手柄已全部实测。

根启动器支持 `--root <合集根> --game espgaluda2 --profile <测试 ID> --data-dir <临时数据目录>`；不指定 game 时打开列表。默认根只取 EXE 所在目录，开发目录内的 out/common/bin 副本需显式传 root。启动前按清单全校验 ROM，并检查 `runtime/games/espgaluda2/runtime-manifest.ini` 记录的配套程序 SHA-256；更新运行文件后执行完整游戏构建／stage，不手改此清单。游戏运行时持有合集级进程锁，profile 另有写入锁。启动器不结束游戏进程，游戏退出后列表仍在。`tests/launch_test.cpp` 仅执行自制辅助程序，覆盖参数转义、中文空格路径、正常／非零／259 退出码与跨进程互斥，不代替真实游戏测试。

界面设计稿在 docs/launcher-design.html，素材来源与哈希在 launcher/artwork.json。`--render-preview <新 PNG 路径>` 输出同一原生绘制器的视觉预览，安装状态明确为示例，不作为实际安装证据。绘制仅在界面改变时进行；窗口使用 Direct2D DC 目标，避免已观测到的 HWND GPU 呈现白屏。宣传素材不属于项目自产代码许可证，正式发布前需核对其分发范围。

“游戏数据…”中的复制操作只写默认 game-data/<id>，拒绝非空目标和输出目录联接。源目录根可以是可解析的别名，内部文件／子目录不能包含链接；复制完整目录（包括额外文件、零字节文件和空目录），逐文件核对内容并复核支持的版本。只有新暂存目录通过全部校验后才启用并更新 config.ini。取消或失败只清理本次随机命名暂存目录；已经验证但配置更新失败的数据保留，并明确报告。窗口关闭会等待复制取消清理，不杀掉后台线程。合集内选择写成相对路径，外部选择保持绝对路径。tests/test_data_import.py 用真实 Windows junction 和自造 main.bin 数据检验边界，不要求 ROM。

启动器先完成五款横幅的视觉方案与自审，不等待用户评审。合集运行与双游戏 SDK、PS4 后端的接入顺序仍按计划推进；首版为三款可玩作品与两款 Coming Soon。最后按同一次已验证构建输出 Internal／Public 两种包，范围与发布门槛见实施计划；出包不自动上传。
