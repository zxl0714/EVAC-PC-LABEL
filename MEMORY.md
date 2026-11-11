# Agent memory: game testing

**每次操作游戏前先读本文件。** 保持短小；更新已验证的做法，不追加逐轮日志。
版本进度、进程与证据位置写进 `AGENTS.md` 的 Current objective。

## 执行计划与效率

- 开始任务先列一份短计划：完整目标、必做项、执行顺序、验收证据与结束条件；有新信息再调整。
- 每次测试先明确要回答的问题，拿到足够证据就结束。已通过且未受改动影响的结果直接复用；
  仅因相关代码改变、新失败或证据确实不足而补测，不反复走相同流程、截图或重搭工具。
- 选小而完整的代表集合，区分问题定位、实验和交付验收；额外实验不自动成为出包门槛。
- 用户要求收尾出包时，冻结实验范围，先列剩余交付步骤并执行到底；不为了更漂亮的数字
  追加多轮优化或重测。未达成的指标如实记录，不写成已完成，也不悄悄扩大发版门槛。
- 等待编译时处理独立的必要工作；先核实原会话状态，不因暂时没输出而重启或重复执行。
- 提高效率不能省略必做项、降低目标或用间接证据冒充完成。收尾按原计划逐项核对，
  未完成的明确保留并继续处理；文档、构建、组包及交付验证都落实后才算完成。

- 仅改文档、组包脚本或独立后端后，直接走 Release.ps1；组包按组件输入复用旧构建收据。
  不为统一提交号而重新执行全部 Build；相关输入变化会明确报告需重建的组件。
- 同一构建的多种包共用一次完整认证，复制文件时仍核对认证哈希；每份 ZIP 只做一次真实
  解压验收，按 ZIP SHA 复用结果。PS2 有约 53 万个生成小文件，不按包的种类重复扫描。
- 发包默认只做程序包，不自动沿用历史 Internal 范围；含游戏数据的包需用户本次明确要求。
- PS2 无 ROM 机制检查优先用 `out/ps2-native-check`。为新增小测试改 CMake 后，
  不先让带旧版约 53 万个生成文件的 `out/ps2-tools` 重配置；完整游戏配置待新输入就绪再做。
- PS2 从 ToolsOnly 切到完整运行时配置可能重新链接生成器；生成游戏代码前先完成目标
  CMake 配置及生成器构建。若只是工具身份变化，使用现有生成结果等价检查与增量构建，
  不重建已验证的整个游戏。
- PS2 实际游玩使用玩家准备链输出的完整目录（含 `MODULES/SDRDRV.IRX`）；
  生成分析用的薄 `disc` 目录只供离线探针，不能用来启动游戏。模式回调探针则需要
  `disc/module-16/analysis.elf` 等分析文件；两种目录用途不同。
- PS2 画廊逐层返回需等过场完成（约 1.5 秒）；连发两次返回会漏掉第二次。
  诊断手柄 a/b/x/y 是 Cross/Circle/Square/Triangle，不能直接按 PS2 原始位值理解。
  已有帧日志只保留前 36,000 帧、输入事件也有容量上限，超出后不冒充完整记录。
- 出包后写交付检查点并冻结源码提交与 ZIP；仅记录结果的文档提交不触发重建、重打包。
- 发布构建先冻结并提交代码。未提交源码下完成的构建记录不能直接用于正式组包；
  需要在干净提交上执行增量构建并重新认证，不手改收据，也不重生成未变化的游戏代码。

- 长帧分析先区分启动／自动演示／实际投币玩家局，并把资源上传时间对到具体画面。
  一次首次界面图集准备不等于关内持续卡顿，先确认用户场景再决定是否优化。
- 实测帧时间时停止同盘的目录遍历、构建和打包。Ibara 从机械盘读 ISO 时，同盘递归搜索曾让
  一次 80 KiB 的同步读取超过 1 秒。先用普通游玩对照，再开局部像素采集测输入响应。
- rexglue 同一 PC 只保留一条 midasm hook。延迟观察器与生产补丁重合时，观察器必须调用原钩子；
  生成后核对实际调用和运行日志，避免测量版悄悄丢掉被测优化。狱门山 core Begin/End 已在
  `tools/latency/prepare_guwange_native.py` 中组合，输入回调仍由原程序负责录像读写。
- Xbox 360 更换图形后端时，要看到窗口里的关卡。若窗口黑而 guest_swap 仍是 60 FPS，
  先对比 Presenter 的客体图像回读与窗口，再用 FIFO 对照。回读有标题而窗口黑时，优先查交换链。
- 帧日志缺行不等于渲染掉帧。先查异步日志 contended／dropped_oldest 与 submission 连续性；
  只少一条时可用两次 completed_clock_us 之差减去后帧 completion_interval_us 还原，
  连续多条缺失不能据此分解成每帧时长。

## 开始前，先确认这四件事

Xbox 360 存档录像的 v0.9.5 测试副本、菜单步骤和结束证据集中在
`.local/v095/saved-r1/`、`saved-run.py`、`sdoj-saved-auto.py` 与
`saved-roundtrip.json`。最大往生跨进程比较先看异步加载结束的
`play_state=1`，再固定对齐；两张绘图页的 X 原点相差 384 像素，
加载差一帧会让后续命令哈希全部不同。保留原始哈希并检查实际坐标，
不要仅凭哈希差异就反复重跑或调整帧节拍。两套关卡模块均需自己的地图初始化补丁。

- 当前 EXE／构建、游戏数据版本、独立测试目录和 PID。
- **该进程实际读取的配置**：优先看 `config.snapshot.ini`、启动参数和按键绑定日志。
  主目录 INI、旧测试 INI、Xenia 和移植版的映射可能完全不同。
- 查上游实现前先确认实际编译的源文件：PS2 的 `third_party/ps2/runtime` 和构建目录的
  source view 会覆盖 `.deps` 原始文件；不要把已修过的依赖原版问题再当作当前缺陷。
- 本轮只列一个小而完整的验收集合；已通过且未受改动影响的项目不重跑。
- v0.9.0 收尾范围以 `docs/PS2_RUNTIME_PLAN.md` 为准，不因上下文切换重开已通过的测试。
- 原始 ROM、冻结发布包和旧项目保留；测试局允许自行重置，不再为同一授权反复询问。
- PowerShell 下给 `rg` 传目录并用 `-g '*.cpp'` 等筛选，不传 `scripts/ps2*` 这样的字面路径。
  独立 CMake 微基准也先点入 `scripts/Build-Common.ps1` 并执行
  `Initialize-CollectionBuildEnvironment`；仅找到 clang 不代表 Windows SDK 的 rc／链接环境已准备好。

## Ibara：菜单与开局

- **日版确认 X（PS2 ○），返回 Z（PS2 ×）**；方向键正常，Enter 是 Start。
  不能沿用 ESPGaluda 的确认键。用 Z 确认 ARCADE 会返回标题，不是加载失败。
- 演示中按 Enter 后等待 MODE SELECT 出现，再用 X 选模式。
  诊断状态 `EE[0x338b08]`：9 是模式／设置菜单，3 是标题，5 是关内；
  `EE[0x338e7c]` 非零的关内是自动演示，不能作为玩家局验收。
- 菜单切换有文字压缩／旋转过场，刚按 X 后会像竖条或缺笔。先等约 1.5 秒再看稳定画面，
  不把过场截图当成字体／纹理故障。已用原始文字网格、UV 和帧缓冲离线核对。
- 原始 ISO 完整目录的最新验证：正常键盘＋200 ms 诊断脉冲，X 关闭读取提示，
  Enter 跳过片头、再次 Enter 后确认 MODE SELECT，再逐层 X。不要盲发16次 Start；
  最近该脚本最后仍在演示，不能算进关证据。先以截图确认当前菜单，再继续输入。
- 50 ms 诊断脉冲虽能到达 PadRead，菜单仍可能漏掉；不要反复猜键。必要时重开
  使用现有 200 ms 脉冲或独立测试手柄。连按要有释放间隔，选项动画要等完成。
  动作后的第一张截图可能仍是上一步，先刷新再判断；按当前高亮项计算方向次数。
- 选项用 X 展开，上／下选择，X 确认；展开／收起后等约 1.5 秒再操作根菜单。
  练习 GAME OVER 会回到练习设置，不能套用标题的 Enter 流程；暂停菜单 TITLE 才回标题。
- 需要更深定位时先从线程快照取 SP，读小段客体栈找调用者，再反编译状态机；
  不以连续猜按键或尝试字节序代替证据。现有 PS2 按键字节序正确。

## 最容易浪费时间的坑

- G1 冷启动测试存档已加载提示：诊断 b 确认，等 3 秒，Start 连发两次（每次200 ms，间隔2秒），
  已验证可到主菜单。截图确认 ARCADE 高亮后再 b 两次进入；不要在片头期间盲发模式确认。
  正常 CPU 采样可用 Ageha：在教程按 C 跳过，约 8 秒后直接开始 5 秒移动／射击采样。起止画面都要有玩家角色；若已到 GAME OVER，舍弃该样本。

- PS2 独立调试 CLI 的 --config 只接受旧键名 [keyboard]（cross/circle/options 等），
  不能直接传合集完整 INI；按实际键位转换一份私有输入快照，或用确认过的默认键位。

- WASAPI 回录不能把 maxInputChannels 当成当前声道数：XMOS 曾返回8，实际双声道可用，
  直接开8会报 Invalid device。先用 is_format_supported 验证双声道，已验证 float32＋1024帧。
  先确认录音已打开，再触发事件；失败空 WAV 不算采到。波形跳变先与源PCM对齐，
  一次起音339已与两版源PCM416对应，50ms样本完全一致。

1. **短按不等于街机核心收到。** 标题／菜单能操作，不代表核心能收到同样短的脉冲。
   先核对窗口按键事件与 PadRead 记录；确认映射后仍漏按，就用现有诊断脉冲，不继续猜键。
   PS4 独立诊断入口已有 `--diagnostic-tap-ms 200`（范围 0–500），只用于自动化测试。
   菜单长按确认另有 `--diagnostic-circle-hold-ms`／`--diagnostic-cross-hold-ms`，按现有 CLI 使用。
   不为每轮测试再写一份输入脚本。

2. **不要凭记忆认方向键。** 历史夹具有 I/J/K/L、T/G/F/H 等方向别名；狱门山的
   I/K/J/L 又可能是式神右摇杆。先读本轮配置，不能把旧夹具映射套给新进程。

3. **截图可能仍是按键前那一帧。** 按工具要求在动作后立即刷新，再隔 150–300 ms
   二次观察；切模式／加载时适当延长。不能仅凭第一张截图就判定没响应并重复按键。
   片头会在闲置后自动重播，已验证的固定间隔流程要复用，避免观察太慢而回到片头。
   观察到标题后立刻进入菜单，别插入编译／文档工作。离线工作后先重新观察，再决定按键。
   带帧数上限的测试会主动关窗；UI 报“foreground has no PID”时先查退出日志，区别正常到限与崩溃。
   实际菜单／游玩自动化不设短 `--frames` 上限；图形采集自己的字节／帧数上限不会关窗。

4. **字母键没反应时先区分输入法与映射。** 激活准确的游戏窗口，检查窗口输入日志。
   日志已显示正确按键交付，就不要再反复切输入法或重映射；继续查核心消费／按住时长。
   不用 Esc 切输入法：物理 Esc 会中止 Computer Use。

5. **不要把测试日志开销当作游戏性能。** 优先走合集正常启动入口。
   手动运行 PS4 NativeProbe 时需要 `--background-log on`；同步日志曾显著拖慢初始化。
   补丁已覆盖的 PS4 数据目录还需 `--merged-app on`，否则会被当作缺少本体而拒绝启动。
   诊断输入、像素采集和额外追踪开启时，不直接拿该轮数据当普通游玩性能。

   PS4 正式构建不包含测试手柄管道；仅设置 `EVAC_LATENCY_TEST_PAD` 不能启用它。
   使用延迟夹具前先核对实际构建的观察器选项；普通构建改用正常键盘输入。
   测试启动先保留 stdout/stderr，不能用 DEVNULL 隐藏配置错误后反复猜黑屏原因。
   `neo` 写在 `[game.<id>]`，不是 `[game.<id>.display]`。

6. **不要开任务管理器。** 曾因管理员窗口长期挡住游戏且无法自动关闭。
   优先正常 Alt+F4；必要时只对已核实路径、PID 与创建时间的本轮测试进程做清理。

7. **检测到人工输入就先停游戏按键。** 继续编译／离线分析；确认用户允许后再接管。
   不同时操作用户正在玩的局。

8. **PS2 的 SDK 调度表替换拦不住生成代码里的直接调用。** 声称服务已接入前，检查生成函数
   是否调用宿主实现；原样保留的 SDK 代码可能仍在等待固件 RPC。生成配置须包含对应 stub
   选择器，并填写 `sdk_bindings` 源文件路径供工具校验；已有无 ROM 的直接调用回归。
   分别分析主程序和覆盖模块还会漏掉彼此调用的入口。先用项目内的跨模块调用导出工具
   批量核对两个方向，再做启动测试；补充静态代码时沿用 `ps2_module_extras.py`，不手写返回值。
   固定调用齐全后，还需核对数据里的回调表／虚表入口；原始文件与运行时转换后的字节表示要分开看。
   主程序的数据也可能存着覆盖模块的回调表；ESPGaluda ARRANGE 已因此漏过进关入口。
   准备工具的 `bootCallbackTables` 按模式提取这些入口，不能只扫描模块自身。
   新回调要带上其全部代码标签；分段条件跳转的 `ctx->pc = ...; return` 也需静态闭合检查。
   主程序导入跳板用 `j` 尾跳转，入口提取不能只收 `jal`。
   Ghidra 导出后必须运行 `RefineLabels.java` 再合并 SDK 入口和生成；原始合成范围会跨进数据区，
   造成成千上万条伪指令错误。Ibara 的已验证链：SeedEntries → ExportPS2Functions → RefineLabels。
   入口位于现有函数内部时，用 Ghidra 外部入口标记保留所有权；新增代码前缀遇到已链接 PC
   就交回现有代码，保持严格覆盖检查。先批量核对原始导入，别等运行时逐个崩溃再补。

   图形 SDK 的结构构造函数不能只核对名字：Ibara 直接把 `sceGsSetDefLoadImage` 的
   96 字节 GIF 数据包和返回的 6 QWC 传给 VIF。简化宿主结构会让纹理被当成指令。
   保留原游戏构造逻辑，并在分析表用 `original_` 前缀避开生成器按 SDK 名字隐式替换；
   仅从配置 stubs 列表移除还不够。生成认证核对实际 stub 数量。

9. **PS2 初始化出现栈破坏时，先核对运行时的内存分区，再查字符串指令。** 自动主栈的
   初始 SP 应在分配区顶端；线程状态报告的是底部。中断／RPC 回调另用独立内核栈。
   类似问题先查固定版本上游的 issue／PR，配合已有离线库函数、索引解析入口缩小范围。
   黑屏停在主程序参数解析器时，检查 `SetupThread(a3)` 是否填写 argc、argv[0] 与末尾 NULL；
   argc=0 曾让 Ibara 在显示与 IOP 初始化之前循环，不是 GPU 绘制失败。

10. **PS2 窗口重绘率不能代表游戏速度。** 分开看 `frame`、`vsync`、EE 时钟和 PCM 欠载。
    声音 DMA 假完成曾制造大量 IOP 空转；修正真实传输后再查热点。
    图形按 [PS2_GRAPHICS_PLAN.md](docs/PS2_GRAPHICS_PLAN.md) 接成熟 GPU 后端，先保留原始 GIF 路径与顺序。
    GPU 的独立 `GsFrame` 一起快照 SMODE1、SYNCV、EXT 与 FIELD；不要再套用旧 CPU 显示请求。
    paraLLEl-GS 的 `vsync()` 消费已 flush 的命令；不能遗漏前面的 flush，也不能用每帧 wait_idle 代替。
    独立图形探针的 DISPLAY 也要提供真实 CRTC 起点；小图在 DX/DY=0 时可能被裁出可见区。
    SDK 显示寄存器走 MMIO，不是 A_D 绘图寄存器；DX/DW 是视频时钟单位。离线 bench
    要排除后台全变体着色器预编译，预热实际工作负载；不要把启动编译计入稳态 CPU。
    PGS 的纹理上传可能保留命令缓冲；先 flush，再进 WSI begin_frame。反过来会在
    next_frame_context 等待自己的未提交缓冲。用持续上传纹理的探针回归，不能只测寄存器画图。
    GS SIGNAL 会等待原游戏写 CSR 确认；EE 的 VBlank 不能同步等同一队列的 Frame，否则
    双方互等。保留一帧在途，用完成事件唤醒调度器；不要自动 ACK 或丢帧绕过。
    卡死先抓小线程 dump 与独立 EE RAM，尽快保存最后90秒音频；全进程大 dump 会错过声音现场。
    有非零 PCM 也不等于声音正确：检查实际 KON 的声 RAM 内容和样本上传返回码。
    通用 `sceSdRemote` 占位实现会假报传输完成；ESPGaluda 应保留原始静态编译的 SDR RPC 客户端。
    大规模消弹后持续花屏／黑屏时，先保存 EE RAM 和实际 DMAC 寄存器，核对绘图链是否到达 END。
    曾有原始 5888 标签的链被运行时在 4096 处静默截断；保护应检测真正的循环并明确失败，
    不能把固定计数上限当正常完成。已有 6000 包、CALL/RET、循环链的无 ROM 回归及现场离线重放入口。
    替换 newlib 分配器时，malloc/free/calloc/memalign（含 `_r` 入口）要一起核对。
    遗漏 memalign 会让原始代码修改宿主分配块前的 chunk header，造成后续对象覆盖文件索引。
    IOP SIF DMA 的带完成回调接口（sifman:32）不能只复制数据；原驱动可能 SleepThread 等
    回调 iWakeupThread。回调需遵守 CpuSuspendIntr/CpuResumeIntr，已有无 ROM 复制／回调回归。
    物理 IRX `_start` 收的是包含路径的 argc/argv，不是原始参数字节数／缓冲。
    `sysclib` ordinal 8 是字符分类位，返回 0 会让 `spucore=1` 的数字解析失效。
    音频抓取仅保留最后90秒；进入目标场景后及时保存，避免拿后面的演示段当玩家局。
    对照原音轨时先用短片段在原采样率精确对齐，再比较最终混音；粗略对齐或叠加语音
    会降低整体相关度，不能据此直接判定 DMA／解码仍错。
    内部 PCM 正确仍不能证明设备出声；必要时用独立 WASAPI loopback 对照实际输出。
    诊断追踪开启时的缺样／丢样只能作为线索，关闭追踪复测后再归因或修改正常输出路径。

11. **新运行时先复用已有计时策略，再测帧率和输入。** 360 的 scoped 1 ms 定时请求已抽到
    `common/windows_timer_resolution.h`，PS2 宿主共用；PS4 的显示时钟和微秒等待使用
    `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`，保留其停止事件与旧系统回退。计时策略要覆盖
    EE 事件截止时间、宿主输入轮询和正常关闭／初始化失败，不只关注 GPU Present。
    Windows 默认精度下，`condition_variable` 等 2 ms 曾实测成约 15 ms，等 16.667 ms
    成约 30 ms。新增宿主验收必须包含短等待精度与实际出帧间隔，不能只看平均 60 FPS。
    普通游玩不要在呈现回调同步采集整份 EE/IOP snapshot 或逐字段写 cerr。PS2 每120帧
    一次的开发状态输出曾把下一帧拉到约36ms，严重时更长；默认关闭，仅 --trace-boot 开启。
    声称 GPU 导致长帧前，先对照 VBlank 迟到与呈现各阶段，不能把日志停顿归给绘图。
    R5900 SQRT.S 的输入是 ft（20..16位），不是 fs；原生成器读错后会让 ADX 预测系数
    发散。已有生成后执行的寄存器区分回归；修生成器后重生成受影响代码，不手改游戏输出。

## 已验证的按键信息

合集正式五参数入口需要诊断脉冲时，用进程环境 `CAVE_DIAGNOSTIC_INPUT_TAP_MS=200`；
不要把 `--diagnostic-tap-ms` 追加到五参数合同后面，它会被精确参数校验拒绝。
原生直接入口才用该 CLI；必须从当次 run.json 核实 diagnosticInputTapMs。

弹铳 v090 的私有复制存档是 Player1 Custom，已在原生 Controller Settings 读到：
L1=Add Credit（键盘 A），Triangle=Bomber（V），Circle=Full-Auto（X），Square=Shot（C），
R2=Restart（W）。不能套用大往生的 V 投币；默认 Player2 配置也不证明 Player1 配置。
此规则仅针对该私有复制存档，普通用户可能是 Default；实际测试先核对原生菜单。
Sky 动作后若截图仍旧，先 `include_screenshot=false, include_text=true` 刷新，再等约200ms
抓图；两个 include 都是false会被接口拒绝，发生后先重新观察，不重复发送按键。

PS4 默认键盘映射：方向键移动；Z=Cross、X=Circle、C=Square、V=Triangle、
Enter=Options、Backspace=Touchpad。**仍以该次配置为准。**

PS2 新 Vulkan 宿主复用 PS4 默认绑定：方向键移动，Z=Cross、X=Circle、
C=Square、V=Triangle，Enter=Start，Backspace=Select。旧 CPU 宿主的 C 确认／WASD
流程已过期，不能再套用。ESPGaluda 的菜单确认仍是 Circle（现在对应 X）。
当前合集宿主已实际确认正常短按可走完整菜单，保持默认 `--diagnostic-tap-ms 0`。
`launch_ps2_probe.py` 默认开启独占的进程内测试手柄，实际键盘不会交给游戏；
这一入口用 `test_pad_client.py` 操作，检查真实键盘必须加 `--normal`。不要混用两条输入路径。
`test_pad_client.py --repeat` 在客户端逐次等待后发送，会持续占用该 exec session；
等同一 session 完成再发其他输入，不能把首个 OK 当成整串脉冲已经结束。
输入 CSV 默认仅保留 200000 条事件；长时间菜单操作会用尽容量。测量前重开短局，
先核对文件头的 overflow，不能拿缺失后半段的记录分析响应时间。
ARRANGE RANKING 按 Cross 返回后需等待实际菜单就绪；已验证单次 Cross 后等约 3 秒，
再 Up→Circle 进入 GAME START。不能在返回动画中连续预发这两个键。
ESPGaluda 原生 BUTTON CONFIG 已核对：ARCADE 的 Square=SHOT、Triangle=KAKUSEI、
Circle=BARRIER、Cross=RAPID（键盘 C/V/X/Z）。同页方括号是 ARRANGE 的功能，
依次 POWER SHOT/KAKUSEI/MAIN SHOT/BARRIER；不能凭别的 CAVE 游戏习惯填写说明。
BUTTON CONFIG 页的脸键会直接修改当前绑定，Cross 也不是返回；用 EXIT 项退出。
旧探针确有漏按时才用 200 ms，并核对 `ps2-pad-event` 与 `ps2-pad-read`；
正式游玩、计时与性能测量保持默认 0。用户已实际游玩并确认 BOSS／重开音乐正常。
ARRANGE 的可靠短流程：主菜单选 ARRANGE MODE → Circle → 等 1.8 秒 → Circle
进入 GAME START → 等 1.8 秒 → 用进程内测试手柄每秒一次 Square（按 300 ms、间隔
700 ms，共 12 次）覆盖加载与选人 → 等 1.6 秒 → Start 暂停。已实际进入 Chihiro
第一关，射击、左右移动、觉圣、暂停和 QUIT→YES 回到标题通过；不要把确认键全发在加载期间。
**暂停恢复有约 3 秒倒计时，等 4 秒后再测输入。** 本轮在倒计时发出的方向命令不能作延迟样本。
ARRANGE 选人后的开场动画也会忽略 Start；先确认已有生命／觉圣 HUD、敌弹和正常滚动，再暂停。
诊断手柄的最后一个命令被接受后仍会继续保持，下一种按钮先等其 hold 时长和释放间隔；
不要在 repeat 刚返回时立刻发确认，覆盖最后一个方向脉冲。
普通 `--normal` 入口已用 Computer Use 的 X／Return／Down 短按完成存档确认、标题、
主菜单→ARCADE SIMULATION→STAGE SELECT→STAGE 1 BOSS、暂停和正常退出；
每次按后先刷新，再等转场完成。不要因为诊断手柄入口收不到键盘就重映射普通入口。
`sys.dat` 的排行榜字节受原作完整性校验；直接改低分数会显示存档损坏，不能作为已加载的成绩夹具。
游戏右上角的 PLEASE WAIT 也会出现在真实玩家局，不能据此认定还在加载或教学。
截图往返之间可过去十几秒，非操作时停在原生暂停菜单，避免玩家已经死亡却继续采样。
短关卡检查优先用 MAIN MENU → ARCADE SIMULATION → STAGE SELECT，默认 STAGE 1 TOP；
再向下一个是 STAGE 1 BOSS。确认都是 X。这样可直接进玩家局，不必重等教学。
需要自然滚动到 BOSS 时，先在 SETTING → SIMULATION SETTING 将 DEATH 设 OFF，
再返回 STAGE SELECT 选 TOP；Return 暂停 → QUIT → YES 可返回后重新开局。
PS2 可见测试窗应可见启动；隐藏启动曾导致没有可操作窗口。后台工具仍隐藏运行。
通过 functions 转发 computer-use 结果时，对图像块用 image(c)，不要 text 整个结果。

大往生 1.00 初次启动的操作图已确认：Circle=连射、Square=射击、Cross=炸弹、
Triangle=投币、Options=开始、Touchpad=暂停。菜单确认用 Circle（默认 X）。
此前投币失败时，日志显示 V 已作为 Triangle 交给 PadRead；不要再猜投币键。
已在大往生 1.00 确认 `--diagnostic-tap-ms 200` 后，V 投币、Enter 开始和 X 选人正常。
见 `.local/v084/play-base100-r5`；该脉冲只用于自动化，不能带入普通性能测量。
大往生直达菜单的 BGM SET 用 Square（C），右键依次切换，Circle（X）确认。进入
街机后先等操作说明出现并用 X 关闭，再等原版画面提示可投币；先发 V，确认信用点后发
Enter。暂停用 Touchpad（Backspace），RETURN TO GAME 用 X。

临时测试映射要避开宿主保留键；F3 曾被配置校验拒绝。L3／R3 可临时用
Numpad0／Numpad1，Computer Use 对应 KP_0／KP_1。

绊地狱离线诊断局：片头 Enter → 约 2 秒后标题 Enter → Direct Menu 的 X 确认。
关闭联网时，主菜单和模式加载可能各出现“PSN 已退出／网络错误”两张提示，分别 X 关闭。
街机核心中 V 投币 → Enter 开始 → X 选机；200 ms 诊断脉冲已实际进第一关并射击。

PCSX2 2.8.2 对照环境：Vulkan IMMEDIATE 的 WGC 画面会停在初始空白，F8 原生
截图有正常游戏内容；D3D12（Renderer=15）可被正常捕获。用独立渲染窗／-nogui。
自动短按可能完全漏掉，使用 PCSX2 自带 Pad1 MacroN/ MacroNBinds/ MacroNToggle=true/
MacroNFrequency=0 保持游戏按键，映射 F12=Start、F13=Down、F14=Circle、F15=Cross、
F16=Up、F17=Left、F18=Right、F19=Square、F20=Triangle。每个宏必须成对触发：
第一次开启后立即 get_window_state，再无条件第二次关闭并立即刷新；方向宏若跨
模型轮次保持，会自动连跳多项。原生短按映射仍为方向键、Z/X/C/V/Return。
这组配置仅在私有模拟器对照环境使用；不能用于输入延迟测量，后者须独立标记。
逐帧观察可在这份独立配置的 `[Hotkeys]` 设 `FrameAdvance = Keyboard/F10`；
F10 会推进一帧并暂停。Computer Use 不支持 F21，别再将它配作测试热键。
每次推进后先立即刷新，再等约 200 ms 取稳定截图；立即截图可能仍是前一帧。
暂停下保持方向宏可观察响应字段数，结束后释放宏；这种方法不测正常呈现队列的墙钟延迟。
需要区分首个按键消费帧时，在独立 PCSX2 配置中临时启用 PINE，以
`tools/latency/pcsx2_pine_probe.py` 只读检查暂停状态下的原游戏按键和主循环计数。
已验证 `003effc0` 低16位：右 `0x2000`、左 `0x8000`；`003589c8` 每次 F10 增一。
配对逐帧截图可确定“已读到输入→首张响应”的更新数，不要把第三次 F10 直接写成三帧延迟。
首次暂停加载画面可能黑屏；推进两个静止帧后用 F8 原生截图刷新，后续截图正常。
退出后恢复独立配置的 PINE／StartPaused；不把这些诊断设置带进发行包。
PCSX2 暂停或新启动器被遮住时，WGC 可能显示背后的旧窗口；先激活目标窗口再截图。
首次旧画面不能证明新 EXE 没更新，核对返回的进程路径并重新观察。

性能采样开始和结束都立即截图，再做符号解析／热点调查。BOSS 放置太久会回到
RESULT；符号解析后的场景不能倒推为采样时的场景。对照 CSV 的 `present_qpc_us`
和 CPU 的 QPC 窗口，排除额外追踪／SuspendThread 阶段，不能对整段混合记录取最大值。

## 复用入口与证据

- `tools/latency/test_pad_client.py`：仅控制明确启用的进程内测试手柄，支持有界
  hold／repeat／gap；它不发送 Windows 输入，也不证明真实设备输入或端到端延迟。
- `tools/latency/launch_ps2_probe.py`：共用 PS2 独立诊断入口，输入追踪、进程内测试
  手柄与 F9 一次性像素采集；`--normal` 关闭这些诊断，用正常输入测性能。
  旧 PS2 CLI 的 `--config` 只收 `[keyboard]`／`[community]`，不能传合集 INI；
  合集正常启动由五个 contract 参数传配置，不把两种配置入口混用。
- `tools/latency/launch_*_probe.py`：先读适用平台、夹具范围与依赖，不把旧探针直接套到普通构建。
- 封面版启动器的当前构建在 `out/common/bin/EVAC PC LABEL.exe`；根目录 EXE 可能仍是旧发行版。
  测试时传独立 `--root`，不要因旧入口看不到 PS2／封面而重查配置。文件夹选择器可用
  Alt+D → 输入路径 → Enter 导航，再点对应弹窗截图中的“选择文件夹”；它只展示文件夹，
  看不到 ISO 属正常。该弹窗 UIA 曾错报焦点、set_value 失败，优先按截图和地址栏操作。
- PS2 只改运行时／宿主 C++ 时，直接 `cmake --build out/ps2-tools --parallel 2`。
  Windows 无窗口测试默认栈只有 1 MiB；测试里再建第二份大型 `PS2Runtime` 时放到堆上。
  `0xC00000FD` 是栈溢出，先核对测试对象和调用栈，别当作游戏代码崩溃。
  `Build-Ps2.ps1` 会重新生成大型构建图，只有生成目录、模块清单或构建配置变化才重跑；
  活跃构建的会话和日志见 `AGENTS.md`，不要因日志暂未刷新就另起一次构建。
  新关卡首次构建使用 `globalRootAliases`，让目标与 PCH 统一重命名入口，避免逐个打开全部生成文件。
  已编译模块保留原来的重命名方式和缓存；原生检查用 `tools/apps/cmake/bin/ctest.exe`。
- `.local/v084/launch-check.ps1`：本次 CPU 兼容验收共用启动入口，按参数选择大往生版本／绊地狱和新日志目录。
- 长时间没推进时，先查看本轮日志／配置和已有验收记录，定位一个原因再行动。
  不重启全部流程，不再全文翻几千行历史寻找“可能用过的键”。
- 只把实际菜单、玩家进关等观察记为通过；演示画面、按键交付、编译成功分别记其真实范围。

## 更新规则

踩过的坑一旦解决，把最短、验证过的操作法替换到这里；保留必要的游戏／版本限制。
恢复旧任务或上下文压缩后，先读这里，再读 Current objective，不重新发明一套流程。
