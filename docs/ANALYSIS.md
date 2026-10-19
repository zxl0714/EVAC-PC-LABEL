# 分析与诊断工具

这些工具用于检查数据版本、模块声明、日志，以及 G2 和虫姬二的社区接口。它们不启动游戏窗口；静态模块检查不会执行客体指令，也不初始化 GPU、音频或输入。实际进关、声音响应和流畅性仍由游玩回归验证。

在合集根目录运行下列命令。`--data-dir` 显式指定要读取的数据，不修改根配置；使用外部目录时替换示例中的路径即可。工具不需要原 ESPGaluda2-PC 工程。

## 数据盘点

```powershell
py -3.12 scripts/xex_inventory.py game-data/espgaluda2 --output .local/analysis/g2-inventory.json
```

逐文件计算大小与 SHA-256，并有界读取 XEX2 头中的基址、入口、Title ID 和压缩信息。输出只有元数据，不含资源或程序正文。不指定 `--output` 时只输出到终端；`--xex-lock` 可另外写一份程序元数据用于比较，不会自动更改维护中的版本清单。

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
