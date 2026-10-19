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
| apps/ | 本机安装的便携构建工具，不进入 Git |
| downloads/、state/ | 下载缓存和准备状态，不进入 Git |

SDK 维护源码位于 `third_party/rexglue/`。依赖从锁文件中的开源上游恢复，不需要下载旧游戏工程。`--cache-from` 是显式、可选的本机缓存参数，不作为新环境的前置条件。

迁入来源和提交见 [源码来源](../docs/SOURCE_ORIGINS.json)。最初导入 SDK 时的文件快照保存在 [迁入基线](../docs/provenance/sdk-import-baseline.json)，用于追溯，不用于选择工具或恢复依赖。
