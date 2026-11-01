# Agent memory: game testing

**每次操作游戏前先读本文件。** 保持短小；更新已验证的做法，不追加逐轮日志。
版本进度、进程与证据位置写进 `AGENTS.md` 的 Current objective。

## 开始前，先确认这四件事

- 当前 EXE／构建、游戏数据版本、独立测试目录和 PID。
- **该进程实际读取的配置**：优先看 `config.snapshot.ini`、启动参数和按键绑定日志。
  主目录 INI、旧测试 INI、Xenia 和移植版的映射可能完全不同。
- 查上游实现前先确认实际编译的源文件：PS2 的 `third_party/ps2/runtime` 和构建目录的
  source view 会覆盖 `.deps` 原始文件；不要把已修过的依赖原版问题再当作当前缺陷。
- 本轮只列一个小而完整的验收集合；已通过且未受改动影响的项目不重跑。
- 原始 ROM、冻结发布包和旧项目保留；测试局允许自行重置，不再为同一授权反复询问。

## 最容易浪费时间的坑

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

PS4 默认键盘映射：方向键移动；Z=Cross、X=Circle、C=Square、V=Triangle、
Enter=Options、Backspace=Touchpad。**仍以该次配置为准。**

PS2 新 Vulkan 宿主复用 PS4 默认绑定：方向键移动，Z=Cross、X=Circle、
C=Square、V=Triangle，Enter=Start，Backspace=Select。旧 CPU 宿主的 C 确认／WASD
流程已过期，不能再套用。ESPGaluda 的菜单确认仍是 Circle（现在对应 X）。
自动化用 `--diagnostic-tap-ms 200`，核对 `ps2-pad-event` 与 `ps2-pad-read`；
正式游玩、计时与性能测量保持默认 0。新宿主已出第一关和续关画面；实际移动／射击仍待确认。
ARCADE 选人后约一分钟教学会显示 PLEASE WAIT，之后才是玩家局；不能把教学或
DEMONSTRATION 当游玩验收。教学结束后按短间隔观察，玩家不动会很快损失全部生命。
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

临时测试映射要避开宿主保留键；F3 曾被配置校验拒绝。L3／R3 可临时用
Numpad0／Numpad1，Computer Use 对应 KP_0／KP_1。

绊地狱离线诊断局：片头 Enter → 约 2 秒后标题 Enter → Direct Menu 的 X 确认。
关闭联网时，主菜单和模式加载可能各出现“PSN 已退出／网络错误”两张提示，分别 X 关闭。
街机核心中 V 投币 → Enter 开始 → X 选机；200 ms 诊断脉冲已实际进第一关并射击。

## 复用入口与证据

- `tools/latency/test_pad_client.py`：仅控制明确启用的进程内测试手柄，支持有界
  hold／repeat／gap；它不发送 Windows 输入，也不证明真实设备输入或端到端延迟。
- `tools/latency/launch_*_probe.py`：先读适用平台、夹具范围与依赖，不把旧探针直接套到普通构建。
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
