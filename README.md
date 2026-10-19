# CAVE PC Collection

CAVE 游戏的 Windows PC 移植合集。目标是使用一个启动器、一份 `config.ini` 和统一的开发工程，按需安装和运行不同游戏。

各游戏保留自己的适配代码和数据，Xbox 360 与 PS4 运行时分别维护。只安装一款游戏的数据也可以使用，不必补齐整个合集。

## 当前版本

当前可玩作品为虫姬二、ESPGaluda II 和绊地狱，怒首领蜂大往生与 ESP Ra.De. 显示 Coming Soon。原生启动器提供纵向游戏横幅、独立的数据检测与选择、配置入口和游戏进程管理；三款作品共用 INI，分别保存存档、录像、缓存和日志。

阅读 [分阶段实施计划](docs/IMPLEMENTATION_PLAN.md)，其中定义了目标目录、可选游戏数据、公共配置、源码迁移、构建发布和每一步的验收条件。

虫姬二与 G2 共用 Xbox 360 SDK、宿主、输入、诊断及 HTTP 通信，可单独或联合构建。绊地狱保留独立 PS4 运行时及驻留 GPU，首次启动从本地数据准备运行镜像。三款作品已接入正式社区服务；各模式、保存、录像和设备的实际覆盖范围见验收矩阵。

源码来源和恢复方法见 [迁移基线](docs/BASELINE.md)，完成范围见 [验收矩阵](docs/TEST_MATRIX.md)。

首版 **v0.1** 包含 ESPGaluda II、虫姬二和绊地狱。提供内部完整版（程序、完整游戏数据和开发源码）与对外程序包（不带 ROM 和源码）两种打包流程，具体范围见实施计划。每个包的版本、源码提交及文件哈希记录在 `release.json`。

v0.1 对应标签 `v0.1`、源码提交 `f652df0`。另备 `CAVE-PC-Collection-v0.1-source.zip`：包含同版本的维护源码、独立 Git、固定依赖源码与构建说明，不含 ROM，可向程序包提供者索取。

打包入口为 `scripts/Release.ps1 -Flavor Internal` 或 `-Flavor Public`。两者从同一发布提交的配套构建组装，生成完整文件清单、ZIP 和 SHA-256，不自动上传；发布前另做实际解压和代表性游玩验收。

当前保留绊地狱退出清理未完成的已知限制。虫姬二与绊地狱的偶发音画卡顿调查、编译器统一，以及更多设备／模式与长期稳定性覆盖继续安排在后续版本；实际验收范围见验收矩阵。

## 游戏数据

用户不需要拥有所有游戏的数据。每款游戏独立检测，缺少某款游戏不影响其他游戏，也不影响打开启动器。

默认放入 `game-data/<游戏 ID>/`，也可以在根 `config.ini` 指向现有目录。原始游戏数据只读；存档、录像、缓存、准备镜像和日志分别保存在合集自己的目录。

源码仓库保存维护源码、配置模板、生成声明、测试和工具版本锁，不保存 ROM 或由 ROM 生成的游戏程序正文。

联网默认开启，无需注册；在同一份 INI 的 `[community]` 中修改昵称 `Player`，或设置 `enabled = false` 关闭。合集 API 为 `https://xiaolu.life/cave/api/v1`，客户端按游戏 ID 连接各自的榜单及录像服务。待传任务保留原昵称与服务来源，不会因修改配置而自动改投其他服务。

## 开发与检测

Windows x64 下准备 PowerShell 7、Python 3.12、Git、VS 2022 C++ Build Tools 和 Windows SDK，然后执行：

源码开发请使用较短的英文目录，例如 `E:/CaveDev/CAVE-PC-Collection`。当前固定 CMake 在中文源码路径下可能异常退出；玩家包的中文解压目录与游戏数据路径不受这项开发限制影响。

```powershell
./scripts/Prepare-Tools.ps1
./scripts/Build.ps1 -Jobs 2
./scripts/Test.ps1 -Scope All
./out/common/bin/cave_inspect.exe --root .
```

公共组件和检测工具不需要 ROM，也不需要先构建游戏 SDK。检测现有数据时可以显式选择目录：

```powershell
./out/common/bin/cave_inspect.exe --root . --game espgaluda2 --data-dir 'D:/Games/G2' --full
```

工具只读返回 JSON，分别报告游戏接入状态和数据状态。快速检查不冒充完整校验；`--full` 校验所有必需文件的 SHA-256。某款数据缺失不影响其他游戏；后端尚未迁入时，即使数据完整也不会显示为可启动。更多目录、命令及接入约定见 [FOR_AGENT.md](FOR_AGENT.md)。

## 启动器

构建后运行根目录 `CAVE-PC-Collection.exe` 或 `启动游戏.bat`。单独构建启动器可用 `./scripts/Build.ps1 -LauncherOnly -Jobs 2`，不需要 ROM 或游戏 SDK。启动器将素材嵌入程序，不依赖浏览器或旧游戏工程。

配置按钮用文本编辑器打开根 `config.ini`；保存修改后重启游戏生效。“游戏数据…”菜单提供两种操作：选择数据目录只保存路径；复制到合集会完整复制到默认目录并逐文件校验，不移动或修改原件。已有非空目标不会被合并或覆盖，合集内路径使用相对路径，方便移动整套目录。复制过程中关闭窗口会先取消并清理本次临时文件。方向键选择作品，Enter 启动或选择数据，Tab／Shift+Tab 切换配置、日志、数据与刷新按钮。启动前完整核对数据与配套运行文件；检测失败时不执行游戏。

虫姬二的 DLC 默认放在 `game-data/futari/dlc/`，也可用 `[game.futari]` 的 `dlc_dir` 指向独立目录；显式留空可关闭附加内容。Black Label 与 Ver1.01 分别校验，缺少一包不影响本体及另一包。F11／Alt+Enter 切换全屏，F8 在当前日志中加入时间标记。

绊地狱首次启动会从本地数据自动生成 `prepared/ketsui/` 下的运行镜像和资源读取覆盖，不修改原始数据，也不需要 Python。后续启动核对缓存收据后复用；程序或数据变化时重新准备。每次运行的配置快照、程序哈希和实时日志在 `logs/ketsui/run-*/`。主动关闭窗口后会恢复启动器；目前仍保留客体退出清理未完成的已知限制，并记录在日志中。

开发时可指定独立测试 profile：

```powershell
./CAVE-PC-Collection.exe --game espgaluda2 --profile test
```
