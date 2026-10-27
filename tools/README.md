# 开发工具

## PS4 玩家解包工具

`tools/ps4-extract/` 是轻量 Win32 图形入口及提取核心，构建不依赖 ROM。
`tools/ps4-extract.lock.json` 固定 PkgTool 归档和配套组件的 SHA-256。

```powershell
py -3.12 scripts/ps4_extract_tools.py prepare
./scripts/Build.ps1 -LauncherOnly -SkipLauncherStage -Jobs 2
```

双击 `out/ps4-extract-player/tools/Extract-PS4.exe` 做本地验证；该目录包含
玩家所需的工具、依赖、许可和双语说明，不是版本发布包。图形入口使用
系统 Win32 控件及静态 C++ 运行库，PkgTool 使用 Windows 的 .NET Framework 4。
正式打包按 v0.7.6 配置自动收集这些组件。脚本高级入口仍是
`scripts/Extract-Ps4Pkg.ps1`，会默认找到项目内的固定 PkgTool。

## 构建工具链

在合集根目录准备工具和 SDK 依赖：

```powershell
./scripts/Prepare-Tools.ps1
py -3.12 scripts/sdk_sources.py restore
./scripts/Build-Sdk.ps1 -Jobs 2
```

| 路径 | 内容 |
| --- | --- |
| packages.lock.json | 全部后端共用的 LLVM 23.1.3、CMake、Ninja；固定归档、安装目录与各工具 SHA-256 |
| sdk-source.lock.json | 当前 SDK 的第三方依赖地址与固定 Git 提交 |
| ps4-source.lock.json | PS4 后端的 19 个独立依赖提交，恢复到 .deps/ps4 |
| python-tools.lock.txt | 仅用于解压共用 LLVM 归档的本地 zstd 解码器 |
| apps/ | 本机安装的便携构建工具，不进入 Git |
| downloads/、state/ | 下载缓存和准备状态，不进入 Git |

SDK 维护源码位于 `third_party/rexglue/`。依赖从锁文件中的开源上游恢复，不需要下载旧游戏工程。`--cache-from` 是显式、可选的本机缓存参数，不作为新环境的前置条件。

PS4 后端源码在 `backends/ps4`，提取的开源图形组件及许可证在 `third_party/ps4`。准备或只读复核依赖：

```powershell
./scripts/Prepare-Tools.ps1 -Ps4
py -3.12 scripts/ps4_sources.py restore
py -3.12 scripts/ps4_sources.py verify
./scripts/Build.ps1 -Games ketsui -NativeOnly -Jobs 2
./scripts/Test.ps1 -Games ketsui
```

依赖恢复可加 `--cache-from '<缓存目录>'`；该目录下按依赖名放置干净、版本及 origin 完全匹配的 Git checkout。复制后的仓库拥有独立 Git objects，不保留旧路径、硬链接或 alternates；已有目录版本不符时保留并报告。PS4 与 360 的 SDL、Vulkan 等依赖分开恢复，互不改写。

`Prepare-Tools.ps1` 为所有后端准备同一份 `tools/apps/llvm-23.1.3` 和 `tools/apps/tool-python` 解压环境；`-Ps4` 仅保留为旧命令的兼容别名，不再选择另一份编译器。360／启动器使用其中的 clang++，PS4 使用 clang-cl，链接器、符号工具与 compiler-rt 同样来自该发行包。工具位置和哈希由 `scripts/toolchain.py` 统一解析，构建收据绑定同一套实际工具；不读系统 PATH 选择替代编译器。

安装使用版本化目录和按锁文件区分的收据，已有 `llvm`、`llvm-ps4` 及其准备记录保持不动，不会覆盖成新版本。可用 `-CacheFrom '<工具缓存的 apps 目录>'` 复制匹配安装；无缓存时从固定归档恢复。准备阶段使用项目内 Python 环境安装固定哈希的 zstd 解码器，不改系统 Python；构建和玩家启动不需要该解压依赖。新工具链属于 v0.1 后的开发，完整重建和实际运行验收仍进行中。

构建入口统一执行两次配置，并在编译前核对实际 CMake 缓存中的工具路径、优化参数、安装位置和后端选项。这使旧编译目录切换工具链后仍使用请求的发布配置；配置失败或参数不一致时不会开始编译。

PS4 输出分别在 out/ps4-native 和 out/ps4-gpu，收据在 out/ps4/build-receipt.json。它绑定维护源码、383 个上游及已声明本地适配文件、19 个依赖、工具以及六对程序／PDB；已有产物在输入变化后不能继续冒充当前构建。`Build.ps1 -Games ketsui` 无需 ROM，校验后整理配套宿主／GPU／符号到 runtime；首次运行时原生准备本地数据。已接通合集启动器，不再要求用旧开发入口启动。

迁入来源和提交见 [源码来源](../docs/SOURCE_ORIGINS.json)。最初导入 SDK 时的文件快照保存在 [迁入基线](../docs/provenance/sdk-import-baseline.json)，用于追溯，不用于选择工具或恢复依赖。
