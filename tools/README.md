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
| apps/ | 本机安装的便携构建工具，不进入 Git |
| downloads/、state/ | 下载缓存和准备状态，不进入 Git |

SDK 维护源码位于 `third_party/rexglue/`。依赖从锁文件中的开源上游恢复，不需要下载旧游戏工程。`--cache-from` 是显式、可选的本机缓存参数，不作为新环境的前置条件。

PS4 后端源码在 `backends/ps4`，提取的开源图形组件及许可证在 `third_party/ps4`。准备或只读复核依赖：

```powershell
py -3.12 scripts/ps4_sources.py restore
py -3.12 scripts/ps4_sources.py verify
```

可加 `--cache-from '<缓存目录>'`；该目录下按依赖名放置干净、版本及 origin 完全匹配的 Git checkout。复制后的仓库拥有独立 Git objects，不保留旧路径、硬链接或 alternates；已有目录版本不符时保留并报告。PS4 与 360 的 SDL、Vulkan 等依赖分开恢复，互不改写。PS4 的原生宿主编译和合集启动接线仍在进行，当前 `Build.ps1 -Games` 尚不接受 ketsui。

迁入来源和提交见 [源码来源](../docs/SOURCE_ORIGINS.json)。最初导入 SDK 时的文件快照保存在 [迁入基线](../docs/provenance/sdk-import-baseline.json)，用于追溯，不用于选择工具或恢复依赖。
