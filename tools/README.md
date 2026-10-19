# 开发工具

在合集根目录准备工具和 SDK 依赖：

```powershell
./scripts/Prepare-Tools.ps1
py -3.12 scripts/sdk_sources.py restore
./scripts/Build-Sdk.ps1 -Jobs 2
```

| 路径 | 内容 |
| --- | --- |
| packages.lock.json | LLVM、CMake、Ninja 的固定版本、下载地址及 SHA-256 |
| sdk-source.lock.json | 当前 SDK 的第三方依赖地址与固定 Git 提交 |
| ps4-source.lock.json | PS4 后端的 19 个独立依赖提交，恢复到 .deps/ps4 |
| ps4-tools.lock.json | PS4 专用 clang-cl 23、链接器和 compiler-rt 的固定哈希 |
| python-ps4.lock.txt | 仅用于解压 LLVM 归档的本地 zstd 解码器 |
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

工具准备的 `-Ps4` 在原有公共工具之外准备 `tools/apps/llvm-ps4` 和独立的解压 Python 环境，不修改系统 Python 或现有 360 编译器。可用 `-CacheFrom '<工具缓存的 apps 目录>'` 复制匹配安装；无缓存时从固定归档恢复。只有准备阶段需要网络，已经准备好的原生构建不联网、不需要 ROM，也不启动游戏。

PS4 输出分别在 out/ps4-native 和 out/ps4-gpu，收据在 out/ps4/build-receipt.json。它绑定维护源码、383 个上游及已声明本地适配文件、19 个依赖、工具以及五对程序／PDB；已有产物在输入变化后不能继续冒充当前构建。`Build.ps1 -Games ketsui` 的完整数据准备和游戏启动流程仍在接线，当前需显式使用 `-NativeOnly`；清单不因此开放启动入口。

迁入来源和提交见 [源码来源](../docs/SOURCE_ORIGINS.json)。最初导入 SDK 时的文件快照保存在 [迁入基线](../docs/provenance/sdk-import-baseline.json)，用于追溯，不用于选择工具或恢复依赖。
