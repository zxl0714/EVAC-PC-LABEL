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

先准备本工程的 SDK 和原生分析工具：

```powershell
py -3.12 scripts/sdk_sources.py restore
./scripts/Build-Sdk.ps1 -Jobs 2
./scripts/Build.ps1 -Games espgaluda2 -NativeOnly -Jobs 2
py -3.12 scripts/analyze_modules.py --game espgaluda2 --data-dir game-data/espgaluda2 --output .local/analysis/g2-census
```

默认检查主程序及七个 overlay；也可以重复指定 `--module default.xex --module dat15.xex`。每个模块使用独立的本机检查进程，记录导入接口、映像区段和输入身份；overlay 另做三次静态装卸载及重叠加载拒绝检查。工具核对 SDK 构建收据和配套 runtime，并在结束时复核整个数据目录未变。

`analysis.json` 是成功收据。超时或失败会保留现场，但不生成成功收据；查看对应的 stdout／stderr 日志。报告中的“已注册接口”和“静态装卸载通过”不代表接口功能或实际模式运行已经验收。

## 回调与社区边界

生成过对应游戏的本地重编译代码后，可以检查数据引用但尚未注册的短跳板候选：

```powershell
py -3.12 scripts/callback_inventory.py .local/analysis/g2-census --game espgaluda2 --data-dir game-data/espgaluda2 --output .local/analysis/g2-callbacks.json
py -3.12 scripts/espgaluda2/analyze_community.py .local/analysis/g2-census --data-dir game-data/espgaluda2 --output .local/analysis/g2-community.json
```

回调检查读取 `generated/espgaluda2/<模块>/*_register.cpp`，结合经过大小和哈希校验的区段筛选候选。候选需要逐项审阅，不自动修改函数声明，也不能据此判断必然存在崩溃。

社区检查保留在 G2 专属目录，因为地址、84 个 view 和成绩先于录像的规则属于本游戏。它核对已审阅表格与函数边界的身份；语义说明见 [G2 原生社区流程](../games/espgaluda2/docs/PROGRAM_LAYOUT.md#原生社区流程)。扫描成功不等同于联网收发通过。

## 已有运行日志

```powershell
py -3.12 scripts/analyze_run.py logs/espgaluda2/<单次运行目录> --output .local/analysis/g2-run.json
```

也可以直接传入 `game.log`。汇总包括模式加载／卸载、未配对事件、错误、F8 标记和按设备分组的输出音频样本。用户关闭之后的音频样本不参与统计；没有错误行不证明日志完整，输出队列正常也不证明游戏声音命令没有延迟。

虫姬二日志中的 `guestAudio` 另行统计已匹配的客体队列头年龄、cue 请求 API 完成耗时、超过 20／100／1000 ms 的队列头样本数、有效玩法探针和丢弃事件。它与输出音频队列分开：同一队列头的重试可能重复采样；没有有效样本返回 null，而不是零延迟。`traceScopes` 表明实际覆盖模块，cue 名称不等于已观察到拾取道具；这些计时也不是扬声器实际发声时刻。

## 绊地狱帧节拍与后台日志

```powershell
py -3.12 scripts/ps4/analyze_timing.py logs/ketsui/<单次运行目录> --output .local/analysis/ketsui-timing.json
```

F8 的 `native_timeline_marker.clock_us` 与帧记录使用同一个宿主单调时钟。用 `--from-clock-us` 和 `--to-clock-us` 选择关卡区间，避免把菜单、载入与实际游玩混成一份极值。`completion_interval_us` 是宿主完成两次呈现之间的间隔，不是输入延迟或物理屏幕扫描时间。百分位采用报告中的有界直方图上界，不能把多个窗口的百分位平均成全局百分位。

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
