# PS4 任务派发与同步

本文说明共享 PS4 运行时的线程、信号量、锁与回调边界，以及当前性能调查能证明和仍不能证明的部分。性能门槛仍开放；不要以平均 60 FPS、单轮平稳回放或替换等待 API 作为关闭依据。

## 游戏的任务链

绊地狱的主线程整理任务队列后，向 work 信号量增加 N 个 credit，再等待 done 信号量取得 N 个 credit。每个工作线程先等一个 work credit，再取得游戏自己的队列 mutex、取一个任务并释放 mutex；任务函数在队列锁外执行，最后增加一个 done credit，重新等待 work。

只读核对的调用点是 eboot 的 0x1a8a40 主派发函数、0x1a9390 工作循环；0x1a8a7f 是批量完成等待的返回位置，0x1a944a 是工作线程发出完成信号后的返回位置。原程序指令与资源不进入源码仓库。

这条链同时存在三种不同的“等”：工作线程等下一帧任务、工作线程竞争队列锁、主线程等本批全部完成。第一种经常正常持续一帧，不能把其整个时长当作执行慢；第三种也不能直接归因于 GPU。

## 锁与对象寿命

| 实现 | 持锁边界与等待规则 | 寿命保障 |
| --- | --- | --- |
| `native_semaphore.cpp` | registry 查找锁不跨对象等待；对象 gate 保护队列、完整 credit 预留与结果发布。成功的 address/event 路径不重取 gate，超时退休重新取锁并复查结果；CV 返回包含重取 gate。通知在 gate 外执行。 | 队列、等待线程、通知链持有 shared ownership。通知 lease 完成且唯一持有后，TLS 才复用 waiter；release/acquire 负责发布，不能用引用计数代替发布屏障。 |
| `native_sync.cpp` 的 mutex | registry 查找后释放；state gate 只保护 guest owner/depth 等状态。实际 guest 临界区不持有这个宿主 gate；有 owner 时以 CV 释放 gate 后等待。 | owner、waiters、condition_users 禁止仍在使用时销毁。 |
| `native_sync.cpp` 的 condition | 注册时按 condition gate → mutex gate 取得；释放 guest mutex 后，CV 等待释放 condition gate。返回后先释放 condition gate，再重新取得 guest mutex，最后单独更新 active。没有反向持有两个 gate 的重取步骤。 | notifier 在 condition gate 内通知栈上的 waiter；等待线程必须重取同一 gate 后才能退出，故不能直接把通知移到锁外。 |
| `native_event_flag.cpp` | 位模式匹配、消耗和排队在 flag mutex 内；等待释放 mutex，通知目前在 mutex 内。 | 与 condition 相同，栈 waiter 的销毁依靠同一 mutex 串行化；锁外通知必须先改成拥有式 lease。 |
| `gpu_core/gpu_worker.h` | 入队锁只保护 commands；执行命令前释放。调用者在入队锁外等 future；所属 worker 上的重入直接调用，不等待自己。 | packaged task/future 保留完成与异常；不是每次 draw 都跨线程。 |
| GPU priority callbacks | GPU tick 完成后执行 callback；销毁 callback capture 后，才在 pending mutex 内发布已完成序号。Drain 只等调用时已有的序号前缀，不另排空 callback。 | 完成序号与 CV 受同一 mutex 保护；CV 比 worker 活得更久。GPU Finish、标签回写与已提交工作的完成条件未删。 |
| 后台日志 | producer 只 try-lock 有界队列，争用时计数并拒收；后台 sink 在队列锁外写文件。 | 完整记录入队，停止后不回退成同步文件写入；遗漏计数单独报告。 |

这是上述代码的锁顺序审查，不是对所有游戏锁、Windows 内部锁或驱动等待的无死锁证明。初始化阶段仍有有限的同步诊断输出，不能把“队列 producer 不等 sink”推广为所有格式化或全部 stdio 都不会等待。

## 批量完成的长尾放大

Signal 在 gate 内先为可满足的 waiter 预留完整 credit，再 release 发布成功，最后在 gate 外通知。这个顺序防止新到线程偷走已授予的 credit，也保持 FIFO／优先级与取消、删除的既有合同。

因此，只要一个已获 work credit 的线程迟迟没有恢复，其他线程即使完成了剩余任务，主线程仍要等这最后一个完成。不能靠抢回已授予 credit、伪造 done、减少任务数或跳过批量等待改善数字。需要区分通知之前的锁／遍历延迟、底层等待函数内部的时间，以及返回后的运行时代码和任务耗时。

## 时间点含义

`native_semaphore_wait_timing` 仅对慢批量等待、迟恢复和慢失败有界输出；不是所有等待的无损 trace。

| 字段 | 含义 |
| --- | --- |
| `wait_sequence` | 本次实际入队等待的序号，结合 handle/thread 区分不同调用。 |
| `lookup_us` | 对象查找与 guest priority 查询，包含查找锁可能发生的等待，不是纯查表 CPU 时间。 |
| `gate_wait_us` | 取得本次 semaphore gate 的宿主墙钟时间。 |
| `queued_under_gate_us` | 取得 gate 后，到完成 waiter 构造与入队的时间。 |
| `queued_clock_us`、`available_credits`、`queued_waiters` | 排队时刻及当时剩余 credit／队列数量；不是最终 signal 时的快照。 |
| `granted_clock_us` | 结果在 gate 内被授予的时刻；批量 done 记录的是最后满足请求的生产者，不代表所有生产者。 |
| `wake_began_clock_us`、`wake_finished_clock_us` | **通知线程**调用 Wake 的前后；不是等待线程从等待函数返回的时刻。 |
| `park_began_clock_us`、`park_returned_clock_us` | **等待线程**最后一次底层阻塞调用的前后。CV 的返回仍包含取回 gate，address/event 不包含。 |
| `park_calls` | 底层阻塞调用次数，允许早醒后重新检查；零次表示未真正进入该阻塞调用。 |
| `clock_us` | 运行时在等待循环之后记录的恢复时刻。 |

早于真正阻塞的成功不会丢失；结果谓词、原截止时间和超时后二次检查仍是依据。通知结束可能晚于等待线程观察成功，也可能尚未记录为非零，不能据此计算负的“通知耗时”。时间点之间可能包含抢占，仍不是纯 CPU 或精确的 OS Ready 时间。

## 当前证据与未通过项

GPU 资源读取／callback 前缀批次的正常绊地狱回放位于 `.local/ketsui-pacing-r9`：同一 ARCADE／Tiger／380 分、53.45 秒录像完整自然返回。取回放约 15–46 秒的 31 秒区间（8636324266379–8636355266379），1,859 个连续 submission 15950–17808，缺号 0，区间内没有完成编译。指令处理均值从旧 r8 的 3.604 ms 降为 3.403 ms，其中 draw/resource 从 2.627 降为 2.461 ms、Drain 从 0.803 降为 0.765 ms；最终输出均值约 0.104 ms。这是近似同片段比较，不是逐游戏帧对齐的严格 A/B。

但最大帧仍为 **33.508 ms**：提交外 18.381 ms，其中批量 WaitSema 15.987 ms；本帧绘图指令 3.713 ms、正常节拍等待 11.150 ms、最终输出 0.166 ms。worker 89508 在通知调用结束后约 15.942 ms 才恢复；这份旧日志还没有等待函数返回时间，不能把这段全部称为 OS Ready 排队。

仅替换成独立 auto-reset event 未通过长尾筛选。相同程序按 address → event → event → address 交错，默认三进程重复、每组 384 样本，六组的像素／任务校验均通过，但以下结果不支持将 event 设为默认：

| 间隔派发 batch 最大值（µs） | address A1 | event B1 | event B2 | address A2 |
| --- | --- | --- | --- | --- |
| 14 任务 | 25819 | 34627 | 23532 | 4864 |
| 16 任务 | 72589 | 1749 | 20237 | 24910 |

现场为 `.local/offline-bench/event-{control-a1,candidate-b1,candidate-b2,control-a2}`。event 仅保留为显式诊断对照，正常路径仍为 address；没有改变 affinity、优先级或系统电源。event 的复用必须清除“观察结果早于阻塞”留下的旧信号，只能在原通知 lease 已结束且独占 waiter 后 ResetEvent；实现遵循 [Microsoft auto-reset event 合同](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw)。

补齐等待线程时间点后的 `.local/offline-bench/sync-phases-r1` 捕获 worker 41672 的 6.115 ms 授予到恢复：lookup 0 µs、gate 3 µs、锁内排队 1 µs、授予至通知结束 11 µs，底层 WaitOnAddress 到 6.115 ms 后才返回，返回至运行时恢复记录小于 1 µs（按整数微秒记录为 0）。这次尾峰不在运行时成功后的 gate 重取或日志格式化路径。它仍不能区分内核等待内部和 OS Ready 排队，需要线程状态／调度证据，不把一个小样本记成普遍根因。

该批次 73 项原生检查通过，含 CV/address/event 各 28 个 semaphore 子例，共 84 项；覆盖完整 credit、FIFO／优先级、截止时间、取消／删除、早返回时 notifier 寿命、再次等待复用，以及通知方和等待方时间点的区分。GPU 前缀完成另有等待 in-flight callback、capture 销毁及不增加提交／tick 的用例；着色器读取有 21 项自造检查，保持当前内容和 metadata 的新鲜性。

### 复验与产物身份

原生检查命令为 `py -3.12 scripts/ps4/run_native_checks.py --output .local/ps4-native-checks/<新目录>`，源码检查为 `./scripts/Test.ps1 -Scope Source`；当前分别 73／373 项通过。离线命令为 `py -3.12 scripts/ps4/run_offline_bench.py --output .local/offline-bench/<新目录> --semaphore-wait address`，诊断对照可显式选择 `cv` 或 `event`。每轮保留全部六组、16 次预热、128 正式样本和三进程重复，任务 checksum／像素／提交数必须通过，不能并行构建或运行游戏干扰测量。

以下均为 fb50411 之后尚未提交时的开发源码快照，不是 v0.3 发行包，也没有完成本批次大往生实际回放：

- Ketsui r9／shader-read-r1、r2：宿主 `117e7f98a0459977b4875046f32f976f99d9835e4ea3e54645494ca42ea81fc1`，GPU `ede6e68396b88553e63b06964552f3b04eb13a246ba75884a669b88a4ea7a192`，源码快照 `f8c98582ff059c4f6f85f67e0d245f4c97fc87a0b7b1a32b72f90b71f1b22214`。r9 stdout SHA-256 `2eaccd9e34206edb3defffb15e5a614dd823aa868a1343a6f4d22f45c89f034d`；accepted=written=82389、dropped/format/sink/frame-emission-failure=0，但 contended=341、host-operation-emission-failure=312，不能称全部诊断无损。
- event A/B/B/A：宿主 `eb0d8e514d7117a2d5c675cb92c6252616542d69c9c14cf927d2df70cb2d6fd5`，GPU 同上，快照 `f473904f710276a6ee071dded86abe56e1068a14abdddbf6ceb1c72e893d475a`。event 只在对照命令显式选取；对照后已撤回其正常运行默认候选。
- sync-phases-r1：宿主 `24d6155080d72515c3bc83be875f5ac52603d6a8149997fee346983c6eb9af6d`，GPU 同上，快照 `64bc74ac69159a25b690fe09c981d172b11fd8a3a7a45405e9d83b837c0e4079`。关键 run-2.jsonl SHA-256 `af53861be0ead9394cb79a901c7c04459afcb273fa1538fef442bcda559110b7`，其中 handle 6002453878354739205、wait_sequence 2181 的阻塞开始为 8637002990129，授予 8637003007067，通知结束 8637003007078，等待函数返回及恢复记录均为 8637003013182；阻塞开始确实早于授予，不是授予之后才准备调用等待函数的另一种间隙。

GPU API 仍为 7，宿主／DLL 配套，不是取消同步或改变原作速度。当前硬件是本机 24 逻辑处理器与 RTX 4080；没有新的较低配置实测。较早四项 GPU CTest 在同一 GPU 代码批次通过，不把随后仅改宿主时间点的原生检查重算为新的图形／游戏验收。

后续先补线程状态／调度证据，并将迟恢复与实际 batch 的最后一个生产者关联；同时继续检查队列 mutex 的真实争用和同步排空。不在缺证据时宣称所有长尾属于 Windows，也不因共同的平均帧率正常而转去模式覆盖或打包。
