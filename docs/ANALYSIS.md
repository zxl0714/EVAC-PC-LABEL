# 分析与诊断工具

线程派发、信号量、mutex／condition、GPU worker 和日志队列审查及当前根因修复集中在 [PS4 任务派发与同步](PS4_SYNCHRONIZATION.md)。已通过交错对照确认旧硬宿主 CPU 绑定放大批量任务长尾；默认改为继承宿主可运行范围，保留六线程和完整同步。375 源码／73 原生通过，两款实际固定录像的关卡最大 17.185／17.188 ms。该文同时保留旧 33.508 ms 实际长帧、未通过的 event 候选、时间点语义、配套身份与覆盖边界；下文较早记录不覆盖其当前结论。

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

### 本体输入轮询与复制的只读探针

`games/sdoj/diagnostics/input_trace.cpp` 和独立 `input-diagnostics-overrides.toml` 仅观察已核对的光盘本体：`82178B78` 进入引擎输入更新，`821A3D90`／`821A3EA8` 是两个系统输入 wrapper 返回点，`821A3C38` 在读取原始按钮字后、复制之前，`82178BFC` 在轮询和输入对象转换返回后。没有替换原函数、写寄存器／客体内存、额外轮询或移动渲染；后两项也不是游戏机体逻辑消费／最终画面时间点。

运行时仅显式 `--sdoj_input_trace=true` 开启，默认关闭。`SDOJ_INPUT` 使用 steady-clock 微秒和实际客体 VBlank epoch，不用宿主 FPS 或 CPU 频率推算。输出只包含状态变化及每约 60 epoch 一条输入处理摘要，全进程最多 2048 条；packet 自增而控制值不变不会耗尽预算。状态只在成功且 16 字节完整可读时解码；失败不复用旧输出，输入更新前后核对本体的 manager 关系。普通 `--input_diagnostics=true` 的键盘事件／采样日志另增加同一时钟和 epoch，仅在原诊断开启时读取时钟，玩家配置与输入保持策略不变。

`.local/sdoj-input-trace-r1/logs/sdoj/run-20261020T071249516Z-43596` 为实际离线、窗口、VSync、禁用实体手柄的独立测试副本。原生菜单、Xbox 360／Shot 玩家第一关及自然局末可见；方向短按实际发生在续关提示后，不记为玩家移动已测。7 个真实键盘新按下沿均在下一客体 VBlank epoch 被采到，宿主键沿至系统轮询结果为 0.962–15.776 ms、中位 8.264 ms；同 user／epoch／按钮值、下一份原始状态之前的复制观察为 5–6 µs。282 个有界输入更新摘要为 3–40 µs、中位 7 µs，未跨 VBlank epoch。统计含探针开销，是墙钟区间而非纯 CPU 时间；样本多为菜单／选人和转场，不能推广为玩法或所有硬件延迟。

这批证据将下一步重点移到游戏玩法输入掩码的消费位置和渲染回调批次，而非直接改变公共 SDL 或把本轮解释成“输入延迟已经修复”。[上游对两类补丁的说明](https://github.com/eandis/SDOJ-Recomp/blob/cc35e038d4126bc1c359597dcb94e6815477818d/README.md#input-lag)针对 TU 1.01，仍不直接套用其地址或帧数结论。小飞机击毁 cue 尚未取得对应链路证据，本批也不标记音效修复。

本批为 `04413a3` 后未提交开发源码构建，宿主 SHA-256 `7a9e441541b2c0f62e9c9fe191d80bea68674a312c51466fbde280a04409b67c`，SDK runtime `a9876fff10180452662c194ffb9216f9c4a812f8a1b819d0dd21cffb0857ff94`，Xenos `40d29441b36cd1651fc2d444bebb0d552c4207deb8b8109132435f35f18b86a4`；14 对 PE／PDB 配套。关闭日志 SHA-256 `af5eb5b41c672a9caa90a4c2c900d8b7ac726d9ecea346c33125624ddd76edc1`，有界配对报告 `.local/analysis/sdoj-input-trace-r1.json`；关闭后进程终止、原生 hard-exit 日志保留。377 源码、8 项去重原生 CTest（含输入观测的 15 个结构／配对子例）通过。此处不是无探针性能验收或最终五游戏发行构建。

## 绊地狱帧节拍与后台日志

### 最终合成提前提交与映射回退

新软件路径将最终合成也提前提交，使显卡利用原本约 10 ms 的软件节拍等待。Prepare 在 GPU worker 上获取图像、录制并提交；节拍等待回到调用线程，worker 保持可处理原生内存一致性请求；Complete 再在 worker 上执行呈现及完整栅栏等待。Prepared／Cancelled 不产生客体完成事件、递增完成数或释放 label。取消会正常退役已获取图像及信号量，后续呈现仍可恢复。显示机会在准备前、原等待调用的位置预留，准备跨过目标时不重新要求再等一整周期。没有修改原作速度、减速或显示时钟。

GPU API 为配套版本 7，同时严格核对结构大小。`between_phases_us` 是 Prepare→Complete 的间隙，包含调用线程等待、交接以及可能被 worker 服务的内存操作；它不是纯 GPU 时间，也不能与父层软件等待相加。worker 的九项工作计时不含此间隙，父层只扣除实际节拍等待，剩余交接仍计入后端耗时。另在 timeline 已知覆盖全部已提交工作时跳过重复驱动计数查询，保留未完成工作查询及栅栏。GPU 自测验证完成顺序、取消／恢复、期间的内存服务以及 1,000 次空闲回调处理不新增驱动查询。

软件-only 正常回放现场为 `.local/ketsui-pacing-r8`／`run-20261020T032640133Z-88752`，离线、vsync=true、窗口、输入延长为 0。相同 ARCADE／Tiger／380 分／00:53.45 录像完整自然返回，随后受控关闭（既有清理限制 27）。宿主 SHA-256 为 `aab91fef0b8f3c36349d7328814c32d334d451f5105127de268097aa2cb558f5`，GPU 为 `33cfe3eea2d52680bc68c2cc9896539b3d8d792c62654e4302f0da57af0b0558`；是基于 52756dc 的未提交开发构建，不是发行包。

F8 在录像约 37.01 秒，取其前 22 秒至后 9 秒，即宿主时钟 `8630477869290–8630508869290`。31 秒有 1,859 个连续 submission（12915–14773），没有缺号或分段校验失败。与较早 r6 的相近关卡片段比较如下；不是逐帧严格对齐或逐项独立 A/B：

| 宿主 wall time | r6 | 软件-only r8 |
| --- | ---: | ---: |
| 指令处理均值 | 3.567 ms | 3.604 ms |
| 最终输出均值，扣除节拍等待 | 0.838 ms | 0.102 ms |
| 两次提交间隔均值 | 1.584 ms | 1.637 ms |
| 整帧 p95／p99 直方图上界 | 17.4／18.9 ms | 17.1／17.2 ms |
| 整帧最大间隔 | 19.803 ms | 17.256 ms |

r8 的指令处理仍主要是绘制／资源 2.627 ms、ACQUIRE 同步排空 0.803 ms；并未取得明确的整体下降。最终输出中完成栅栏等待均值约 1.3 µs、最大 12 µs，而约 11.275 ms 的正常软件等待仍在。这表示先前设备工作已与等待重叠，不表示显卡只运算了 1.3 µs，也不是删除了 GPU 工作。报告为 `.local/analysis/ketsui-pacing-r8-software-only-gameplay.json`；日志 SHA-256 为 `83462b40180c67215820cc098e8b047a9f47a24bdc116b749218fb0652bcd660`。选定区间没有着色器／管线编译；全进程仍有 81 条日志竞争拒绝，故不能声称所有诊断完整。

大往生使用同一配套宿主／GPU，现场 `.local/daioujou-pacing-r3`／`run-20261020T033215952Z-21096`，普通输入、离线、vsync=true。MY REPLAY 的相同 1780 分／Type-A／Shotia 记录完整自然返回。F8 在返回列表后，选其前 40 至前 10 秒的 `8630768237598–8630798237598`，不冒称精确对齐的游戏计时窗口。30 秒有 1,798 个连续 submission（10110–11907），包含 4 项完成的着色器／管线编译记录；p99 上界 17.2 ms，最终输出均值 0.103 ms，但仍有两次约 33 ms 长帧：

| 依次发生的阶段 | submission 11662 | submission 10503 |
| --- | ---: | ---: |
| 整帧间隔 | 33.740 ms | 33.618 ms |
| 上次提交结束→本次提交开始 | 14.974 ms | 16.493 ms |
| 其中：主线程等待任务完成（前项子集） | 13.239 ms | 14.876 ms |
| 复制绘图指令 | 0.068 ms | 0.072 ms |
| 指令处理，包含同步 | 5.981 ms | 6.412 ms |
| 其中：ACQUIRE 同步排空（前项子集） | 0.764 ms | 2.882 ms |
| 正常软件节拍等待 | 12.573 ms | 10.407 ms |
| 最终输出，扣除软件等待 | 0.141 ms | 0.230 ms |

子项不可重复相加，另有数微秒收尾／舍入。10503 的六个工作线程在任务授予后约 14.77–14.83 ms 才恢复，最后完成信号授予后主线程约 4 µs 恢复；任务 handle 为 6002453878354739201，完成 handle 为 6002453878354739202，主线程请求 14 个完成。调用点分别为 eboot+0x1a0575、0x1a0738、0x1a0728、0x1a0584。恢复包含重新获得 semaphore gate，不能直接把全部延迟判作纯调度或纯计算；这条交接路径与指令排空仍开放。报告 `.local/analysis/daioujou-pacing-r3-software-gameplay.json`；日志 SHA-256 `1b5415016a74a2de3e4ed77952fc312f1d3635fd9299eeda45111b06104a8470`。

CPU 硬映射试验没有合入：r7 将六个虚拟 CPU 映射到不同物理性能核，实际固定录像的选定 31 秒仅 702 帧，最大间隔 450.061 ms。新进程冷启动仍退化，10 秒 474 帧、最大 200.193 ms；同进程仅把六个 worker 恢复成旧逻辑映射后，后续 10 秒为 600 帧、最大 17.434 ms，软件路径不变。两段场景不完全相同，不据此宣称物理核优先在所有机器上必然更差，但足以拒绝当前策略。现场／报告为 `.local/ketsui-pacing-r7` 和 `ketsui-pacing-r7-cold-topology.json`／`cold-legacy-map.json`。原线程映射已恢复；没有改变系统电源、其他进程或游戏任务数。

### 地址等待与通知后的长尾

成功等待的恢复路径改为独立 waiter 的原子结果与 Windows `WaitOnAddress`／`WakeByAddressSingle`，不再重新争用共享 semaphore gate。队列次序、完整 credit 预留仍在原 gate 内完成；先建立通知对象的拥有链，再 release 发布结果。等待以 acquire 读取结果，早于停车的通知不会丢失，虚假唤醒继续检查原截止时间。只有超时退休才重新取得 gate，并再次检查是否已获授予；已预留的成功不会被超时覆盖。取消、删除及通知对象生命周期沿用原语义，复用仍有独占所有权和显式发布屏障。依据为 [Microsoft WaitOnAddress 合同](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress)；不依赖其唤醒顺序实现 guest FIFO。原 CV 路径保留作诊断对照，不改 CPU 映射、电源或优先级。

日志的 `native_semaphore_wait_timing` 现在包含 `wake_began_clock_us`、`wake_finished_clock_us` 和 `address_wait`，可区分发通知之前与之后的延迟。成功可能早于通知调用／结束被观察到，此时相应字段为 0 或通知结束晚于恢复，不作负耗时相加。CV 恢复仍包含重新取 gate；address 的成功路径不包含此步骤。WPR 的 CPU 调度采集因当前非管理员权限未能启动，已核对未在 recording，故没有用内核调度 trace 证明剩余时间全属 Ready 排队。

两种传输各完整执行 26 个 semaphore 子例，共 52 项；包含成功是否需要重取仍被夹持的 gate、不得在首个 semaphore 发布后切换传输，以及全部原 FIFO／优先级、credit、超时、取消／删除和生命周期场景。完整 73 项原生和 373 项源码（含 13 项 bench 协议）通过，日志 `.local/v03-address-wait-r2-{build,source,native}.log`。GPU DLL 未改，原 API7 配对及既有图形机制证据沿用，不写成新测。

离线对照使用同一程序、相同默认参数和六组负载，每轮每组 384 个正式样本，按 CV→address→address→CV 顺序，各三次独立进程重复；所有像素、任务 checksum、绘图计数和完整性检查通过。现场 `.local/offline-bench/wake-cv-a1`／`wake-address-b1`／`wake-address-b2`／`wake-cv-a2`，宿主 `6d54daded6f6ec507a8e67a1d044e5458c70681049daf4bc96bc6346fa18902f`，GPU `bcfc29dcc2b603722604882767d6a215479a10ade9d8eca4db5f1b2533aacdc3`，快照 `363a7dc0f09bc8fd99c3bd8c7d8e93d58bbf3297b6ece617f4b045747e1b3a06`。显式 `--semaphore-wait cv|address` 只改变宿主等待原语，不改变任务、GPU 负载、节拍或核映射，报告身份记录实际选择。

| 间隔派发交接（µs） | CV A1 | address B1 | address B2 | CV A2 |
| --- | --- | --- | --- | --- |
| 14 任务中位数 | 38 | 29 | 31 | 34 |
| 14 任务 p95 / p99 | 1273 / 9215 | 105 / 1236 | 108 / 669 | 90 / 422 |
| 14 任务最大 | 22435 | 1952 | 2108 | 7664 |
| 16 任务中位数 | 35 | 31 | 29 | 39 |
| 16 任务 p95 / p99 | 216 / 2194 | 90 / 1426 | 90 / 1152 | 144 / 3010 |
| 16 任务最大 | 3514 | 2589 | 4433 | 18226 |

CV A1 的 22.435 ms batch 对应 worker 69912：授予到通知开始 8 µs、通知调用 1 µs、通知结束到恢复 **22392 µs**；A2 的另一 worker 为 5／5／18177 µs。长尾不在发通知之前。address 常见路径及这些尾部有所减少，但 A2 的 14 任务 p95 本来就低于两个 B，不能宣称每个分位数均改善，或将有限样本的极值差异保证为普遍收益。后续仍以实际回放和剩余恢复尾峰为准。

正常运行的默认切为 address 后重新配套构建；373 源码／73 原生复验通过。宿主 `d2ff8b9094dc7e19352c79b54550ae00b7e0636b981814d4b2cfed8cb421e337`，GPU 不变，快照 `c667c307a28a4286366ae83cabfa05980ae4459c14dee111e073c6c9d9f13aef`，基于 df79594 的未提交开发构建。实际 `.local/daioujou-pacing-r5/logs/daioujou/run-20261020T043810503Z-81076` 为正常五参数合同、输入延长 0、离线、vsync=true、窗口；同一 MY REPLAY 1780 分／Type-A／Shotia 完整自然返回，随后受控关闭为清理限制 27。F8 UI 约 17 秒，取其前 2 至后 28 秒 `8634727323344–8634757323344`，与旧窗口仅近似场景对齐，不宣称逐游戏帧 A/B。

该 30 秒包含 4 项完成编译、1,800 个连续 submission（10171–11970），无缺号：平均间隔 16.666 ms、p95／p99 上界 17.1／17.2 ms、最大 **17.325 ms**；指令处理均值 4.006 ms、最大 7.954 ms，提交外均值 1.576 ms、最大 5.672 ms，最终输出均值 0.100 ms。正常节拍等待约 10.932 ms，未删除 GPU／呈现完成条件。worker 21820 仍在 `8634753148504` 通知结束后到 `8634753152309` 才恢复，**3805 µs**；它发完成信号后主线程约 4 µs 恢复。此 address 成功路径不再取共享 gate，但没有内核调度证据将全部剩余恢复时间归因于 OS Ready 排队。没有新 33 ms 不等于问题关闭。

报告 `.local/analysis/daioujou-pacing-r5-address-wait.json`，日志 SHA-256 `3aa85bf3d6e21a60ec09bc62824b61374c3988cadc5dbc7463ca268c48c779f0`。最终 accepted=written=62278、dropped=0、format/sink failure=0、frame emission failure=0；全进程仍有 697 次日志竞争拒绝及 682 次 host operation 发送失败，不能称全部诊断无损。当前按用户明确优先级继续性能：同路径绊地狱回放、绘图／资源处理及同步排空的细分与软件优化，以及剩余宿主恢复尾峰。未处理完不切回模式或出包，也不据此删去 v0.3 其他范围。

### 信号量交接的软件批次

信号量热路径现在使用每线程两个弱引用句柄缓存，减少 work/done 交替操作的全局表锁；删除仍在状态锁内判定，句柄不复用，缓存不延长已删除对象的生命周期。客体线程优先级由创建时不可变的快照直接保存在 TLS，不再每次等待都锁线程注册表；以后实现运行中修改优先级时须同步修改此合同。FIFO／优先级顺序、完整批量 credit 预留、超时、取消及删除返回值均不改变。

唤醒通知移到释放 semaphore gate 之后。队列及无分配的通知链共同持有 waiter，确保等待线程提前返回甚至退出后，notifier 使用的 condition_variable 仍然有效。每线程只在唯一持有 waiter 时复用它；旧 notifier 尚未释放时重新分配，不能覆写仍被通知的 CV。原生检查包含等待线程先退出、带旧通知开始第二次等待、跨线程删除缓存句柄、优先级继承和未托管线程默认优先级；24 个 semaphore 子例、18 个 thread 子例及完整 73 项原生入口均通过。源码 372 项通过。没有修改 CPU 映射、系统电源、进程优先级或游戏任务数。

离线筛选报告为 `.local/offline-bench/semaphore-software-r1/report.json` 和 `r2/report.json`，沿用相同六组负载和默认参数，每组每轮 384 个正式样本，全部像素／任务 checksum／绘图次数通过。两轮共同宿主 SHA-256 为 `7bf1e9d6b032e1ea8053eb7b12dd7f1960cf8b1516e5f5fdd7f5bf2b5e07ce5a`，GPU 保持 `bcfc29dcc2b603722604882767d6a215479a10ade9d8eca4db5f1b2533aacdc3`；基于 300a0b1 的开发构建，测量快照为 `de8c06866c3696dd701337132e942d8264e2183088f3e6b5e9c8eba0de9a6fba`，不是发行包。

| 组件交接耗时（µs） | 原程序 A/A 两轮 | 软件批次两轮 |
| --- | --- | --- |
| 连续 14 任务中位数 | 18 / 18 | 17 / 17 |
| 连续 16 任务中位数 | 19 / 18 | 17 / 17 |
| 60 Hz 间隔后 14 任务 p95 | 122 / 129 | 87 / 89 |
| 60 Hz 间隔后 16 任务 p95 | 130 / 133 | 92 / 97 |

第二轮软件批次仍有连续 14 任务的 7.546 ms、间隔 16 任务的 3.044 ms 尾峰；连续均值因此并非两轮都下降。两种 GPU 负载均值约 0.869–0.874／1.066–1.077 ms，未修改的路径只在既有噪声范围内波动，不记作 GPU 优化。表内常见交接下降支持实际回归，但不能证明已消除游戏的 13–15 ms 等待，也不把少数样本最大值当确定性收益。

实际大往生使用正常五参数合集合同、离线、vsync=true、窗口、输入延长 0，独立副本现场 `.local/daioujou-pacing-r4`／`run-20261020T041204645Z-14680`。同一 MY REPLAY 1780 分／Type-A／Shotia 录像完整播放并自然返回，随后受控关闭为已知清理限制 27。F8 时 UI 约 13 秒，取其后 2–32 秒 `8633119210246–8633149210246`：1,800 个连续 submission（7854–9653），无缺号，包含 4 项完成编译。平均间隔 16.666 ms、p95／p99 直方图上界 17.1／17.2 ms、最大 17.311 ms；指令处理均值 3.985 ms，提交外均值 1.570 ms、最大 2.828 ms，最终输出均值 0.102 ms。正常节拍等待均值 10.957 ms，完整 GPU／呈现栅栏保留。本次未出现此前两次 33 ms，但旧 r3 与此窗口不是精确游戏帧对齐，均值差异不单独归因于信号量改动。

实际日志 SHA-256 为 `db3f968aec5643952a69575a197561e518dcf262a6cde272b47f7807906325af`，分析报告 `.local/analysis/daioujou-pacing-r4-semaphore-gameplay.json`。最终后台排空 accepted=written=47448、dropped=0、format/sink failure=0；全进程仍有 434 次日志竞争拒绝，不能称所有诊断无损。此批是软件路径与短回放的改善证据，不关闭整体性能门槛，不替代剩余主要模式、较低配置覆盖及 SDOJ 声音／输入调查。

复用发布还显式使用 notifier 的 release／下一次复用的 acquire；引用计数只证明独占生命周期，不能单独作为链节点写入的发布屏障。该补强后的宿主为 `d4b721dd7ed0aec85dde200ccb78a26984974116c61ddff7c405453c2d2c0568`，快照 `26caf45ce0140cdbf39ea954179b9894772a8d1c6e0b8e0dbeed1c469f0065e2`，GPU 不变。完整源码 372／原生 73 再次通过，日志为 `.local/v03-semaphore-software-r3-{build,source,native}.log`。同参数离线 `semaphore-software-r3/report.json` 六组正确性通过、约 20.688 秒；连续 14／16 任务中位数均为 17 µs，p95 为 23／21 µs，但间隔派发 p95 增至 168／305 µs、最大 8.837／9.191 ms。尾部改善没有在这轮保持，不能接受“交接长尾已修复”的结论，也不能仅凭此轮差异归因于发布原语。上面的实际 r4 明确属于发布补强之前的候选；最终程序的主要模式及帧节拍仍需合并做轻量实机回归，未由组件通过替代。

### 无窗口离线性能 bench

入口复用配套 `KetsuiNativeProbe --offline-bench`，由脚本校验构建收据、复制匹配 GPU DLL 到新的本地目录，再启动独立诊断进程。不装载 ROM，不进入客体，不创建游戏或呈现窗口，不使用原不限速游戏诊断。构建及运行方法：

```powershell
./scripts/Build.ps1 -Games ketsui,daioujou -Jobs 2
py -3.12 scripts/ps4/run_offline_bench.py --output .local/offline-bench/baseline
py -3.12 scripts/ps4/run_offline_bench.py --output .local/offline-bench/candidate --compare .local/offline-bench/baseline/report.json
```

默认每组 16 次预热、128 个计时样本、三次独立进程重复。快速筛选可加 `--samples 32 --warmup 4 --repeats 1`，最后再用默认参数比较。输出目录必须是 `.local` 下的新目录，不覆盖旧结果、游戏数据或 profile。`report.json` 绑定源码快照、可执行文件／GPU SHA-256、API、主机与实际 Vulkan 设备／驱动；比较要求相同参数／主机／设备。原始 JSONL、stderr、逐轮和汇总的精确 nearest-rank 分位数均保留。测试期间不并行编译、运行其他测试或另开游戏。

六组固定负载包括：

- 两组自造 GCN 点／颜色着色器与 PM4：170 draw／60 prefetch 和 250 draw／86 prefetch，分别复用实际 `ExecuteNativeGpuCommands`、原生内存跟踪、驻留 GPU、提交及完整 Drain／回写回调。在第 8 个 draw 后执行一次 ACQUIRE，最后提交；交替两种颜色，逐次实际读回核对像素，最后核对整体绘图次数。复制、指令细分、worker 交接、完成等待及验证读回分别计时。
- 14 和 16 个任务的批量信号量交接，各有连续派发与生产 `NativeDisplayClock` 的 60 Hz 间隔派发。六个 host worker 使用生产 semaphore 函数，每个任务执行确定性计算；逐项核对完成数量及 checksum。默认继承宿主可运行范围；`--worker-affinity pinned` 显式复现退役的硬逻辑 CPU 映射，仅作组件对照。间隔等待单列，不加入任务交接耗时。

可选 Vulkan validation instrumentation 在 bench 中关闭，像素、任务、绘图计数及错误检查仍强制执行；游戏默认行为没有改变。第一份 smoke 因沿用游戏默认请求的验证层在本机不可用而失败，保留在 `smoke-r1`，不当作通过。显式设置后的 `smoke-r2` 和完整运行通过。

`.local/offline-bench/software-baseline-r1`／`r2` 为相同程序的 A/A：各六组×三次，实际测量约 20.188／20.172 秒；不含启动前构建收据核对时间。源码快照 `537ae89a55b138a36eb60187bdb8a858f894413726b5d4de579d8887a98b6b95`，宿主 `1ed2d643fec8c2388edd208b1785419dbaccc351ab6ca58a7f5ad314123eea88`，GPU `bcfc29dcc2b603722604882767d6a215479a10ade9d8eca4db5f1b2533aacdc3`。每组共 384 个正式样本，全部像素／任务／绘图计数和报告完整性检查通过。

同程序对照中，170-draw 的指令处理均值约 0.886→0.881 ms，250-draw 为 1.125→1.070 ms；这种约 0.6–4.9% 的单轮均值差异并不是优化收益。连续任务交接均值约 19–20 µs，60 Hz 间隔后的约 87–111 µs。第二轮 16 任务间隔测试最大 6.719 ms，原始日志配对到工作线程任务授予后约 6.696 ms 才恢复，随后完成信号发出，主线程约 6 µs 恢复；已能离线观察同类数毫秒尾部，但没有复现游戏中的 13–15 ms，不能据此排除后者。尾部分位数与最大值在 A/A 中也明显波动，后续比较必须连同逐轮分布、原始等待链和正确性审查，不凭一次百分比或最大值关闭问题。

这不是游戏帧的完整录制回放：自造着色器、单像素目标及资源变化远简单于游戏，只是复用实际路径的固定组件负载；不能把约 0.9–1.1 ms 套作游戏成本。它不覆盖最终显示合成／呈现／扫描输出、完整客体逻辑、游戏资源载入或较低配置硬件，也不把 component throughput 叫作游戏 FPS。用途是快速筛选和排除优化候选，之后集中做有针对性的正常游戏回归。v0.3 总体性能、其他模式及 SDOJ 调查门槛仍开放。

### 绘图提前提交与排空回调修正

正常路径现在将已记录的绘图指令提前提交，让显卡利用原本的节拍等待时间；最终合成／呈现仍在原显示时钟边界执行。没有更改游戏速度、原作减速、ACQUIRE_MEM 的同步回写、呈现栅栏或 buffer label 的完成条件。同步 Drain 另保留第一次 Finish 和下载回调 FIFO 屏障，但将回调屏障绑定到已提交的 timeline tick，不再为唤醒 CPU 回调额外提交／等待一批空 GPU 指令。普通当前命令回调接口不变；图形自测验证旧回调按序完成且屏障不增加提交或 tick。

`command_stages` 随原有完成帧记录汇总逐次绘制、ACQUIRE_MEM、预取、提前 Flush 及剩余指令处理的宿主耗时。总和及父作用域由分析器校验，父层减内层另列 worker 交接等成本。它不是 GPU timestamp：绘制包含资源／缓存处理，同步排空包含设备和回调等待。新增 `native_semaphore_wait_timing` 记录对象 handle、请求数、排队／授予／恢复时间和最后授予信号的线程及客体调用点；恢复时间包含重新获得 gate，不是纯调度延迟。只记录至少 1 ms 的批量完成等待、至少 2 ms 的授予后恢复或至少 4 ms 的失败等待，每线程每秒最多 8 条，在释放 gate 后格式化。正常单个工作线程待命不刷日志。

两次独立离线、窗口／vsync=true、按键诊断 0 的 380 分／00:53.45 录像均完整自然返回并正常关窗。均为基于 `5ebc29b` 的工作树开发构建，不是最终发行提交。r5 仅有提前提交及分段计时；r6 再增加排空回调修正和收窄的 semaphore 日志。下面是约 16–47 秒的相近关卡窗口，非逐帧严格对齐的 A/B 或所有卡顿已修复证明。

| 宿主 wall time | 原 r4 | 提前提交 r5 | 再修正回调 r6 |
| --- | ---: | ---: | ---: |
| 指令处理均值 | 3.614 ms | 3.630 ms | 3.567 ms |
| 最终输出均值 | 约 1.340 ms | 0.866 ms | 0.838 ms |
| 两次提交间隔均值 | 约 1.586 ms | 1.582 ms | 1.584 ms |
| 整帧 p99 直方图上界 | 19.3 ms | 18.8 ms | 18.9 ms |
| 整帧最大间隔 | 33.589 ms | 20.055 ms | 19.803 ms |

r5 的 31 秒窗口为 8618711572842–8618742572842，共 1,860 帧且 submission 连续；报告 `.local/analysis/ketsui-pacing-r5-early-flush-gameplay.json`，进程 88332／run-20261020T001122338Z。宿主 SHA-256 `170aa9d7d1de20fbf86843a4daa87734d24434468ece3b6f17fc0ecbe9ba1f8e`，GPU 与 r4 相同；日志 SHA-256 `5a62bb42124bc51c6efa1a23d258e16077e9929c50e2eb79847ea0ae7d85d3ce`。该版会记录正常待命线程，已在 r6 收窄；不能把它的全进程日志量差异当成关卡负载变化。

r6 为 8619295172809–8619326172809，共 1,860 个连续 submission（12278–14137），报告 `.local/analysis/ketsui-pacing-r6-callback-drain-gameplay.json`，进程 84268／run-20261020T002027894Z。宿主 SHA-256 `5e42c9e7f4755f01c0fe4240a058a8d08d5bd8e78f08180489da26345991249f`，GPU `19dd4c8e4d9e86525bdaa113e8b3382b6b997f5c787ddfede2275cb199a3e7ae`，日志 `ede5ce288af42b29667a2a565ec1b506e7eaff87dbd5070a2cabb794bd75ab49`。指令处理的均值细分为绘制／资源 2.648 ms、同步排空 0.749 ms、预取 0.045 ms、提前提交 0.028 ms、其余指令 0.083 ms、外层交接等 0.015 ms；每帧一个 ACQUIRE_MEM、59–61 次预取。最终输出的 GPU completed-fence 等待仍有 3.425 ms 尾峰，指令处理也仍有 6.411 ms 尾峰，不能用均值改善关闭问题。

提交外路径得到同一时刻的生产者／等待者证据：r6 submission 12625 的提交外间隔 8.245 ms，主线程 79596 的 handle 6002453878354739202／请求 16 在 8619300952620–8619300959250 阻塞，最后信号来自线程 69048（eboot+0x1a944a），授予后主线程 6 µs 恢复；这个工作线程在配对 handle 6002453878354739201 被授予任务后，却到 6.613 ms 后才恢复（8619300952615–8619300959228），随后约 16 µs 便发出了完成信号。另两次同链为约 2.6–2.7 ms 的工作线程恢复延迟。它说明尾部来自工作线程的授予后恢复，不是主线程收到完成信号后睡了数毫秒；不把它等同于 r4 那次尚未分解的 19.821 ms。

只读检查本地 eboot 的 0x1a8a40 和 0x1a9390 周边，确认主线程发出 N 个任务、等待 N 次完成，工作线程取队列、调用任务函数、发完成信号后重新等待。没有把原指令正文写入仓库。慢工作线程 guest affinity=32/native=32，另一条慢链为 guest=1/native=1；当前 MapAffinity 仅按可用逻辑处理器序号映射六个客体 CPU，未区分 SMT 兄弟核及混合架构性能类。下一步应做拓扑驱动的映射对照并保留低核数／受限进程 mask 回退，不据此直接改线程优先级、系统电源设置或游戏任务数量。

r6 的 358 项源码检查、73 项原生检查和 4 项 GPU CTest 通过；50 项时序分析用例涵盖新分段的和、类型、嵌套、舍入及窗口边界。回放期间没有并行构建／测试。最终日志排空完成，但全进程仍有 99 条竞争拒绝及一次 frame counter 记录失败，不能宣称完整无丢失；统计窗口的完成帧已单独核对无缺号。两次受控关闭均为既有清理限制 27。v0.3 的三段性能门槛及大往生同路径回归仍开放。

### v0.3 正常节拍的三段成本定位

当前检查覆盖指令处理、最终输出和两次提交间隔三段，不再继续不限速／极限 FPS 试验，也未关闭性能问题。`e462310` 的 `.local/ketsui-pacing-r4` 使用独立存档副本、普通输入诊断 0、离线、vsync=true、窗口模式；同一份 ARCADE／Tiger／380 分／00:53.45 录像完整播放至自然返回，然后受控关闭。宿主 SHA-256 为 `95f386d4d24607a51494e6a006fd29f90e886a4441d4e1b7e0e2e58a4245bab9`，GPU 为 `dc34d3c503bc0de53c1714c54c38a5bcc4eb6476d2ed5ae0c2049075902b67ae`，日志 SHA-256 为 `f9fb4962172e8ad18cb0e033a1bc614b743ffcb9130e7068e4f416b57fb86b04`。诊断构建已配套重建，GPU 的 4 项非不限速机制检查及 47 项分析器用例通过；源码回归为 355 项通过。

F8 后 30 秒（8617894315942–8617924315942，回放约 17–47 秒）有 1,799 个连续 submission，编号 13008–14806。间隔平均 16.675 ms、p99 直方图上界 19.3 ms、最大 33.589 ms。所有帧均有有效的九段后端分解，不存在分段校验失败；报告 `.local/analysis/ketsui-pacing-r4-details-gameplay.json`。GPU 等待函数平均 1.221 ms、p99 上界 3.7 ms、最大 3.847 ms，而交换链获取、合成命令准备、提交和 present fence 等待的均值分别为 0.005／0.017／0.036／0.001 ms。19–20 ms 尖峰中的额外后端时间主要落在 `vkWaitForFences(completed)`，不是获取图像或 present fence；这个函数耗时依然混有驱动／调度，尚不能当作纯 GPU 运算。下一步需区分设备执行与完成通知／唤醒，不根据这组数据更改电源、驱动或删除同步。

更长的 submission 14045 为另一条路径：33.589 ms 间隔中，提交本身 12.094 ms，两次提交间隔 21.495 ms，其中主提交线程 83552 在 `sceKernelWaitSema` 阻塞 19.821 ms，调用点为 eboot+0x1a8a7f。该事件的 `detail=16` 是请求计数，不是 semaphore handle，现有日志不足以确定具体对象、最后发信号的工作线程及授予后唤醒延迟。后续应沿此生产者／等待者链定位；不得把该长帧归因于 GPU，也不能把平均 1.58 ms 笼统归作“其他”。3.6 ms 指令处理段的全局 Drain 仍为待测候选，未证明占比或实施同步削减。

本次最终日志排空完成、满队列淘汰和格式／写入错误为零，但全进程有 94 条竞争拒绝，不宣称全日志完整；上述帧编号已单独确认连续。退出仍为已知受控关闭 27，不代表客体完整清理通过。这是定位进展，不是性能修复验收。

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

离线呈现不限速对照通过临时进程环境 `CAVE_DIAGNOSTIC_UNPACED=1` 启用，仅允许 `--profile benchmark-<名称>`、共享联网关闭且 `vsync=false`。`run.json.diagnosticUnpacedPresentation` 和 `native_presentation_pacing` 明确标注；启动器／默认 INI 不保存此选项。直接后端使用 `--diagnostic-unpaced on`，同样要求离线合集 INI 与绝对的 benchmark-* 用户目录。它只跳过软件 `WaitFlipOpportunity`，保留真实 GPU／WSI fences、内存写回、客体 vblank 时钟和其他游戏等待，不伪造成功。因此若仍被音频、客体时钟或逻辑等待限制，观测 FPS 不是整机性能上限，需要继续定位；测试值不作为正常游玩节拍验收，也不得用加速局上传成绩。使用独立 profile 副本，正常节拍与对照分别留存日志。

`eb9235a` 的正常模式已完整回放同一 380 分录像至自然返回。约 15–47 秒（8616883466111–8616915474968）的 1,921 帧平均间隔 16.666 ms、p99 上界 19.0 ms、最大 19.636 ms。软件节拍等待平均 10.071 ms，后端呈现平均 1.310 ms；最慢 submission 9680 的这两段为 10.522／3.893 ms，指令处理 3.639 ms，未随尖峰增加。约 2.97 ms 的额外间隔主要出现在后端，但该作用域尚不能区分驱动、GPU 与调度。报告在 `.local/analysis/ketsui-pacing-r3-normal-gameplay.json`，不算性能问题收尾。独立 `82528` 不限速试验及 `71468` 仅关闭宿主 vsync 的试验均被 CU 观察为黑屏，仍有约 60 Hz 提交；前者日志明确有客体 `sceKernelWaitEqueue` 等待。两次未进入有效回放、没有取得性能上限，均已正常关窗并保留现场，不把这些记录冒充游玩／图形正确性验收。

新的 `native_gnm_frame_completed.backend_stages` 在同一记录内细分 resident worker 的准备、旧资源排空、交换链获取、合成命令准备、提交、`vkQueuePresentKHR` 调用、GPU fence 等待、present fence 等待及收尾。分段连续且总和必须等于 `duration_us`，还要嵌套在同帧 `present_stages.backend_present_us` 内（允许 1 µs 舍入差）；失败、最小化或重试不生成有效分段，不将缺失值算零。`backend_present_substages` 汇总这组数据，外层减内层另记线程交接／API 收尾等开销。GPU／present fence 的等待函数耗时仍包含驱动和调度，不是 GPU timestamp 或物理扫描时间。共享 GPU 运行接口提升为版本 6，旧宿主会拒绝混用；后端和两款宿主必须同次重建。

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
