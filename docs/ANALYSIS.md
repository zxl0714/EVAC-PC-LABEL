# 分析与诊断工具

这些工具用于检查数据版本、模块声明、日志，以及 G2 和虫姬二的社区接口。它们不启动游戏窗口；静态模块检查不会执行客体指令，也不初始化 GPU、音频或输入。实际进关、声音响应和流畅性仍由游玩回归验证。

在合集根目录运行下列命令。`--data-dir` 显式指定要读取的数据，不修改根配置；使用外部目录时替换示例中的路径即可。工具不需要原 ESPGaluda2-PC 工程。

## 数据盘点

```powershell
py -3.12 scripts/xex_inventory.py game-data/espgaluda2 --output .local/analysis/g2-inventory.json
```

逐文件计算大小与 SHA-256，并有界读取 XEX2 头中的基址、入口、Title ID 和压缩信息。输出只有元数据，不含资源或程序正文。不指定 `--output` 时只输出到终端；`--xex-lock` 可另外写一份程序元数据用于比较，不会自动更改维护中的版本清单。

### 最大往生本体与更新识别

```powershell
py -3.12 scripts/sdoj_inputs.py '<已解出的最大往生目录>' --output .local/analysis/sdoj-inputs.json
```

这是后续接入的预检，不是启动器的数据清单。它只按固定来源哈希识别光盘／GoD 两组主程序、CA022100／CA022110 模块和对应 TU 1.01；原始光盘 TU 可以任意命名，GoD 使用三个已提取的补丁文件。输入参考位于 games/sdoj/input-references.json，并记录上游提交。

结果区分已知程序与更新、缺更新、更新版本混用、未知／不完整程序及尚待解包的容器。RAR／ISO 等只列为候选，不自动解压或计算整包哈希；其他资源不递归读取。工具不下载、安装更新、执行游戏或改动源文件，报告仍使用统一的 .local 输出边界。

即使得到 `known_programs_and_update`，`readyToRun` 和 `fullDataVerification` 仍为 false：完整资源、准备结果和运行时尚未验收，最大往生也未因此进入可玩清单。

## 静态模块检查

### PS4 录像结构检查

在已构建的 PS4 运行时上，可只读检查本地压缩附件：

```powershell
./out/ps4-native/KetsuiNativeProbe.exe --community-replay-profile-check daioujou '<本地压缩附件>'
py -3.12 scripts/ps4/run_replay_checks.py
```

第一个命令显式指定标题；可追加原生榜单编号和原生分值整数核对元数据，不猜测另一游戏或版本。第二个命令用自造的不可游玩数据检查两款结构，并将逐项日志及构建身份保存在新的 `.local/ps4-replay-checks/`。结构校验不写入玩家存档、不执行快照，也不代表游戏内回放已通过。

绊地狱沿用 0x80000 字节／1.02 合同；大往生本地存档记录为 0x65600 字节，网络附件内的记录为 0x80000 字节，容器 0x10030、core 0x10000，并接受本体明确附带的 ARCADE_01000／01010／01020／01030 内核。真实测试记录已确认新容器可使用 ARCADE_01000，容器与内核版本不可混为一谈。两者的综合榜编号、角色范围和分值含义不同。大往生代表性原生 loopback 收发、保存及离线冷回放已通过，正式 HTTPS 和正常合集入口仍待验收，当前能力开关保持关闭。

先准备本工程的 SDK 和原生分析工具：

```powershell
py -3.12 scripts/sdk_sources.py restore
./scripts/Build-Sdk.ps1 -Jobs 2
./scripts/Build.ps1 -Games espgaluda2 -NativeOnly -Jobs 2
py -3.12 scripts/analyze_modules.py --game espgaluda2 --data-dir game-data/espgaluda2 --output .local/analysis/g2-census
```

默认检查主程序及七个 overlay；也可以重复指定 `--module default.xex --module dat15.xex`。每个模块使用独立的本机检查进程，记录导入接口、映像区段和输入身份；overlay 另做三次静态装卸载及重叠加载拒绝检查。工具核对 SDK 构建收据和配套 runtime，并在结束时复核整个数据目录未变。

`analysis.json` 是成功收据。超时或失败会保留现场，但不生成成功收据；查看对应的 stdout／stderr 日志。报告中的“已注册接口”和“静态装卸载通过”不代表接口功能或实际模式运行已经验收。

### 最大往生静态装载

原生检查实现已移到 backends/xbox360/diagnostics/inspect_xex.cpp，保留 espgaluda2_inspect.exe 的兼容文件名。上述 NativeOnly 构建也会更新此工具；不需要 G2 数据。最大往生使用单独的只读入口：

```powershell
py -3.12 scripts/sdoj_analyze.py --data-dir '<最大往生原始解包目录>' --output .local/analysis/sdoj-census
```

识别受支持的光盘／GoD 本体后，分别静态装载 default.xex、CA022100.bin 和 CA022110.bin。两个 .bin 实际是 XEX 模块；沿用同一工具模式，检查其三次装卸载、相同基址互斥与主程序分配不被破坏，并记录各模块的系统接口及区段。报告只证明本体程序静态兼容，不要求或应用 TU，不采用音乐补丁，也不标记完整资源或游戏可玩。工具前后复核的是三个选中程序，不宣称盘点了整个资源目录。

## 回调与社区边界

生成过对应游戏的本地重编译代码后，可以检查数据引用但尚未注册的短跳板候选：

```powershell
py -3.12 scripts/callback_inventory.py .local/analysis/g2-census --game espgaluda2 --data-dir game-data/espgaluda2 --output .local/analysis/g2-callbacks.json
py -3.12 scripts/espgaluda2/analyze_community.py .local/analysis/g2-census --data-dir game-data/espgaluda2 --output .local/analysis/g2-community.json
```

回调检查读取 `generated/espgaluda2/<模块>/*_register.cpp`，结合经过大小和哈希校验的区段筛选候选。候选需要逐项审阅，不自动修改函数声明，也不能据此判断必然存在崩溃。

同一入口支持 `--game sdoj`，读取 generated/sdoj 及相应的三模块输入表。报告分为短转发入口、有界只读叶函数／条件尾调用候选，以及 codePointerRuns 连续代码指针组；后者保留组位置、长度和缺注册目标，帮助集中审阅接口表。异常／展开元数据也会形成指针组，因此它不是 vtable 或可调用函数的证明。浮点、直接存储、栈暂存等不在保守 CFG 类别内，不代表它们不是合法函数，需结合引用来源和完整反汇编判断。声明修改后重新生成，不手改派生正文。

社区检查保留在 G2 专属目录，因为地址、84 个 view 和成绩先于录像的规则属于本游戏。它核对已审阅表格与函数边界的身份；语义说明见 [G2 原生社区流程](../games/espgaluda2/docs/PROGRAM_LAYOUT.md#原生社区流程)。扫描成功不等同于联网收发通过。

## 已有运行日志

```powershell
py -3.12 scripts/analyze_run.py logs/espgaluda2/<单次运行目录> --output .local/analysis/g2-run.json
```

也可以直接传入 `game.log`。汇总包括模式加载／卸载、未配对事件、错误、F8 标记和按设备分组的输出音频样本。用户关闭之后的音频样本不参与统计；没有错误行不证明日志完整，输出队列正常也不证明游戏声音命令没有延迟。

虫姬二日志中的 `guestAudio` 另行统计已匹配的客体队列头年龄、cue 请求 API 完成耗时、超过 20／100／1000 ms 的队列头样本数、有效玩法探针和丢弃事件。它与输出音频队列分开：同一队列头的重试可能重复采样；没有有效样本返回 null，而不是零延迟。`traceScopes` 表明实际覆盖模块，cue 名称不等于已观察到拾取道具；这些计时也不是扬声器实际发声时刻。

## 最大往生音效与输入的当前证据

小飞机击毁音效反馈尚未定位到具体 cue，不应通过改变全局音量、缩短整个输出队列或套用虫姬二地址来处理。对既有 `logs/sdoj/run-20261019T161136128Z-78436/game.log` 的只读分析得到三路各 1,119 个输出样本：队列 32–42.7 ms，最老块最大 51.8 ms，消费年龄最大 63.8 ms。这份日志没有用户 F8 标记或 SDOJ 客体音效命令探针；没有秒级输出积压并不证明某个击毁音效正常。完整摘要在本地 `.local/analysis/sdoj-output-audio-v03-r1.json`，源日志 SHA-256 为 e59be8355400fdc3ed8474d294c14e9ac723152a5616f28ff78c5817e7b1004a。

当前 SDOJ 使用光盘本体，不是上游 TU 1.01。固定上游 cc35e038 的说明将控制器临近消费重采样与提前渲染分为两项降延迟补丁；其输入 wrapper 地址为 0x822169E0，而本体通过 0x82215D68 转发到 XamInputGetState。这只确认版本差异与原始调用链，不能证明延迟是多少，也不能把 upstream 的 3f／1f 宣称当成本机测量。后续需在本体实际输入消费与画面提交处对应采样，分别验证实战、菜单和录像；公共帧率或单独包装器计时不能代替端到端延迟。当前没有修改采样或渲染顺序。

## 绊地狱帧节拍与后台日志

大往生网络附件与本地存档记录的尺寸不同。受支持归档中 `1100/config/title_struct_replaydata.psb.m` 的每条记录为 0x65600，数据位于 +0x500、长 0x65000；`system/config/struct_replay.psb.m` 的网络根结构为 0x80000，数据同样位于 +0x500、长 0x78000、尾部 0x7b00。原生 ranking_ctrl 上传及下载均构造后者，上传通过 CopyStructValue 拷贝本地记录的同名字段。第二份真实网络现场的附件为 26,536 字节，旧客户端按本地记录上限解压时报 inflate_output_limit。因此网络校验改为精确的网络布局，而非无界放宽；容器 0x10030、core 0x10000、元数据与快照／按键边界保持检查，绊地狱同尺寸附件仍因版本不符拒绝。结构定义与解码输出只留本地 `.local/daioujou-resource-inspect-r2`；Git 仅记录布局元数据与定义 SHA-256。实际网络收发／下载回放仍须修正后验证。

大往生也使用这一 PS4 时序分析器。其首轮联网实战的第一关 F8 后 10 秒区间 `8609862441413–8609872441413` 仅完成 422 帧，123 个间隔超过 20 ms，最大 100.777 ms，P95 直方图上界 50.2 ms。绘制执行平均 4.575 ms，提交外间隔平均 9.587 ms；不是仅凭整进程平均 FPS 得出的结论。

这一区间的 122 个相邻完成记录之间含周期 `native_allocation` 日志，其中 120 个间隔超过 20 ms；没有该日志的 300 个区间仅 3 个长帧。后续演示画面的独立短采样在同一提交线程的 300 个指令指针样本中有 88 个落在 ZwWriteFile，栈内地址候选不视为完整展开。采样会扰动时序，不纳入上述第一关帧时间。定位到 TraceAllocation 仍直接调用无缓冲 stdout 的 printf，832717c 改走已有有界后台日志队列，保留日志内容和频率。

修复版从 profile 副本冷启动，完整播放这局 1780 分录像并自然返回列表。第一关 F8 前／后两个连续 10 秒区间各有 600 个完成间隔，平均分别 16.666／16.666 ms，最大 19.943／19.388 ms，均无超过 20 ms 的记录；后一区间提交外间隔平均降至 1.599 ms。原实战与新进程回放不是逐帧对齐的微基准，不泛化为全场景无卡顿；这验证周期同步分配日志移出游戏线程后，同局第一关短回放已恢复原作约 60 Hz 节拍。原始日志、两份报告、录像身份与采样分别留在本地 `.local/daioujou-network-r1`、`.local/daioujou-perf-r2` 和 `.local/analysis`，不进入发行包或源码 Git。

绊地狱也调用同一 TraceAllocation，修复已随同一宿主整理到两款 PS4 别名；这不等于既有卡顿已经通过回归。对原 380 分录像区间 `8595894411303–8595927769180` 的 1901 个完成记录重新检查，598 个相邻记录区间含分配日志，其中仅 1 个超过 20 ms（33.412 ms）；其余 1303 个区间中有 4 个长帧（33.357、32.622、32.968、1633.499 ms），未夹有该日志。异步完成记录和直接日志的相邻关系只能提供关联线索，不能将没有相邻记录理解为完整因果排除；不过这里没有大往生的 120／122 强关联。结论是公共修复适用于绊地狱，但其小卡顿不能全部归于同一原因，仍需新宿主的相同录像短回归，保留同步、呈现及提交外间隙的调查。

这项短回归现已完成：`d4bf1a1`、`.local/ketsui-perf-v03-r2`、`run-20261019T225916633Z-83508`，GPU 与旧 r1 的 SHA-256 相同，普通输入、离线、vsync=true、全屏。同一份 380 分录像完整播放至自然返回；关卡约 15–47 秒（`8614455686824–8614487730619`）的 1,922 个连续 submission 间隔平均 16.666 ms、p99 上界 18.4 ms、最大 19.638 ms，无超过 20 ms 的间隔。提交外间隙最大 4.004 ms，区间内无编译记录。最终报告 `.local/analysis/ketsui-replay-present-r2-final-bounded.json`；源日志 SHA-256 为 `737b2b7a76ea3c0cab908d62e93120676b5befd782e8cacde257c77c3b407ec8`。这是改善证据，不是单改动、逐帧对齐的因果实验；回放返回附近另有 50.084／66.711 ms 间隔，未纳入上述关卡统计。没有删同步或改变游戏速度。保留偶发卡顿问题，不根据这份短片段将其全部关闭。

```powershell
py -3.12 scripts/ps4/analyze_timing.py logs/ketsui/<单次运行目录> --output .local/analysis/ketsui-timing.json
```

F8 的 `native_timeline_marker.clock_us` 与帧记录使用同一个宿主单调时钟。用 `--from-clock-us` 和 `--to-clock-us` 选择关卡区间，避免把菜单、载入与实际游玩混成一份极值。`completion_interval_us` 是宿主完成两次呈现之间的间隔，不是输入延迟或物理屏幕扫描时间。百分位采用报告中的有界直方图上界，不能把多个窗口的百分位平均成全局百分位。

v0.3 开发构建另外对至少 20 ms 的已完成呈现输出 `native_video_present_timing`：分别记录状态锁等待、准备、游戏显示时钟等待、后端呈现和完成处理。各段之和必须等于同一单调时钟范围内的总时长，分析器会拒绝不匹配的记录，并将有效作用域关联到长帧。`present_substages` 只统计达到阈值的样本，不代表每帧分布；后端段仍包含 resident worker 交接、Vulkan 与窗口恢复等待，不能当作纯 GPU 耗时或物理扫描时间，也不能仅凭一条记录删除同步。

为调查约 19 ms 的间隔，新的 `native_gnm_frame_completed.present_stages` 在已有每帧记录内附带上述五段和总时长，不额外增加日志行或时钟读取。仅真实完成后返回分段；取消或失败不产生完成记录。分析器以 `all_frame_present_substages` 独立汇总这组数据，校验非负整数、分段和及外围呈现时长（允许 1 µs 舍入差），不与旧的阈值事件重复计数。旧日志缺失分段或无效记录不当成零耗时，也不能凭后台呈现段宣称 GPU 运算过载。v0.3 须完成大往生、绊地狱性能定位与修复验收后才发行；短片段约 60 Hz 的旧结果不关闭这个门槛。

合集启动默认启用后台诊断队列。提交线程只格式化和投递完整记录，GPU 每帧诊断、宿主慢操作和 NGS2／WASAPI 热路径的文件写入交给独立线程；低频启动与终止诊断仍可直接输出。队列固定为 1,024 条、每条最多 4,096 字节，不因磁盘慢写无限积压；队列满保留最新尾部，短暂竞争或超长记录会被拒绝并计数，不输出截断 JSON。

`native_async_log` 每约两秒报告累计状态，分析器保存在 `async_logging` 中：

- `dropped_oldest`、`contended`、`invalid_records`、`format_failures` 分别记录满队列淘汰、竞争拒绝、无效长度与格式错误。
- `sink_failures` 是后台写入／刷盘失败次数，`sink_wall_max_us` 是本次进程的后台写入最大耗时。
- `pending` 与 `in_flight` 区分排队和正在写入；受控退出先排空队列，再写终止诊断。最后一条状态的 `stop_requested=true`、`drain_completed=true`、两项待写数量为零，表示这次排空完成，不代表此前没有丢弃。

这些值是进程累计状态，选择时间区间后也不重解释成局部计数。旧日志没有这组状态时，完整性仍为未知。新日志中的 `diagnostic_log_us` 和 `previous_frame_log_us` 只计算提交线程的格式化／投递耗时；分析时与后台写入耗时分开。异常强制终止可能来不及排空，锁独立的最后故障记录继续保留；绊地狱退出码 27 的既有限制未因此改变。

独立诊断命令默认保留同步输出。`KetsuiNativeProbe.exe --async-log-self-test` 使用自造阻塞 sink 验证提交与磁盘分离、容量及整条记录、失败计数、多线程投递和排空；`scripts/ps4/run_native_checks.py` 另外检查终止记录仍为最后一条完整 JSON。这些机制检查不执行游戏。

固定录像复测路径为：进入 ARCADE → Backspace 打开暂停菜单 → Materials Room → Replay Theater → My Replay。现有 380 分／TYPE-A／00:53.45 录像可重复用于对照；播放期间不发射击、暂停或加速输入，等待自然返回，再核对 F8、程序哈希、选定区间、丢失状态及音频观测。共享 INI 的临时离线与 CU 方向别名在测试结束后恢复。

## 虫姬二本机联网现场

```powershell
py -3.12 scripts/futari/local_service.py --directory .local/community/futari-check --seconds 1800
```

服务只监听 127.0.0.1 的随机端口，使用原虫姬二协议和全新 SQLite。终端与现场目录的 `service.json` 给出临时 API 根地址；将它临时填入唯一根 INI 的 `[game.futari.community]` 中的 `endpoint`，并显式设置 `enabled = true` 和测试昵称，即可从游戏原生流程验证榜单、成绩及录像。客户端自行追加 `/games/futari`，不把完整游戏路径再填进 endpoint。服务默认 30 分钟后停止，Ctrl+C 可提前结束。

输出必须是合集 `.local` 内不存在的新目录，不复用数据库、不连接正式服务，也不更改服务端默认路由。验证结束后关闭游戏并恢复 INI；保留现场可供只读分析。未完成的待传记录仍属于原服务地址，不能改填正式地址以补传本机测试成绩。

## 输出与源码

报告必须使用 `.local` 下的新路径，不覆盖已有文件，也不与输入、默认或配置中的游戏数据目录重叠。静态检查会写出本地解压区段，这些属于游戏派生数据，只能保留在忽略目录，不加入 Git、公开程序包或通用日志分享包。

共享盘点、模块及日志工具位于 `scripts/`；游戏专属地址分析位于 `scripts/espgaluda2/`。源码来源与提交见 [源码来源](SOURCE_ORIGINS.json)。

## 原 G2 回归的迁移对应

原 ESPGaluda2-PC 基线的 66 项 Python 检查按下表迁移。64 项保留为合集 Python 测试；两项旧 Python 数据导入检查由现有的原生复制测试覆盖，不再维护第二套复制实现。

| 原测试文件 | 合集对应位置 |
| --- | --- |
| `test_project.py` 的 8 项头部／盘点检查 | `tests/test_xex_inventory.py` |
| `test_project.py` 的 4 项报告／源码边界检查 | `tests/test_source_boundaries.py` |
| `test_project.py` 的 2 项复制检查 | `tests/launch_test.cpp` 的 DataImport 场景及 `tests/test_data_import.py` |
| `test_sdk_tools.py` 的 8 项 | `tests/test_tool_archives.py`，使用合集依赖锁格式 |
| `test_generation.py` 的 5 项 | `tests/test_g2_generation.py`，使用合集清单和 DLL 注册路径 |
| `test_build_receipt.py` 的 5 项 | `tests/test_build_identity.py` |
| `test_callbacks.py` 的 5 项 | `tests/test_callbacks.py`，另增加指令偏移边界用例 |
| `test_run_analysis.py` 的 5 项 | `tests/test_run_analysis.py` |
| `test_community_analysis.py` 的 6 项 | `tests/test_g2_community_analysis.py` |
| `test_community_server.py` 的 18 项 | `tests/test_g2_community_server.py`，核对合集路由及游戏隔离 |

新增路径和静态收据边界用例在 `tests/test_analysis_paths.py`、`tests/test_module_analysis.py`。全部源码测试通过 `./scripts/Test.ps1 -Scope Source` 运行，测试数据自行构造，不需要 ROM；HTTP 用例只访问临时本机服务，不连接正式服务。
