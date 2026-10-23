# 合集实施验收

实施顺序和每阶段要求见 [实施计划](IMPLEMENTATION_PLAN.md)。源码及机制测试与真实游玩分开记录；只有满足对应阶段全部条件才标记完成。

## v0.6 ESP Ra.De. 真实联网与离线录像

- `.local/v06-esprade-online-r2`（PID 95148，已关闭）：独立 profile／loopback，ARCADE／祐介实际游玩至 1390 分、不续关。综合榜 0 与角色榜 1 的成绩及两份录像上传成功；原生排行榜显示 Player／1390 和录像标记，下载保存到槽 `0018-0000`，完整播放并返回列表。附件均为 76,826 字节，SHA-256 `5ce1932dd2f83c730f5831045cc3d3ef7c88ec4b91cbaa42ff93c35efe4be751`；网络头 version `0x10022`、data version `0x10000`、region `ARCADE_01000`。原局还完成原生 AC-OSARAI／解锁进度保存提示。
- 下载列表原先在 Sony ID mapper 初始化失败后残留 NOW ACCESSING。现在只转换本作本地下载缓存查看路径，使用存储昵称、移除本地缓存的在线 UGC 查询，`pollCorrectList` 原生加载／校验方法逐字节不变。输入以 CUSA15155／01.04 原始 SELF 与脚本 SHA 绑定，原数据未修改。115 项原生资源检查、6 项 oracle 测试及真实资源的 native／Python 对照、固定 9145 字节覆盖、冷／热准备缓存检查通过。证据：`.local/v06-esprade-resource-checks-r1`、`v06-esprade-replay-fix-r1` 和 `v06-esprade-replay-fix-build-r1.log`。
- `.local/v06-esprade-offline-replay-r1`（PID 93980，已关闭）：复制上述 profile 并关闭联网，冷启动下载列表正常，完整回放至 1390 分并自然返回，无残留面板；播放期间未发送游玩输入。Alt+F4 完成呈现回收，68,418 条日志全部写出，无丢弃或 sink failure。既有 guest-finalizer／exit 27 限制仍在。详细 timing trace 关闭。
- `.local/v06-esprade-service-preflight-r1`：ECS 独立临时目录的 26 个源码成员 SHA 核对、Python 3.10.12 下 31 项 ESP／PS4 profile／备份／部署检查、三个 ESP systemd 单元 verify 通过。现有五款服务 PID／active 状态未变；未安装 ESP 服务、改 nginx 或正式数据库。

### 正式服务与普通启动器入口

- `.local/v06-esprade-deploy-r1`：从提交 `d66c11afdee8` 安装独立 `cave-esprade.service`／每日备份及 nginx 路由，本机端口 18089，版本指针 `/opt/cave-community/esprade-current`。六款 HTTPS 身份、两个旧入口、未知游戏 404、首次 SQLite 备份完整性通过；原五款服务 PID 未变，未写入自造分数。
- 开发 catalogue 已启用 ESP。`.local/v06-esprade-https-r1/collection` 的普通启动器 PID 12048 启动本作 PID 81344，正常五参数合同、独立新存档、首次准备及只读数据校验通过。配置自动把共享 endpoint 解析为 `https://xiaolu.life/cave/api/v1/games/esprade`。详细追踪关闭，菜单短按辅助为 120 ms。
- ARCADE／祐介实际射击计分、死亡复活，不续关自然得到 3300 分；综合／角色榜及两份录像上传均 HTTP 200。原生榜单显示 Player／3300 与录像图标，下载成功保存至 `0018-0000`。下载录像实际完整播放至 3300，约 82 秒后自然返回干净列表，期间未发送游玩输入。
- 正式附件 75,727 字节，SHA-256 `61ca84e93ec1cb9826961c108870fddfe44caaa530b601c9e716c92228350ef4`；原生 version `0x10022`、data version `0x10000`、region `ARCADE_01000`、4961 帧。`online-result.json` 核对两个正式条目、附件 SHA、实际保存槽的状态／按键数据与模式／成绩／角色。最初假设整个网络 0x80000 封套可在本地容器中逐字节找到不成立：本地槽的校验字段和步长不同；改按已解析有效区间核对，不以填充区比较推断回放损坏。
- 集成补齐 ESP 正常关窗收据识别，沿用 PID／进程出生时间／呈现回收的严格条件，146 项 launch/config/process/data-copy 检查通过；横幅分类改为 PS4 · 2019。旧复制启动器确实误报 exit 27，新构建已在后续实际关窗中正确显示 Game closed，无异常弹窗；原始退出收据及清理限制仍保留。
- 同一独立目录重启后，启动器 PID 77368／游戏 PID 32232（均已关闭）使用热准备缓存并保留原生设置；实际 ARCADE PLUS／祐介第一关射击、死亡复活及自然结算得到 3510 分，正式 20／21 榜与两份录像均成功。附件 76,900 字节、SHA-256 `98fd1f54079b6f79e5f70f8274a51b31a3966590be3495c418457a4d7c6c4148`，原生 mode=2、5634 帧。`plus-result.json` 保存正式读回及结构核对。设置界面小字先后读作 65／68 不可靠，不能用存档中恰好等于 68 的任意偏移证明音量；准确百分比待单独复核。
- 六游戏 format-2 Public／Internal、英文 BAT、六份放置说明和双语玩家 README 已接入；55 项打包／产品／清单测试通过（1 项 Windows 文件符号链接权限跳过），证据 `.local/v06-release-profile-tests-r1.log`。本节是自造小包测试，不是最终发行包验收。

- `.local/v06-present-regression-r1/esprade-off-guest-fence`（86076，已关闭）补充
  SUPER EASY／祐介玩家第一关，射击、死亡复活及自动防护，至中 BOSS、13780 分。
  不是 DEMONSTRATION。此 profile 的 CREDIT 为 Triangle／键盘 V，R2 未绑定；
  不沿用绊地狱测试档的 W 投币。实际原生菜单核对后成功投币选人。
- 同次实际将 BGM 从 69% 改为 68%，SE 保持 70%，返回后原生保存提交完成。
  独立 `cold-restart`（4240，已关闭）沿用同一 profile 和相同程序／DLL，
  原生 SOUND SETTING 明确读回 BGM **068%**、SE **070%**。启动前系统存档
  214016 字节，SHA-256 `55493f5d654f942660e64a0a1ec2fe4c243cb27446a97f95fad77b41ff9200b9`。
  ARCADE 投币／演示界面短按 Enter 是开始，Backspace／Touchpad 打开暂停菜单；
  OPTION SETTING → L1／A 切到 SOUND。此前将此入口记为 Enter 暂停不准确。
  正常关窗完成呈现回收，88227 条已接收日志全写出，无 sink failure。

设置冷读回及 ARCADE／ARCADE PLUS／SUPER EASY 代表性玩家入口已验证。
PS4 帧节拍短回归已完成，结果见下节；最终发行构建／双包验收仍未完成。

## v0.6 PS4 异步呈现候选

配套 ABI 11 将窗口 acquire／合成／present 移至独立线程；三张宿主图像和
两个窗口资源槽限制积压及资源寿命。游戏只等待必要复制与原作节拍，不等待
当前窗口合成。支持第二条同族队列，也保留有外部同步的共享队列路径。
普通游戏不调用像素读回，默认不开详细计时。

- `.local/v06-present-async-build-r2.log`：六对 PE／PDB 通过并整理三款运行文件。
- `v06-present-async-ctest-r1.log`：5/5 GPU CTest 通过。
- `v06-present-async-validation-r1.log` 与 `shared-validation-r1.log`：独立及共享
  队列各自 Vulkan 验证错误为 0。暂停消费者的自造测试证明只保留最新 ready
  图像，读回像素／原缓冲改写隔离、取消、缩放、重开和 drain 正确。
- 实际 `.local/v06-present-async-play-r1/`，PID 85148 已关闭：离线独立 profile，
  ARCADE／Type-A 进关，射击至 320 分，死亡复活、自然结束和正常关闭。
  此 profile 原生 CREDIT 绑定 R2，所以实际键盘投币为 W；未更改绑定。
- `play-summary-r1.json` 限定可见第一关约 30.977 秒：1858 条连续窗口记录，
  相邻 queuePresent 返回间隔最大 17.552 ms，0 次超过 18 ms；1834 条客体完成
  记录最大 17.542 ms。客体详细发布日志有 24 处序号缺口，不能把其相邻日志
  的 33 ms 当成出帧间隔；窗口记录在这一片段无缺口。音频欠载计数未增加。
- 正常关闭完成呈现回收，437607 条日志全部写入，sink_failures=0；原有
  incomplete guest finalizer／exit 27 状态保留，不改写成 guest 完整退出。
  此前正常启动器路径 PID 103484 也已关闭，共享 INI 已逐字节恢复。
- 捕获分析新增两条独立时间轴和窗口阶段拆分；5 项分析器用例通过，包括序号
  重置、日志缺口及无客体事件时的窗口长间隔。源日志与全程报告均保留。

### 每帧 guest barrier 与窗口 drain 分离

- `.local/v06-present-regression-r1/ketsui-off`（PID 19964，已关闭）关闭详细追踪，
  SUPER EASY／Type-A 实玩至第一关 BOSS、20152 分。两个 F8 间 144.252 秒，
  8653 条完成记录，99 百分位 17.125 ms，但出现一次真实 33.327 ms 长帧，
  其中 command acquire 15.681 ms。音频欠载增量为 0。另有一条日志缺口，
  与该长帧不是同一位置，不能相互抵消。正常呈现回收完成，89042 条已接收
  日志全部写出；日志竞争与被限流诊断仍按原计数保留。
- 源码定位到原生每帧 ACQUIRE_MEM 调用 full Drain，连带排空独立 presenter，
  使下一帧重新等待窗口。ABI 12 新增 `finish_guest`：保留 guest GPU fence、
  写回与已提交 CPU 回调完成条件，只移除不属于客体内存屏障的 WSI 排空。
  关闭、解绑和完整 drain 仍等待呈现资源。没有提前释放原游戏缓冲。
- 暂停窗口消费者的组件测试在修复前明确失败（`v06-guest-fence-red-test-r1.log`），
  修复后 5/5 GPU CTests 通过；独立与共享队列的 Vulkan 验证错误均为 0。
  同次构建六对 PE／PDB 通过。日志：`.local/v06-guest-fence-{build,ctest,validation,
  shared-validation}-r1.log`。
- ABI 12 实玩 `.local/v06-present-regression-r1/ketsui-on-guest-fence`（66832，已关闭）：
  SUPER EASY／Type-A，射击、自动炸弹、死亡复活，第一关结算 20372 分并进入第二关。
  两个 F8 限定 132.279 秒，跨过两个分钟边界；7844 条完成记录的 p99 为
  17.143 ms、最大 17.732 ms，无已记录间隔超过 18 ms。连续窗口提交间隔最大
  18.330 ms，超过 18 ms 一次、超过 20 ms 零次；音频欠载增量为 0。
  guest fence 平均 1.314 ms、最大 2.746 ms，未重现旧的 15.681 ms acquire。
  详细日志存在客体及窗口序号缺口，报告单列全记录／连续记录统计，不把缺口
  当作已证明的实际掉帧或零掉帧。完整 538794 条已接收日志写出，无 sink failure，
  呈现回收完成。`play-summary.json` 保存原始边界与统计，源日志 SHA-256
  `a8bc0908b13999fe8c4e2c6ca574492c751172847d819c2cdce9a004887d1ad2`。

- ABI 12 大往生 `.local/v06-present-regression-r1/daioujou-off-guest-fence-r2`
  （83372，已关闭）：关闭详细追踪，ARCADE／Type-A／Shotia 实际进关、射击、
  死亡复活并自然结束于 560 分。首个 F8 后的 10 秒关内片段有 600 条连续完成记录，
  p99 17.116 ms、最大 17.346 ms，音频欠载增量 0。两 F8 的完整 142.105 秒范围
  包含结算保存及停留在投币解锁提示，不能全部称为游玩：其中三次约 33 ms 间隔
  出现在结算保存事务和 480／900 ms 的原生 NGS rack 持锁附近，音频欠载增加 2。
  这些帧的 command acquire 约 1.05–1.10 ms，未重现旧的 15 ms 窗口 drain；
  保存与音频锁的因果关系尚未证明，作为结算阶段独立问题保留，不混入关内稳定性结论。
  `transition-analysis.json` 与 `play-summary.json` 保留两种范围和原始事件；源 SHA
  `3a72d9976e2d6b45add61b09101e1b7b54a5c87b48ad740a0a35ea626571fa48`。
  正常关窗完成呈现回收，77625 条已接收日志全部写出，无 sink failure。

- ABI 12 ESP `.local/v06-present-regression-r1/esprade-off-guest-fence`：F8 限定
  SUPER EASY 玩家第一关 61.088 秒，3659 条完成记录无序号缺口；p99 18.275 ms，
  最大 33.324 ms，7 次超过 20 ms，音频欠载增量 0。长帧 command draw 为
  10.793–13.599 ms，执行后软件节拍等待又达 12.645–16.819 ms；窗口提交交接
  本身仅约 0.064–0.083 ms。不能把整段写为稳定通过，继续针对绘图／节拍边界分析。
  182555 条已接收日志完整写出、呈现回收完成。源 SHA-256
  `99d92e76833997c351fc9adf181b404ab3e83806ea93a03911d1b41a4bba19a2`。

### ESP 绘图读取优化及关闭追踪的最终短回归

详细批次拆分定位到 fetch shader 逐 4 字节读取引起的重复一致性访问。
固定 64 项、线程局部的表只记最近成功读取长度，不缓存程序内容或解码语义；
每次仍读取实时内容，长度增长、缩短、映射变化和非法指令保留有界逐字读取路径。
原始数据、玩法更新、渲染指令和原作减速规则未改。

- `.local/v06-fetch-batch-red-test-r2.log` 的热路径单次读取检查在修复前失败；
  修复后 `v06-fetch-batch-ctest-r1.log` 的 5/5 GPU CTest 通过。新增 9 项读取检查，
  包括同地址改写、旧返回值所有权、增长、映射缩短、非法指令、恢复及提前返回。
  r1 的测试错误地嵌套了内存上下文，不能当作预期失败证据；保留原记录。
- `v06-fetch-batch-build-r1.log` 验证 6 对 PE/PDB，并整理三款 PS4 的 11 份运行文件。
  接受候选 host SHA-256 为 `17e67da4cb5e198e1aff3fd52e91a45e17c18bfae196a070f7c73d4c9ca61e83`，
  GPU DLL 为 `659cd846ad27389e8942b323cd8527d134ee8e18e5dd03feef6c261d3fe537f0`。
- `.local/v06-present-regression-r1/first-minute-comparison.json` 固定比较每次 F8 后
  前 60 秒、SUPER EASY／祐介；这是独立实玩，不是相同输入录像。详细追踪开启时，
  pipeline 平均从 7.474 ms 降至 3.269 ms，每 draw 从 29.31 µs 降至 12.92 µs；
  program-read 是其中的嵌套部分，不能与 pipeline 相加。关闭追踪时，绘图指令处理
  从 10.100 ms 降至 4.908 ms，完整指令执行从 11.609 ms 降至 6.422 ms；
  已记录最大完成间隔从 33.324 ms 降至 17.512 ms，超过 20 ms 从 7 次降为 0。
- `esprade-on-fetch-batch`（101292，已关闭）完整标记范围 77.635 秒包含最后的续关画面，
  不全部记作关内；上述固定前 60 秒是游玩范围。连续窗口提交间隔最大 17.568 ms，
  无超过 18 ms，音频欠载增量 0。窗口序号有一处缺口，相邻日志 33.528 ms 不能
  单独当作真实出帧间隔；422134 条已接收日志全部写出，呈现回收完成。
- `esprade-off-fetch-batch`（94988，已关闭）两个 F8 限定 65.942 秒实际游玩，
  从 STAGE 1A 到 1B，包含射击、死亡复活及中 BOSS；3956 条连续完成记录，
  p99 17.131 ms、最大 17.512 ms，超过 18／20 ms 均为 0，音频欠载增量 0。
  124408 条日志全部写出，呈现回收完成。源 SHA-256
  `9e6cda03d2c341e5bf6d98f59c45fb7898a25ea3693b6a43376f7b25892e07b7`。
- `ketsui-off-fetch-batch`（85888，已关闭）同一候选、独立离线档，SUPER EASY／Type-A。
  中断后的旧局仍在运行；本轮通过原生 RESTART 返回选机，不重启进程。两个 F8
  限定重新进入后的 127.231 秒、分数 0→16403，包含射击、自动炸弹、死亡复活、
  中 BOSS 和第一关 BOSS；不是关后菜单或演示。7634 条连续完成记录，p99 17.183 ms、
  最大 17.808 ms，超过 18／20 ms 均为 0，音频欠载增量 0。128507 条已接收日志
  全部写出、无 sink failure、呈现回收完成。源 SHA-256
  `5af76937cccd8f82114ab0ca833bde1e5cf9cfd8860674dd7b99646a3b580a1e`。

两个关闭追踪样本均没有详细窗口时钟；只报告连续客体完成间隔，不推断物理扫描。
采样时没有并行编译或日志分析，桌面刷新率未改变。正常关窗仍有原作 finalizer
未完整执行的 exit 27 收据，启动器按既有呈现回收条件识别。至此本轮性能短回归
通过；结算阶段的音频锁／保存问题单独保留，不再追加全模式、无限 FPS 或微小极值调优。

## v0.6 PS4 呈现：此前资源回收与独立图像复制步骤

开发候选使用两个有上限的呈现资源槽，复用时才等待旧窗口合成／呈现资源。
游戏画面先在 GPU 上复制到独立图像，窗口合成不再直接采样游戏缓冲。
`RuntimePresentResult.tick` 指复制批次，`composite_tick` 指后续窗口合成批次；
游戏完成要求复制 fence 已完成且既有 pacing 接受，不等待当前窗口合成 fence。
两个 tick 和呈现资源 fence 都不是屏幕扫描时间。GPU ABI 为 10，配套宿主与 DLL
由 Build-Ps4 同次构建；六对程序／PDB 收据通过，三款 PS4 的运行文件已整理。

- 五组 GPU CTest 通过，覆盖调度、绘制、FIFO 和关闭 VSync 的呈现路径。
- 显式 `--validation --presentation` 通过，Khronos 核心／同步验证错误为 0。
- 组件测试实际读回独立图像，线性 RGBA 和平铺 BGRA 与预期像素逐字节一致；
  改写自造的原始游戏缓冲后，已复制图像仍逐字节一致。
- 两槽轮换、取消、drain、缩放、最小化／恢复、解绑／重开通过；稳定循环中
  没有原生内存后端的像素读写流量。诊断读回只由测试调用，不进入正常呈现。

日志：`.local/v06-present-snapshot-{build-r2,ctest-r1,validation-r1}.log`。
首轮构建的 const 参数／buffer 类型错误已修正，失败日志保留。此前资源槽测试
的验证层首次运行失败是搜索路径缺失；设置进程级 VK_LAYER_PATH 后通过，
日志为 `.local/v06-present-slots-validation-r{1,2}.log`，未注册全局验证层。

待完成：将窗口图像获取／提交移出游戏 GPU 工作线程、有限最新图像交接，
以及真实游戏帧节奏回归。本轮不启动游戏，不将组件通过写成卡顿改善或物理延迟结论。

## v0.6 最大往生默认 1V

玩家实际对比 1V／3V 后确认 v0.6 默认采用 1V。Settings 缺省、嵌入模板与
本机 `user-data/config.ini` 已设为 1；已有显式 1／2／3 不被迁移覆盖。
456 项 common 和 238 项隐藏启动器检查通过，含新配置、缺键配置、原样迁移、
非法值拒绝与三语言独立进程冷读回。日志：`.local/v06-sdoj-default1-checks-r1.log`。
本轮只构建测试组件；正式运行组件留待 v0.6 统一构建，未修改既有交接算法，
未新增游玩验收结论，冻结的 v0.4 程序和发行包不变。

## v0.6 启动器布局与昵称提示

开发构建完成全屏／昵称分区和 `Launch.bat`。沿用真实 renderer 六张三语言、
两尺寸／DPI 预览的自审；本次桌面检查也确认昵称与全屏区域位置正确。
昵称检查同时覆盖手动保存与启动前自动保存，原生适配和启动器共用同一
NFKC、ASCII 别名及 UTF-8 截短规则。PS4 字库覆盖按实际资源读取的单字符
键核对，绊地狱 01.00 与 01.02 分开；本项不是游戏内新字体渲染验收。

- common／launch 两组 CTest 通过，common 为 456 项检查；首次回归暴露
  页头缩短后旧窗口高度常量断言，更新为新布局的 998／1493 后通过。
- 238 项隐藏生产窗口检查通过，包括缺字提醒、默认返回、保留草稿、
  不写 INI、不启动子进程、继续保存保留 Unicode 和仅改全屏不重复提醒。
- 7 组 Xbox 社区原生／本机 HTTP 回归通过；PS4 社区库和宿主实际重编，
  客户端自检通过。首轮仅旧测试仍将 esprade 当未知标题，更新该用例为
  真正未知标题并补跨作品 endpoint 拒绝后通过；原路由限制保留。
  3 项本地化检查通过。上述回归不写正式服务分数，不重新跑游戏模式。
- 真实独立启动器 PID 97768 输入 `友人😀` 后显示缺字、游戏和别名详情；
  默认 Return 返回编辑，INI 仍为 Player，草稿保留。Alt+F4 正常关闭，
  根配置及既有 v0.4 包未改动。
- 497 项源码回归中，其余项目通过、1 项符号链接权限跳过；唯一失败是
  BAT 改名尚未进入 Git 索引，源码边界检查仍枚举已删除旧文件。登记改名后
  17 项清单／边界检查单独重跑通过，未放宽审计或重跑整套检查。

记录：`.local/v06-nickname-{common-checks-r2,ui-checks-r1,360-checks-r1,
ps4-build-r1,localization-r1}.log`；桌面配置在
`.local/v06-nickname-desktop-r1/`。本节完成不代表 ESP Ra.De. 的可玩与联网已验收。
PS4 客户端记录 `.local/v06-nickname-ps4-client-r2.log`；源码记录
`.local/v06-source-regression-r1.log`／`.local/v06-source-audit-checks-r2.log`。

## v0.4 1V 持续输入短回归

使用已发行 ddf71cf 的宿主和模块、独立离线配置启动 Arcade HD／Type-A／SHOT
训练第一关。既有 TestPad 支持按输入帧保持组合状态；本次不修改程序，不依赖
人工长按，也不再把按住时失焦列为验收前置条件。

8 段共 750 个输入帧，逐次核对实际本体更新：右＋射击 120、只保留射击 120、
回中 60、左＋射击 120、只保留左 60、回中 60、右上＋射击 90、回中 120。
750 次逻辑更新的端口值全部匹配，均处于 core_state=7／play_state=4；每次状态
切换在相同输入帧完成本体掩码复制，松开后无残留输入。窗口确认进入训练关卡、
实际分数增长，最终正常 Alt+F4 关闭；没有崩溃文件或遗留游戏进程。

- 全程 6285 次提前请求／prefix／driver 回执配平，6140 次低延迟发布；无超时、
  预算跳过或所有权拒绝，最大完成等待 955 µs，初始化发生一次恢复。
- 长按段内六个完整呈现汇总窗口共 722 个间隔，最大 17.307 ms，无超过 20 ms；
  33 个完整音频窗口无补静音，队列上限 42.7 ms。这是本段诊断运行的统计，
  不等同于设备到屏幕延迟测量。
- 沿用已通过的 r7 训练重入及完整录像结果，不重复模式矩阵；本次不代表实体
  键盘或焦点边界测试。共享配置仍为 3V，已冻结包不重建。

记录：`.local/v04-sdoj-held-r1/logs/sdoj/run-20261023T041238915Z-102632`；
命令序列、分析脚本和结果在同根 `hold_sequence.py`、`held-commands.json`、
`analyze_held.py`、`held-result.json`。分析读取完整轮转日志 `game.1.log`＋`game.log`，
不只读取退出前的最后一段；首次漏读轮转的分析失败结果单独保留。

## v0.4 win-x64 双包

当前程序包固定于 `ddf71cfb5f1e015fdd634c8f561ddd85065521ef`。format=2 仅生成
`EVAC-PC-LABEL-v0.4-win-x64.zip` 和 `EVAC-PC-LABEL-v0.4-win-x64-internal.zip`，
均为平铺布局，不附用户配置或源码。旧四种 ZIP 及其验收记录保留，不覆盖。

- 497 项源码回归通过（1 项 Windows 文件符号链接权限跳过），源码审计通过；
  其中 34 项打包测试与 6 项历史源码附件测试覆盖新规则及历史格式验证。
- 五款增量构建的 56／6／2 对程序和符号通过，27 项公共／360 原生 CTest 通过。
  相比 `091c15c`，common、launcher、backends、games、third_party 和默认配置无代码差异；
  PS4 程序、启动器、360 模块和 SDK 的文件身份保持一致。三个 360 宿主因构建身份
  增量重链，五款运行清单更新提交；不将新哈希写成新的游戏实测。
- 两个最终 ZIP 各实际解压一次，完整载荷校验通过；每包九项配置、存档、录像、
  数据及其他用户文件哨兵保留。36 项共用运行文件在两包中逐项一致。
- 外部包 136 项载荷，game-data 只有六份放置说明；实际解压的检测程序正常执行，
  五款显示缺少数据，ESP Ra.De. 保持未接入，不创建用户配置。
- 内部包 602 项载荷，其中 435 项游戏／DLC 数据、67 项运行程序／符号、40 项运行库、
  51 项许可说明、9 项使用与数据说明。含五款完整数据、Black Label、Ver.1.01 和符号，
  不含源码、配置、玩家存档、缓存或日志。

| 包 | 字节数 | SHA-256 |
| --- | ---: | --- |
| win-x64 | 78205835 | `6aa98f48759d291787d11f9e7619ca9429e6f0278f7d708f07cc52d128d945aa` |
| win-x64-internal | 7085755632 | `44c94ad6bd3643ab83ab4291858206d9138085b1024bea92fd4383031ecbdefe` |

构建、回归及本地验收记录在 `.local/v04-package-format2-*`，一次解压收据在
`.local/release-acceptance-v04/<SHA前16位>/accepted.json`。复用已接受的游戏回归；
SDOJ 默认 3V，1V 持续输入／松键专项已完成，见上节。截图复用同哈希启动器的
`.local/v04-release-media/launcher-v0.4.jpg`，新群文案为 `.local/v04-win-x64-group-message.txt`。

两包及校验文件均已上传，远端大小、SHA 元数据、公开校验文件和 32 字节 ZIP 头
全部通过；没有重新下载整包。证据为 `.local/v04-package-format2-{public,internal}-oss-r1.json`。
v0.4 标签指向配套程序提交 ddf71cf，后续说明记录不改变包身份。QQ 已通过实际
QQNT 程序唤出，目前停在登录页，等待本人登录；未发送消息或截图。

## v0.4 旧四包的配套构建与一次解压验收

发行程序和对应源码固定于 `091c15c76b9fb51919332310fc802280026cd613`；后续下载说明
和交付记录不重打这些 ZIP。PS4、Xbox 360、启动器的构建收据同提交，56／6／2 对
PE／PDB 及五款运行清单通过，27 项公共／360 原生 CTest 通过。最终 SDOJ 两模块及
PS4 宿主／GPU 与已实际回归的候选逐字节相同。证据为
`.local/v04-release-{build-r1.log,native-r1.log,certified-set-r1.json}`。

| 包 | 字节数 | SHA-256 | 实际解压 |
| --- | ---: | --- | --- |
| Public | 72888170 | `8fd27d85b9a313fa002253de5c279415bfd53059c79969eaa03a4e00eb88ea96` | 136 文件，通过 |
| Update | 78200361 | `ec24d85f4469b947683bf4042ea991cad979bf12398d880857fa094707d68aec` | 130 载荷文件，通过 |
| Internal | 7105602167 | `5e5f91d38c783e895ca36515f5f7abd8270df28b4b4907d4da9047a801c3cdaa` | 2915 文件，通过 |
| 对应源码 | 288246758 | `193396c82304a6cad74dc2c4ea8be1b62f47d6ab2438092cb9ff9cd66bbe9c17` | 37205 文件，通过 |

每种最终 ZIP 只真实解压一次，收据以压缩包 SHA 绑定在
`.local/release-acceptance-v04/<SHA前16位>/accepted.json`。Public／Internal 的六份放置
说明、Internal 的五款数据与 DLC 清单、独立源码 Git、Public／Update 无 ROM 或源码
边界通过。三种玩家包共有的 35 项运行文件哈希相同；旧文件名入口与新 EXE 完全相同。
Update 覆盖已解压的 v0.3 程序副本，六项旧配置／数据／存档／录像／源码哨兵保持不变，
真实旧入口正常打开 EVAC PC LABEL，读回选择与设置，正常关窗。

对应源码包含 25 项 SDK 和 19 项 PS4 固定依赖，源码 Git 与对象独立。首次在较深验收
目录核对 ext-boost 时，Git 默认长路径限制将七个长文件名 HTML 报为修改；文件 SHA
已与 ZIP 清单一致。在该次验证进程单独设置 `core.longpaths=true` 后依赖身份及干净状态
全部通过，未改变全局 Git 设置、源码内容或 ZIP。开发文档要求较短英文目录仍有效。

Internal 解压副本的真实启动器正常打开，默认昵称 Player、首款选择、数据识别和布局
正确，截图保存为 `.local/v04-release-media/launcher-v0.4.jpg`，随后正常关闭。未为不同
包重复运行游戏；复用同组件身份的上述代表性回归。1V 实体长按／射击松开尚未实测，
保留独立待办，默认仍为 3V。公网上传、发布标签和群通知另行记录。

四种 ZIP 与校验文件已上传至 OSS 的 `v0.4/`。Public、Update、Source 和 Internal
均通过远端大小／状态、公开 SHA-256 文本及 32 字节 Range 头部核对，不重新下载
整包。证据为 `.local/v04-oss-{public-update,source,internal}-verified-r1.json`。
中英 README 仅列不含 ROM 的 Public／Update／Source 下载。发行标签和群通知未执行，
1V 实体长按待办是否延后已向用户提出；截图、群文案保留在本地。

## v0.4 桌面恢复后的实际回归

用户于 10 月 22 日手动关闭 Taskmgr 后，游戏窗口可正常激活。以下日志位于新工作目录，
替代下文历史记录中的“等待窗口恢复”状态；不再打开任务管理器或重试窗口权限恢复。

| 项目 | 实际结果 | 记录 |
| --- | --- | --- |
| G2 Xbox 360 Novice／Ageha | 通过：实际玩家第一关、分数增长、暂停、Previous Menu 返回、dat163 卸载、正常关闭 | `logs/espgaluda2/run-20261022T115223367Z-81172`，基线 `8fab1ce` |
| SDOJ 基线 1V／Arcade HD／Type-C／Shot | 再现统计缺失崩溃；原有迟解析修正不足以解决全部缺口 | `logs/sdoj/run-20261022T115752086Z-101808`，基线 `8fab1ce` |
| SDOJ 未解析批次保护候选 | 已进关、射击得分 12620、自然结算及录像选择，未再出现该异常；超时后持续两帧时序，1V 收尾仍未通过 | `logs/sdoj/run-20261022T121639607Z-10556`，未提交候选 DLL |
| 启动器桌面单实例与选择记忆 | 通过：选择绊地狱、最小化、重复运行恢复原窗口；正常关闭并重新运行后仍选中绊地狱 | 同一根目录的基线 `8fab1ce` 原生 EXE；原 PID 99140、重复调用 PID 71660 自动退出、新 PID 100576 |

G2 的 dat163 于 19:53:27 加载，19:56:35 正常卸载，19:56:57 正常退出；实际画面
显示 PLAYER-1 与 Xbox 360 Novice 暂停菜单。代表性进关覆盖完成，无须重开全模式测试。

SDOJ 崩溃现场保留完整统计环：逻辑 19625、缓冲 `42990030`、due 19957 未解析；
19626、缓冲 `42AC0030`、due 19958 已解析。第 19627 次更新在 calls 19958 需要
19625 的原时序统计，因不存在而抛出异常。现场有一次预算跳过，没有超时。
候选在 bootstrap／提前唤醒之前检查停驻批次是否已有真实解析结果，避免先发布旧批次
又立刻用新批次覆盖它；原异常与日志保留，未猜测零统计。1134 项所有权检查、五项
源码约束检查和两模块定向编译通过，但这些不证明 1V 持续工作。

实际候选在资源加载后正常启用提前交接；随后一次 2049 µs 等待超时，early_requests
停在 3303、low_publications 停在 3168，而总 publications 继续增长。这证明其保护
能持续退到普通后段唤醒路径，不能仅凭 `effective_policy=1` 宣称仍是 1V。
下一项须处理恢复／重新对齐策略，保持原作减速与缓冲所有权，再做小范围针对性验证。

### 未解析批次保护与提前唤醒恢复（r5 历史结果）

r5 后续训练重入出现新的断言失败，不能作为最终候选；原因与修正见下一节。

普通绘制先消费尚未解析的完成批次，再将该帧尚未执行的一次后段唤醒移到下一帧的提前槽位。
同一逻辑计数对应的减速统计随唤醒保留；不额外解析、不丢弃批次，也不增加逻辑更新。
完整批次身份、发布回执、原计数和暂停状态均须符合才允许恢复，外设尾部仍正常执行。

实际时序显示，原后段驱动位于 `ReadEnd` 与原生统计存储之间（r4 的
`logs/sdoj/run-20261022T124443959Z-97992`）。r5 在真实解析返回时采集 renderer
的 +544／+548，再在原统计存储处逐次核对；原生成代码不改，统计不一致仍抛出异常。
1345 项 C++ 机制检查和七项源码约束检查通过，两模块定向编译通过。

`logs/sdoj/run-20261022T125057047Z-96616` 实际完成 1V／Arcade HD／Type-C／Shot
选人、第一关和自然结算，正常关闭。首次未解析批次只触发一次恢复，随后提前请求持续
增长；最终 10553 次提前请求、10416 次低延迟发布、10615 次统计核对，无超时或统计
错误。此轮没有实际射击得分证据，不替代普通按住／松开或完整音画验收。

r5 模块 CA022100 SHA-256 为 `0190034e34866e4045530953b54133c57ee51c74445ac0d913d584120e5d12c7`，
CA022110 为 `30075afed576b77c0cdbe0c3bcb215f708c61717c7756389cb0815031cf453a9`。
候选仅在 out 树运行，已认证 runtime 不变。以下两次独立离线冷读同一短录像，均
正常完成、自然返回列表并关闭；本机共享配置保持 3V。

| r5 运行 | 原生结束更新 | 同类历史参考 | 完整逐更新对比 |
| --- | ---: | --- | --- |
| 2V，`.local/v04-sdoj-recovery-replay-balanced/logs/sdoj/run-20261022T125641692Z-48960` | 5504 | 1V／98776 | 5504／5504 一致 |
| 1V，`.local/v04-sdoj-recovery-replay-low/logs/sdoj/run-20261022T130451615Z-34332` | 5505 | 2V／98276 | 5505／5505 一致 |

参考是旧目录 `.local/sdoj-frame-delay-map-init-r1` 下的冻结运行，详见本页“常态节奏、
音频队列与完整录像复核”。比较从逻辑 1 至原生结束请求，不裁剪、重排或屏蔽命令；
端口快照、命令大小／原始 SHA、状态与 depth 一致，另核对两项减速操作数亦零差异。
两份各有 131 次 depth=2。原录像 SHA `6b724e45…3e7c` 未变。
报告为新目录 `.local/v04-sdoj-recovery-r5-{balanced,low}-old-exact.json`。

新两轮落在原作已有的不同初始化类别：状态 4→5 分别在 16／17 次更新发生。
同构建直接比较的 5504 次更新有 5468 次不同，失败报告
`.local/v04-sdoj-recovery-r5-cross-init.json` 保留，不标成通过。上述是与同类历史
参考的跨构建完整一致，不是这两次新运行彼此完整一致；无需为凑相同初始化反复启动。

1V 回放的单次恢复后，5638 次提前请求／prefix／driver 回执配平，5506 次低延迟发布，
7307 次统计核对，等待最大 971 µs。无超时、排空和所有权拒绝；一次唤醒前预算不足，
并非“零回退”。2V 的 7205 次统计核对也全通过。超时现场的保护由机制序列覆盖，
本次实际回放没有触发超时，不把它写作真实超时恢复通过。普通输入／失焦短测及正式
同提交发行仍独立待办。

定向构建后的 CTest：27 项原生组全通过；source 组运行 489 项（1 项权限跳过），
其中 Ketsui 服务的连接数耗尽用例遇到一次 Windows 10053 连接中止，独立复跑通过。
失败总报告保留于 `.local/v04-sdoj-recovery-r5-native.log`，不称本轮全套零失败。
该用例与 SDOJ 修改无代码交集，未据此修改或部署正式服务；源码边界审计通过。

### 训练重入的原生统计清零（候选 r7）

r5 的训练重入运行 `run-20261022T131308846Z-100092` 抛出
`render metrics changed after parser return`；r6 仅加诊断，在
`run-20261022T132945075Z-101048` 用“训练进关→暂停返回→外菜单→训练”再次复现。
二者均在 `.local/v04-sdoj-recovery-replay-low/logs/sdoj/`，转储与失败日志保留。
诊断明确显示同一 renderer `885F1A70`、logic 4843 的解析统计为 `1688688,0`，
最终原生存储为 `0,0`，control+720 为 1。原生 `sub_88046330` 在此条件下，
于 `88046514/88046518` 清零 renderer+548/+544；并非缺失解析，也不是模块卸载。

r7 将身份与值分开：解析结束只固定批次身份，仍在原生最终存储处采集真实操作数；
一次恢复唤醒先按准确的解析／发布身份挂起，再于最终采集后冻结减速输入。
原生训练清零与普通非零值均照实保留，不再错误假定解析返回时数值已经最终确定。
同一批次、renderer、帧号、唤醒计数及统计冲突检查仍存在；3V 路径不变。

1371 项机制检查、七项源码约束检查、27 组公共／360 原生 CTest 通过；两模块编译通过。
日志为 `.local/v04-sdoj-native-final-r7-{build,ctest}.log`。CA022100 SHA-256 为
`030309a6882955fc7b99b0860a5a171e13f2355cb489d6254cb5f87ac66bce5d`，CA022110 为
`c1f1f5bea545e9d0930a663fbbe100ccd3b41a735d892f001490ab18bb6f0209`。

实际训练运行 `run-20261022T133616139Z-43740`（同上独立根目录）完成进关、自然返回、
暂停、切到启动器再切回并恢复，最后从外菜单重新进入训练且正常关闭。
重入时两次实际清零被记录为 `SDOJ_METRICS_FINALIZED`，不再触发旧断言。
最终 23166 次提前请求／prefix／driver 回执配平，22737 次低延迟发布，
27814 次最终统计采集、5 次恢复、4 次预算跳过，无超时／排空／所有权拒绝。
确认／返回及失焦恢复通过；短方向键已进入原生输入位，但画面未充分证明方向位移，
长按移动、射击／松开与实体设备验收仍开放，不能由工具短按或机制检查代替。
固定录像 r7 复核已完成：`run-20261022T134620194Z-86580` 从 Arcade HD 读取
第二槽（2026/10/21 10:36，`REPLAY_XB02.DAT`；文件名不等于菜单模式），自然返回列表
并正常关闭。module 1／generation 2／逻辑 1–5505 与历史 2V／98276 完整一致，
含原始命令 SHA、核心状态、depth 和两项减速操作数，131 次 depth=2。
报告 `.local/v04-sdoj-native-final-r7-{closed-exact,slowdown}.json` 绑定关闭后的日志哈希。
该进程先误选 Xbox 360 菜单的第二槽，执行的是 module 2；这一段不计入普通核心对照。
普通核心最终 5639 次提前请求／两种回执配平、5506 次低延迟发布、8449 次最终采集，
一次恢复，无超时、预算跳过、排空或所有权拒绝，最长等待 990 µs。
r5 的完整对照保留为其自身版本的证据，不再重复已接受的短录像。

修正已提交并推送为 `af7246e`。提交后完整源码套件 489 项通过（1 项文件链接权限跳过），
日志 `.local/v04-sdoj-native-final-r7-source.log`；源码边界审计通过，无 ROM。

早期保护候选 10556 从 `out/xbox360/bin/sdoj.exe` 运行，进程模块枚举确认实际加载
`out/xbox360/games/sdoj/sdoj_CA022100.dll`。该 DLL SHA-256 为
`94FD34EBE7F020479FDB2E420A4FDBDA85CA82D3FC4C958DBC86A15994B53DA7`；
CA022110 为 `ec331eec5a728bc22f53dd5ebd52d62a7075cc0d4404d87d56f975ae7ec16220`。
该运行的 run.json 仍列出已认证 runtime 清单，不代表候选 DLL 身份；正式发行前须更新
配套收据。先前 run 91536 因编译目录缺少模块而停在 Loading，未执行修正，不计作游戏失败。
两次独立候选进程均正常关闭；认证 runtime、原数据和冻结发行包未变。

测试临时增加了 CU 方向键所需的 Numpad 别名，并将本机 frame_delay 改为 1；结束后
已恢复原方向键及 3V。C 短按实际完成角色／Shot 确认和射击，属于操作工具经 Windows
输入链送达的结果，不等同于实体摇杆或手动长按验收。失焦恢复与完整音画观察仍未关闭。
启动器桌面检查已确认实际选中边框、标题栏新名称及冷读回，配置落盘为
`[collection].selected_game = ketsui`。首次重启命令使用 Hidden，窗口因此未显示；
随后正常 GUI 启动经单实例消息恢复同一 PID 100576，不属于启动器丢失选择或多开。

## v0.4 新工作目录

当前开发目录为 `E:/Projects/EVAC-PC-LABEL`，由新 SSH 远端独立检出 `80edd1c`，
提交历史一致、无 Git alternates。旧 `E:/Projects/CAVE-PC-Collection` 完整保留；
下文较早的 `.local/` 证据仍存放在那里。直接改名失败后的恢复记录也保留在旧目录，
不再继续分步移动文件、改权限或建立旧路径别名。

- 固定 LLVM／CMake／Ninja 已复制并核对锁定身份，工具 Python 在新路径重新创建。
  25 项 SDK 依赖（22 项顶层）和 19 项 PS4 依赖从已核对的缓存独立检出，保留上游
  remote、固定提交与本地对象库，不依赖旧目录的 Git 对象或工具路径。
- 已安装 SDK 复制到新 `.deps/sdk-install`，仅三份 pkg-config 文件的安装前缀更新；
  编译后的程序不改动，源码／工具／二进制收据验证通过。这是安装产物复用，不是
  SDK 干净重编译。CMake／pkg-config 安装元数据不再引用旧工程前缀。
- 公共组件与启动器从空 `out/common` 生成并完成 19 项编译，配套两组 PE／PDB 通过。
  common、launch、source 三组 CTest 全通过；未启动游戏。
- 五款游戏及虫姬二两份 DLC 按支持清单复制，所复制文件身份校验通过；原始数据
  不改动。新原生检测 CLI 完整检查五款数据通过，绊地狱识别为单目录 01.02。
  此时尚无五款运行组件，状态正确为 missing_runtime；不记为可玩验收。
- user-data 逐文件身份核对后复制，仅新配置的四处本体／DLC 路径改成合集相对位置，
  昵称、按键、3V、存档和录像不变，旧配置完整保留。

新目录证据：`.local/v04-new-root-{tools-r1.log,sdk-deps-r1.log,ps4-deps-r1.log,
common-build-r1.log,common-native-r1.log,data-r1.json,inspect-r2.json}`。
检测 r1 含错误命令选项，失败记录保留，纠正命令后的 r2 才是上述数据检查结果。
迁移时的桌面检查有 Taskmgr 遮挡；新启动器激活返回
`failed to activate captured window`，已结束自己打开的测试启动器并请求手动清除遮挡。
桌面恢复后的 G2 与 SDOJ 实际结果见首节；绊地狱两版游玩及最终发行继续待办。

### 新目录五款运行组件与针对性检查

配套运行组件源码固定为 `8fab1ce9f2aeea899a7001fc6221ba56774e03fe`。PS4 图形／原生
分别完成 375／555 项编译，Xbox 360／公共组件完成 508 项；未复用旧绝对路径的
构建缓存。宿主、PS4 和启动器三份收据同提交，56／6／2 组 PE／PDB 校验通过，
五款 runtime INI 的提交和实际文件哈希均匹配。

- 5 项 PS4 GPU CTest、73 项 PS4 原生机制检查、27 组公共／360 CTest 全通过。
  最后者包含 1122 项最大往生 Frame Delay 所有权检查；未重复已通过的完整源码测试。
- 新生成的 G2 `0x880EEB00` 入口完成 12 项隔离 ABI 检查。不执行游戏，也不代替
  Xbox 360 Novice 选人后进关测试。
- 原生 CLI 的程序级检查确认五款 missingRuntime 均为空；资源级别仍显示
  needs_verification，这是程序级而非完整资源检查的预期状态。完整资源身份沿用
  本节前面的 r2 检查，不把程序级结果升级为资源或游玩验收。
- 263 份 user-data 文件复制后核对，除新配置中四处数据路径外，其余 262 份保持
  身份；旧配置不变。实际游戏、存档重启和社区流程仍按开放项验收。

证据为新目录 `.local/v04-new-root-{ps4-build-r1.log,xbox-build-r1.log,
ps4-gpu-tests-r1.log,ps4-native-r1/result.json,xbox-native-r1.log,g2-callback-r1.log,
certified-set-r1.json,inspect-r3-programs.json,user-data-r1.json}`。
构建及检查进程均已成功退出，无后台编译待等；本次没有启动游戏、出包或上传。
该次只读桌面复查仍显示 Taskmgr，未重复失败的恢复操作；后续已由用户关闭。
后续状态文档提交不改写上述构建身份；最终发行仍需实际游玩与最终提交配套认证。

## v0.4 迁移前五款组件基线与桌面前置条件

运行组件源码为 `3fc66258d5e78cf5780178fdb08395b6b7c71b32`。Xbox 360、PS4、启动器
三份收据均绑定该提交，五款游戏的运行证书逐文件身份核对通过：56／6／2 组
PE 与 PDB；27 组公共／360 原生检查通过。未生成或发布 v0.4 正式包。
记录为 `.local/v04-five-game-certified-set-r1.json`、同前缀的
`xbox-build-r1.log`、`xbox-finalize-r1.log`、`ps4-build-r1.log`、`xbox-native-r1.log`。

构建目录中四个旧名启动器 EXE／PDB 已按原哈希移到
`.local/v04-retired-launcher-build-outputs-21f5e359094e4e26b1ee26faf08a4a6a/`，
不再混入新输出认证。备份可恢复；两份旧程序现已按用户授权关闭，文件保留。

独立 SDOJ 离线 1V 测试 PID 78892 使用当前组件、空测试 profile 与既有逻辑输入。
确认键及 Start 的命令已送达，画面到标题与首次存档提示；同一任务管理器继续遮挡
关键区域，因此停止，没有进入目标模式或实际验收减速统计修复。`5 quit` 经过正常
停机路径退出，进程消失；`.local/v04-sdoj-metrics-r2/summary.json` 和对应 run 日志
保留现场。它不证明物理键盘、玩家第一关或朋友崩溃场景通过。

该轮后 GitHub 已由用户更名，新 SSH 地址读取 main 成功，历史提交一致，
两份旧启动器已关闭；新工作目录现况见本页首节。实际游戏／桌面和最终包的
开放项继续保留。

首次物理目录改名失败后的分步移动已恢复到原根目录；当时新目标目录不存在，Git 只
保留预期维护改动。配置、G2 入口程序、新启动器及 .git/HEAD 的大小／SHA-256
与改名前记录一致，19 项 PS4 源码依赖验证通过。原子目录改名仍返回拒绝访问，
不做权限修改或继续分步移动。证据为 `.local/v04-workspace-rename-before-r1.json`
和 `.local/v04-rename-recovery-r1.json`。此核对不替代迁移后的干净构建。

## v0.4 程序品牌、入口与覆盖更新

当前构建输出 `EVAC PC LABEL.exe`／配套 PDB，标题栏及两类游戏窗口前缀统一。
实际 Windows 版本信息读回 FileDescription／ProductName 为 EVAC PC LABEL，
OriginalFilename 为 EVAC PC LABEL.exe，FileVersion 为 0.4.0.0，ProductVersion 为 0.4。
BAT 正确引用带空格的文件名，构建收据验证新命名的两组 PE／PDB。

- 27 项打包测试通过（1 项 Windows 文件链接权限跳过），使用自造数据实际组装、ZIP 和解压 v0.4 Internal／Public／Update。新文件名、源码 ZIP 前缀、六份数据说明和 Public 无 ROM／源码边界正确。Update 平铺并含新旧两个文件名入口，两者均取同一认证原生 EXE；校验拒绝另一个二进制冒充旧入口。覆盖后配置、数据、存档、录像和旧源码哨兵不变。旧版归档命名继续验证，不重打冻结包。
- 226 项隐藏生产窗口检查、386 项公共原生检查和 139 项启动／配置／复制检查通过。PS4 六组 PE／PDB 编译及配置读回通过；Xbox 360 公共宿主库完成编译，仅修改标题，不将库编译当成五款最终运行组件认证。
- 486 项源码测试通过（1 项文件链接权限跳过），源码边界审计通过，未包含游戏数据；记录为 `.local/v04-brand-source-r2.log`。
- PS4 第一轮为 72／73，失败为信号量自测。原记录显示 Address 等待在约 199069 微秒已看到成功结果，剩余少量时间是合法结果；断言却要求恒为零，且短路跳过 Delete，导致后续注册表检查连带失败。同一未改程序的定向复查通过，但不据此抹掉首轮。仅修改自测，按真实经过时间核对剩余预算并无条件清理；未改生产 Wait／Signal／Grant。修正后第二轮 73／73，通过记录中的真实时间及剩余预算继续保留。

证据：`.local/v04-brand-{package-r1,build-r1,launcher-r1,common-r1}.log`、
`.local/v04-brand-properties-r1.json`、`.local/v04-brand-ps4-build-r{1,2}.log`、
`.local/v04-brand-ps4-native-r{1,2}/result.json` 和
`.local/v04-brand-semaphore-recheck-r1.jsonl`。中英文首页、玩家数据指南、v0.4 包内说明和开发命令同步到开发版行为；已发布下载仍明确指向 v0.3.2。

GitHub 已更名且新 SSH origin 可用；新工作目录的结果见本页首节。正式同提交包、实际桌面复开／输入、报告模式和绊地狱两版游玩仍待相应验收，不由上述自造数据或隐藏窗口检查替代。

开发根已整理出新文件名启动器；根文件和 out/ 的程序／PDB 身份通过构建收据校验，
两份原运行启动器的 SHA-256 均未变化。新 EXE／PDB 采用精确根路径忽略规则，
不扩大为任意二进制豁免。记录在 `.local/v04-brand-root-stage-r1.log`、
`.local/v04-brand-preserved-launchers-r1.json`，不是正式发行或游戏内回归。

## v0.4 预建游戏数据目录与放置说明

`config/game-data/` 的六份中英双语说明进入 v0.4 完整包的 `game-data/<id>/放置说明.txt`。每款说明标明数据版本、顶层入口、内部目录和适用更新／DLC 放法，ESPRaDe 保持 Coming Soon。Public 仅允许六个固定说明路径，且内容需匹配维护源码；即使重写 ZIP 清单和校验也不能用任意文件冒充说明。Update 仍拒绝 `game-data` 下的任何内容，旧版本包的结构不回改。

- 25 项组包用例完成（1 项文件链接权限跳过），其中 v0.4 使用自造资源走 Internal／Public 的真实组装、ZIP 和分别解压，说明均存在，Public 无 ROM／源码。不是实际发行包的解压验收。
- “复制到合集”可使用只含说明的默认目录；原说明内容和属性保持，目录在 `cache/<id>/contract-1/import-guides/` 留下可恢复备份，新副本仍通过完整数据检查后发布。相同来源说明可复用；来源／目标说明冲突或目标含其他文件时拒绝，源数据和原配置保留。
- common／launch CTest 通过，启动／配置／进程／复制共 139 项；隐藏启动器 226 项通过。481 项源码检查完成，1 项权限跳过，其余通过，源码边界审计无游戏数据；公共组件及启动器两对 PE／PDB 配套编译通过。实际检测 CLI 使用六份真实说明，五款缺数据状态与 Coming Soon 分离，检测不写盘。

记录：`.local/v04-placement-{build-r2,package-tests-r2,native-r1,source-r1,launcher-r1}.log`。首轮 fixture 的跨平台换行不一致和 C++ 声明类型错误已修正，失败日志保留。未改原始游戏数据、共享配置或冻结 ZIP；最终 v0.4 包和真实游戏短回归继续待办。

## v0.4 启动器选择记忆、单实例与布局

启动器窗口标题为 EVAC PC LABEL，默认 EXE 文件名及其余构建／发行品牌仍待后续统一。共享配置新增 `[collection].selected_game`；正常销毁窗口时仅将当前选中 ID 合并到最新磁盘文本，下次打开恢复并滚动到可见位置，未安装游戏也保留选择。`--select` 指定 GUI 初始选项，`--game` 保持原有直接启动含义；重复打开已有 GUI 不用新参数改写原窗口。

- 226 项隐藏生产启动器检查通过，包括关闭／独立进程冷启动读回、缺数据保留、空／未知 ID 缺省、命令行选项优先、BOM／未知节及外部编辑文本保留、昵称草稿不被关闭动作隐式提交。
- 使用真实命名互斥量与跨进程窗口消息，覆盖安装目录别名、最小化恢复请求、同时启动只选出一个 GUI、正常退出释放、启动期间异常退出的遗弃锁接管，以及不同根目录各自运行。原选择、未保存昵称和一份 fixture 子进程句柄在激活前后不变。恢复／前台请求和外部 Shell／对话框在 fixture 边界截获，不操作用户桌面或启动游戏。
- 六张原生渲染图覆盖中／英／日，以及 1080×900／96 DPI、720×520／144 DPI。Logo 可见顶端为 23／23.33 DIP，语言文字顶端为 24 DIP；底部主按钮和工具按钮底边一致。已检查三语言代表图，无常驻箭头／Enter 说明、重叠或错位。图像为带明确示例标记的开发预览，不作为正式发行截图。
- common／launch 两组 CTest 及 479 项源码检查通过（1 项文件链接权限跳过），源码边界审计无游戏数据。保留旧游戏会话互斥量名字，仅抽出相同目录身份计算供 GUI 复用。公共组件配套编译及两对 PE／PDB 收据通过，未替换仍打开的根目录旧启动器。

证据为 `.local/v04-launcher-ux-build-r1.log`、`.local/v04-launcher-ux-r4.log`、`.local/v04-launcher-ux-source-r1.log`、`.local/v04-launcher-common-native-r1.log`、`.local/v04-launcher-visual-r1.log` 和 `.local/v04-launcher-visual-r1/`。游戏行为、实体输入、最终改名发行版的桌面启动及出包验收不由这些 fixture 代替。

## v0.4 绊地狱单目录与双版本准备

### 10 月 22 日本体实际回归与录像版本修正

独立现场 `.local/v04-ketsui-desktop-r1` 使用本体 01.00、独立 profile 和
loopback 服务，不访问正式榜单。基线 `8fab1ce` 的 PID 67116 实际完成 ARCADE／
Type-A 选机、第一关、暂停／原生按键设置、自然结算和游玩解锁保存；810 分录像已
进入本地原生槽位。输入使用 CU 短按及显式 120 ms 诊断脉冲，不作实体长按验收。
窗口关闭走已有呈现清理／退出码 27；不是完整客体 finalizer 清理的新证明。

本体局末采集的 `valid:false` 定位为版本合同缺项，不是低分资格：实际录像容器为
`0x10001`、核心数据为 `0x10000`，既有代码错误地只接受更新版 `0x10001` 核心数据。
两版原生录像 PSB 结构相同，更新版原生 viewer 有明确的 minor-version 0／1 分支。
修正将每个已核验可执行文件绑定自己的采集版本，公共 envelope 接受两个已知数据
版本，保留原始记录和原生播放版本判断，不修改原作回放或伪造 header。

真实 810 分原记录 SHA-256 为
`c1dfeb666d95abd63e0a5519ec615874684f9cea5cc077ef0b574a6cc4a05869`，524288 字节、
4183 个 consumed frame、417 个快照块。旧程序拒绝，修正程序完整解析及成绩匹配通过。
候选 EXE SHA-256 为 `9abbe1c885844980604ce79b940114228701df72fbd0b4a223b573939adcef30`。
49 项采集、38 项 journal、19 项 audit、24 项 envelope 测试、33 项双作品格式检查及
10 组本机 HTTP 检查通过；新增本体收发校验含数据库中的录像字节和下载哈希。
证据为 `.local/v04-ketsui-base-replay-*`，原记录分析为上述现场的
`base-replay-analysis.jsonl` 与 `base-810-{before,after}.jsonl`。HTTP r1 在运行前因测试
脚本已更新而拒绝旧收据；重新构建／收据验证后的 r2 通过，失败记录保留。
修正后的 PID 103252（`base-loopback-r3`）冷读回本体存档，实际 ARCADE／Type-A
自然结束的 0 分局成功采集两榜并落盘，reference 为
`67f6c0bf0b6b1f8c71d9643e8756d74c`；服务端综合／Type-A 榜均接受，两个 18641 字节
录像附件 SHA-256 同为 `8acbe51b786f60afb368e371f8e5af5898a58422fbf3d0f9a3abfb2207a77451`。
实际 Online Ranking 显示 V04-Base，下载返回 200／完整元数据校验通过；Download Replay
中的该录像完整播放约 62 秒并自然回到列表，之后正常关窗。投币解锁上限从前进程保存的
5 继续增长到 6，未重置。本机服务中是实际游戏采集，不是 fixture 或正式服务写入。
原生保存记录包含后续本地元数据处理，未声称整个存档槽逐字节等于网络容器；网络原记录
与附件身份见 `base-native-delivery.json`。489 项源码测试通过（1 项权限跳过），源码审计通过。
更新版 PID 102668（`updated-loopback-r1`）使用 47 文件合并单目录和本体 profile 的
独立副本，实际 ARCADE／Type-A 进关、炸弹击毁得分及 1130 分自然结算通过；读取既有
6 次投币解锁进度并保存至 7。综合／Type-A 两榜成绩及录像均返回 200，原生回放证明
返回 0。reference 为 `1df90b40372f2147cd42bb8456f17a89`，附件 31626 字节，SHA-256 为
`612a9286b8f29587e516aa6d365a1e30a0ab9d599dd69ab4f471a19b3225f843`，数据版本为
`0x10001`。该进程的 Download Replay 冷读回继承的 V04-Base 录像，完整播放约 62 秒
并自然返回列表，验证了原生 viewer 的旧数据版本分支。随后 Alt+F4 正常关窗，呈现
清理完成；退出码仍为既有的 27，没有声称 guest finalizer 全部清理。测试服务仅接受
这两局各两个榜单记录，未改正式服务。程序、完整日志和 SQLite 均保留在独立现场。

### 单目录准备基线

开发代码已将绊地狱改为单个实际游戏根目录：01.00 本体使用 41 项清单，完整覆盖 01.02 的数据使用 47 项清单。以根 `eboot.bin` 的大小和 SHA-256 选择版本，再校验该版本的全部程序、资源和禁止混入项；不按文件夹名猜测、不在缺少更新文件时退回本体清单。旧双目录在实际只读检查中返回 `unsupported_version`，不自动迁移。

- 从旧清单精确对应原文件，建立 `.local/inputs/ketsui-v04-single-root-r1/{base,update-102}` 两份全新测试副本。复制前后原文件和副本的大小／SHA-256 一致，共享配置未改；未在原始目录覆盖补丁。
- 只读反汇编与重定位表核对了联网适配的 11 个函数，以及工厂、任务和后端槽位。两个版本分别由完整程序 SHA 绑定到 `ketsui_version.h` 的明确位置，不采用通用地址差值；原状态字段与 PSB／录像结构仍按既有合同处理，实际游戏行为另验。
- 本体与更新版各自通过完整检测 CLI，分别报告 `base / 01.00 / base` 与 `update-102 / 01.02 / merged-patch`。两者的三个 ELF 准备结果逐字节匹配 Python 实现；音乐保存、录像批量提交和局末成绩资源修复也逐字节一致，正确装载本体／合并内容并校验原资源覆盖范围。冷准备／热缓存返回一致，全部原始输入复核未变，未进入原始游戏代码。
- 389 项公共原生检查、45 项原生整局适配检查（含双版本安装、未知／错配版本拒绝）、73 项 PS4 机制回归、107 项资源检查及 118 项隐藏生产启动器检查通过。源码共 479 项，1 项文件链接权限跳过，其余通过；源码边界审计无游戏数据，配套 PS4 六对和启动器两对 PE／PDB 收据通过。
- 初次准备检查只因 Windows 路径排序与文本排序不同而失败，文件身份无差异；改为跨平台一致的文本排序并补用例。失败日志保留，后续 r2 两版本结果通过。内部组包现在按实际识别的版本选择完整清单，尚未生成真实 v0.4 包。

记录为 `.local/v04-ketsui-single-root-{build-r2,source-r2,launcher-r1,round-r1}.log`、`.local/v04-ketsui-{base,updated}-prepare-r2/result.json`、`.local/v04-ketsui-native-checks-r1/result.json`、`.local/v04-ketsui-resource-checks-r1/result.json` 和 `.local/analysis/ketsui-v04-base-contract-r2.json`。

本体和更新版的实际进关、保存读回和社区／录像代表性短回归已由上节补齐。
六个预建目录与模板复制已在后续章节完成，最终 v0.4 发行包尚未生成；冻结的 v0.3.2 未回改。

## v0.4 游戏数据层级提示

声明根入口 `default.xex`／`eboot.bin` 的作品，缺少根入口但在两层包装目录内找到同名入口时，报告 `nested_data` 和实际候选目录。候选只是层级提示，未通过版本／资源校验；不改配置或原始文件，不跟随 junction／链接，也不掩盖根目录已存在的未知版本程序。仅有放置说明的目录仍显示未安装。

- 公共／启动两组 CTest 通过，其中公共用例为 380 项；实际检测 CLI 的 11 项测试覆盖空模板、360／PS4 多层目录、显式改选后的版本校验和真实 junction 排除。
- 118 项隐藏生产启动器检查、473 项源码测试（1 项文件链接权限跳过）及源码边界审计通过。日志为 `.local/v04-data-root-build-r2.log`、`.local/v04-data-root-launcher-r1.log` 和 `.local/v04-data-root-source-r1.log`。
- 本批不移动 ROM、不自动导入，也未接入绊地狱本体／合并更新版本。其旧清单仍待后续替换，不能以通用层级提示作为该适配的验收。

## v0.4 两份崩溃的增量处理

### SDOJ 迟解析机制修正

新增的确定性用例按“完成批次 2 → 帧初选择批次 2 但尚未解析 → 其他批次解析 → 批次 2 真实解析 → 原后段唤醒”执行。旧模型在第 5 项断言失败：真实解析后本帧选择仍缺失。修正后只由该冻结来源的真实 Capture 补回统计，已解析的其他批次和后续新批次都不能替换它；下个 video frame 不重复消费。已有资源等待、暂停、复用和计数回绕检查保留。1V 在来源未就绪时返回原绘图／后段驱动，不生成提前输入与驱动收据；没有猜测零统计、删除异常检查或修改原减速公式。

- 两个模块重新构建，配套 56 对 PE／PDB 验证并整理通过。
- frame-delay 机制共 1122 项通过，源码共 470 项通过（1 项权限跳过），当前公共／360 CTest 27／27 通过。记录为 `.local/v04-sdoj-late-metrics-build-r1.log`、`.local/v04-sdoj-late-metrics-source-r1.log` 和 `out/xbox360/.local-v04-late-metrics-native-r1.xml`。
- 这证明修复了一个可复现的机制缺口，尚不证明它就是朋友第 3187 次更新的唯一根因。保留该场景的实际回归、普通输入和 2V／1V 行为对照，默认仍是 3V；没有重启游戏、修改原始数据或生成新发行包。

### G2 遗漏回调与首批定位

G2 的 `dat163.xex` 新增已核实的 `0x880EEB00`／12 字节声明，自动生成原生转发函数并注册，未修改生成正文。目的函数 `0x880EE9E8` 和相邻 `0x880EEB10`／`0x880EEB20` 原已存在。`Test-G2Callback.ps1` 隔离编译这次实际生成的入口，以自制目的函数检查四种 64 位参数、完整上下文和返回传播，12 项检查通过；不运行游戏循环或设备。新 dat163 DLL SHA-256 为 `1156b4264d04779d43d9abffadecccf92a8b7821632b8919cab5f9fe47125a29`。

- 所有八个 G2 程序重新生成，配套 360 构建与 56 对 PE／PDB 验证通过，三个游戏运行文件已整理到 runtime；不改变旧发行 ZIP。
- 生成清单与独立 fixture 的源码用例检查模块专属 include、注册去重、输出不进入 ROM／源码目录，以及缺少或重复生成函数时拒绝测试。静态 census 检查保留在 `.local/analysis/g2-callback-inventory-v04-r1.json`；dat163 没有其他有引用的短入口候选，其他模块候选不据此自动注册。
- 实际运行 `run-20261021T142321809Z-94912` 正常到 G2 标题。computer-use 已重新发现窗口，但输入返回 `failed to activate captured window`，截图仍显示前台任务管理器；未进入 Novice，不能将标题启动算作模式回归。仅结束本次新开的 PID 94912，没有改动任务管理器或旧启动器；强制结束不算正常游戏退出。
- SDOJ 缺统计异常现在输出 `SDOJ_METRICS_MISSING` 和八项 `SDOJ_METRICS_ENTRY`，包含真实缺失来源、已消费来源、当前解析来源、video／native 计数和缓冲状态，只在错误时写日志。原选择算法与缺失检查保持不变，不猜测统计、不改减速公式。两模块编译通过；新增状态／重载机制检查通过，所有 frame-delay 机制检查共 1112 项。根因和实际修复仍待完成。
- 最终 469 项源码检查通过（1 项文件链接权限跳过），源码边界审计通过；当前构建的 27 项公共／360 CTest 全部通过。记录为 `.local/v04-crash-source-r1.log` 和 `out/xbox360/.local-v04-crash-native-r1.xml`。机制通过不替代上面的实际模式待办。

默认保持 3V。普通输入和两处受影响场景尚未新增通过；不扩大旧模式或联网回归。

另一次有界 SDOJ 定位使用 `.local/v04-sdoj-metrics-r1` 的独立离线配置和复制 profile，进程 97224／48620 均已通过诊断 quit 正常终止。第一轮实际进入 Xbox360／模块 2，不能验证模块 1 的统计缺口；窗口模式重开仍无法激活，前台窗口也遮挡模式列表。未复现目标异常，不将此项计为修复通过，不再盲试菜单。

## v0.4 默认恢复 3V

根据新增 1V 减速统计缺失崩溃反馈，`Settings` 缺省与新配置模板恢复为 3V；显式 1／2／3 继续保留。配置迁移不改旧文件正文，缺少该键时解析为 3；中英文 README、v0.4 玩家说明和开发文档同步更新，旧发行记录不改。

- `Build.ps1 -SkipLauncherStage -Jobs 2` 成功构建公共组件、检查器与启动器，核对两对 PE／PDB。没有替换仍在使用的根启动器，也没有重新构建游戏运行组件或生成发行包。
- `Test.ps1 -Scope Source`：465 项通过，1 项 Windows 文件链接权限跳过，源码边界审计通过。`ctest --test-dir out/common -R '^(common|launch)$'`：2／2 通过，包含缺省、新配置、旧配置缺键和显式三档保留。
- `Test-Launcher.ps1 -Jobs 2`：118 项隐藏窗口集成检查通过，含三语言独立进程配置读回为 3V。未打开或操作实际游戏窗口。
- 本机原配置备份为 `.local/config-before-default-3v-8e1d663393e140e6920fcb61bd0fd0e4.ini`；只新增一行 `frame_delay = 3`，去掉该行后与原文本完全相等，供旧运行组件下次启动明确使用 3V。

默认值修改不修复 1V／2V 的统计跟踪缺口，也不关闭 G2 遗漏回调的问题。两项仍在 v0.4 优先修复清单，已有 v0.3.1／v0.3.2 ZIP、标签和 OSS 对象保持原样。

## v0.3.2 绊地狱 CUSTOM 增补

已上传并公开核验：[v0.3.2 覆盖包](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip)、[校验文件](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip.sha256)。公网 HEAD=200，大小、SHA-256／源码／修订元数据与本地验收身份一致，公开校验文件匹配；32 字节 ZIP 文件头范围请求为 206。复用既有 ZIP，未重建、重解压或覆盖旧 OSS 对象，凭据在 `.local/v032-custom-oss/receipt.json`，群消息由用户自行发送。

当前更新包的运行组件固定为 `a8a75fb9d86a11003c8fa9f3e1fb38521bcb2a07`（标签 `v0.3.2-custom`），包含下述大往生 AC-CHALLENGE 修复。绊地狱按原生定义开放 CUSTOM 综合／Type-A／Type-B 的 60／61／62 号榜，上传限制为 100／10／10，录像保留原 mode=3 和完整快照／按键数据。不按参数摘要拆原生榜，也不改变续关、双人和练习等资格判断；旧显式摘要分区保留，不合并成绩。

- 465 项源码测试通过（Windows 文件链接权限跳过 1 项）。29 项原生录像校验、36 项原生整局采集、37 项 journal 测试通过，覆盖三榜与模式／机体匹配、原字节保持、存盘重开及提交引用。九项本机 HTTP 检查通过，包括三榜各自的原生 ABI 成绩／个人榜／录像上传下载，CUSTOM 整局 worker 的两榜成绩、原附件证明与完成归档，以及原有两作普通收发和大往生挑战榜回归。均为自造不可游玩记录，不执行游戏。
- 通用 PS4 原生套件首次为 72／73，event 信号量的 `repeated_create_delete_does_not_reuse_stale_identity` 清理断言失败；同一未改二进制的独立信号量复查通过。首份失败和复查都保留，不写成首轮全通过，不在本任务改动同步实现。记录分别在 `.local/v032-custom-native-checks/` 和 `.local/v032-custom-semaphore-recheck.txt`。
- 绊地狱服务单独更新到 `/opt/ketsui-live/releases/a8a75fb`。目标 Python 3.10.12 上 44 项服务／profile／备份／部署测试通过，已有数据库副本兼容性检查不改变原有行。切换前另存一致性快照；5 位玩家、20 条成绩和 18 份录像保留原字节。旧版和合集入口的三个 CUSTOM 榜均读取为 200／空榜，普通榜仍有原记录；大往生服务版本和四款同伴 PID、nginx、systemd 单元均未变。未向正式服务提交自造分数。
- 新 ZIP 为 `releases/v0.3.2-custom-a8a75fb/CAVE-PC-Collection-v0.3.2-update.zip`，72,777,904 字节，SHA-256 `9bb302546e3f42c9050a4180e1cbee0d3b7784a272979fa71825835173b7ca99`。只实际覆盖解压一次，129 个载荷、12 个配置／用户／数据哨兵和安装后 CUSTOM 录像／原生 profile 检查通过。继续覆盖 v0.3／v0.3.1，也可覆盖前一份 v0.3.2，不含游戏数据、配置、用户目录、源码或符号。旧包和 `v0.3.2` 标签不改。

证据为 `.local/v032-custom-{source-tests,native-round,journal,replay-checks,community-checks,deploy-result,overlay}.txt`、对应检查目录及 `.local/v032-custom-overlay-a8a75fb/accepted.json`。沿用已知桌面激活阻挡，本轮未重复 Taskmgr／焦点恢复，也没有新游戏内菜单或实际录像回放目视验收；未上传 OSS 或发送 QQ。

## v0.3.2 首批 AC-CHALLENGE 排行榜修复

运行组件固定为 `a865369bfdfd880789d7ca1de00f4f0a121fe77d`。补接大往生 99 号榜，按原生奖牌累计积分登记；原生定义已设 `uploadable=0`，无需改资源脚本或生成录像。普通榜单、CUSTOM 限制、录像协议和最大往生默认 1V 不变。

- 440 项源码测试通过（Windows 文件链接权限跳过 1 项），源码边界审计通过。73 项 PS4 无 ROM 检查通过，其中原生 profile 14 个断言覆盖挑战榜、精确积分、上限与无录像状态。五项独立 Windows HTTP 检查通过：两作的普通成绩／录像回归，以及挑战榜空榜、零积分、最高分更新、等分／低分、个人榜和翻页。SQLite 单测另覆盖 12 人分页与原有普通成绩保留。记录为 `.local/v032-{source-tests,ps4-checks,community-checks}.txt` 和对应检查目录。
- Ubuntu Python 3.10.12 上 profile、备份和部署测试通过。大往生服务单独更新至 `/opt/cave-community/releases/a865369`，先保存一致性数据库快照；已有一条成绩及其录像保持原字节，其他四款 PID 和 nginx 片段哈希未变。公开 99 号榜由 400 恢复为 200／空榜，普通榜仍有原有记录；未向正式榜单写入测试积分。部署凭据在 `.local/v032-{deploy-prepared,deployed}.json`。
- `releases/v0.3.2-update-a865369/CAVE-PC-Collection-v0.3.2-update.zip` 为 72,773,377 字节，SHA-256 `e7349d8c50fa1072f1fe940bc85953a8aa03b141b0c095ba12885554e0f38e7f`。一次真实覆盖 v0.3 程序副本后，129 个载荷哈希、12 个配置／用户／数据哨兵及实际安装的原生 profile 自测通过；不含 ROM、源码、配置、用户目录或符号。记录为 `.local/v032-overlay-a865369/accepted.json`，不重复解压同一包。

computer-use 初始化和窗口发现正常，但启动器激活及刷新后重试均返回 `failed to activate captured window`。本轮未新增游戏内 AC-CHALLENGE 菜单目视验收，也未恢复旧的 Taskmgr／输入测试；上述通过范围为源码、原生 ABI／HTTP 和发行覆盖。更新包尚未上传 OSS 或发送群消息。

## v0.3.1 字标图标修订验收

启动器 EXE、标题栏与任务栏使用完整 EVAC＋PC LABEL 主字标，不单独采用星形；左上角显示含说明字的完整透明 Logo。图像由内置图像工具按用户附件去白底，图标去掉微小说明字，保留原文字和配色。资源、处理要求及 SHA-256 在 `launcher/branding/` 和 `launcher/branding.json` 中维护。

- 运行组件固定为 `f26c9e73a2f89cb6bca8060a3cf458100b12f942`（标签 `v0.3.1-logo`），五款完成配套构建、符号和收据核对。仅改启动器及资源生成／审计支持，沿用既有默认 1V 游玩验收，没有重开游戏测试。
- 更新包为 `releases/v0.3.1-logo-f26c9e7/CAVE-PC-Collection-v0.3.1-update.zip`，72,764,656 字节，SHA-256 为 `ae987c08b26e3b3e2ed082b5e199edf58b183c63477e61a45aa5f633f9137ac4`。129 个 Update 载荷文件，标识嵌入程序；不含 ROM、配置、用户目录、散装图片、源码或符号。
- 437 项源码回归通过（文件链接权限跳过 1 项），源码边界审计核对 6 张横幅及 3 项品牌资源；118 项隐藏生产窗口检查通过，包括 16／20／24／32／40／48／64／128／256 尺寸图标、嵌入 Logo 字节和透明通道、720／1080 宽度布局及既有语言／配置功能。切图前景主要为 alpha 253／254，检查接受正常近不透明像素；早期要求大量恰为 255 的过严断言失败记录保留。
- 最终 ZIP 只真实覆盖解压一次，129 个载荷哈希及 11 个用户／数据哨兵通过；9 项冷启动配置检查确认迁移字节不变、SDOJ 缺省为 1。只读映射覆盖后实际 EXE 的 PE 资源：Logo 与源码 PNG 完全相同，全部 9 个图标帧与字标 ICO 一致，没有执行游戏。

记录为 `.local/v031-branding-{source-tests,sdk-build,build,launcher-r2,package,overlay,overlay-pe}.txt`、`.local/v031-branding-overlay-f26c9e7/accepted.json`；正式程序的原生视觉预览为 `.local/v031-branding-preview/final-zh.png`，预览安装状态为示例。旧 ZIP、标签及 OSS 对象保留。

为用户准备群公告时，字标修订包已上传至新前缀：[最新版 v0.3.1 覆盖包](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.1-logo/CAVE-PC-Collection-v0.3.1-update.zip)、[校验文件](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.1-logo/CAVE-PC-Collection-v0.3.1-update.zip.sha256)。公网 HEAD=200，大小、SHA-256／源码／档位元数据和公开校验文件一致，32 字节 ZIP 范围请求为 206；未全量重下载或重新解压。交付凭据在 `.local/v031-logo-oss/receipt.json`，群消息由用户自行发送。

## v0.3.1 默认 1V 修订验收

按用户要求，修订版将内置缺省与新配置模板设为 1V；旧配置没有该键时自动使用 1，显式 2／3 不变。只改变档位默认选择，不改交接、超时降档、原作减速或输入映射。运行组件固定为 `38717a1f2d7e3479415ff13f1d753a047cd0aaed`（标签 `v0.3.1-1v`），旧 `v0.3.1` 标签和首批 ZIP 保留。

- 新 ZIP 为 `releases/20261021T091643Z-a185ee75/CAVE-PC-Collection-v0.3.1-update.zip`，72,159,381 字节，SHA-256 为 `2330d6078cc0327b9a480367e8e89c41cf21ac3acd2f68a0d7c7a38f30930ff9`。沿用平铺 Update 规则，129 个载荷文件，不含游戏本体、配置、用户目录、源码或符号。
- 433 项源码测试通过（文件链接权限跳过 1 项），源码边界审计通过；公共配置／启动、SDOJ 交接／逻辑观察器／地图初始化 5 项原生 CTest、95 项隐藏生产窗口启动器集成检查通过。新增用例验证内置缺省、模板、新建文件、旧版缺键及显式 1／2／3 原样迁移。五款配套构建及 SDK、PS4、360 和启动器的二进制／符号收据通过；不重复无相关行为变更的 PS4 实战或全套机制检查。
- 最终 ZIP 在独立 v0.3 程序副本上真实解压覆盖一次，载荷哈希及 11 个用户／数据文件哨兵通过。生产 App 的独立进程冷启动 9 项检查通过，旧配置逐字节迁移，实际解析出的 SDOJ 档位为 1；这是配置读回，不冒充新游戏进关或实体键盘验收。

记录在 `.local/v031-1v-{source-tests,sdk-build,build,native-tests,launcher-tests,package,overlay}.txt` 与 `.local/v031-1v-overlay-38717a1/accepted.json`。沿用既有 1V 游玩／完整录像／节奏结果，普通键盘和失焦恢复仍留在 v0.4。对应源码由 SOURCE.txt 指向本次维护提交，不将首批源码附件当成本次修订的对应附件。

该默认 1V 更新包已按用户要求上传 OSS：[程序覆盖包](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/CAVE-PC-Collection-v0.3.1-update.zip)、[SHA-256 校验文件](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/CAVE-PC-Collection-v0.3.1-update.zip.sha256)。上传前核对本地验收身份；公网 HEAD 为 200，大小及 SHA-256／源码／档位元数据匹配，公开校验文件一致，32 字节范围下载为 206 且 ZIP 文件头正确。未重新下载完整包或解压；交付凭据在 `.local/v031-1v-oss/receipt.json`，此次未发 QQ 通知。

## 首批 v0.3.1 覆盖更新验收

更新包固定为提交 `d1b32cdb9d5f74ba7fcdf7d45505401ea83fcef6`，五款运行组件、公共 SDK、启动器和 PS4 后端完成配套构建及收据核对。开发根启动器仍在运行，因此通过 `Build.ps1 -SkipLauncherStage` 保留旧窗口，Update 包取认证的 out/ 新程序，不从运行中的旧根 EXE 取件。

- `CAVE-PC-Collection-v0.3.1-update.zip` 为平铺根目录，72,159,515 字节，SHA-256 为 `1fa5e982021435445559ec061d37965a8d195e6059138806e818051be2593fc1`。129 个载荷文件包括程序、运行库、许可证和说明；不含 ROM、配置、user-data、源码、符号、准备镜像、缓存或日志。
- 本批完整源码回归 433 项（Windows 文件链接权限跳过 1 项），脚本收尾另通过 21 项打包用例（同一权限跳过 1 项）和 4 项源码附件用例。27 项去重的公共／360 原生 CTest、73 项 PS4 原生检查、5 项 PS4 GPU CTest、95 项启动器隐藏生产窗口集成检查通过。源码边界审计通过。
- 最终 ZIP 只实际解压验收一次：以保留的 `v0.3`／`b8807ca` Public 程序目录建立独立副本，直接覆盖。逐项核对新增载荷的大小和哈希；五款程序均无缺失运行组件。该副本不含真实 ROM，数据检查如期报告缺少数据，不据此声称五款实际启动通过。
- 覆盖前后及冷启动后，11 个独立的存档、录像、游戏数据、准备文件、缓存、日志和源码哨兵全部保留。8 项隐藏生产 App 冷启动检查通过：旧根配置移动至 `user-data/config.ini` 后逐字节一致，SHA-256 为 `6d9dfef46542b31cb962949be1a481aad48311d29a69a7a921cab6c50f4919a6`，语言和昵称读回正确。

记录在 `.local/v031-{build-final,xbox-native-tests,ps4-checks,launcher-tests,package,overlay}.txt` 与 `.local/v031-overlay-d1b32cd/accepted.json`。此次复用既有可玩和 Frame Delay 录像证据，不重启完整实战或全模式测试；普通实体键盘／失焦短测及默认 1V 调整仍待 v0.4。冻结包不因后续文档或交付记录重新打包。

## v0.4 启动器与配置开发检查

启动器批次实现语言文字选择、配置迁移、新最大往生横幅和底部工具行，当时未修改 Frame Delay；后续延迟开发记录在下一节。这批功能现已收录上节的 v0.3.1 覆盖更新。

- 416 项源码回归完成，1 项因 Windows 文件链接权限跳过；源码边界审计通过。23 项去掉重复源码回归的 Xbox 360／公共 CTest 全部通过，包含 344 个公共原生检查和 118 个启动、配置、进程与数据复制检查。
- 五款游戏完成配套重编译与 runtime 整理；360 宿主、PS4 的六对程序／PDB、启动器的两对程序／PDB 收据均通过。PS4 配置自测 16 项和输入桥接自测通过；真实新位置 INI 的只读配置检查保留全屏、昵称、键盘与 endpoint。
- `ConfigPath` 的只读查找、新位置优先、旧文件原样迁移、无效新配置拒绝回退、BOM／注释／换行／未知项保留、相对数据目录不随 INI 移动均有针对性用例。实际开发根第一次启动后，旧根配置消失，`user-data/config.ini` 与事前副本 SHA-256 相同；原四款已配置的数据目录读回一致，大往生未配置时仍显示缺少数据。
- 三语言原生预览已检查，真实启动器可显示文字语言栏、底部工具行和新横幅。图片源与 SHA-256 在 `launcher/artwork.json`，构建通过显式资源依赖更新嵌入图片。
- 新增 `scripts/Test-Launcher.ps1`，95 项启动器集成检查通过。它在独立临时目录创建隐藏的生产窗口，直接调用生产窗口的鼠标／键盘消息处理，不注入桌面输入。720／1080 两种宽度下三语言切换、INI 实际写盘、原生控件更新、未保存昵称保留、忙碌拦截、保存及刷新均通过；中、英、日分别在新进程中读回语言、昵称及全屏。底部工具布局无重叠，嵌入的 SDOJ 图片与源文件逐字节一致。
- 配置／日志点击经过生产消息处理，到 `ShellExecuteW` 边界截获请求：目标分别为真实存在的 `user-data/config.ini` 与 `logs/`，使用正常的 `open` 调用；测试不启动外部编辑器。桌面 computer-use 的激活、点击和 Raise 失败记录保留，最终使用上述窗口事件集成与冷启动测试完成功能验收。

本地记录在 `.local/v04-source-tests-r2.txt`、`.local/v04-native-tests.txt`、`.local/v04-launcher-integration-r2.txt`、`.local/v04-build-xbox360.txt`、`.local/v04-build-ps4.txt`、`.local/v04-validation/` 与 `.local/v04-artwork/`。新布局的组包用例只使用自造文件，未生成真实游戏发行包。

## 最大往生 Frame Delay 开发检查

两模块进关已通过；修正统计年龄后，Arcade HD 的同构建 2V／1V 完整录像逐逻辑帧一致。原版自身未初始化的地图边缘已修正；修正版模块 1 的三档首次输出链和关闭细粒度延迟追踪后的节奏／音频队列检查均已完成。首批 v0.3.1 默认 3V；用户随后明确要求默认 1V，缺省与模板同步调整，保留已有显式 2V／3V。普通键盘／失焦短测仍按用户要求暂缓至 v0.4，不因默认切换记作通过。

### 地图初始化修正版的输出链

冻结程序为 `.local/sdoj-frame-delay-map-init-r1`，13 个运行文件与 original 的 run.json 哈希核对一致；正常模块 DLL 为 `5b2cf417…b2f2903`。三次运行均 cold-load 同一 `REPLAY_XB02.DAT`（`6b724e45…3e7c`），进入第一关、自然返回列表并经正常关窗退出，未改帧率、优先级或最终等待。

| 策略／PID | 正常速度首次输出 | 中位／p95／最大（ms） | copy→source／输出周期 | 减速段首次输出 |
| --- | ---: | --- | --- | --- |
| 3V／94360 | 2125／2125 | 49.957／50.345／51.585 | 2／3 | 129／129，输出跨 4 周期 |
| 2V／26132 | 2125／2125 | 33.266／33.657／34.002 | 1／2 | 129／129，输出跨 3 周期 |
| 1V／97596 | 2125／2125 | 16.847／17.293／17.690 | 0／1 | 129／129，输出跨 1 周期；后续原生减速等待仍保留 |

范围由真实 `core_state=7/play_state=4` 界定，每个完成结果的第一次输出单独关联，不以重复画面的年龄计算延迟。这是输入消费至 Present CPU 返回，非设备到屏幕出光。2V／1V 的细粒度追踪在返回后达到预算上限；表内全部正常及减速结果都有完整链。2V 另有一条拒绝观测，第一关范围仍无缺失。

1V 与 3V 的 1–5505 逐项比较：命令字节／SHA、原生状态、端口和 depth 全同，原生结束请求同在 5505，depth=2 均为 131。`slow_stat0` 有四处差异（2877／4236／4703／5100），原版各读取上一份统计，low 选取模型规定的当前原始年龄；相邻真实统计记录支持这一区别，四处均未改变 depth。两份实际 parsed 统计在初始化 18／19 的差异另保留，第一关 parsed 统计相同。1V 全程无超时、排空或所有权拒绝，最大完成等待 1064 µs；停机摘要的 effective_policy=3 是 Stop 关闭新请求后的返回值，不是运行时降档。

2V 在原生状态 4→5 比另外两次早一次（16 对 17），后续状态与结束请求整体早一次。原生 `88080088` 的状态 4 在 `880803C8–88080424` 查询两个就绪条件后才转 5；本轮不修改其时机，也不把初始化轮次当成录像帧。全窗口比较和按进入状态 7 锚点的诊断比较仍有命令差异，均保留为未通过，不能仅移位或忽略哈希完成验收。下面两次常态检查补齐了两种初始化类别的同类比较，没有按结果删除或重排更新。

报告：`.local/analysis/sdoj-map-init-r1-{original,balanced,low}-{events,output,stage}.json`、`original-low-exact.json`、`original-low-metrics.json`，以及保留的 `original-balanced-exact.json`、`balanced-low-exact.json`、`original-balanced-native-entry.json`。这些结果补齐模块 1 输出证据，实体键盘与最终默认值仍单独验收。

### 常态节奏、音频队列与完整录像复核

同一冻结程序的 `run-20261021T065344361Z-98276`（2V）和 `run-20261021T065727724Z-98776`（1V）关闭 latency、output、audio 和命令内容细粒度追踪，仅保留 `sdoj_logic_trace` 与常态汇总。两次均冷读同一录像、进入第一关、自然返回列表并正常关闭。五次修正版运行的 13 个程序／符号文件完全同源，实际运行文件哈希和六份录像副本亦重新核对一致。

| 第一关内完整汇总窗口 | 2V／98276 | 1V／98776 |
| --- | ---: | ---: |
| 呈现窗口／间隔数 | 19／2290 | 18／2170 |
| 累计时间 | 38.167 s | 36.166 s |
| 平均／最大间隔 | 16.667／17.430 ms | 16.667／17.530 ms |
| 超过 20／33／50 ms | 0／0／0 | 0／0／0 |
| 音频窗口／驱动数 | 117／3 | 115／3 |
| 补静音块／队列峰值块数 | 0／8 | 0／8 |
| 队列时长峰值／消费年龄最大值 | 42.7／52.0 ms | 42.7／52.0 ms |
| 提交／回调间隔最大值 | 12.4／12.2 ms | 12.4／12.2 ms |

只取完全落在原生 `core_state=7/play_state=4` 内且两端留 20 ms 余量的汇总窗口；窗口数不同不代表录像长度不同。各窗口 p99 的最大直方图上界均为 17.5 ms，不当作全部间隔的精确 p99。音频队列没有新增积压或断供证据，实际声学输出时间不由队列日志证明。

2V／98276 的初始化在逻辑 17 完成、结束请求为 5505；它与 1V／97596 的全部 5505 次更新完全相同，包括命令、状态、减速操作数及 depth。1V／98776 在逻辑 16 完成初始化、结束请求为 5504；它与 2V／26132 的全部 5504 次更新也完全相同。两组均有 131 次 depth=2，比较日志哈希已复核。这证明两种已观测初始化类别在两档间均有完整一致结果，不把跨类别的失败比较改成通过，也不把差异归因于某一档或某个追踪开关。

1V／98776 全程实际策略为 1，完成等待最大 999 µs，超时、排空与所有权拒绝均为 0；唯一一次 `low_budget_skips` 在第一关之前、发布计数为 601 的汇总中已出现，之后至退出未增加。它是唤醒前预算不足时走正常路径，不是写入中超时；不报告为“零回退”。提前输入／驱动的后段记录均为 5637，退出前全部结清。两次停机都无崩溃文件或遗留游戏进程。

报告为 `.local/analysis/sdoj-map-init-r1-normal-pacing-audio.json`、`normal-balanced-low-exact.json`、`balanced-normal-low-exact.json` 与两份 `normal-*-events.json`（后几个文件同用 `sdoj-map-init-r1-` 前缀）。这批关闭的是细粒度延迟／音频追踪，不是全部逻辑观察器。最终普通键盘与失焦验收无需再次运行这套完整录像比较。

### 当前固定录像对照

`RenderMetrics` 依据完成记录的原生计数基线与 depth，保留原管线本应读到的绘图统计；只绑定实际被解析的完整命令，不把每次 wrapper 调用当成新批次，也不使用固定 N−2 推测。资源等待可以增加视频帧而不增加逻辑更新，原生减速公式保持不变。

同一冻结 `content-r1` 程序与同源独立存档、关闭音频 trace 的结果：

| 对照 | 核对窗口 | 结果 |
| --- | --- | --- |
| balanced 86068／low 54144 | 1–5504，原生结束请求为边界 | 命令、状态、减速操作数与 depth 全部一致；131 次 depth=2；均自然返回并正常关闭 |
| original 82044／balanced 86068 | 1–5014，original 提前结束 | 命令、状态、depth 一致；3 次操作数差异是原版先读到上一份统计，未改变 depth；不称完整 original 回放 |
| original 82044／original 87968 | 1–5014，同策略 A/A | 453 次命令不同、157 次 depth 不同；后者完整自然返回，有 212 次 depth=2 |

low 54144 全程实际策略 1，timeouts／draining／ownership skips 为 0，最大完成等待 1082 µs。两次运行中完全位于第一关状态内的常态呈现窗口分别含 2293／2296 个间隔，均值约 16.667 ms，最大 17.940／18.156 ms，>20 ms 为 0；这是逻辑摘要开启时的常态窗口，不是关闭所有诊断后的最终节奏验收，亦非全局精确 p99。

A/A 的第 2845 次更新，641 与 677 条命令之间仅多了 36 条 64×64 背景块。只读探针定位到 `88032CA8` 的四层背景地图第 63 列。`tilemap-r1` 的 original-a 96968／original-b 93144 均完整回放自然返回、正常退出，131 次 depth=2；选定帧的四层该列均为零，完整地图样本可重建 90 条真实背景命令，逐字匹配完成缓冲前缀。

原生代码分配四块 32768 字节的地图后没有初始化，滚动更新写 12 列×10 行，绘制窗口另外读取前一列。对旧 A/A 的命令反推该列的有效位，仅替换四层各 9 个边缘单元，即可重建全部 126 条背景命令及其顺序；余下 551 条命令完全相同。报告 `sdoj-background-border-r1.json` 区分这份反推与实际内存捕获，未推定纹理命令不使用的位。

`compat/tilemap.cpp` 在 `88051C34`、新分配返回后和原指针写入前，仅清零该块完整地图。3V／2V／1V 共用，不改已有地图、滚动更新、纹理命令或减速公式。83 项源码检查及交接／观察器／初始化三项 CTest 通过；初始化用例检查带脏值与两侧哨兵的完整分配、错误尺寸拒绝和 64 种回绕起点。修正版的实际回放、输出链与常态检查已完成，见上两节；默认值仍待普通输入验收后调整。

本批 82 项 SDOJ 源码检查、交接和逻辑观察器两项 CTest、17 对 PE/PDB 与 13 个运行文件的构建收据通过。报告位于 `.local/analysis/sdoj-content-r1-{balanced-low-exact,original-balanced-prefix,original-balanced-metrics,original-aa-exact,original-aa-command,balanced-low-pacing}.json` 与 `sdoj-tilemap-r1-original-a.json`。早期失败窗口保留如下，不重标为通过。

### 早期录像对照与减速差异

同一 replay-r1 程序的旧 Xbox360／Shot／9786 分录像，在 original 与 balanced 下均自然返回，4535 条完整记录逐项相同；报告为 `.local/analysis/sdoj-replay-3v-2v-r1.json`。同期 low 只有 4534 条，前缀相同不记作完整通过，因而补充原生结束状态观测。

replay-r2 的 original `run-20261021T045659555Z-89024` 与 low `run-20261021T050359219Z-91880` 使用相同程序，两次均冷启动播放旧 Xbox360 录像，再切至 Arcade HD 播放先前保存的新槽位 2，各自自然返回后正常关闭。原始 run.json 保留程序身份；新录像 `REPLAY_XB02.DAT` 为 79332 字节。

| 对照项 | Xbox360 旧录像 | Arcade HD 新录像 |
| --- | ---: | ---: |
| 首次原生结束请求所在更新 | 两者均 4533 | 两者均 5504 |
| 命令 SHA-256、尺寸、端口和状态差异 | 0 | 0 |
| 原生等待深度差异 | 0 | **141** |
| depth=2 次数（original／low） | 0／0 | **131／102** |
| 声音请求／分发 | 各 141 项，完整顺序相同 | 各 31 项，完整顺序相同 |

退出请求出现后 core_state=10，原生分支不再调用玩法主体，只执行结束收尾。总观察次数分别为 4536／4535、5507／5506；这些额外收尾次数与真实玩法更新分开报告，不用总次数差直接推断漏跑玩法，也不忽略结束请求之前的减速差异。两模块在 low 的最终摘要均为 effective_policy=1、timeouts=0、low_ownership_skips=0；最大新增等待为 1133／1012 µs。

声音请求与分发来自完整 phase=play_request／dispatch 序列，没有耗尽预算。original 的队列观察器有 try-lock 竞争缺项，不能把该观察器缺项叫作丢声音，也不能据此比较完整入队时间；扬声器时刻未测。精确结果和原日志哈希在 `.local/analysis/sdoj-replay-r2-pair-audit.json` 及同目录两份 `sdoj-replay-r2-3v-1v-module*-through-end.json`。

减速差异集中在 CA022100。静态路径为 renderer +544/+548 → `888791E0/E4` → `88123B98` 的原生减速公式；提前绘图可能改变所读统计所属帧。新增默认关闭的 `SDOJ_METRICS` 记录实际已绘制命令批次和原生公式操作数，下一次只对照这份新录像。尚未修改减速公式或宣称原因已由动态对照证明。此发现也要求核对 balanced 的同模块，不能从其 Xbox360 结果推广到全部减速路径。

### 较早的提前交接与输入检查

- SDK 与 SDOJ／两模块配套编译通过；r2 的 16 对 PE／PDB 与整理后的 13 个运行文件完成收据及符号身份校验。根目录启动器仍开着，阻止普通 Build.ps1 替换 EXE；本次通过同一构建环境完成游戏收据和整理，不强行关闭其他窗口或更改构建校验。
- 421 项源码测试通过，1 项链接权限用例跳过；9 组 SDOJ／公共 CTest 通过。新增交接模型实际执行 88 项检查，涵盖真正完成确认、读者保护、交换完整性、减速倒计时、原生基线、迟到结算、跨暂停、模块代与缓冲复用。共享配置检查为 357 项中的一部分，不将纯模型用例称为实战验证。
- r1 独立训练运行 `run-20261021T021601550Z-81060` 在暂停后恢复异常退出。已修正一次性交换记录错误地要求相同宿主帧号的问题；原生计数与缓冲序号才是回调结算身份。该失败运行保留，不作为稳定性通过。
- r2 `run-20261021T022449322Z-85720` 实际从 Xbox360 菜单冷读旧 9786 分录像，播放到对应分数、暂停／恢复，最后自然回到录像列表。随后切至 Arcade HD，实际选人、进入第一关、原生结算，新录像保存到空槽 2；`REPLAY_XB02.DAT` 为 79332 字节。后者的冷启动播放和逐逻辑帧对照尚待完成。
- 同一 r2 进程完成 CA022110 → CA022100 → CA022110 切换，第二次 CA022110 的加载代为 2。CA022100 卸载摘要为 21730 次提前交换、21729 次已结清后段交换、21730 次完成确认、101 次减速倒计时门控，未报元数据／所有权错误。多出的一次为退出后不再调用的原生回调记录，不能当成重复交换。Xbox360 首代有 4536 次提前交换；暂停恢复未再崩溃。
- r2 来源报告 `.local/analysis/sdoj-frame-delay-r2-output.json` 有 4536 份首次结果输出和 2690 次同结果重复呈现。首次输入消费至对应 Present 返回中位 33.134 ms、p95 33.584 ms；首次 copy→source 有 4532 条跨 1 epoch、另有 1／1／2 条跨 2／3／4 epoch。四个 >40 ms 样本包含进入／结束及一条 copy 后约 14.35 ms 才开始写命令的停顿，最大 100.364 ms；尚未通过同录像 original 对照完成归因，不称全部帧已稳定两帧。
- trace 共到 131072 条上限，未丢前缀、observer_rejected=0；后续 Arcade HD 与重载只保留常态摘要及实际画面，不声称它们也有完整输出链。r2 在保存完成后正常 Alt+F4 关闭，无测试游戏进程遗留。

### 1V 隔离检查与待验收项

- 1V 保留原生玩法线程及最后 VBlank 等待，提前复用 `88044960` 的输入前段和 `88030D90` 的回调／计数；外围调用仍在原晚段执行。SDK 的只读下一 VBlank 截止时间用于限制等待，不新增计时线程。等待上限 2 ms、绘图预留 4 ms；三次超时后排空并在本进程锁定 balanced，主机继续转发排空入口。
- 交接模型现有 784 项检查通过，增加请求前完整交换门控、两类取消、停止期间的逐步骤结算、超时迟到，以及深度 1–16／计数回绕时恰好一次玩法唤醒。到期更新必须已有成功交换和有效的下一写入区，不能绕过交换失败或资源门控。
- `scripts/Test-SdojPrefix.ps1` 的两个原生隔离程序各通过 1292 组输入场景，分别执行 335697 项字段／getter／ABI 检查。覆盖 action 0–63、双玩家设备 2／0、映射组合、负值与 ±9000 边界、加入操作；与独立 oracle 对照七字节端口及控制器副作用，并比较原位置与提前栈上的原生前段。晚段复用生产 `NativePrefixResult`，没有第二次 action 查询，五个解析入口值、原生栈／LR／非易失 GPR／FPR 一致。两个派生 fixture 的 SHA-256 分别为 `8bed30dbcf6c29e241260a75822bc4c896132af51f676352c4a4661bad02e8f4`、`0d45d7ab3196ca373ab22e4e17992add7eef287ea8bdab8c154e6eeb6e35e1d4`；只保存在 `.local/analysis/sdoj-prefix-fixture/`。
- 隔离程序使用合成 engine getter，不查询设备，也不执行命令解析、玩法／GPU／音频循环。实际短按、失焦、录像输入来源、完整调用的浮点环境和输出结果仍须游戏检查，不能以这些数字称 1V 已验收。
- 本批完成 423 项源码检查（1 项因文件链接权限跳过）、9 组原生 CTest 和源码边界审计。新增交换门控、生产输入恢复 helper 和统计计数完成配套编译；16 对程序／PDB、13 个整理运行文件及 host 收据核对通过。没有据此增加游戏内覆盖。
- 独立 low-r1 的 `run-20261021T030408351Z-75816` 已正常启动到片头；computer-use 两次激活均报 `failed to activate captured window`，因此没有玩家输入、1V 请求或延迟结论。该进程使用本节新增交换门控及摘要计数之前的二进制，不代表最终候选。

后续集中补同录像逐逻辑帧对照、实体方向输入、新录像冷读回及无细粒度 trace 的短段节奏／音画检查。两模块进关短测更新如下；未重新验收联网、其他四游戏或制作发行包。

### 1V 资源加载死锁修复与针对性回归

- 修复前，同一 r3 程序仅切换 `frame_delay=1/2`：1V 的 `run-20261021T033942099Z-88880` 从 Xbox360 的 1P START 进入后卡在白屏，逻辑 1040 在生成命令前进入资源等待，超时排空又禁止后续原生唤醒；2V 的 `run-20261021T034303627Z-94632` 正常选人并进入第一关。两份记录位于 `.local/sdoj-frame-delay-debug-r3/logs/sdoj/`。
- 修复区分资源等待与整帧完成：只接受已核对的原生调用者及实际逻辑线程，恢复资源重试所需的正常帧推进，保留输入／驱动去重记录，不发布未完成缓冲。新增预算内和超时后中途等待的交错用例；交接模型累计 821 项检查通过，3 项 `test_sdoj_frame_delay.py` 源码约束检查通过，17 对 PE／PDB 和 13 个整理运行文件完成校验。
- 修复后 `.local/sdoj-frame-delay-debug-r4/logs/sdoj/run-20261021T034707421Z-96188`：Xbox360 与 Arcade HD 均实际选人、进入第一关、诊断方向移动／射击得分、暂停及返回菜单；Xbox360 另验证恢复及一次续关。同进程完成 CA022110 → CA022100 切换。两模块各经过两次已知资源等待，均继续推进；画面由 computer-use 观察，输入来自显式离线诊断 pad，不称实体按键验收。
- 模块 2 卸载摘要：`early_requests=6649`、`prefix_receipts=driver_receipts=6649`、`low_completed=low_publications=6647`，`timeouts=0`、`low_draining=0`、`low_ownership_skips=0`，实际策略始终 1。两次资源等待是请求与整帧完成数之差，不作为整帧成功发布。模块 1 停机前 `early_requests=12666`、两类 receipts 均 12666、`low_completed=12176`、`low_publications=11688`、`slowdown_frames=976`，同样无超时、排空或所有权拒绝；观察到减速路径，不据此单独宣称原版节奏对照完成。
- `.local/analysis/sdoj-low-resource-yield-r4-output.json` 关联 6615 次成功输出，其中 5526 份首次结果、1089 次重复结果。首次输入消费至 Present CPU 返回中位／p95／最大为 16.845／17.302／18.249 ms；仅覆盖细粒度 trace 达到 131072 上限前的模块 2。后段缺少来源的输出不纳入统计，未声称模块 1 也有完整链路。
- 桌面输入问题独立于上述死锁：片头阶段优化尚未运行，CU 激活失败、游戏无按键事件；只读检查发现前台任务管理器为高完整性，游戏与操作工具为中完整性。关闭任务管理器被系统拒绝，尚未完成移除它后的 A/B，不能断言它是唯一原因。
- r3 的 1V／2V 和 r4 均在诊断 `quit` 后产生同一 `sdoj.exe+0xB2C0` 退出阶段异常；旧测试命令只结束 UI 循环，绕过原生关窗的玩法线程终止。诊断入口已改走正常停机／关窗路径，这些旧运行不作为干净退出证据，原始 crash 文件继续保留。
- 退出修正后的 `.local/sdoj-frame-delay-debug-r5/logs/sdoj/run-20261021T040015665Z-79596` 仅做启动／退出检查：命令走过 `User requested close`、`Window closing` 和 `Title terminated`，进程返回 0、无 crash 文件；不增加游玩覆盖。最终配套构建、host 收据、13 个整理运行文件和针对性的 CTest／3 项源码检查通过。

### 固定录像对照工具

已补默认关闭的 `sdoj_logic_trace`，不受 latency trace 的 131072 条预算影响。它在已核对调用者、基线已捕获的玩法等待点读取完整命令，仅输出 SHA-256、输入和减速深度；记录上限为每模块加载代 32768。观察器的自造 SDK 内存测试已实际检查已知字节的 SHA-256、默认关闭、重复完成、缺 sentinel、错误缓冲／等待深度／线程、多批次及模块重置；没有运行游戏。五项 Python 对比工具测试通过，显式窗口的缺帧、重复、批次不连续或无效记录均拒绝；所有语义字段差异直接报告，不自动重新对齐、屏蔽指针或归一化命令。原始摘要若存在非语义地址／画布差异，也先报告，不能据此直接判定玩法分歧或相同。

输入前段 fixture 现在复用生产 `GuestCall`，正常返回和 C++ 异常路径均验证完整 PPC 上下文和宿主浮点控制状态恢复；两模块各 1292 场景、338282 字段／getter／ABI 检查通过。先前 335697 次的结果对应尚未加入此保存环境检查的版本。`low-r1` 的 PID 75816 在当时检查中仍响应，但再次点击游戏可见区域也报激活失败，未发送游戏按键；该旧进程现已关闭，不把那次启动作为游玩验收。

## v0.3 发行验收

发行标签 `v0.3` 已推送，固定在 `b8807ca623f0d72bb0f7707e62a7e700c0d09ec8`。
五款配套构建的 53／6／2 对 360／PS4／启动器程序与 PDB 收据通过；381 项源码回归与最终 24 项去重原生 CTest 通过。
主要模式、保存、正式社区与性能分别沿用下文已接受的证据。

三个最终 ZIP 各完成一次中文空格路径的真实解压和文件核对，结果绑定以下身份：

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| CAVE-PC-Collection-v0.3-internal.zip | 7129056312 | c66a5f532ba429bce0059807341c5a99fe639363251aaede199c3febcec20511 |
| CAVE-PC-Collection-v0.3-public.zip | 71970830 | 4e0fae4aba939f8c28e59adf474fad8fcdc4e928b646f3f8b09614b524a7af72 |
| CAVE-PC-Collection-v0.3-source.zip | 285109353 | 99d96f89343d34c085c1b17fd93e98cc0cf3c95496b057a59dc5db33923bff0f |

Internal 为 2850 项清单文件，包含五款数据、虫姬二两份 DLC 与独立源码 Git；Public 为 130 项，按白名单排除 ROM、源码和符号。
两包共用的全部 130 个文件哈希一致，其中 35 个为运行文件。源码附件核对 37133 个文件、独立 Git、25 项 SDK 和 19 项 PS4 依赖，绑定 Public 的 release.json。
本地一次验收记录在 `.local/v03-acceptance 中文 space/*-accepted.json`。

实际 Public 解压包在无 ROM 时正常显示启动器，再仅配置 SDOJ 数据、关闭联网，正常 GUI 入口到达标题及 Xbox360 主菜单，原生新建 SYSTEM.DAT 为 630704 字节。
Alt+F4 后进程退出、启动器恢复可用。运行 `run-20261020T095513928Z-65064`，日志 SHA-256 为 `1f765aa34b2168fcd88424e984f885c809cb10806ba8b85da5ddbe9812705137`。
Internal 的实际默认启动器和新大往生选择状态均正常，六横幅可见，截图为 `.local/v03-delivery/launcher-v0.3.jpg`。
相同程序不在另一包内重复五款实战，开发恢复也复用固定依赖的既有结果。

对应打包工具的后续目录扫描优化与文档更新单独提交，不改变上述冻结文件和标签。
工具的 18 项发行检查为 17 通过、1 因本机无文件符号链接权限跳过；真实 Windows junction 的根、祖先、子目录拦截通过，4 项源码附件检查通过。
OSS 上传与两个 QQ 群通知另行记录；此处的本地产物验收不代替交付状态。

## v0.3 开发阶段记录

### 当前代表性结论

五游戏候选已在干净提交 `0dcefea92cb0bf09dc21f5970ba9b4156ca4a365` 统一构建：360 的 53 对、PS4 的 6 对及启动器的 2 对 PE／PDB 均通过配套收据核对。完整源码检查为 380 项，统一 360 构建的 24 项去重 CTest 全部通过。日志为 `.local/v03-five-game-build-r1.log`、`.local/v03-accepted-scope-source-r1.log` 和 `.local/v03-five-game-native-r1.log`。这仍是开发候选，后续文档提交须重新签发同提交构建收据，尚无 v0.3 正式包或上传。

在独立 `.local/v03-five-game-check-r1` 中，正常合集启动合同、离线／窗口／VSync、未启用输入延长或游戏探针，实际到达虫姬二与 G2 标题画面。返回的窗口标题分别为 `CAVE PC Collection — 虫姫さまふたり Ver.1.5` 和 `CAVE PC Collection — エスプガルーダII ブラックレーベル`。对应 `run-20261020T090721893Z-81144`／`run-20261020T090803052Z-89352`，关闭后的 game.log SHA-256 分别为 `df16773b1d5bd3ad29bda1d122ddc9a1859aa7b50798865bb0771a0d92039e84`／`d00ba435f3cf5196df34128be2ee97020c6aaec970650ac7f4ea79e6a58550eb`。两游戏及启动包装进程均已关闭，保留原生 hard-exit 日志。结合 SDOJ 和两款 PS4 的既有实际窗口观察，五款标题核对完成；未进入新关卡，不新增模式、实体手柄、性能或联网结论。

PS4 任务交接的硬宿主 CPU 绑定已修正，保留六线程、原任务数、完整信号量和 GPU 同步。相同程序 pinned A1／inherited B1／inherited B2／pinned A2 对照六类正确性均通过，硬绑定有 26–37 ms 批量尾峰，继承策略两轮最大 67–86 µs；默认实现再通过 375 源码和 73 原生检查。正常绊地狱 `.local/ketsui-pacing-r10` 与大往生 `.local/daioujou-pacing-r7` 均完整播放既有录像并自然返回，选定 31／30 秒有 1860／1800 个连续帧、无缺号，最大间隔 17.185／17.188 ms，提交外最大 2.400／2.842 ms。按截图推算的整段回放均未观察到 >20 ms；不是精确客体帧 trace，也不代表较低配置或所有模式已测。原始窗口、身份、日志遗漏及旧候选对照在 [同步机制](PS4_SYNCHRONIZATION.md)。

本批已取得根因对照、修复及本机代表性性能回归。用户已接受大往生现有结果，不再补模式细项；最大往生小飞机音效保留为未确认反馈，不继续扩大本版排查。继续其余合集核对与发行门槛，最终五游戏配套构建及发行／上传仍未完成。以下 r3–r9 的“性能未收尾／暂停模式”描述对应早期候选，不覆盖当前结果，亦不将其重标为通过。

### 大往生主要模式的增量覆盖

独立现场 `.local/daioujou-modes-r2` 使用离线、窗口、VSync 和正常游戏节拍，未改原始数据。正常五参数合集入口已到达原生参数界面，但该次取消了长按确认，不记为 CUSTOM 游玩通过。下表的新游玩证据来自显式 `--enter` 原生开发入口：短按诊断 120 ms、`--diagnostic-circle-hold-ms 1500`；H 满足原生确认，后续 opt-in Shift+H 最多持续 Circle 30 秒以观察射击。默认输入、玩家 INI 和正常合集入口不变，不用于物理设备、输入延迟或新性能验收。

| 入口 | 已观察到的实际流程 | 尚未独立覆盖 |
| --- | --- | --- |
| CUSTOM 参数路径 | ARCADE 暂停 → GAME SETTING，NORMAL 改 EASY，原生排名 ON → OFF；原生长按重置后 Type-A／Shotia 进入 Stage 1A，2010 分自然 GAME OVER，返回 DIRECT MENU；后续新进程读回 EASY／3 自机／排名 OFF | 独立死亡到复活过程；不将 CUSTOM 可玩等同于可上榜 |
| ARRANGE S | Type-A／原作固定 Shotia，Stage 1A 玩家自机、射击／爆炸，164330 分；推进到中型敌机，547280 分 GAME OVER，暂停及返回 DIRECT MENU | 独立死亡到复活过程 |
| ARRANGE L | Type-A／原作固定 Leinyan，Stage 1A 射击／爆炸，余机 2 → 1 后仍见自机；推进到 Stage 1B／1125660 分，暂停及返回 DIRECT MENU | 单独持续激光操作，不从发键推断视觉通过 |
| ARRANGE EX | Type-A／原作固定 Exy，Stage 1A 射击／爆炸；余机 2 → 1 后自机继续运行，推进至中型敌机／1385010 分，暂停及返回 DIRECT MENU | 更多机体、后续关卡不属本轮轻量范围 |
| BLACK LABEL | 默认 Version-B（NEW），Type-A／Shotia 玩家第一关射击／爆炸，52360 分自然续关倒计时，暂停及返回 DIRECT MENU | 独立死亡到复活过程，不从局末倒推 |
| 怒首領蜂III | 原生额外版本，Type-A／Shotia 玩家第一关射击／爆炸，余机 2 → 0 后仍有自机；248580 分中型敌机／自然续关倒计时，暂停及返回 DIRECT MENU | 更多机体及后续关卡不属本轮轻量范围 |
| ARCADE CHALLENGE | ROUND 1／Shotia／Stage 1 AREA A，默认 level 10／NORMAL／推荐资源；玩家射击、原生失误回卷与重试次数 20 → 17、返回挑战列表及 DIRECT MENU | 不扩展全部挑战、奖牌或榜单验收 |
| ARCADE OSARAI | 原生失误槽 01／Type-A／Shotia／Round 1 Stage 1A，实际载入、玩家射击、MISS!! TRY AGAIN 回卷、返回片段列表及 DIRECT MENU | 不扩展全部记录或榜单验收 |

CUSTOM 对应 `native-hold-r1/stdout.jsonl`，宿主 SHA-256 `7f705237250b3eca8a40e4c7ff64648a91381a1396f88b8d13a6946aa9dd294b`，日志 SHA-256 `912e0a99ed60d7697305fb61e348454359191e3295104d30b8e8d4a7f268fb76`。S／L 对应 `native-hold-r2/stdout.jsonl`，宿主 `f1683bc58c3023d0981348a33a264d0a284246dae42c27f258e160ad4a14bb31`，日志 `ac93ef58dede926e18f3c0809b58847c99605d95812ca12a29650c1f2676ea88`。两次 GPU 均为 `ede6e68396b88553e63b06964552f3b04eb13a246ba75884a669b88a4ea7a192`，关闭后日志均排空，保留既有退出码 27；细粒度诊断存在竞争拒收，不声称所有事件无损。后者 38 项 pad 子例、375 项源码和 73 项原生检查通过。

新增五入口及 CUSTOM 冷读回对应 `native-hold-r3/stdout.jsonl`，干净提交 `eb0bcbe` 的配套重建，宿主／GPU 与 r2 相同，日志 SHA-256 `e0f3ad1837d9d38704b16602475d4a35c806d5e2dcc2b0cfac33a1931ef6f5e1`。F8 标记 1（黑标暂停）为 8642329877999，标记 2（冷读回设置）为 8642968479917。已核对进程终止、窗口销毁、后台日志排空及既有退出码 27；accepted=written=266118、dropped／format／sink／frame-emission-failure=0，但竞争拒收 570、host-operation-emission-failure=537，仍不声称全部诊断无损。关窗后即时 CU 刷新没有前台 PID，重新列窗及查进程均确认已结束，没有重启或重复发送关闭操作。

ARCADE／SUPER EASY 的既有玩家结果和 LUNA TOUR 首区域结果分别沿用。所有可见主要游戏入口已有代表性玩家片段，本轮未观察到新崩溃。用户随后确认“大往生可以了”，本版主要模式按此范围验收，不再追加模式测试；CUSTOM／ARRANGE S／BLACK LABEL 独立死亡复活、LUNA TOUR 适用失误行为和未隔离的持续激光仍未测，作为扩展覆盖保留，不倒推为通过。追加启动的 native-hold-r4 在进入新测试前即按该指令受控关闭，不产生新游玩结果。最终合集构建、解压与交付仍须独立完成。

### 最大往生音效链及补丁分析

默认关闭的 `sdoj_audio_trace` 已覆盖两套本体模块的常规 6 字节声音命令，以及主程序实际 44 字节引擎 push／pop／原生播放。离线副本正常菜单播放 Xbox360／Shot／9786 分旧录像并自然返回，随后关闭进程。141 个模块 dispatch／返回、282 个可配对引擎 head 和 157 个原生播放返回有证据；没有 push 失败、最终零增益或空 voice 返回，但尚未观察扬声器发声，也未绑定小飞机死亡 cue。通道 4 的部分音效等待约 103–122 ms，保留调查。槽索引与空队列重置均进入离线配对，不能使用初版误跨槽复用算出的秒级结果。380 源码／9 组去重 CTest／12 个音频结构子例通过。

已有“音乐完美补丁”只读解出一个更新版主程序，没有资源替换；静态装载与音频区结构筛选已完成，并反汇编确认 raw-file 循环播放参数有改动。原生音效库 cue 走另一条路径，没有证据据此认定小飞机音效修复。补丁未安装，原数据、正常播放参数与 v0.2 包未改；干净 TU1 对照及实际具体 cue 仍有缺口。用户在分析后认为可能本来正常，本版将此项保留为未确认反馈，不再扩大排查，不作音效修改；不等同于已证明正常或已修复。原始身份、计时边界和静态证据见 [音效队列与补丁分析](ANALYSIS.md#音效模块与引擎工作队列)。输入调查与最终发行仍单独核对。

### 最大往生输入链路的阶段证据

后续已增加默认关闭的渲染顺序观测，独立 Xbox360／Shot 训练 Stage 1 实际启动并关闭。存活右键沿至 raw copy 为 4.900 ms，再到逻辑复制 0.360 ms；复制后 16.515 ms／下一 epoch 观察到缓冲交换。原生等待全程 114 个配对样本均 depth=1／一个 epoch，但包含菜单与转场，不是全程玩家数据，更不是按键到屏幕延迟。381 源码／9 项去重 CTest／27 输入渲染子例通过；准确身份、无效 DXGI 全零反馈与数据边界见 [渲染时序分析](ANALYSIS.md#渲染交换与原生帧等待的观测)。两项上游降延迟补丁均未接入，同 epoch 不等于问题已不存在或我们已经修对了某件事。

新增光盘本体限定、默认关闭的只读输入探针；不改变采样、输入保持、客体状态或渲染顺序。独立 `.local/sdoj-input-trace-r1` 通过原生菜单及 Xbox 360／Shot 玩家第一关，实际窗口标题为 `CAVE PC Collection — 怒首領蜂最大往生`。7 个新按下沿在下一个客体 VBlank epoch 被原生轮询采到，键沿至轮询为 0.962–15.776 ms，轮询至按钮字复制为 5–6 µs；282 个有界输入更新摘要最大 40 µs。主要是菜单和转场样本，方向短按落在续关提示后，不能宣称玩法移动、物理输入或端到端延迟已测／已修复。

本批 377 源码、8 项去重原生 CTest 通过，其中输入观测自造原生子例 15 项。14 对 PE／PDB 核对及实际生成代码插入位置已检查；关闭后进程终止，SDK 原有 hard-exit 行为保留。身份、时钟配对及限制见 [输入探针分析](ANALYSIS.md#本体输入轮询与复制的只读探针)。下一步仍须核对玩法输入掩码消费与渲染批次；小敌机音效缺失没有对应 cue 证据，不据此关闭。其余两款 360 的日文窗口标题实际核对和最终配套构建仍待完成。

两套玩法掩码观察已增补。`.local/sdoj-core-input-r1` 的 Xbox360／Shot 第一关训练中，存活右键从原始按钮复制到掩码生成为 507 µs，随后 23 µs 在同一 VBlank epoch 被复制；其后左键发生在续关提示。返回菜单并实际启动 Arcade HD 训练，右／左方向位生成到复制均为 10 µs、同 epoch。两模块最后可见累计 5303／20730 次匹配复制均为同 epoch，但包含菜单／演示／转场，不将整份累计样本叫作玩家实战。未耗尽日志预算，观察器无争用计数，进程已关闭；详细身份、范围和限制见 [玩法掩码分析](ANALYSIS.md#玩法输入掩码生成与复制)。378 源码、8 项去重原生 CTest 和 20 个自造输入子例通过。不把当前排除结果写成输入延迟已修复；后续逻辑／渲染与音效 cue 仍待定位。

### 较早候选与历史定位

地址等待候选通过 373 项源码、73 项原生检查，semaphore 两条传输路径共 52 子例。正常大往生 `.local/daioujou-pacing-r5` 固定录像完整自然返回，30 秒／1,800 连续帧（10171–11970），p95／p99 上界 17.1／17.2 ms，最大 17.325 ms。提交外均值 1.576 ms、最大 5.672 ms；有 worker 在 Wake 返回后仍延迟 3.805 ms，成功路径已不重取共享 gate，性能未收尾。离线同程序 A/B/B/A 的身份、峰值和通知分段见分析文档；不把旧候选／新候选或组件结果混成同一次验收。当前按用户要求优先继续性能，暂停模式和发行工作。

模式部分结果保留：`.local/daioujou-modes-r1/run-20261020T042318188Z-84944`（日志实际位于 logs/daioujou 下）使用 df79594、输入诊断 120 ms、离线／正常节拍。SUPER EASY／Type-A／Shotia 实际选机、选人、第一关射击得分、死亡复活、返回 DIRECT MENU 通过；LUNA TOUR／Casual／Shotia／Type-A 从路线板进入，射击得分、首区域 AREA CLEARED、取得首个章并返回巡回板通过，未覆盖其死亡。其他入口及 CUSTOM 仍未测，不以这两个结果代替全模式通过；这轮在性能优先指令后受控关闭为既有清理限制 27，不再追加模式操作。

显式 release／acquire 发布补强后，完整 372 源码、73 原生和六组离线正确性复验通过（日志 `v03-semaphore-software-r3-*`）。离线第三轮连续交接中位数仍为 17 µs，但间隔交接 p95 168／305 µs、最大 8.837／9.191 ms，尾部改善未保持；不将本批标作长帧修复。下段 r4 实机对应补强前候选，最终程序仍需将模式与帧节拍合并轻量回归；具体两份程序身份见分析文档。

信号量软件批次已通过 372 项源码和 73 项原生检查，包含解锁后通知的对象生命周期、仍有旧通知时再次等待、弱缓存跨线程删除、不可变优先级的 TLS 与继承（semaphore 24／thread 18 子例）。两轮离线相同负载的常见交接及 p95 下降，但仍有 3–7.5 ms 尾峰。正常 `.local/daioujou-pacing-r4` 的同一 1780 分录像完整自然返回；F8 后 2–32 秒共 1,800 连续 submission（7854–9653），p95／p99 上界 17.1／17.2 ms、最大 17.311 ms，指令处理均值 3.985 ms、提交外 1.570 ms（最大 2.828）、最终输出 0.102 ms。包含 4 项完成编译，没有排除异常帧；与旧 r3 不是逐游戏帧对齐，不把本次未出现 33 ms 当成根治。身份、原始日志哈希、竞争拒绝和分析区间见分析文档；整体性能及主要模式门槛保持开放。此检查日志为 `.local/v03-semaphore-software-r1-{build,source,native}.log`，离线报告为 `.local/offline-bench/semaphore-software-r1/` 和 `r2/`。

软件合成提前提交的正常回放已取得新证据：绊地狱 `.local/ketsui-pacing-r8` 完整自然返回，31 秒内 1,859 个连续 submission（12915–14773）最大 17.256 ms、p99 直方图上界 17.2 ms，最终输出均值 0.102 ms；指令处理／提交外约 3.604／1.637 ms，未取得明显下降。大往生 `.local/daioujou-pacing-r3` 的相同 1780 分录像完整自然返回，选定 30 秒有 1,798 个连续 submission（10110–11907）、p99 上界 17.2 ms，但仍有 33.740／33.618 ms 长帧。它们主线程的任务等待分别为 13.239／14.876 ms，不是最终输出又耗了十几毫秒。r7 的 CPU 物理核心优先硬映射实际回放严重退化，已撤下；当前只保留软件改动。具体身份、窗口和分段见分析文档；大往生剩余等待及总体性能门槛不记作通过。

无 ROM／无窗口的离线 bench 已实际运行：六组负载、128 样本／16 预热／三次重复，两轮同程序 A/A 均约 20.2 秒，逐帧像素、实际绘图次数和任务总数／checksum 校验通过，比较入口通过。组件证据不代替游戏：连续批次约 19–20 µs，而 60 Hz 间隔后派发约 87–111 µs，第二轮单次最大 6.719 ms；相同程序的尾部也明显波动。两个点着色器绘图负载远简单于游戏，不用其 0.88–1.12 ms 指令处理推断游戏成本。报告在 `.local/offline-bench/software-baseline-r1/` 和 `r2/`。此批完整源码回归为 372 项（包含 12 项 bench 协议用例）、原生 73 项、GPU 四项非不限速机制检查，均通过；源码审计未包含游戏数据。日志分别为 `.local/v03-offline-bench-source-r1.log`／`native-r1.log`／`gpu-r1.log`。

前一轮三段优化回归为基于 `5ebc29b` 的 `.local/ketsui-pacing-r6` 工作树构建，不是正式发行：提前提交已录制绘图，去掉 Drain 回调屏障导致的空 GPU 提交／二次等待；必要栅栏、回写及原作速度不变。固定 380 分录像完整自然返回，31 秒有 1,860 个连续完成帧，p99 上界 18.9 ms、最大 19.803 ms。指令处理均值 3.567 ms、最终输出 0.838 ms、提交外 1.584 ms；原 r4 输出约 1.340 ms。尾峰及提交外问题未关闭：当时配对到工作线程授予后恢复 6.613 ms、主线程随后等待完成，因而继续拓扑映射试验；该试验后续退化及撤回见本节前部。358 项源码、73 项原生和 4 项 GPU 机制检查通过；详细哈希、分段和日志缺失限制见 [绘图提前提交与排空回调修正](ANALYSIS.md#绘图提前提交与排空回调修正)。该段保留原阶段证据，不覆盖上方的新回归和开放门槛。

v0.3 性能门槛尚未通过，范围包括大往生与绊地狱的指令处理、画面输出和两次提交间隔；用户已停止不限速 FPS 试验。最新绊地狱正常节拍回放 `e462310`／`.local/ketsui-pacing-r4` 完整自然返回，30 秒内 1,799 帧连续，p99 上界 19.3 ms、最大 33.589 ms。19–20 ms 的输出尖峰主要位于等待 GPU 提交完成的函数，尚未区分设备执行与驱动／唤醒；33.589 ms 帧则有 21.495 ms 提交外间隔，其中主线程 semaphore 等待 19.821 ms。三段仍须继续定位与安全优化，不能据平均 60 Hz 标记性能问题解决。详细身份、范围及日志完整性限制见 [分析工具中的三段成本定位](ANALYSIS.md#v03-正常节拍的三段成本定位)。

大往生的正常合集与正式 HTTPS 代表性闭环已通过，构建为 `ce006b1`。在独立 `.local/daioujou-collection-https-r1` 的 GUI 选择本作并点击统一启动按钮，正常五参数合同完成首次准备，窗口标题为日文原名；`run-20261019T231514277Z-83076` 实际进入 ARCADE／Type-A／Shotia 第一关，射击得分、死亡复活、F11 全屏和 1,040 分自然结算通过。原生 board 0 的整数 104 与 26,625 字节录像均经正式 HTTPS 上传并返回 200；游戏内榜单正确显示昵称、1,040 分及录像标志，下载结构／元数据校验、0020-0000 原生保存 commit 完成。附件 SHA-256 为 `0388e187bc5e4c6b94445f28f3b8421a885725e535fb3d080db29f0e20d3a628`，只读公网下载与其一致；新下载槽位 9 的记录 SHA-256 为 `52fadac7761dc41d3f5f6aa0988dc39cc87591c56da9c982489708911c9222e6`，本地 mode=0／score=104／extra=4，保留原生网络 mode=3 到本地的转换。

关闭后，GUI 显示正常返回及既有清理限制；随后同一独立 root 关闭联网，正常合同新进程 `run-20261019T232122411Z-70548` 的诊断按键延长为 0、缓存命中，未创建社区 provider、没有通信请求。DOWNLOAD REPLAY 中准确选择 1,040 分记录，实际播放第一关至自然返回下载列表，整份录像存档 SHA-256 保持 `326ba7eed05d29790d236c07c80cc66300d96bd0beee27a96e5f6ec223ced29a`。两进程均受控关窗（27）；没有修复客体 finalizer／宿主完整清理，也没有把 120 ms CU 进关诊断当作实体输入测试。正式证据在 `.local/analysis/daioujou-collection-https-r1/result.json`，包含两个运行身份、请求状态和附件／保存哈希。

候选重建前的完整源码回归为 349 项通过；第一次运行只有旧双语标题用例仍假设大往生未接入而失败，调整该状态断言后重跑全套通过。公共 common／launch 两项 CTest、66 项 PS4 原生检查、6 对 PS4 与 2 对启动器 PE／PDB 配套检查通过。当前五游戏发布构建、实际 ZIP 解压、对应源码附件、SDOJ 未结调查与发行交付仍待完成；本节不宣称 v0.3 已发行。

正式大往生服务已增量更新到 `171d8a5`：ECS Python 3.10.12 的 17 项协议／备份／部署检查通过，19 个源码成员逐项校验，更新前一致性快照的数据库身份和完整性通过。只切换 daioujou-current 并重启 cave-daioujou；旧四款 PID、G2／SDOJ 指针与 nginx 片段哈希不变，七个正式 HTTPS 健康入口及未知游戏 404 通过。正式数据库仍为空，未写 fixture。当前将大往生接入开发候选 catalogue 的正常启动／联网入口，等待配套重建和真实 HTTPS／正常入口回归；这不是该入口或 v0.3 发行已验收。

大往生的真实原生 loopback 闭环已通过：成绩／录像上传、榜单读回、下载保存，以及关闭联网后的冷启动完整回放。`d4bf1a1` 的 `.local/daioujou-network-r8` 加载两项本作只读资源覆盖，未创建社区 provider，DOWNLOAD REPLAY 显示已保存的 1780 分记录；播放第一关后自然返回下载列表。槽位 7 的记录 SHA-256 仍为 `1745f92e85524448ce0ad87fefe826cfe4d9be72640c38556965a5ad2cd8a2e6`，整个录像存档与 r6／r7 的 SHA-256 也相同（`5271cdc8756d73e086d6d1bc83dcd2b9c95519db32dbbd73cbd2bd1d1fe96c66`）。107 项原生资源对照及 5 项源码资源检查通过，实际准备覆盖与独立 Python oracle 一致。该离线回放独立于正式 HTTPS 和正常合集入口验收；候选接入状态见上一段。后文 r1–r7 保留失败定位和分步证据，不代表这些失败在 r8 仍未处理。

绊地狱在同一 `d4bf1a1` 宿主上完成 `.local/ketsui-perf-v03-r2` 的代表性复测：普通输入诊断为 0、离线、vsync=true、全屏，同一份 ARCADE／Tiger／380 分／00:53.45 录像自然播放到结束。关卡约 15–47 秒的宿主时钟区间 `8614455686824–8614487730619` 中，1,922 个连续 submission 的出帧间隔平均 16.666 ms、p99 直方图上界 18.4 ms、最大 19.638 ms，没有超过 20 ms 的记录或着色器／管线编译记录。GPU SHA-256 与旧 r1 相同；报告为 `.local/analysis/ketsui-replay-present-r2-final-bounded.json`。这是相同录像的短片段对照，并非逐帧对齐、只改一处代码的 A/B 实验；支持公共日志修复后该片段有改善，不将旧四个约 33 ms 长帧和 1.633 s 间隙全部归因于日志，也不宣称所有卡顿已修复。回放结束附近另有 50.084／66.711 ms 间隔，不混入关卡片段统计，仍保留返回阶段的调查。

该绊地狱进程最终日志排空完成，满队列淘汰及写入／格式错误均为 0；全进程累计竞争拒绝 113 条，因此不能宣称全部诊断完整。上述区间的帧完成记录 submission 连续。主动关窗仍记录已知退出码 27 和本次受控关闭收据，不记作退出清理修复。

### 分步证据

大往生第二份独立实战 `.local/daioujou-network-r2` 取得 290 显示分，成绩提交后网络附件被本地校验以 inflate_output_limit 拒绝。只读解析自带结构确认网络根大小 0x80000／数据 0x78000，不是本地记录的 0x65600／0x65000；客户端、服务端及自造附件已修正为网络合同。本作版本／元数据、解压上限、内层快照与按键目录校验保留，新增较小本地记录、其他标题、错误版本／数据范围和尾随数据的拒绝用例。7 项服务协议及 15 项清单／源码审计检查通过；不把这些检查当作实机网络闭环，重建后的真实上传和下载回放尚待完成。

网络尺寸修复后 bfc5808 的 16 项原生结构／4 项双标题 HTTP 用例通过；r3 真实 1240 分附件已通过结构、仍被模式检查拒绝。e85f288 新增有界头部诊断并通过旧绊地狱 ABI／HTTP 定向回归，r4 的实际 400 分网络头记录 mode=3，其他版本／分数／角色符合。静态核对原生 rankingModeConv/checkRegistRanking 证明符合条件的临时 CUSTOM=3 归入 ARCADE，网络头保留 3、本地保存转为 0。新增 ARCADE 0／3 合同及跨模式拒绝用例，8 项服务检查通过；没有取消 CUSTOM 榜及 ALL-MIX 的原生资格限制，实际游戏网络闭环仍待修正后重测。

48a28d0 的 20 项原生结构检查通过，r5 真实 1650 分自然局末已成功向独立 loopback 提交 board 0 原生整数 165 及 26,547 字节附件，两次 HTTP 200、服务数据库与附件哈希一致；原生与服务端离线检查同样通过。进入 Ranking Viewer 后，游戏自行将该 profile 既有的 1780 分补传，实际列表显示昵称 Djo-Network5、1780 和可下载录像标记。点击下载尚被 Sony ID-mapper 的网络初始化失败挡住，未发生 fetch，不算下载验收。已增加本作限定的社区资源回退，保留正常列表路径，后续验证它的实际下载、保存与冷回放；正常合集入口及发行仍未完成。

8dde8b9 的 101 项原生资源检查通过；实际原生覆盖准备及六程序合集缓存生成通过，修正脚本与 Python oracle 的 SHA-256 相同，完整输入校验通过。r6 冷启动加载一项本作覆盖，从原生榜单成功下载 Djo-Network5／1780 的附件，校验通过并完成保存事务 commit。新增槽位 7 的 native score=178、extra=4、psnid=Djo-Network5，记录 SHA-256 为 1745f92e85524448ce0ad87fefe826cfe4d9be72640c38556965a5ad2cd8a2e6。r7 从关闭后的副本离线冷启动，未创建 provider、read_patch_count=0；DOWNLOAD REPLAY 列表被原生 PSN 退出提示阻止，尚未回放。上传、读榜、下载及落盘已有实际证据，离线冷回放、正式 HTTPS 适配和正常合集入口仍未通过，不将完整联网闭环标记完成。

本节是分步实施结果，不是 v0.3 发行为已完成。大往生 ZIP 已完成下载，61 个文件全部通过 CRC／SHA-256：CUSA32997／01.04，单目录合并更新布局，完整副本在 .local/inputs/daioujou-20261020-r2，原 ZIP 保持不变。清单 games/daioujou/data.json 包含 60 项、1,116,174,744 字节运行资源，不包含 Project.gp4。保留失败的 r1 提取目录，不使用其内容，也不将输入确认当作 ready。

发行脚本已新增 v0.3 五款版本档案和两款 PS4 宿主别名的证书映射；Internal 按大往生 60 项清单复制，Public 仍使用独立白名单。未接入、Coming Soon、缺运行清单或未开放联网的任一应含作品均会阻止两种包生成。15 项自造发行测试通过，其中 v0.2 与 v0.3 两组实际执行组装、ZIP、中文空格目录解压、独立源码 Git 核对与两包运行文件比较；这些文件不可执行，不是实际游戏包验收。

对应源码附件已由本地一次性脚本整理为 Release-Source.ps1／source_package.py，绑定已校验的程序包、干净源码提交及 release.json 哈希，恢复独立 Git 和固定依赖后组包，并流式校验 ZIP 成员。4 项自造源码附件检查通过，覆盖实际 Git 克隆／ZIP／解压、错误提交、成员及哈希、ROM／生成正文／工具目录排除；依赖恢复在此测试中使用 mock，不声称完成真实 v0.3 附件或恢复构建。完整源码回归为 344 项通过，源码边界审计未包含游戏数据。当前没有 v0.3 正式压缩包、标签、OSS 上传或群通知。

大往生的 ECS 部署模板已准备：独立版本指针、18088 loopback 端口、数据库目录和每日备份；代理模板只增加本作路由。4 项静态部署检查通过，其中保留旧四款路由／端口与未知游戏 404。b9a8f4f 的源码归档在 ECS 临时目录完成 19 项成员哈希核对，Python 3.10.12 下 profile／备份／部署共 15 项测试通过，三个本作 systemd 单元的离线 verify 成功。未安装单元、切换版本指针、启用本作或更改 nginx；G2／SDOJ PID 前后一致。证据在 `.local/daioujou-service-preflight-b9a8f4f-r2`，不是线上健康或原生游戏联网通过。首次 d1ac115 归档遗漏测试引用的 fixture，预检失败并保留；已将自造记录生成器独立为 replay_fixture.py，避免服务器测试依赖 Windows 构建工具。

随后完成独立服务部署：`daioujou-current` 指向 b9a8f4f，18088 仅监听 127.0.0.1，代码由 root 维护，状态目录由低权限 cave-community 写入。nginx 先备份原片段，再核对仅新增本作 location，校验后 reload。五款合集和两条旧入口的 HTTPS 身份、未知游戏 404、本作 49 个空榜与 Windows 本机健康检查通过，未向正式服务提交 fixture。服务及每日备份 timer 已启用，首次快照的 CUSA32997／application_id 0x444A5356 和完整性检查通过。旧四款 PID、G2／SDOJ 指针保持不变，回滚备份与交付外证据在 `.local/daioujou-deploy-b9a8f4f-r1`。这完成服务端部署，不代替原生游戏内提交、下载和回放。

正常 PS4 合集入口已统一传 configured provider，run.json 分开记录联网请求、能力、实际开关、地址与昵称；仅绊地狱启用自己的 whole-round。大往生能力仍关闭，参数存在不绕过 catalogue。开发构建及整理通过，66 项原生、15 项自造录像和双标题 HTTP 2 项通过，现场后缀为 v03-configured-provider-r1；尚未运行新的真实游戏入口。

双标题的原生 NpScore ABI → Windows 客户端 → HTTP → 服务链路已用不可游玩的自造录像贯通。`v03-native-replay-bridge-r2` 的四项检查通过：两款直接客户端，以及两款 ABI 成绩提交、个人榜、附件上传／分块下载、新高分后的旧 entry 钉住与错误分值拒收。服务最终留两条有效最高分、一条同分拒收回执、仅一份对应旧成绩的附件，下载字节／SHA-256 与输入一致；不访问正式服务、不执行客体或原生录像。r1 的两个 ABI 程序本身成功，但汇总误认为拒收请求不应占数据库行而失败；已修正汇总并保留旧结果。旧绊地狱相同功能定向回归 1 项、完整 66 项原生检查通过；不把这些结果标为游戏内联网验收。

真实游戏启动随后已恢复，未换启动通道：`.local/daioujou-network-r1` 实际 ARCADE／Type-A／Shotia 第一关及自然局末为 1780 分，独立 loopback 的 board 0 收到原生整数 178，成绩 HTTP 200。随后录像本地校验失败，游戏显示网络错误，未上传录像、未完成综合榜或网络回放闭环。保存文件中的同局 0x65600 字节记录只读提取后，以 board 0／178 检查通过；记录 SHA-256 为 e2b10aa8cf3c573c4744b623543ac4506630fc6479971e10a72c64d7650f24e3。上传缓冲与保存记录的差异仍待定位，不放宽验证条件。

这轮实际暴露严重帧间隔问题：第一关 F8 后 10 秒（8609862441413–8609872441413）完成 422 帧，123 个间隔超过 20 ms，最大 100.777 ms。周期 TraceAllocation 仍同步 printf，122 个相邻完成记录区间包含该日志，其中 120 个为长帧；其余 300 个区间仅 3 个长帧。另一次会扰动时序的短采样在提交线程 300 次指令指针样本中有 88 次位于 ZwWriteFile，不把采样窗口当作帧时间验收。

832717c 将这一处调用接入现有有界后台队列，未改逻辑速度、刷新率、同步或日志采样频率。PS4 重建、6 对程序／PDB 与运行整理、后台日志自测及默认计数模式 8 项检查通过，另加一项源码调用点回归。修复宿主 SHA-256 为 5c4dda5fc4b1193385e73baeb3f39d1d607149f9e525dcff0328f1f6aabd176f。`.local/daioujou-perf-r2` 冷启动读到同局录像，完整播放至自然返回 MY REPLAY；第一关 F8 前／后两个连续 10 秒（8610505355098–8610515355098、8610515355098–8610525355098）各完成 600 帧，最大分别 19.943／19.388 ms，均无超过 20 ms 的间隔。原实战与新回放不是逐帧对齐的双版本基准，不宣称所有场景已稳定；确认周期同步日志问题修复及真实本地录像冷回放通过。在线附件失败、正常合集入口和发行仍待完成。

最大往生既有 `run-20261019T161136128Z-78436` 音频输出分析已完成：3 路各 1,119 个关窗前样本，队列 32–42.7 ms、最老块最大 51.8 ms、消费年龄最大 63.8 ms。没有观察到输出队列秒级积压，但该日志没有 F8 或本作击毁 cue 探针，不能排除个别音效未触发／混音遮蔽。报告 `.local/analysis/sdoj-output-audio-v03-r1.json`；小飞机音效仍未复现定位，不标记修复。

新的 PS4 ZIP 工具只向新的 `.local` 目录提取，拒绝逃逸、大小写冲突、链接、Windows 保留名、文件／目录冲突及宿主程序；逐成员 CRC 和 SHA-256 后才报告完整。8 项自造 ZIP 用例和原 3 项 SFO／盘点用例通过，不执行下载程序或改写原包。

启动器已实现三语言翻译表、右上角语言菜单与 `[collection].language=zh|ja|en`，旧配置默认中文。六款标题保留英文元数据，另由官方来源提供日文显示名；横幅显示日文主标题及小字英文，数据／存档 ID 不变。两个后端的窗口标题源码已统一使用日文显示名，尚未重建／实际验证游戏窗口。

无 ROM 的独立 `.local/v03-ui-r1` 实际窗口验证中文 → 日文 → 英文；修改昵称但不保存后切换日文，草稿仍保留而 INI 昵称不变。随后保存、关闭并新进程重开，日文及昵称读回正确；改为英文后原生控件显示 Start fullscreen／Save，未知配置与注释保留，最后已关闭测试窗口。主配置未被此测试修改。三语言原生 PNG 预览与窄窗口实际布局已自审；预览状态是示例，不当作游戏安装或游玩证据。

320 项源码测试及审计通过；公共 common／launch 两项 CTest 通过，语言代码／缺失翻译／配置保存读回有原生用例。大往生可玩与联网、既有音频／输入延迟／Replay 卡顿问题、配套游戏重建及发行交付仍未完成，不复用本轮界面证据将其关闭。

绊地狱追加慢呈现分段日志后，24 项原生增量编译与 6 对 PE／PDB、运行组件整理通过，GPU 无需重新编译；配置原生自测 13 项通过，实际窗口标题为日文元数据。42 项时序分析器用例通过，包含新分段的总和／时钟范围校验及长帧关联。独立离线现场 `.local/ketsui-perf-v03-r1` 的第一进程仅做菜单探索：新日志有 6 个有效分段，最大样本来自启动，不能作为录像卡顿的根因或修复证据。该进程已正常关窗；原默认存档副本的 My Replay 没有条目，副本移到同一现场的 profiles-initial 保留，再复制已有 toolchain23-r1 固定录像存档用于后续对照。原始数据与原存档均未改动，问题继续保持未解决。

第二进程 `run-20261019T174625951Z-6864` 使用普通输入（诊断延长为 0）、离线、vsync=true，同一份 ARCADE／Type-A／380 分／00:53.45 录像完成全屏播放并自然返回 My Replay。F8 标记 1–2 之间为录像约 15–47 秒，1,901 个完成间隔；四个约 32.6–33.4 ms 的长帧中呈现段为 12.3–16.3 ms、执行段为 10.8–13.6 ms。另有一次 1.633 s 的提交外间隙，尚未区分关卡载入、客体同步与宿主调度，不归为稳定游玩帧时间。该窗口无至少 20 ms 的慢呈现记录，未复现旧日志的 26–33 ms 呈现段；不据此排除问题或宣称修复。边界限定报告为 `.local/analysis/ketsui-replay-present-r1-bounded.json`；同名无后缀报告只有起点，不用作精确场景统计。测试进程已受控关窗，退出码 27。

PS4 准备缓存按 recipe.game 的清单 ID 隔离到 prepared/<game>，收据也核对同一游戏身份；原生存档从配置的内容 Title ID 建立目录，不再固定 CUSA09062。23 项缓存检查通过，包含大往生与绊地狱分别生成／命中且互不改写、未知 ID 拒绝。第一轮 linked-container 的错误分类断言未通过，调整为先检查字面链接路径后，第二轮完整通过；保留两份结果。53 项既有原生检查及新增标题存档自测通过，覆盖 CUSA32997／CUSA09062 的目录分离、当前标题限制、初始化期间禁止切换和非法标题拒绝。322 项源码测试及源码审计通过。仅使用自造数据，不算大往生实际保存或游玩通过。

新增显式合并更新布局，默认仍要求本体；只在 --app-dir 配合 --merged-app on 时接受 gp 分类的完整目录，不允许叠加第二份 patch。60 项原生／配置／自造内容检查通过，包含原有 19 项内容语义及 4 项合并布局检查，原资源哈希不变。首次合并 CLI fixture 错用了不受支持的导入类型，修正自造 fixture 后第二轮通过，不涉及真实 ROM 修复。

明文 SELF 的 Python／C++ 读取实现仅接受未压缩、未加密段及精确的可选版本注记尾部；加密、压缩、重复／越界映射、缺段及截断均拒绝。43 项编译后 ELF 准备／路径对照检查及 4 项 Python 用例通过。实际大往生 eboot 使用随包 libc 验证单字节 _init_env RET，C++ 与 Python 准备输出完全相同，SHA-256 为 a82a03e87d5b2877341cb7598d2ccf7b387aea6e25bd238ee5ac24b7ebf8e7a6。源文件未改，未执行客体入口。

六程序静态绑定盘点共 1,216 项导入，其中宿主已有 368 项、模块导出可提供 553 项、未绑定 295 项（包含重复及未执行路径）。未调用模块初始化或入口，不能把可解析符号数量当作语义兼容；后续结合实际启动路径适配，不将其他作品的 NID／内存地址直接套用。盘点结果在 .local/analysis/daioujou-bindings-r2.json。SELF 格式参照本地固定 shadPS4 529ceb32 的 core/loader/elf.h 与 elf.cpp，读取代码由本项目独立实现。

首次大往生原生入口使用独立 `.local/daioujou-entry-r1`、离线、单目录合并数据和共享驻留 GPU；已执行 `_init_env`、构造器及显示事件注册，实际停止在 libc `bcmp`（调用点 eboot+0x1f29ec），未到可操作菜单。原始数据与其他作品存档不变。待绑定的接口按静态盘点分组处理；不以这一轮初始化进度代替实际选人／进关验收。

后续独立入口按真实调用修复 bcmp、受控堆 calloc、sincosf 及 Sony sprintf_s 语义；新增接口分别有原生 ABI／边界用例，sprintf_s 的尺寸不足返回 0 并清空缓冲，不调用 Windows CRT 的终止型无效参数处理。原生汇总 64／64、源码 327 项及源码审计通过；格式器 33、受控堆 10、数学 6 项均通过。使用显式后台诊断后，直接测试入口不再同步逐帧写日志；不以菜单约 16.7 ms 的出帧代表完整游戏性能。

`.local/daioujou-entry-r6` 离线、普通输入（诊断延长 0）实际经过标题 → Direct Menu → ARCADE → V 投币（CREDIT 01）→ Return 开始 → Type-A → Shotia，随后自然 Game Over，原生侧栏显示 420 分并出现游玩进度提示。其间未持续操作，玩家第一关画面尚需直接观察；后来正常演示的第一关建筑、敌机和爆炸可见，但演示不是玩家验收。新进度提示已确认，最后受控关窗。实际窗口使用本作日文标题，存档配置使用 CUSA32997；尚未做冷重启保存读回、正常合集入口或本作联网，不将这些项目标为通过。

随后 `.local/daioujou-entry-r7` 使用正常准备缓存生成的六个镜像，直接观察到 ARCADE／Type-A／Shotia 第一关：自机、建筑、敌机、子弹正常绘制，C 射击后得分 90，死亡复活及暂停菜单可操作。F8 标记对应第一关实战；CU 短按在本轮显式延长 120 ms，不能冒充实体输入结果。游戏声音设置从 BGM 70% 改为 69%，退出设置页后原生日志确认 0008-0000 保存事务 commit；尚待 r8 冷读回。Quick Save 的长按确认本轮取消，不能标为已保存或重启快照通过。

共享准备流程按标题选择六程序、随包 libc 和合并布局，不对大往生应用绊地狱三个资源读取补丁。真实完整清单检查、首次缓存生成及第二次命中已通过；生成阶段不执行游戏。两款 PS4 的运行别名及符号来自同一认证宿主，各证书只包含本作 EXE 和共用 GPU；自造整理用例通过。328 项源码回归、公共 common／launch／source 三项 CTest 及 64 项原生检查通过；准备缓存命中不代替正常启动器实战或联网验收。

`.local/daioujou-entry-r8` 使用 r7 的独立存档副本冷重启，进入 ARCADE → 暂停 → Option Setting → SOUND，实际显示 BGM 69%，原生设置保存／重启读回通过，最后受控关窗（27）。本作静态 NpScore 引用、七筛选榜单、传输分值单位及原生资格条件已记录为社区接口参考；未接入本作客户端和服务，联网能力仍关闭。

PS4 HTTP 客户端现显式携带标题身份，检查各自的 endpoint、Title ID、服务版本、昵称映射域及录像格式。绊地狱保留原 account ID 向量，大往生同昵称使用独立映射；后者不复用绊地狱 whole-round 恢复或原生录像验证器。新的大往生服务 profile 复用原存储／HTTP，并隔离数据库 application_id 和 Title ID，49 个配置榜单中 CUSTOM 只读，成绩上限为原生 999999999，暂不广告或接收录像。

实际 Windows HTTP 的独立 loopback 检查两款均通过：`.local/ps4-community-checks/v03-title-profile-r1/result.json`；仅使用自造数据，大往生测试整数 123456789 完成提交、综合榜及本人读回、较低分拒收，不是原生玩家成绩。5 项新服务 profile 用例、原绊地狱服务 24 项兼容回归、334 项完整源码回归通过。原生检查的 r2 为 64／65，新增 client 检查漏传目录参数，修正 runner 后 r3 为 65／65；保留失败结果。源构建的首次 string_view／JSON 比较歧义已改为显式读取字符串，成功构建与收据另计。

大往生录像静态定义为 0x65600 字节、数据区 0x65000、起点 0x500、尾部 0x100；脚本容器版本为 0x10030，可识别旧容器版本，但 core data_version 由模块另提供，尚待真实记录核对。不能复用绊地狱的 0x80000／0x10001 校验条件；当前客户端原生 Score bridge 仍拒绝大往生 attachment，合集能力不开启。尚未做正式服务部署、原生成绩／榜单／录像闭环或 v0.3 打包／上传。

原生 Score bridge 已支持独立标题参数，9 项大往生自造原生检查通过：原生分值不再次缩放、综合榜 6、CUSTOM 与越界分数拒绝、他作 account receipt 不写排名、本作成绩回执及榜单读回、请求清理。完整原生检查 66／66、双标题 Windows HTTP 2／2 通过，现场后缀 `v03-native-score-profile-r1`。运行时配置入口仍不开放大往生 provider，先完成录像验证与真实菜单闭环。静态反汇编已找到本作 getEmuVersion 的注册和返回 0x10000 的叶函数，仍须对照真实录像的 data_version，不将静态事实标为 native recording verified。

录像结构读取现按标题选取准确长度、数据区和双版本，新增大往生的综合／六角色及模式元数据校验；typed HTTP worker 使用配置标题，不通过字节数猜游戏。11 项自造双标题检查与 66 项原生机制检查通过（现场 `v03-layout-r1`／`v03-replay-layout-r1`），覆盖正确本作结构、角色／分值单位不符、错误 core／容器版本、他作记录和压缩尾部拒绝。原绊地狱录像 23 项既有用例及 whole-round 35 项自测通过；先前沿用脚本的依赖检查仍指旧工程，改为本仓库 PS4 路径和工具锁后完整通过。自造数据不可游玩，不能代替实际本作录像读回；本作 provider、服务录像及合集能力尚未开放。

独立 r6 的 0020-0000 原生保存实际已有 21 个 0x65600 槽，前两槽是同一局 420 分录像，记录 SHA-256 为 b449a812c64df8b32459670279ce648719307e2dae97f5a759b8178afce479d1。只读提取到 `.local/daioujou-native-record-r1` 后检查通过：容器 0x10030、core 0x10000、ARCADE_01000、原生分值 42、角色 0、到达场面值 5、4079 帧，快照 457343 字节／404 块，7 个输入页。到达场面值不解释为第五关。结构及 core 值现在有真实记录证据，但未执行这一录像的原生回放。

这一记录纠正了把当前容器版本限定为 ARCADE_01030 的假设；显式允许输入实际附带的四个 core revision，未知 core 仍拒绝。修正后真实记录连同综合榜 6／分值 42 的元数据检查、15 项自造录像检查、66 原生检查和 335 项源码检查通过。服务端也完成相同有界结构与回执元数据校验，6 项 profile 测试通过，原始测试存档未改写。

原生 provider 的代码路径现可使用本作 protocol；未验收标题仅允许显式 development loopback opt-in，并保持原 INI 联网开关、精确内容 Title ID 及 literal 127.0.0.1 检查，正常 configured 启动不绕过 capability。DaiOuJou 不启用 Ketsui whole-round。随后 `.local/daioujou-network-r1` 的真实联网游戏启动命令被执行策略拒绝，游戏没有启动，也没有换路径重试；独立服务虽已启动，但数据库仍为 0 成绩／0 录像，随后停止并保留现场。不能把该现场或先前 fixture 写成游戏内联网通过。v0.3 仍未验收／出包／上传或通知群。

## v0.1 最终验收结果

发行源码为 `f652df0c316b4afcba7ae8f04a9d0c025ec368c6`，标签 `v0.1` 已推送。三款运行组件、启动器及其符号均在该提交重建并核对，360／PS4／启动器分别为 45／6／2 对程序与 PDB；240 项源码回归、50 项 PS4 原生／配置检查通过。以下为最终文件身份，后续文档更新不改写这些 ZIP 或标签。

| 发行文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| CAVE-PC-Collection-v0.1-internal.zip | 2,975,097,397 | 1c300b7b71bd610b9e44ef81ccee18cc01ee43391684ffd395b8722e2782ce88 |
| CAVE-PC-Collection-v0.1-public.zip | 56,907,517 | 857dfd0cf8f6636818f7351b2bbc8839b5f52898aadea7ceaf4c61ffd4f89b00 |
| CAVE-PC-Collection-v0.1-source.zip | 284,152,283 | fac24982d88d1e6351a6de48e6f7bfdc0f755d1134f85d589714888df4b24653 |

最终 Internal／Public 分别在新的中文空格目录实际解压，2571／123 个清单文件校验通过。Internal 完整数据核验为虫姬二 52 项本体、7 项 DLC，G2 155 项、绊地狱 59 项，三款在实际启动器中均为可启动；独立 source Git 干净。Public 无数据时可正常打开列表且三款均报告缺数据、运行组件无缺失；最终包仅提供虫姬二数据后实际到达标题，G2-only 实战与绊地狱-only 准备／菜单沿用前述候选包证据。

三款各一个代表模式进关、保存与重启读回、共享配置／全屏和最短联网成绩／录像流程已完成或明确沿用可靠结果。虫姬二 RC2 `run-20261019T043252588Z-74072` 实际 Black Label／Arcade／Original／Reco 进关、自然结束和 No.1 录像保存，正式 HTTPS 的两个 view 成绩与录像上传均 HTTP 200；G2 正式联网与冷启动网络回放沿用已验收基线。绊地狱启动器实战及普通输入冷启动回放见 P5 专项记录。不把本轮短测扩写为所有模式、角色、实体设备或长期稳定性通过。

另在全新英文目录独立 clone 发行提交，显式从缓存恢复并校验本目录工具及 19 个 PS4 依赖，执行 `Build.ps1 -Games ketsui -Jobs 2`：GPU 374 项、原生 549 项和公共／启动器 18 项编译完成，运行文件整理和 6／2 对符号校验通过，无需 ROM、未自动启动游戏。新产物的输入诊断自测及 common／launch 两组 CTest 通过；此前包内源码 pull、独立公共编译结果继续保留。

单独源码附件含同一提交的独立主 Git、25 个 SDK 及 19 个 PS4 固定依赖源码和构建说明；37,041 个文件的 ZIP 成员集合与逐文件 SHA-256 全部核对，已知 ROM／DLC 内容匹配为 0。用户选择按需提供该附件，Public ZIP 仍不放 ROM 和源码，不改变私有仓库访问权限。

已知限制保留：绊地狱主动关闭仍是退出码 27，客体／宿主完整终止未完成；启动器只依据本次进程显式关闭收据识别受控退出。开发构建需短英文源码路径，玩家中文路径已验证。偶发音画卡顿、实体设备及扩展模式覆盖继续跟踪，编译器统一和虫姬二／绊地狱帧节拍专项在下一版本实施。下面 RC1／RC2 和较早构建的章节保留各自时点的历史证据，当前发布结果以本节为准。

## v0.2 最终验收结果

发行源码为 `562e01944ffab219dc64e2a9eb17921548d768b3`，标签 `v0.2` 已推送。四款组件与启动器均在此提交配套构建，360／PS4／启动器分别为 51／6／2 对 PE／PDB；309 项源码检查通过。最大往生的可玩接入、个人榜单／录像、商店进度和发行验收完成，实际覆盖如下。后续文档提交不改变冻结的 ZIP 或标签。

| 发行文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| CAVE-PC-Collection-v0.2-internal.zip | 6,164,389,090 | 968edcdcdcd5a86df6232274b5bb79dbcf4707d2dedb1642129b615b0d2db13b |
| CAVE-PC-Collection-v0.2-public.zip | 68,206,836 | 13b5402bd9b5770477147a658ec8e860461bd4ef8c2ed2adab4491b772181ea7 |
| CAVE-PC-Collection-v0.2-source.zip | 284,580,215 | 166d090a1ef08f4a9b598c9eb90084258eda745a18109cad57e555c78ac0e504 |

| 项目 | 当前状态与通过条件 |
| --- | --- |
| 最大往生输入与构建 | 本体全部文件与 ISO 哈希一致；正式三模块生成、宿主构建、配套 PE／PDB 及 runtime 整理通过。已修复查明的遗漏回调和菜单纹理兼容，四主模式实际进关；当前基线为光盘本体，不混用 TU，音乐补丁暂缓 |
| 合集启动与配置 | 正常 GUI 选择 SDOJ、启动、玩家第一关、正常退出恢复启动器和再次启动通过；共享 INI／联网／独立存储与日志已核对。7ac0e67 实际验证全屏启动、F11 和 Alt+Enter 双向切换，不调用另一套独立程序 |
| 实际游玩 | 四主模式玩家入口、Arcade HD／Ver.1.5 自然局末与返回、Arcade HD 录像保存通过；9,286 分下载录像在新进程离线完整播放至自然返回。更多声音／角色及长期稳定性为扩展覆盖 |
| 最大往生社区／Live | 正常合集 GUI 启动后的 Xbox 360／Shot 10,866 分已实际提交正式 HTTPS；游戏内读榜、正确日期、录像上传／下载及空槽保存通过，下载文件与服务原字节哈希相同。本机 9,286 分下载录像的离线冷启动完整回放证据沿用；不将团队评分混作普通个人榜 |
| 本体附加内容 | 以自然游玩所得 10 金币购买第一张壁纸，退出商店自动保存后，新进程读回 0 金币及 SOLDOUT；未赠送金币或改解锁标记。窗口直接关闭不保证写入尚未保存的修改，音乐补丁暂缓 |
| 既有三款回归 | 按用户“别测太繁琐”的要求，沿用下节统一工具链的三款完整短回放，不再重复实战。7ac0e67 补充虫姬二本体／DLC 菜单与旧录像列表检查，未重新播放；机制测试另记如下 |
| 四游戏发行 | 同提交配套重建完成。Internal／Public 实际解压，2,738／128 个清单文件验证通过；内部四款及虫姬二 DLC 完整，对外无 ROM／源码且四款无运行组件缺失。Public 实际 SDOJ-only 启动与正常关窗通过；无 ROM 的对应源码附件完成 |

两种 ZIP 在新的中文空格目录实际解压。Internal 全量数据检测为四款 ready（52／155／59／114 项资源，另含虫姬二两份 DLC），独立 source Git 身份与干净状态通过；73 个玩家程序／运行库文件与 Public 逐项相同。Public 无数据时实际打开启动页、提示缺少数据；仅指定 SDOJ 外部数据的 `run-20261019T142031204Z-80152` 显示片头及跳过后的标题过渡，正常关窗，sourceCommit 正确、sourceDirty=false、共享联网默认开启。未在最终包重新跑关卡，将已接受的游玩证据明确沿用。

源码附件包含同一提交的独立主 Git、25 个 SDK 和 19 个 PS4 固定依赖源码；37,084 个 ZIP 文件的成员集合、大小及 SHA-256 全部核对，不含 ROM、生成游戏正文、工具安装或玩家现场。本轮未重新执行独立全量开发构建，开发恢复基线沿用 v0.1 证据。最终 Internal 的真实启动器截图另存本地，不纳入源码或压缩包，也未上传／发群。

大往生和 ESP Ra.De. 保留 Coming Soon。SDOJ 团队评分、音乐补丁、扩展角色／模式及长期稳定性不在本次通过范围；绊地狱退出清理、偶发音画停顿和开发源码中文路径限制继续保留。冻结的 v0.1 文件和验收结果保持不变。

### 本轮收尾检查

7ac0e67 已配套重建四款组件和启动器，360／PS4／启动器分别核对 51／6／2 对 PE／PDB。`integration-r1` 的 `run-20261019T124255118Z-85596` 验证全屏切换、9 金币不足时购买被拒绝，以及 Arcade HD／Type-C／Shot／Auto Bomb OFF 自然结束取得 1 金币。首次购买后直接关窗未触发存盘，冷启动仍为 10 金币、商品未购买；这项对照未被改写为保存通过。

`run-20261019T130215038Z-63588` 用同一份正常进度重新购买，原生退出商店后 SYSTEM.DAT 更新，SHA-256 为 `beeac1d365b7bcca1a4ddd19347b3398ef0dcb7fe1c8963a4c33398fabc1baa2`。随后 `run-20261019T130904660Z-86332` 冷启动实际显示余额 0、第一张壁纸 SOLDOUT，购买／扣款／保存读回通过。未验证全部商品或壁纸应用组合。

虫姬二 `run-20261019T131320847Z-85976` 实际进入 Black Label／Xbox 360 菜单及旧录像列表，Ver1.01 入口可见，正常关闭；不据此声称再次进关或完整回放。九组定向原生检查中，common、launch、同步桥接、通用社区及 SDOJ 的八组通过；Futari native fixture 首次报 `native view batch results missing`，同一程序单独复跑通过（5.03 秒）。原因未确定，保留为偶发测试失败，未声称代码已修复。已有 309 项源码测试和三款旧录像证据沿用，不再扩展场景。

## 后续开发：统一工具链与日志开销

开发工具已统一为同一份固定 LLVM 23.1.3。Xbox 360／启动器使用 clang++，PS4 保留 clang-cl 及自己的 ABI／优化参数；CMake 4.2.3 和 Ninja 仍由合集工具锁恢复。旧版本工具、原项目与 v0.1 发行物保持不变。

完整重建采用 `fa89a5d` 的源码：SDK 752 项构建任务，联合 360 的 387 项，PS4 GPU／原生的 374／549 项，以及公共组件和启动器均完成。SDK／360／PS4／启动器分别核对 3／45／6／2 对 PE／PDB；360 的 18 组 CTest、PS4 的 50 项机制／配置、5 项 GPU 自造场景、36 项镜像、95 项资源和 21 项缓存检查通过。此次迁移中发现并修复了 CMake 更换编译器后重置缓存、丢失 Release／SSSE3 等显式参数的问题；配置入口现在重新应用并逐项核对请求值。

代表性实机回归使用存档副本 `toolchain23-r1`，关闭社区，不重复联网或全模式验收：

| 游戏与现场 | 实际结果 | 短区间帧时间 |
| --- | --- | --- |
| 虫姬二 `run-20261019T061412730Z-78700` | Black Label／Xbox 360 的 No.2／Reco／Original 旧录像进入第一关、自然结束并回到列表；关窗恢复启动器，无错误级日志 | F8 内完整的两秒汇总窗口：guest_submit 1,085 个间隔，最大 17.463 ms；host_present 1,087 个间隔，最大 17.477 ms，均无超过 20 ms 的记录 |
| G2 `run-20261019T062056996Z-81160` | No.1／Black Label／Seseri 旧录像完整播放并自然返回，dat15 加载和卸载配对；关窗恢复启动器，无错误级日志 | 同法取完整窗口：guest_submit 1,688 个间隔，最大 18.890 ms；host_present 1,569 个间隔，最大 18.727 ms，均无超过 20 ms 的记录 |
| 绊地狱 `run-20261019T062424359Z-53084` | ARCADE／TYPE-A／380 分／00:53.45 旧录像自然播放至结束，返回 My Replay；普通输入诊断为 0，关窗有受控退出收据 | 单调时钟区间 `8555123841294–8555143841294`，1,200 个间隔，平均 16.666 ms、p99 上界 18.8 ms、最大 19.915 ms |

上述为关卡及局末的短回放证据，不包含新玩家局、全模式或实体输入重测。360 的汇总窗口带边界余量，不构造跨窗口全局百分位；三个短测极值均不是输入响应时间，也不是后续所有场景的硬上限。

### 绊地狱诊断写入优化

旧进程中曾测到提交线程的前帧日志路径耗时 56.8／100.7／181.7 ms，分别落在 66.4／116.3／200.0 ms 的完成间隔内。记录范围含菜单与回放，墙钟耗时也包含调度，不能将这些样本全部归为关卡磁盘卡顿。针对已明确处于提交路径及音频图锁内的同步写入，新增有界后台队列，保留完整记录、累计损失／失败与退出排空状态。

256 项源码测试和更新后的 53 项 PS4 原生检查通过，包括阻塞写入仍可投递、满队列保留尾部、多线程整条记录、写入／刷盘失败和两种终止路径。实际候选程序 SHA-256 为 `70d10aa669801b0efbb75d39b5448a36f9ccb999085bf77e2cf8285b271af6f2`，图形 DLL 与优化前相同；此候选包含日志维护改动，原始构建收据明确记录 sourceDirty=true。

`run-20261019T065405228Z-83520` 从同一 profile 再次播放相同 380 分录像，观察到第一关、续关倒计时和自然返回列表，无人工中断。下表各取同一录像内 20 秒，非逐帧对齐微基准；主要验证日志开销移出提交线程，不以微小极值变化宣称全部卡顿修复。

| 指标 | 同步日志基线 | 后台日志候选 |
| --- | ---: | ---: |
| 分析单调时钟区间 | 8555123841294–8555143841294 | 8556758289275–8556778289275 |
| 完成间隔样本 | 1,200 | 1,200 |
| 平均完成间隔 | 16.666 ms | 16.667 ms |
| p99 直方图上界 | 18.8 ms | 18.3 ms |
| 最大完成间隔 | 19.915 ms | 19.721 ms |
| 每帧诊断及前帧记录平均耗时之和 | 0.933 ms | 0.0152 ms |

候选区间的 NGS2 15 个样本最大 render 为 26 μs，未见图锁等待，观测到的 starved_grains 为 0；它不是扬声器实际发声或全程音效验收。进程累计接收并写出 136,494 条记录，竞争拒绝 102 条、满队列淘汰／超长／格式／写入失败为零。后台单次写入最大墙钟耗时为 348.335 ms，属于整个进程，不定位为该关卡片段的停顿。最后状态队列与 in_flight 均为零、drain_completed=true；终止记录仍为完整 JSON，受控退出码保持 27。

虫姬二短区间当前稳定，未额外改动等待、音频逻辑或游戏速度。更多场景的偶发音画卡顿继续保留观察；下一步按相同录像与设置做发行候选轻量回归，不展开孤立毫秒极值调参。

## 后续开发：启动器选择与快捷设置

开发启动器的横幅仅选择作品，底部唯一的启动按钮操作所选游戏；卡片不常驻显示校验结果。快速检查只读取当前选择，完整数据／运行组件核对仍由 PrepareLaunch 完成。自审包含 1080×900 原生绘制预览、小窗口滚动与宽窗口，Coming Soon 保持明确文字及禁用启动。

实际 CU 在根目录测试全屏与昵称 `Launcher-Test`：保存只改变共享 INI 的两个目标值，关闭／重开后控件读回一致；Tab 从页面进入全屏和昵称控件。另在没有 ROM／runtime 的独立空目录打开启动器，所选游戏显示缺数据、统一启动禁用，点击另一个横幅只更换选择。原根配置已按备份哈希恢复；这些是界面与配置证据，不替代新的三款进关或联网测试。

原生公共测试补充共享默认读取、多游戏继承、游戏级覆盖保留、BOM／大小写／换行／注释与空白保留、缺节／键插入、旧表单冲突拒绝，以及非法昵称不产生部分保存。数据目录编辑复用同一原子写入实现，已有 launch／data-copy 检查继续覆盖。124 项公共原生断言、common／launch CTest 及 256 项源码回归通过。

### 最大往生输入预检

已对照固定上游源码复核 13 个本体／补丁／TU 哈希。8 项自造输入检查覆盖光盘与 GoD、缺更新、混用更新、未知程序、容器仅作为候选、忽略无关资源、参考表格式和报告输出边界；统一源码回归共 264 项通过。

真实旧数据的 default.xex、CA022100.bin、CA022110.bin 均匹配光盘版参考，三份 XEX 头的 Title ID 为 435A07E7；两个模块均使用 0x88000000。未找到匹配的 TU，报告为 missing_or_unrecognized_update。音乐补丁按用户要求暂不采用。识别与头部报告保留在 .local/analysis/sdoj-local-*；本体资源与静态检查的后续结果如下，不涉及应用更新或游玩验收。

### 最大往生本体资源与静态兼容

本地 ISO 只读目录盘点得到 116 个文件；对每个 ISO 文件范围和原解包文件分别计算 SHA-256，大小／内容全部相同，共 3,285,901,225 字节。其中游戏数据 114 个，另有两个 $SystemUpdate 文件。报告为 .local/analysis/sdoj-disc-content-20261019-r1.json；不修改原目录，不把全部附带工具、音乐补丁或 ISO 文件自身当作运行资源。

共享静态检查实现已移到 backends/xbox360/diagnostics/inspect_xex.cpp，G2 保留兼容命令名。270 项源码检查及审计通过；NativeOnly 实际重建检查工具，11 组非 source CTest 通过。新增 6 项自造数据用例覆盖 SDOJ 身份／入口、.bin 生命周期及区段合同、三个程序的头部及未知输入拒绝，不使用真实 ROM fixture。

.local/analysis/sdoj-base-census-20261019-r1 中三个程序分别在独立进程静态装载：主程序导入 245 项，CA022100／CA022110 各 45 项，未注册均为 0。两个模块各三次装卸载、相同基址重叠拒绝、主程序分配不变通过；GPU／音频／输入未初始化，没有执行客体。SDK 的接口注册不等于实现行为全部验证。

原始本体程序的独立代码生成试验在 .local/analysis/sdoj-base-codegen-20261019-r2 完成，主程序及两个模块各产生独立输出；没有导入上游应用声明、使用 TU／音乐补丁或编译／运行游戏。前一个 r1 保留为失败：生成器不能直接识别本次中文源路径；r2 使用逐个哈希校验的英文路径副本。公共生成器的 UTF-8 路径处理、目标版本及正式接入仍待完成，不将此试验计为 v0.2 可玩通过。

后续修复了代码生成器中把 TOML UTF-8 字符串当成本地代码页路径的问题，涵盖主程序／模块、game_root、include 和输出。SDK 实际增量重建 8 项，3 对程序／PDB 配对通过。.local/analysis/sdoj-utf8-codegen-20261019-r1 从原中文目录生成到中文／emoji 输出，引用中文／emoji include；76 个 C++／头文件与 r2 逐字节相同。ROM-free 原生路径检查另以无效 XEX 验证中文输入／include 已到达静态装载器并被拒绝；额外非 BMP 数据根试验失败保留，不宣称完全 Unicode 支持。

维护中的 data.json／inputs.json、三模块生成声明、CollectionApp 宿主及 Build／Generate 入口已接线。275 项源码检查及审计通过；没有将此写成宿主构建或实际进关。六条目录项中 SDOJ 仍 integrated=false，默认 communitySupported=false；其余三款保持支持，共享 INI 不添加联网覆盖。实际输出、离线 profile 和新运行日志继续由后续构建／实战验证。

7fadbb9 的首个实际宿主构建完成 113 项编译／链接、11 对程序及 PDB 配对和 13 个 runtime 文件整理。common、launch、content_source、codegen_paths 四组无 ROM 原生检查通过。直接运行共同宿主的现场 run-20261019T082546643Z-75424 中图形／输入初始化完成，run.json 明确记录 communityRequested=true、communitySupported=false、communityEnabled=false，没有启动网络 worker；随后在未注册的主程序入口 0x825B6F48 退出，没有得到菜单或进关证据。

该地址的三个指令为已引用的初始化转发入口。静态核对主程序共 29 个可调用转发入口后加入维护声明，并在重新生成映射中确认 fatal 地址已注册；不将缺注册修复的静态证据当作实战通过。候选扫描另排除五个依赖非易失寄存器／帧状态的异常处理尾部，新增自造指令回归，避免一并误注册；两个模块当前没有额外的此类转发候选。

4e06a8e 的增量构建完成 47 项及 11 对程序／PDB 配对。run-20261019T083423628Z-62484 已实际越过初始化转发入口，随后在 0x824982C0 缺注册处退出；仍未进入菜单。独立反汇编和有界控制流扫描确认这是 136 字节的无栈比较函数，包含循环和两处返回，与 G2 已支持的缺发现类型相同。已补齐此入口及另 6 个逐项审阅的数据引用叶函数，重新生成映射通过，实际回归待进行。

新增只读叶函数候选检查只允许有界分支、整数／读取指令及易失寄存器；拒绝调用、存储、帧依赖和超界控制流。它不自动修改源码，也不宣称枚举了所有遗漏入口，代码中合成的函数地址仍需运行日志或另行分析定位。

7cbbcbb 的宿主及符号重新构建完成；run-20261019T084350463Z-78140 已越过比较函数，后在 0x82251C10 未注册处退出，没有菜单证据。该函数含多路虚调用，不能套用“直线转发／只有 blr 的叶函数”边界。已逐项反汇编六个同类包装，分别按 88／244／40／40／40／40 字节声明。CFG 检查区分 CTR 未初始化／已初始化的到达状态，只有所有进入 bctr 的路径都显式设置 CTR 才列为候选；链接 bctrl 仍拒绝。实际修复结果待新构建验证。

93de541 的 47 项增量构建、11 对程序／PDB 与运行文件整理通过。run-20261019T085135538Z-83800 已越过条件尾调用并记录 WASAPI／48 kHz 音频设备初始化，随后在浮点入口 0x8217F850 缺注册处退出；未观察到菜单、实际出声或玩家进关。已按只读接口指针组集中审阅 21 个额外入口，涵盖浮点、成员写入和索引虚调用，按返回或已注册尾目标确定入口片段范围；帧依赖的异常表片段不纳入。维护声明合计 63 个，新生成／构建和实战待验收。

81fd6ce 的重新生成／47 项构建和 11 对 PE／PDB 通过。run-20261019T090549572Z-38532 实际显示片头，但标题 UI 纹理被 GPU 拒绝，跳过片头后黑屏；这轮正常关窗，无新缺注册崩溃。下一轮使用同一程序（SHA-256 43f702f44b502aee89a6f8e9a38055795d3331a5e72fc7a727d88c445dbf2941），仅在命令行启用 gpu_allow_invalid_fetch_constants=true：run-20261019T090707327Z-83504 的标题、存档创建提示、主菜单和 Xbox 360 模式均可见，确认 Shot 类型后实际进入玩家第一关、观察到敌机／弹幕和分数变化，再暂停并正常关窗。独立 profile 的 SYSTEM.DAT 已落盘，音频日志有非静音样本；未验证重启读回、录像、声音响应、其他模式或实体输入。模块 ca022110.bin 原生装载，另有查询 ordinal 4–6 不存在的日志，尚未据此断言功能受损或无害。

现将纹理兼容开关放入 SDOJ 专有的预初始化钩子，其他作品和共享配置不改变；下一轮须不带该命令行参数重测。光盘本体清单另显式拒绝三个自动更新伴随文件，生成与原生检测同用 forbiddenFiles 元数据，防止 SDK 静默应用未匹配更新；自造数据检查与实际新构建结果分别记录。

d7c4058 的 288 项源码检查、28 项宿主增量构建、11 对 PE／PDB 及四组无 ROM 原生 CTest 通过。run-20261019T091412944Z-83532 不带额外 GPU 参数，日志确认 SDOJ 纹理兼容默认启用；重启进入主菜单，无重新创建系统存档提示。经 Arcade HD／START／1P START／Type-C 与 Shot／Auto Bomb OFF 进入玩家第一关，观察到敌机与弹幕。此后在局末附近 0x880943E8 缺注册处退出，不将这轮写成局末、死亡复活或录像通过；SYSTEM.DAT 为 630,704 字节，读档前哈希 d00cb588bc877d5716760ce690a57146e5435701895481592eeac4dc54458a1d。

独立反汇编确认 CA022100 的 0x880943E8 是一条 b 0x88098520，CA022110 的对应入口 0x880952F0 是 b 0x88099390；目标实现已正常注册。这两个地址分别在 0x88094C94／0x88095B04 由 lis/addi 构造后存入回调槽，不在原 .rdata/.data 指针扫描范围。已分别加入各模块的维护声明；生成和实际局末回归待进行。工具新增保守的直线常量构造引用，遇调用、分支或未知写入即丢弃，不自动注册所有候选；邻近异常继续点也可能被报告，仍需人工审阅。

749cd9b 的 292 项源码检查、31 项增量构建、11 对程序／PDB 及四组原生 CTest 通过。run-20261019T092402477Z-46520 使用相同独立 profile，经 Arcade HD／Type-C／Shot／Auto Bomb OFF 再进第一关并自然 Game Over，越过先前崩溃点到结算页；选择保存并在空白槽位 1 完成写入，界面显示 2026/10/19 17:30、Stage 1、0 分，随后回到模式待机。REPLAY_XB01.DAT 已落盘 79,328 字节、SHA-256 bfee320a5fc3ed55832c9b64f094538294b7b5c0138713c79630fdb61f3fdab3；SYSTEM.DAT 更新为 b39760bd4f944f25fe9a963bdd60ca0b331920d1b4a84d08966722cad0f0b773。录像跨进程读回／完整播放仍待测，不因保存成功提前通过。

v0.2 打包按版本固定四款集合和三款 360 联合收据；v0.1 仍保留原三款档。新增自造 Internal／Public 双包与解压检查包括 SDOJ，Public 不接受其数据和源码，Internal 的 SDOJ 仅包含资源清单，不递归复制 ISO、音乐补丁或工具。295 项源码检查通过，新校验器实际只读核对冻结 v0.1 Public 的 123 项文件通过。这些不是新四款游戏包的实际验收。

同一 749cd9b 游戏进程继续从 Arcade HD 返回主菜单，切入 Ver.1.5／Type-C／Shot／Auto Bomb OFF，经第一关到续关倒计时和自然结算，再选择不保存第二份录像并返回；未重新执行额外的重复进入循环。之后切入 Novice，实际确认相同角色／类型并观察到第一关玩家自机、射击、敌机及分数变化。进入不同模式时，CA022100 的装卸载配对可见。所有角色／类型、全关卡、Training／Challenge 和实体设备不属于这次短测。

Arcade HD 关卡的 17:27:40–17:27:53 日志选区中，guest_submit／host_present 各 7 个完整汇总窗口、843／845 个间隔，最大 17.403／17.483 ms，超过 20 ms 为 0；不计算跨窗口百分位，不将这个短区间结果当作全程保证或输入延迟。

d0904f9 启动器单独重建 15 项及两对 PE／PDB 通过；原生预览 .local/sdoj-six-banner-preview-20261019-r2.png 中六张卡片在 1080×900 完整显示，标题／Coming Soon 与底部按钮不重叠。SDOJ 官方方形标题图改为完整缩放显示，不再放大裁掉标题；后续仍可改善其宣传图选材。此为视觉自审，不是实际 launcher 启动 SDOJ 的证据。

### 最大往生社区接线与原生商店

社区适配正在进行，仍不宣称可用。独立审阅本体的五个静态 XDK 边界：0x8221C418 排名估计、0x8221C188 录像上传、0x8221BEA8 下载、0x8221C5F8／0x8221C690 本人／他人路径；只在这些 API 边界转入公共桥接，保留游戏自身的参赛资格、成绩构造与确认。82156F10 的成绩写入／flush、82157128 的估计／录像上传和部分榜单读取使用空 OVERLAPPED 的同步调用；现有桥接只支持异步，需补同步兼容并验证取消及旧异步路径，不据此照搬 G2 的属性表或录像格式。

749cd9b 的 run-20261019T095707650Z-13132 实际重新启动并进入 Shop，金币从此前自然游玩存档读回为 3；壁纸、GAME OPTION 与 SOUND 分类可见，GAME OPTION 中有自动 Hyper 等辅助项目、Semi-Auto／Full-Auto Maneuver，以及标价 3000 的隐藏项。未购买、增发金币或改解锁标记，也未据图标推断隐藏项的角色。开发者访谈说明此商店使用游戏内获得的点数，不采用游戏外付费（https://dengekionline.com/elem/000/000/626/626186/）。用户要求保留这种免费游玩解锁；此次只证明商店展示与金币读回，不将其记作购买／隐藏角色／购买后重启验证通过。检查后正常关窗，无新增崩溃。

9a9b68d 的同步兼容已完成构建：SDK 11 项增量／三对 PE-PDB，SDOJ 14 项增量／12 对 PE-PDB 及 13 个运行文件整理通过。296 项源码检查通过；common、launch、content_source、codegen_paths、community_sync 五组原生 CTest 全部通过，共 0.97 秒。community_sync 使用自造数据与假 provider，在真实 SDK 内存／内核中验证同步成绩读写、flush、枚举计数、排名估计、录像上传／下载、错误时不写输出、原有异步完成合同，以及同时取消两个同步请求后的迟到回调不再写 RAM。没有访问 HTTP 或执行游戏，不是 SDOJ 联网验收。

本次宿主 SHA-256 a3200d658dbb17fe24367c7985c9da5bf111e2c81ce95feb0d7e4f24d31a2ca8，公共运行时 a9876fff10180452662c194ffb9216f9c4a812f8a1b819d0dd21cffb0857ff94；尚未用新运行时重测原三款实战。根 config.ini 的临时方向别名已撤回，哈希恢复 0c58f87697cc5126ae91e0a3524e049a9688362144e786f3293ab74d525a1cf7；商店检查没有修改 ROM 或解锁标记。

### SDOJ 原生格式与独立传输适配

3051bc0 的 `run-20261019T101751294Z-84904` 使用显式 native trace 和诊断登录，未创建 provider、未访问 HTTP。实际进入 Xbox 360／Shot 在线榜单，观察 view 23，Laser 为 24；随后 Score Attack（个人）／Saya／Shot 玩家第一关自然结算 9,786 分、Stage 1、Continue 0，获得游戏内 2 币，保存本地 AR01 后原游戏尝试联网并如预期报告不可用。录像为 79,270 字节、SHA-256 `415e838a2349c5a3d6146be0dcde8f98a813f2c41436ecdfd018d9ad31eccf77`。这证明原生调用及本地保存，不是线上上传成功。

本体的 view 选择器、属性构建及录像读写核对结果：普通个人榜共 39 个 view；总分属性 20000004、时间 20000002，其他属性为角色／服装、关卡索引、续关和选项元数据。录像头 304 字节，magic 01304250，+92 保存总长，+8 为加权 BE16 校验；未使用字保持原字节。原生时间把 FILETIME 的低 DWORD 放在前，可能表现为负 signed64，网络和读回均保留原值。网络录像与先保存的本地录像时间可不同，不从本地文件猜测实际传输字节。原游戏上传顺序为完整成绩、录像、10008011 大小单字段确认；最后一步只确认已经关联的附件，不生成另一局成绩。另一个评分表及其累加语义未按个人榜接受，不将其标为团队功能已完成。

c3d0bbe 的 306 项源码检查和审计通过，83 项无需 ROM 的原生构建成功；18 组 CTest 通过，其中包含 G2 原有 schema／worker、G2 loopback、SDOJ schema／worker 和 SDOJ loopback。两种 loopback 都由测试程序新建随机本机端口与临时 SQLite，检查真实 Windows HTTP、精确 64 位成绩／有符号时间、未排名本人、最高分／估计、成绩先提交、原字节录像关联／下载及 SDOJ 大小确认，不执行游戏或向正式榜单造分。G2 共享 worker 重构的传输回归通过，但不代替原三款实战回归。

22cc51d 的 SDOJ 宿主完成 54 项增量构建，13 对 PE／PDB 核对及 13 个运行文件整理通过；宿主 SHA-256 为 `d36980f570f69dc2ca5a8a6cbc146877900d52806212efb3a1ba3cec6c1de225`。新 C++ 只读检查器通过上述 AR01 的完整头、长度和加权校验，未上传文件。为实机测试准备了独立 loopback 服务，但自动启动游戏的命令被工具策略拒绝，未启动游戏、未换通道重试；只执行过健康检查。测试服务已停止，独立数据库和日志保留在 `.local/sdoj-live-integration-20261019-r1/`。因此本次没有客户端实战联网、正式服务部署或验收通过声明；communitySupported 继续为 false。

### SDOJ 独立服务上线

服务源码固定为 af116e091a804afff16357c089394c54f90c79df，归档 SHA-256 `a40830e4095a047978c3367ac893492a40c132f0426712e2b117db1dfb01aff6`，17 个提取文件逐个与归档哈希相同。Ubuntu Python 3.10.12 上 35 项服务、备份和部署模板测试通过；systemd 单元校验成功，仅提示既有 snapd 单元的未知 RestartMode，与新单元无关。SDOJ 程序目录 root:root／755，数据库目录 cave-community／750，数据库文件 640，服务仅监听 127.0.0.1:18087。

nginx 旧三款 location 和未知路径 fallback 与上线前原文完全相同，新增 SDOJ 后 `nginx -t` 成功才 reload。四款合集入口和两条旧入口均返回各自 Title ID，未知游戏返回 404；SDOJ view 23 的正式 HTTPS 查询为空榜，没有提交自造分数。G2 current 仍指向 60f9d3d，虫姬二／绊地狱／G2 的进程 PID 分别保持 2431692／2491911／2506221。

cave-sdoj.service 与每日备份 timer 均 active／enabled。首次快照 `community-20261019T110000628246-0000.sqlite3` 与运行数据库均为 sdoj／435A07E7／schema 1，完整性为 ok，scores 与 replays 均为 0。部署及检查日志在 `.local/deploy-sdoj-af116e0-*.log`；旧服务和数据库未迁移。本节只验收服务器部署，不改变客户端 capability，也不宣称游戏内联网闭环完成。

0619bd6 的无需 ROM 构建完成 7 项增量。SDOJ 和 G2 的原生 C++ `--check-endpoint` 均通过实际 Windows HTTP 读取正式 HTTPS 榜单／本人状态，分别检查 view 23 和 71；没有执行客体、提交成绩或录像。6 组 community CTest 再次通过；完整源码检查现为 309 项，审计无游戏数据。SDOJ 空榜保持为空，未用 fixture 冒充真实游玩联网结果。

额外评分表 826273F8 另有 40 项，按 8×5 索引选择，ID 为 44–48 和 50–84；26988 状态为 10 的分支使用 score／10000＋10 和 kind 3，而普通个人分支使用原分数和 kind 0。仅做了本地只读反汇编／表核对，尚未取得团队菜单及评分累加的实战证据，因此这些 ID 不进入当前普通个人服务，不把未知类别误称 Challenge 或普通最高分榜。

### SDOJ 原生联网实战与字段映射修复

8a7917f 的 `run-20261019T113130542Z-83100` 使用同一合集宿主、独立 integration-r1 profile 和显式 loopback provider；没有改变公开 capability 或正式服务。Xbox 360／Saya／Shot 的 Score Attack 自然结束于第一关，Continue 0、9,286 分，原生奖励 2 金币；保存到空白录像槽位 2 成功。原生确认流程分别显示排行榜上传完成、录像上传完成，随后在游戏内读到 Player／第 1 名／9,286 分，并完成录像下载。

隔离服务 `.local/sdoj-live-integration-20261019-r3/` 记录 `/scores`、`/estimate`、`/replays`、榜单读取及录像下载均 HTTP 200。view 23 成绩引用为 `6014e02928744b719084b3c7d14539ba`，录像引用为 `c2b0b3f5390645b2acd3856d035f0495`，79,270 字节、SHA-256 `18525a459b88551bf0302b2100d6e152408af77e637825cf8af8eaa9cca33a19`。这是实际客体游玩传输结果，不是 fixture，也没有写入正式榜单。

本轮不能通过完整联网验收：榜单日期显示 1601/01/01，确认下载完成后产生 C0000005。配套 PDB 将宿主 RVA 215B95 定位到 `sub_821479B8` 的 82148D44 界面绘制分支，错误角色编号被用于索引矩形表。原生读取器 8212CB40–8212CBCC 实际将列 1 的 64 位值写入录像头 +32，将列 2／4／5／6 的 32 位值写入 +52／+56／+60／+76，分别是时间、角色、关卡、续关和选项；分数来自 row.rating。此前按写入 property 的低 16 位返回列号，使列 1 变成 0、列 2 变成时间高 DWORD，直接解释了日期和预览越界。

修复仅调整 SDOJ DecodeCommunityColumns 与未排名行类型：时间保留原 signed64 位形，映射到列 1；角色、关卡、续关、选项分别映射到列 2／4／5／6。39 个个人 view 的读取映射、独立非零元数据、未排名行、最高分精度及录像传输检查已补充；7 组社区原生 CTest 通过。新宿主的日期显示、下载后预览／保存和完整回放仍需实际验证，不因传输成功提前开放 capability。

修复提交 `514f079` 已由正常 Build 入口完成三款 360 联合构建：8 项增量、51 对 PE／PDB、三款运行文件整理及启动器收据通过；309 项源码检查和审计通过。SDOJ 宿主 SHA-256 为 `b52a6f911c231f9444b45102d2464e74b3a853388253981999f6b17a95947e2d`。`run-20261019T115428505Z-81796` 从同一隔离服务读回旧进程真实成绩，日期正确显示 2026/10/19 19:36；再次下载相同录像并确认，越过原崩溃点进入保存界面，成功存入空白槽位 3。`REPLAY_AR03.DAT` 为 79,270 字节，其 SHA-256 与服务录像完全相同。

随后正常退出，在新进程 `run-20261019T115948628Z-52060` 不启用开发 provider／诊断 Live 登录，日志明确 community=disabled 且没有网络 worker。主菜单读回 7 金币（上一真实局的 5＋2），本地列表正确读取槽位 3；下载录像自行重现 Saya／Shot、第一关和 9,286 分 Game Over，并自然返回录像列表，全程没有人工游玩按键、快进或退出回放，也没有新崩溃现场。本机原生成绩、榜单、录像收发／保存及离线冷启动回放闭环通过；正式 HTTPS 游玩、商店购买保存、统一启动器和旧三款定向实战／四游戏发行仍待完成。

### SDOJ 正常启动器与正式 HTTPS

构建提交 `1bb60d7` 完成四游戏统一构建，360／PS4／启动器分别核对 51／6／2 对 PE／PDB。正常 GUI 在共享根配置下选择 SDOJ，产生 `run-20261019T121651439Z-80052`；日志确认无开发 provider，communityRequested／Supported／Enabled 均为 true，昵称 Player、API 根为 `https://xiaolu.life/cave/api/v1`。Saya／Shot 的真实 Score Attack 第一关自然结束为 10,866 分、Continue 0，原生奖励 2 金币，录像保存到空槽 4。

游戏显示成绩、录像上传完成，正式 view 23 由空榜变为 Player／10,866／第 1 名；游戏内读榜正确显示 2026/10/19 20:21、Saya／Shot、Stage 1。成绩引用 `e4de28757ef04d7e978d02541db5b7c3`，录像引用 `9da7605f0ee0415099309a842876b20e`。从该榜下载后预览无崩溃，保存至空槽 5 的 `REPLAY_AR05.DAT` 为 79,276 字节、SHA-256 `81e5655a6c225385ad65c2d640ef4da442602400dc2dbf3c26c58a9a122c1192`，与正式服务返回的身份完全相同。此为原游戏自然成绩，非 probe 或人工造分。

正常关闭退出码 0，GUI 恢复，再次经同一启动器创建 `run-20261019T122737538Z-15652`；临时关闭本作联网后主菜单读回 9 金币（7＋2）。本次未重复完整播放新录像，完整冷回放沿用前节同一适配实现的实际证据；商店购买、全屏、旧三款定向回归及四游戏实际发行仍待完成。测试后撤销临时联网覆盖和方向键别名，保留本地 SDOJ 数据路径；没有修改原始 ROM、赠送金币或改解锁标记。common 短回归中仍要求 SDOJ capability=false 的过时断言已随正常接入同步修正，common／launch 两项检查通过。

### 启动器默认高度

默认高度按卡片数量和当前窗口 DPI 计算，外框限制在所在显示器工作区内；内容放得下时隐藏滚动条，小屏幕仍可滚动，宽度不随本次改动扩大。100%／150% DPI、较小工作区和增加卡片四个尺寸检查纳入 common 测试。实际 144 DPI 窗口客户区由 900 增至 1467 个物理像素，六张横幅与底部操作完整可见、无滚动条；common／launch 两组原生 CTest、309 项源码测试及无 ROM 源码审计通过。未以模拟尺寸测试冒充实际多显示器检查。

## 阶段状态

| 阶段 | 状态 | 证据和剩余条件 |
| --- | --- | --- |
| P0 源码基线 | 通过 | 三份工作树快照及文件哈希核对；来源提交、未提交 G2 改动和依赖固定提交已保留；三个原工程的源码审计通过 |
| P1 G2 可玩基线 | 已收尾 | PLAY 六种模式均已选人进入第一关，局末保存和模块卸载返回、本地跨进程回放、网络录像上传／下载／回放及榜单显示通过，帧节拍已收敛。用户完成同进程三次进退并接受模式设置现有覆盖；实体输入和扩展覆盖留在合集回归 |
| P2 合集工程 | 通过 | 原生 common 库和 cave_inspect 可独立构建；五款清单与 G2 完整元数据、INI 路径、数据／运行组件状态、目录联接边界、固定工具恢复及源码审计通过下述回归，不依赖 ROM 构建 |
| P3 G2 合集启动 | 主要闭环通过，扩展回归继续 | SDK、宿主与七个模式 DLL 已独立构建；原生启动器、六种 PLAY 模式进关／返回、本地保存／重启回放、共享配置、异常退出／再次启动及专项分析工具通过。正式 HTTPS 服务、真实成绩／榜单、录像上传／下载和冷启动回放通过；最新联合构建与新宿主回归见 P4。实体输入、扩展覆盖及音画回归继续跟踪 |
| P4 合入虫姬二 | v0.1 代表性闭环通过，专项继续 | 八模块已有入口／卸载记录，Black Label 同进程两次进关、本体与 DLC 冷启动录像通过。关闭 DLC 的本体 Palm 进关、暂停／返回及全屏、启动器恢复通过；G2 公共宿主游玩与旧录像回放通过。本机服务的成绩／录像上传、榜单、下载保存及跨进程完整回放通过，正式 HTTPS 成绩／录像上传已在 RC2 实际局补齐。声音、实体设备和扩展覆盖继续，其他模块重复进入暂缓 |
| P5 合入绊地狱 | 启动器实战、保存及联网闭环通过，扩展项继续 | 固定源码、19 个依赖、宿主／GPU、共享 INI、59 项数据和原生准备已接通。240 项源码及 50 项原生／配置检查通过；镜像／资源／GPU 结果沿用。启动器 ARCADE／Tiger 玩家进关、380 分及正式双榜成绩／录像上传通过；冷启动读回 BGM 69%、榜单 Player 380 分和网络录像下载保存通过。本地新录像跨进程播放见下文。进关使用显式 120 ms CU 诊断，普通输入冷启动诊断为 0；不宣称实体设备或全模式重测 |
| P6 v0.1 双出包与交付 | 出包验收通过，指定交付完成 | f652df0 最终双包实际解压、文件身份及数据／源码分流通过；独立源码 pull、全新单游戏／启动器编译和两组 CTest 通过。无 ROM 的对应源码附件已单独备好。Internal 群文件上传完成，版本说明与实际启动器截图已发送并观察到；具体现场留在本地，不入 Git |
| P7 后续作品 | 最大往生静态／试生成通过，实际接入未完成 | 本体 116 个文件与 ISO 大小／哈希全相同，三个程序静态装载、模块隔离和独立代码生成试验通过。匹配 TU、生成器中文路径修复、正式构建／启动器接线与实战／出包仍待完成；其余新作品未开始 |

## v0.1 发布门槛

验收采用用户指定的小规模代表性集合：每款一个模式选人进关，合计覆盖本体／DLC、保存与重启读回、配置／全屏、启动器进退、最短联网主流程，以及 Internal／Public 解压使用。沿用可靠既有结果，只补迁移影响和关键缺口；不重跑全模式、长时间和故障注入，不新增缓存／路径极限集合。原 60 分钟混合流程暂缓，未重测范围不标为本轮通过。

版本范围调整：编译器统一、虫姬二与绊地狱的帧节拍专项调查及优化均移至下个版本，不是 v0.1 发布门槛，现有证据不因此记成通过。届时以 G2 已有帧时间调查为方法参考；约 19.1 ms 为一段实际关卡短测的最大出帧间隔，不是输入延迟或所有游戏的验收硬上限。当前仍继续已有机制回归和整合验证。

以下为本次已完成的发布检查，具体新测／沿用范围以上述最终结果和各专项记录为准：

- 三款游戏、共享配置与启动器整合，以及 P6 的跨游戏回归完成后才标记 v0.1；Coming Soon 项目不阻塞首版。
- Internal 与 Public 从同一已验证构建出包，公共运行文件的版本和哈希一致，均附文件清单与 ZIP SHA-256。
- 内部包完整包含三款游戏必需数据与受支持的 DLC／补丁；数据缺失时明确失败。新目录解压可直接玩，独立 source/.git 可 pull、恢复依赖与增量 build。
- 对外包按白名单组装；检查 ZIP 不含 ROM／提取资源、源码、生成 C++、Git 历史、PDB、凭据或作者测试数据。
- 对外包无 ROM 可打开启动器；分别只放入一款受支持数据时能启动该款，缺少其他游戏不阻塞。许可证与数据放置、配置和日志提交说明齐备。
- 两包不带旧存档、录像、待传、日志或机器路径，不覆盖原工程、旧包和原始数据；构建和出包脚本不自动上传或通知。

### 验收后交付状态

- 已完成：核对最终 Internal ZIP 的版本、文件名、大小和 SHA-256，上传到 QQ 群“人均233.3”的群文件，并观察到对应条目上传成功。
- 已完成：向同一群发送版本／使用／日志反馈说明及一张实际最终启动器截图；已观察到发出消息和加载完成的图片，输入框清空、未见发送错误。
- 包验收、群文件上传和消息发送分别记录证据；交付现场仅留本地，不进入源码 Git 或分享包。

## P6 出包工具的机制证据

release_package.py 在工作树干净、同一提交的联合 360／PS4／启动器构建收据与实际程序全部匹配后才组包；Public 不接受数据／源码包含参数。Internal 另核对三款必需数据及虫姬二两份 DLC，逐文件复制验证并克隆不借用旧对象的源码 Git。输出只写新 releases 目录；两包均排除现有配置、profile、日志、缓存及凭据，不执行上传。

8 项自造数据回归覆盖 Public 白名单、拒绝额外文件／ROM／源码／符号、文件哈希变化、完整数据与独立 DLC、独立 Git objects／origin，以及中文空格目录 ZIP／CRC／SHA-256；其中一项完整执行同一自造源码／数据的 Internal 与 Public 组包，并确认运行文件哈希相同、源码／数据／PDB 分流正确。3 项构建数据回归覆盖显式优先、包根原生 INI 解析与坏配置不回退。原生检测工具新增 contentDirectory 字段供打包读取；本机已实际解析到 G2 及虫姬二配置的数据目录，未修改配置。

当前无 ROM 公共构建已产生并校验 launcher_receipt 的两对 EXE／PDB。上述为机制检查，不代表完整 v0.1 ZIP、Windows 新环境、GPL/LGPL 等许可交付或真实解压游玩通过；正式出包需要最终提交重建三款组件并补足这些实际证据。

### 实际 RC1 包与解压回归

b5ca101 已执行三款联合构建，360／PS4／启动器的 45／6／2 对程序与 PDB 配套校验通过。RC1 两包从该同一提交生成，尚未标为 v0.1 成品或上传：

| 包 | 本地现场 | 大小／SHA-256 |
| --- | --- | --- |
| Public | releases/20261019T035819Z-69927237/CAVE-PC-Collection-v0.1-rc1-public.zip | 56,905,426 字节；f8299b694c6fc51c37b5de1347d8e11162b76a6063a79c92aac206e2c7808ad1 |
| Internal | releases/20261019T035821Z-8b509312/CAVE-PC-Collection-v0.1-rc1-internal.zip | 2,975,096,933 字节；8a9d8402804d4849556d07e93a94aa3d22caa39201998abefd8b7735ce416fe8 |

Public 实际解压到 .local/release-validation/候选包 public rc1，123 个文件及哈希清单通过。无数据时三款均为 missing_data、运行组件无缺失；实际启动页可用。仅通过数据选择对话框记录 G2 外部目录后，另外两款仍缺数据，G2 可独立启动。父进程 59324 的子进程 72812 创建系统数据；重启子进程 73896（run-20261019T040358989Z-73896）读回系统数据，Black Label／Seseri 选人进入玩家第一关并自然结束，随后保存 GAME DATA 471,040 字节及 No.1 录像 17,208 字节。列表显示 Black Label／Stage 1／0 分和本地时间 12:07:58；本次未重播该录像，沿用既有播放闭环。两次退出码均为 0，启动器恢复。

自动工具方向键仍按小键盘事件送达，第二轮只在测试副本增加四方向 Numpad 别名，结束后已移除；发行 ZIP 和默认模板不变，不据此宣称实体输入全部实测。首次进关后未确认暂停成功，不将该操作计为通过。

Internal 解压到 .local/release-validation/内测 rc1 后，所有文件哈希通过，但 verify 在源码 Git 状态处失败：22 个未初始化子模块目录未被写入 ZIP，Git 因而显示删除；HEAD、origin、Git objects 和 fsck 均正常。archive 已修正为空目录写入目录条目，并在既有双包自造检查中加入真实 gitlink／ZIP 解压／Git 干净状态验证，8 项打包检查通过。RC1 Internal 保留为问题现场，不补改后宣称原 ZIP 已通过；下一候选必须重新组包、解压并验证。

## 源码快照工具

四项自造数据回归通过：保存已修改和未跟踪源码但排除忽略数据；拒绝已有及重叠目标；发现备份损坏；拒绝路径越界。实际三份快照分别复制并复核 1306、1343 和 698 个普通源码文件。快照与运行现场保留在本地忽略目录，不进入 Git。

## P2 公共工程

合集使用自身的固定 LLVM／CMake／Ninja 构建 common 静态库与 cave_inspect，公共构建未读取旧工程 SDK 或游戏生成正文。首次工具准备从显式本地缓存复制并核对固定入口哈希，后续不引用缓存目录；下载恢复路径与归档校验也在维护源码中。

18 项 Python／命令行回归、28 个原生断言及两组 CTest 通过。覆盖五款顺序与 Coming Soon、完整元数据、重复与越界清单拒绝、空数据／缺文件／同大小错误程序／损坏资源／缺少运行组件、快速检查不误报已验证、外部与中文空格路径、INI BOM／大小写／重复键／错误版本，以及无关游戏错误路径不阻塞当前游戏。

Windows 实际目录联接回归通过：数据根可指向外部目录；资源子路径不得跳出数据根；logs 联接到数据目录时，即使目标游戏子目录尚未创建也被拒绝，原测试文件未改动。检测无数据目录时不创建配置或其他文件，改变工作目录不影响显式合集根解析。

现有 G2 数据只读全校验通过：155 个文件、677,185,607 字节、155 个哈希匹配。合集游戏后端未迁入，报告仍为 not_integrated，与 verified 数据状态分开；未把完整数据校验当作合集可玩或正式发行通过。

## P3 合集 G2 运行时

首次完整构建 d59cd09 使用合集自己的工具、SDK 源码和独立依赖，从受支持的本地数据重新生成八个程序模块，再编译宿主与七个模式 DLL；不调用旧 G2 EXE，也不读取旧工程的 SDK 安装或生成正文。构建收据包含全部生成文件哈希，验证 28 个二进制／PDB 对；runtime 按清单整理 22 个程序／符号文件，不复制 ROM 或旧存档。

独立 profile migration-r1 实际通过标题、原生系统数据创建／自动保存、Black Label／Seseri 选人及第一关、暂停与恢复、F11 全屏、Alt+Enter 返回窗口和自然 Game Over。GAME DATA 与 SYSTEM DATA 写入分游戏目录；No.1 录像为 0 分／Stage 1／Seseri，17,208 字节，SHA-256 前缀 a278d768。重启后列表显示正确模式、角色、关卡和本地时间，完整回放后自然返回列表；日志确认 dat15 客体模块注销。常态音频及帧诊断继续输出，不把本次短测当作长期稳定性验收。

无窗口预检验证外部原始数据、首次配置生成和 profile 排他占用；实际游戏运行时第二个进程访问同一 profile 被拒绝，关闭后锁自动释放。原 G2 源码保持干净，原配置 SHA-256 未变。自动操作工具的方向键被 SDL 收为小键盘键，回归期间只在本地 INI 临时加方向别名，结束后已恢复原配置哈希；发行模板未改，也未据此宣称实体输入全部通过。

原生启动器五款横幅已完成浏览器设计自审和原生渲染对照；实际 150% DPI 小窗口可显示、键盘切换控件并滚动。HWND 图形目标曾实际白屏，改为按需 Direct2D DC 绘制后真实窗口可见，离屏效果图不再作为唯一视觉证据。配置按钮打开根 config.ini 的 Notepad 标签；数据选择对话框实际定位到原 G2 数据并保存到同一 INI，未复制或修改原数据。运行文件配套清单缺失时显示缺少运行组件，未误报可启动。

新增进程辅助程序回归覆盖中文空格路径、空参数、引号与尾部反斜杠、非 shell 元字符、固定工作目录、独立进程互斥、进程结束自动释放和 0／37／259 退出码；配置选择保持 BOM、CRLF、注释、未知字段和大小写，拒绝数据／输出重叠。素材回归检查五款顺序、固定哈希和资源编号、生成幂等及拒绝伪装成图片的程序。公共构建、33 项 Python 回归、源码审计及三组 CTest 均通过；这些不冒充真实游戏退出或崩溃回归。

启动器真实启动记录为 logs/espgaluda2/run-20261018T203715497Z-78968，测试 profile 为 default（此前不存在的开发测试目录）。父进程 77600 通过固定合同调用合集 runtime/xbox360/espgaluda2.exe，日志确认 root-INI、独立数据路径及 dat15 原生模块。Black Label／Seseri 进入第一关后自然 Game Over，系统数据 2,560 字节、游戏数据 471,040 字节与新 No.1 录像 17,144 字节落盘；录像 SHA-256 为 cf6d9d04dab6b2b44770a4044136770a89fa783ff091637c50e91381fbf6b7d3。关闭父窗口仅隐藏启动器，游戏继续；游戏窗口 Alt+F4 后父日志记录 child-exit code=0，列表恢复且可再次启动。真实游戏存活时，无窗口预检即便指定另一 profile 也被合集级锁拒绝，没有创建该 profile。原 G2 配置 SHA-256 仍为 417cdc76，旧工程保持干净。本轮未将短测写为全模式、重启回放或长期流畅性通过。

显式“复制到合集”已通过原生菜单、来源选择和复制确认的真实操作。父进程 18280 的 launcher 日志记录 155 个文件、677,185,607 字节复制完成；原始下载目录与新 game-data/espgaluda2 分别执行完整检测，均为 155 个 SHA-256 匹配、无缺文件或版本问题。复制后界面确认配置已更新，临时目录未作为安装目标保留。自造数据另覆盖非空目标保留、取消、源文件编辑／改名、额外资源、零字节文件和空目录；四项 Windows junction 实测覆盖默认容器／目标重定向拒绝、源子目录链接拒绝和源根别名只读复制。上述检查与原生进程测试一起运行；本段不代表取消按钮 UI 或整包移动的发布验收已通过。

启动器从合集数据副本重启游戏的记录为 run-20261018T205310057Z-71280，仍使用 default 测试 profile。公共全屏 true／音量 0.60 与 G2 覆盖窗口／音量 0.25 的对照中，实际窗口为窗口模式，Host 日志为 full-screen=false、volume=0.25。临时方向别名通过同一 INI 生效，成功用 computer use 导航到 REPLAY DATA；列表读回前一进程 No.1 的 Black Label／Seseri／Stage 1／0 分和本地时间 04:40:28，完整播放至自然返回列表。dat15 的加载／卸载时间为 04:55:09／04:56:17，录像 SHA-256 仍为 cf6d9d04，未因播放改写。

录像结束且已回到列表后，按进程 ID、程序绝对路径和父进程核对，仅强制终止上述独立测试游戏以模拟异常退出。父进程 44220 记录退出码 4294967295，实际显示警告且列表保留；确认后可在同一启动器再次启动，子进程 74004 正常显示标题页。恢复公共默认、移除全部临时覆盖后，新日志为 full-screen=false、volume=1。该操作只证明异常子进程生命周期处理，不记为游戏自身崩溃或崩溃修复；未影响原工程和 ROM。

原 66 项 Python 专项检查已按 [迁移对应](ANALYSIS.md#原-g2-回归的迁移对应) 适配：64 项继续运行，两项复制检查由合集已有的原生 DataImport 测试覆盖；另增加 12 项路径、静态报告和指令偏移检查。当前共 114 项 Python／CLI 测试与 12 组 CTest 通过。HTTP 用例验证新的合集游戏路径和其他游戏路由隔离，仅使用临时本机服务。

合集分析工具实际检查了 default 及七个 overlay，收据在 .local/analysis/collection-p3-tools-r1/analysis.json。七个 overlay 分别完成三次无客体执行的装卸载，并安全拒绝占用区间的重叠模块；检查前后整个 155 文件数据盘点一致。社区扫描读回 84 个 view 和 15 个已审阅边界，回调候选报告保留供人工分析，不自动修改游戏声明。旧回放 run-20261018T205310057Z-71280 的日志分析显示 dat15 加载／卸载各一次、无未配对事件或错误行，213 个音频样本的输出队列为 32–42.7 ms、静音补块为零；这不是声音命令延迟、日志完整性或新增游玩验收。上述现场均留在忽略目录，未提交游戏区段或 ROM。

### 合集可见模式迁移回归

构建提交 39f7274 的一次真实启动由父进程 58920 创建子进程 4988，使用合集自己的 game-data/espgaluda2 和 default 开发测试 profile；现场为 logs/espgaluda2/run-20261018T211434792Z-4988。下表五个新检查在同一游戏进程中连续完成，没有用静态模块装卸载代替实际游玩。

| 模式／角色 | 合集证据 |
| --- | --- |
| Arrange／Ageha | dat11 05:15:39 加载；已见玩家第一关、射击得分、石头消耗及暂停，Previous Menu 确认后 05:17:14 卸载并返回模式列表 |
| Arcade／Ageha | dat00 05:17:38 加载；已见玩家第一关、死亡及续关后恢复，Previous Menu 确认后 05:19:31 卸载返回 |
| Xbox 360 Novice／Ageha | dat163 05:19:55 加载；等待操作说明完整结束，确认玩家第一关而非说明演示，暂停及 Previous Menu 返回，05:23:15 卸载 |
| Black Label Novice／Seseri | dat164 05:23:44 加载；选人后已见第一关玩家得分 7,360、暂停及 Previous Menu 返回，05:25:42 卸载 |
| Xbox 360／Ageha | dat12 05:26:13 加载；等待说明结束后实际第一关及自然 Game Over，拒绝本局录像另存，再从标题 EXIT 返回，05:30:59 卸载 |
| Black Label／Seseri | 前述独立进关、局末保存与跨进程完整回放现场已覆盖；本轮不重复录制和播放 |

五个模块均只加载／卸载一次，日志无未配对事件、错误级或致命错误行。关闭前 1,002 个输出音频样本的队列为 32–42.7 ms、静音补块为零；不据此声称声音命令延迟或听感同步全部通过。Arrange 的关卡片段仍见约 34 ms 的出帧间隔，混合现场包含捕获、输入、暂停和加载，继续保留实战帧时间复查，不将单次极值写成已修复或稳定性通过。新工具的汇总为 .local/analysis/collection-p3-modes-run-r1.json。

子进程在模式列表 Alt+F4 正常退出，父日志记录 code=0，启动器实际恢复可启动状态；随后正常关闭父窗口。自动操作期间的四方向小键盘别名已移除，根配置恢复 SHA-256 c50320b8。未另存这轮临时录像，也未修改原 G2 工程或 ROM。

六种可见 PLAY 模式的选人、进关和卸载返回迁移覆盖已完成。实体输入、更多角色／训练以及音画和长期稳定性仍按范围继续；dat162 的实际可达入口未由静态检查推断。原 SDK 退出时的 hard-exit 行为尚未在此次迁移中重写。P3、v0.1 和后续两款整合均不标记完成。

### 正式社区服务与网络录像

服务部署固定为源码提交 `60f9d3d`，归档 SHA-256 为 `454915838e24230a1c2cae27eb9206f8f83802a4ef745f572bce44ffdbe014b9`。Ubuntu 22.04／Python 3.10.12 上 18 项 G2 服务和 4 项 SQLite 备份测试通过，随后启用独立的低权限 G2 服务、每日七份保留备份与合集 nginx 路由。三款合集 HTTPS 健康检查和虫姬二／绊地狱旧入口均通过；旧两款服务程序哈希未变且未重启。G2 数据库 identity 为 espgaluda2／435A07D8／1，完整性检查为 ok。

| 实际流程 | 证据 |
| --- | --- |
| 原生联网游玩与提交 | 合集启动器创建 PID 79096，现场 `run-20261018T214454510Z-79096`。测试昵称 CollectionCheck、SCORE ATTACK／Black Label／Seseri／Stage 1，原游戏提交最终 10 分到六个 view，随后上传 17,176 字节原生录像；六条成绩与六条附件关联均成功，服务端只存一份录像 |
| 榜单读回与下载 | 游戏实际榜单显示排名 1、10 分、Seseri、Stage 1、Continue 0 及录像图标。GAMECLIP 下载成功后保存到原先为空的 No.2；未保存该局的本地原件，所以该槽确实来自网络 |
| 完整性 | 网络 SHA-256 为 `989e7402bdcf4970bc6480372a8baa4441e3e77f945eb3a30511aed1c0662dd7`，No.2 落盘为 `54798537fe1a55e96e5f21c2c396537d8118014fabcef4669cb3090c13580862`。差异仅在本地校验和及下载标记的偏移 2、3、79；104 字节头部之后的输入数据完全一致。原 No.1 哈希仍为 cf6d9d04 |
| 冷启动回放 | 正常结束前一进程后，由同一启动器创建 PID 76860，现场 `run-20261018T215912794Z-76860`。No.2 重启读回后自然完整播放并返回列表；dat15 于 06:02:46 加载、06:04:04 卸载，无错误级日志或未配对模块事件，两份录像哈希未变。游戏正常关闭后启动器恢复可启动状态 |

该次重启现场有 299 个音频输出样本，队列 37.3–42.7 ms，静音补块为零，不据此推断声音命令延迟或长期帧时间完全通过。原生提交结束时仍见既有 XGI `0x000B0013` 清理告警；不影响本次上传和读回，保留单独跟踪，不隐藏或冒称已修复。发行模板及内置默认值现为联网开启／Player；显式关闭仍通过配置回归，测试用昵称与方向别名已从根 INI 恢复。

回放的 06:02:52–06:04:00 诊断窗口内，guest swap 共 4,097 个间隔，只有一次超过 20 ms，最大 49.506 ms；同一时刻 submit 为 49.798 ms、host present 为 49.505 ms，而 VBlank 最大 17.758 ms、Present 调用最长 0.299 ms。该异常不能归结为 VBlank 失速或 Present 调用阻塞，但目前没有根因证据；现场包含截图，不能据此排除或确认捕获影响。其余模式中观察到的约 34 ms 峰值也保持跟踪，不把平均 60 FPS 当作完全消除卡顿。

## P4 Xbox 360 公共化

`cave_xbox360_input` 在后端级构建一次，直接接收公共 Settings；移除对 G2 配置适配器的链接依赖。键盘短按、焦点清理、SDL 合并与死区实现未改写，仅调整归属及命名空间。输入回归增加虫姬二／G2 同一 INI 的不同覆盖字段对照，不以此宣称虫姬二已能启动。

公共库首次编译后的 G2 冒烟现场为 `run-20261018T221018190Z-69200`，从合集启动器实际验证 Enter 进入菜单、Z 确认进入 SCORE ATTACK 子菜单、X 返回，再正常退出恢复启动器。根 INI 未加临时别名。此轮只验证菜单输入与进程生命周期，不重复宣称全模式、物理手柄或联网回归通过。

虫姬二固定来源仍为 `7dced597`，旧 `sdk-source/` 对应合集 `third_party/rexglue/`；对照 1,255 个 Git 跟踪路径发现 26 个文件不同。已核对社区录像命名空间的默认值保留 futari-replay，G2 使用独立覆盖；模块路径规范化、每镜像内存所有权和合并输入序号修复在合集保留。其余 SDK 差异和虫姬二实际模块生命周期仍需后续核对及运行验证，不因默认接口兼容就判定全部合并通过。

初始 39 个虫姬二维护文件共 164,202 字节，导入前逐个核对已提交 Git blob，导入工具拒绝脏来源、既有目标和非 .local 报告路径。旧宿主、图形设置向导、重复 SDK、ROM 与生成正文均不在选择内。另保留原服务与测试源码，改用公共 HTTP worker／昵称验证，原生协议、待传和录像解析逻辑保留；来源见 docs/SOURCE_ORIGINS.json。

本体数据只读盘点为 52 文件／841,393,788 字节，全部 SHA-256 通过，九个 XEX 的 Title ID 均为 435A07D7。元信息不包含文件正文，DLC 不并入本体必需清单，后续仍须独立挂载和保存回归。检测结果为 dataState=verified、state=not_integrated，未把资源完整当作已可启动。

合集自己的 SDK 从外部只读数据目录生成主程序和八个 overlay，共 172 个忽略目录内文件，收据在 generated/futari/generation.json；每个 overlay 单独进程生成，保留嵌套路径、模块专有 switch/hook includes 和 31 个主程序回调。生成前后所需数据的哈希未变，没有复制旧生成正文。通用化后的 G2 同时重新生成，129 个输出哈希全部与前一版本相同。

虫姬二 native 构建不要求 ROM，原生机制验收如下，不作为实际进关证据：

| 检查 | 已通过范围 |
| --- | --- |
| 音频等待 on／off | 仅声音线程正向 1 ms 等待生效，零轮询、其他线程、原寄存器及先前策略保持，停止后恢复 |
| 音频计数与 guest 追踪 | 并发计数、XMA 活动及只读队列／对象探针；自造 guest 内存前后不变，无无效指针解引用。Release 测试显式保留 assert |
| 原生社区与共用 HTTP | 本机随机端口和临时 SQLite 完成整数成绩、录像路径 ABI、收发校验、损坏附件拒绝、待传恢复、取消和离线超时；未使用正式榜单或旧 profile |
| 服务回归 | 11 项临时数据库测试，包括不可变去重、分页／本人、昵称与整数精度、录像格式及哈希、容量边界、重启与在线备份 |

合集宿主、启动合同、分游戏写入路径与独立只读 DLC 源现已接通，主程序和八个模式 DLL 首次完整编译通过。首次现场 run-20261018T224754408Z-41384 实际到达标题，窗口身份及 F8 双标记通过；尚未进入菜单或选人。输入诊断现场 run-20261018T224906447Z-76788 与 run-20261018T225127867Z-2744 显示同一瞬间的连续两次轮询先读到 Start、再读到零，标题不响应；不把标题画面当作进关通过。后续需验证按客体 VBlank 保留的短按，以及本体／DLC 的实际选人、进关、保存和音频。当前不将 P4 标记完成。

共用 schema=2 崩溃 writer 在 .local/verification/shared-crash-r1 完成独立进程 synthetic-context 验证：game=espgaluda2、build=self-test、DUMP_OK=1；未触发 OS 崩溃或使用游戏内存。只读 DLC 内容源原生测试验证枚举／读取可用，创建、改写、安装与删除被拒绝，SaveGame 头部仍落到独立 profile，自造源字节不变。它不证明真实 DLC 的加载与保存已经通过。

同一 CMake 配置以 `-Games espgaluda2,futari -NativeOnly` 完成双游戏 native 构建，只定义一份 cave_xbox360_input 和 cave_community_http，读取同一已校验 SDK。17 组 CTest 全部通过，其中公共三组没有重复计数；这证明两款适配和机制测试能共同编译／运行，不代表双游戏完整宿主或模式 DLL 已验收。

公共宿主、只读内容源和客体 VBlank 采样接入后，更新的双游戏 native 构建与 18 组 CTest 全部通过，源码 141 项回归及审计通过。键盘机制测试包含同帧 1,000 次重复读取、下一帧清除短按、已采样长按立即释放、扳机、失焦与面板屏蔽；不以机制测试代替真实菜单、选人或实体设备。整理测试验证两款使用独立 helper 名称，不重写相同 SDK 字节，且损坏源在改变既有 manifest 前被拒绝。

### 虫姬二首轮真实保存回归

游玩构建为 `1e9e0dd`、sourceDirty=false。主现场 `run-20261018T225640855Z-68140`；重启现场 `run-20261018T230832327Z-28492`。独立合集 profile 为 user-data/futari/default；本轮明确关闭社区，仅验证本地路径，不代表联网已经验收。

| 流程 | 实际结果 |
| --- | --- |
| 标题／新存档 | 普通短按 Enter 进入，不再停在标题；确认创建系统存档成功，数据在 profile 的本地 XUID／435A07D7 下 |
| Ver1.5／Arcade／Original／Reco Normal | `reco_normal` 加载，已见玩家第一关、死亡和自然 Game Over；No.1 本体录像保存成功。返回顶层时模块卸载 status=0、still_loaded=false |
| 同进程 Black Label／Xbox 360／Original／Reco | `kiniro_abnormal` 在本体卸载后加载，已见玩家第一关、死亡和自然 Game Over；No.2 DLC 录像保存成功，未覆盖 No.1 |
| 冷启动系统及本体回放 | 新进程直接读入系统存档，无新建提示；Ver1.5 列表读出 No.1 的 Original／Stage1／Continue0；选择后自然播放至结束并返回录像列表，无人工中断 |
| 只读源 | 游玩后本体 52 项全量哈希复核通过；DLC 7 文件／275,443,888 字节与游玩前 SHA-256 完全相同。profile 中没有 DLC 正文副本 |
| 当前保存容器 | 合并两槽的 replaydata.bin 为 4,044 字节，SHA-256 `a9358392b43ba6f14c9efbe736766a108815497c54671470f7c861738358ba87`。这不是 DLC 冷启动播放证据，该项仍待从 Black Label 的过滤列表验证 |

本轮只覆盖两个模块入口，不能代替其余六个模块、所有角色／难度、音效延迟与花屏、联网及长时间稳定性。G2 使用同一新 SDK 完整重建，31 对二进制／PDB 和 23 项运行文件通过，单游戏 13 组 CTest 通过；虫姬二完整构建的 9 组 CTest 通过。G2 新公共宿主的实际游玩回归仍需继续。测试用方向别名及离线覆盖在结束后移除，发行模板未改。

### DLC 冷启动与双游戏联合构建

独立冷启动现场 `run-20261018T231722029Z-77480` 使用干净构建 `1e9e0dd`。从 Black Label／Xbox 360 的过滤列表读回 No.2（Original／Reco／Stage 1／Continue 0），自然完整播放并返回列表，没有发送中断输入。返回顶层时 `kiniro_abnormal` 于 07:24:01 卸载，status=0、still_loaded=false；随后正常关闭。4,044 字节录像容器的 SHA-256 仍为 `a9358392b43ba6f14c9efbe736766a108815497c54671470f7c861738358ba87`。这补齐上轮尚未验证的 DLC 冷启动播放，不改变之前仅本体通过的历史范围。

该现场关闭前的 427 个输出音频样本队列为 32–42.7 ms，静音补块为零；分析未发现错误级日志或未配对模块事件。这里验证的是输出后端状态，不是声音命令延迟或主观音画同步。

完整联合构建基于干净源码 `640ea91`，显式选择 `espgaluda2,futari`，输出在 out/xbox360。收据核对两款生成输入、同一 SDK 和 45 对二进制／PDB；G2 整理 23 项、虫姬二 25 项运行文件。141 项源码回归、源码审计及完整构建目录的 18 组 CTest 全部通过。它不是此前 NativeOnly 构建的重复计数，也不代替下列实际运行。

### 联合构建新增真实入口

现场 `run-20261018T232624425Z-72708` 的 run.json 记录 `640ea91`、sourceDirty=false。仍使用合集独立测试 profile，并明确关闭虫姬二联网。

| 菜单与角色 | 实际进入和返回 | 日志中的模块生命周期 |
| --- | --- | --- |
| Arrange／Original／Reco | 已见玩家第一关、局内射击画面与暂停菜单；确认结束测试局后回到标题，再返回顶层 | palm_normal：07:29:09 加载、07:31:35 卸载，status=0、still_loaded=false |
| Novice／Original／Palm | 从 Arrange 旁的 Novice 卡片进入，实际切换到 Palm 并见玩家第一关；暂停、返回标题和顶层通过 | palm_abnormal：07:32:29 加载、07:38:19 卸载，status=0、still_loaded=false |

本轮没有保存额外录像，没有覆盖 No.1／No.2。不能将菜单卡片一概解释为 Arcade／Xbox 360 配对：Arrange 与 Novice 是两个实际不同的入口。至此四个模式模块有真实进关证据；reco_abnormal、kiniro_normal、hirow_normal、hirow_abnormal 的实际入口仍需补齐。日志没有错误级或致命记录，两个模块全部卸载；输出音频队列没有持续增长，初始化样本曾有两个静音补块，不写成全程零补块或音效延迟已修复。

### 公共宿主下的 G2 回归

合集启动器实际启动 `run-20261018T233919082Z-17708`，run.json 为干净 `640ea91`，使用 game-data/espgaluda2 和 user-data/espgaluda2/default。窗口标题符合统一格式，既有系统数据和两槽录像直接读入，没有要求创建新存档。

- PLAY／Xbox 360／Ageha 已选人进入玩家第一关，观察到死亡与局末；本局有续关，不作为不续关成绩验证。放弃保存临时录像后，从标题 EXIT 返回模式列表；dat12 于 07:40:28 加载、07:45:05 卸载。
- REPLAY DATA 读回既有 No.2（Black Label／Seseri／10 分／Stage 1），确认后完整自然播放并返回列表，未发送局内输入；dat15 于 07:47:29 加载、07:48:46 卸载。17,176 字节录像 SHA-256 保持 `54798537fe1a55e96e5f21c2c396537d8118014fabcef4669cb3090c13580862`。
- Alt+F4 后游戏退出，启动器记录 child-exit code=0，真实窗口恢复“开始游戏”。未执行新的榜单提交或录像上传操作；不重复替代已有正式联网基线。

极短 Start 在 Xbox 360 局内未稳定打开暂停，与原基线已有记录一致；后续须区分游戏采样与真实设备行为，不用延长自动测试按键来宣称修复。本轮关闭前 584 个音频样本队列 32–42.7 ms、静音补块为零，没有错误级日志或未配对模块事件；不据此扩大为全模式、实体手柄、长期流畅性或声音命令延迟验收。

全部测试结束后关闭测试窗口，根 INI 移除两款的临时小键盘别名及虫姬二离线覆盖，恢复 SHA-256 `c10e3d965c10a443828117232ab650e21212158d01336a82b3ecb360f2879dcb`。三个原工程工作树仍干净；虫姬二本体 52 项由联合构建的检测工具再次完整校验通过。

### 八模块入口补齐

现场 `run-20261018T235451981Z-19608` 使用干净联合构建 `2dfd87a`。profile 仍为合集自己的 user-data/futari/default，明确关闭联网；本轮不执行成绩上传，也不保存额外录像。

| 菜单／Original／角色 | 实际观察 | 模块加载／卸载 |
| --- | --- | --- |
| Ver1.5／Xbox 360／Reco Normal | 手动确认难度、角色与 Shot，已捕获存活玩家第一关、死亡和局末；包含续关，不作为无续关成绩证据 | reco_abnormal，07:56:30／08:04:10 |
| Ver1.01／Arcade／Reco Normal | 手动选人后已捕获存活玩家第一关，局末拒绝额外保存，返回顶层 | hirow_normal，08:06:04／08:12:48 |
| Ver1.01／Xbox 360／Reco Normal | 手动确认后捕获 PLAYER1 开场及第一关 Continue 倒计时，随后到达局末保存提示并返回；两次观察之间未捕获存活玩家画面 | hirow_abnormal，08:14:27／08:23:24 |
| Black Label／Arcade／Reco | Original 及 Reco 选择流程后，已捕获第一关玩家、死亡复活、局末及返回；一次 Start 在续关界面生效，本局不是无续关记录 | kiniro_normal，08:23:49／08:26:32 |

四个模块均加载／卸载各一次，卸载 status=0、still_loaded=false；进程在顶层菜单正常关闭。分析报告 `.local/analysis/collection-p4-futari-remaining-r1.json` 检查 14,440 行，未发现未配对模块事件、错误级或致命记录。关闭前 1,907 个音频输出样本的队列为 32–42.7 ms、静音补块为零；它不证明声音命令响应、主观同步或长期流畅性。旧两槽录像未被本轮另存，临时方向别名及离线覆盖已移除，根 INI 恢复 c10e3d96。

至此八个模块均有真实入口与返回记录；同模式重新进入、更多角色／难度、实体输入、声音响应和虫姬二联网仍分别验收。自动操作的截图间隔可跨过短暂的存活片段，不能把 Continue 画面前后发送的 Start 解释成已证实的暂停采样故障。

### DLC 独立清单与挂载边界

本体仍为 52 项必需文件。新增两个可选包清单分别包含 Black Label 的 5 项、Ver1.01 的 2 项，合计 275,443,888 字节；所有路径、大小和哈希均与本地只读来源核对。更新后的 cave_inspect 在 Full 下返回本体 verified、两包分别 verified，没有未知或额外文件；启动器 integrated 状态仍独立保持 false。

146 项 Python／CLI 回归、common 的 96 个原生检查及 out/futari-native 的 9 组 CTest 通过。自造数据覆盖单包缺失、同大小错误内容、截断、缺头部、额外文件、链接目录、快速检测不冒称哈希通过，以及一包损坏不影响另一包。Windows 路径分隔符差异曾使正确文件误报为额外项，已规范化并通过实际两包与原生 fixture 复测。

SDK 测试在自造存在的包目录上验证：白名单筛选枚举，未验证包的 Exists／Resolve／Open 被拒绝，空列表不回退为全量枚举，其他 Title ID 不能复用本游戏许可；SaveGame 仍写独立 profile，源文件不变。新宿主在没有外部 DLC 时使用 prepared 下的空内容视图，防止读取旧 profile 中未经校验的附加包。此处不代替新构建的实际挂载、模式进关或联网验收，完整宿主重建继续进行。

同一轮源码回归暴露备份时间戳碰撞：Windows 快速重复备份可能得到相同 UTC 时间。备份现在保持独占创建并使用递增后缀，固定时钟的九次备份保留最新七份、无重名覆盖；16 项备份／虫姬二服务专项测试通过。只修改仓库维护源码与临时测试数据库，没有变更正式服务。

### 启动器、重复加载与关闭 DLC

干净联合构建 `e6c832e` 完成主程序、全部游戏模块及公共运行文件的整理：45 对二进制／PDB 通过配对验证，G2 为 23 项、虫姬二为 25 项运行文件。148 项 Python／CLI 回归、源码审计和 out/xbox360 完整构建目录的 18 组 CTest 全部通过。虫姬二在清单中设为 integrated=true，启动器实际提供“开始游戏”。

| 现场 | 实际覆盖 |
| --- | --- |
| `run-20261019T004829280Z-12940` | 启动器创建游戏，既有系统存档自动读取。两包 DLC 分别 Full verified（5／2 文件及哈希），Black Label／Xbox 360／Original 两次进入玩家第一关，观察到死亡复活和局末；两次分别返回顶层。第二次为离线 SCORE ATTACK，局末明确拒绝成绩注册及录像保存；第一局角色未单独确认，不扩写为 Palm。F11 切至 2560×1440 全屏，Alt+Enter 返回 1280×720 窗口。 |
| 同进程模块生命周期 | kiniro_abnormal 于 08:50:01 加载、08:55:05 卸载，再于 08:55:34 加载、08:58:20 卸载；两次均 status=0、still_loaded=false。局内自然结束后均回到顶层，非单纯重复启动进程。 |
| `run-20261019T005901888Z-76180` | 同一启动器重启游戏，本地临时设 dlc_dir 为空，两包均 missing_data、未检查正文。实际顶层 Black Label 不可选且 Ver1.01 消失；既有系统存档正常读入。Ver1.5／Arcade／NORMAL PLAY／Original／Palm Normal 手动选择后捕获存活玩家第一关，短按 Enter 打开暂停，确认结束测试局后返回顶层。reco_normal 于 08:59:51 加载、09:03:44 卸载，status=0、still_loaded=false。 |
| 启动器恢复与文件保留 | 两个游戏进程正常 Alt+F4 退出，launcher/run-71128.log 分别记录 code=0，实际界面两次恢复“开始游戏”。没有另存录像，4,044 字节录像容器 SHA-256 仍为 a9358392；临时离线／方向覆盖移除，DLC 路径恢复，根 INI SHA-256 为 c10e3d96。三个原工程工作树保持干净。 |

两个分析报告分别为 `.local/analysis/collection-p4-futari-launcher-r1.json`（5,190 行、两对模块事件）及 `collection-p4-futari-no-dlc-r1.json`（2,313 行、一对模块事件），均无未配对事件或错误／致命级记录。关闭前输出音频样本分别为 595／293 个，队列 32–42.7 ms、静音补块为零；这项数据衡量输出后端，不作为死亡／道具后的声音命令延迟验收。其余七个模块的同进程重新进入按用户要求暂缓，不记为通过，也不作为当前迁移的前置条件。部分损坏 DLC 的真实菜单对照继续保留，联网新增结果如下。

### 虫姬二真实联网与来源约束

实际游戏使用干净 `756de6f`，现场为 `run-20261019T011304271Z-49560`。独立本机服务位于 `.local/community/futari-gameplay-r1`，只监听 127.0.0.1 随机端口，使用合集 `/cave/api/v1/games/futari` 路径和新 SQLite；没有向正式榜单提交记录。

| 流程 | 实际结果 |
| --- | --- |
| 原生局末 | Ver1.5／Arcade／SCORE ATTACK／1P SIDE／Original／Reco Normal 已选人进入玩家第一关，自然 Game Over，0 分、Continue 0，保存至空槽 No.3。没有因按键尝试而扩写为成功使用炸弹或获得分数 |
| 注册与上传 | 按游戏提示确认注册与分享录像。09:21:06，view 100／101 分别接受成绩及录像，游戏显示注册结束；数据库均为 CollectionCheck／0 分／build 756de6f。两个附件各为 1,630 字节，SHA-256 均为 `165b3d2bbd23333d5b996d6dc0aed2bbf4bc5135dad81666e4753d11896381ae` |
| 榜单与下载 | 游戏 LEADERBOARD 读回排名 1、Reco Normal、Stage 1、1P、昵称及录像图标。Details 下载保存至空槽 No.4，实际显示保存完成；09:24:26 日志确认 view=100、bytes=1630 校验通过 |
| 本地记录 | 99 槽容器的 No.3／No.4 记录均为 1,634 字节，SHA-256 相同：`8babf901a3757bf7c5eadd07b8429028eca674ffe9626fd573a13986fa14704d`。完整四槽容器为 7,312 字节，SHA-256 `10578d799c01a23911004fe510aee85f7654b8c767515c0130873b42e29ee171`。网络 envelope 与本地槽格式不同，不用两者长度直接判损坏 |
| 退出与恢复 | 09:27:45 正常关闭游戏，launcher/run-4232.log 记录 code=0，启动器恢复。局末提交与下载均完成，active outbox／pending 为空；本机服务停止，根 INI 恢复 c10e3d96 |

注册结束有一条错误级 `Unimplemented XGI message app=000000FB, msg=000B0013`，继续榜单与下载均正常。该兼容清理告警继续跟踪，不宣称日志全无错误；关闭时模式仍活动，不将缺少顶层模块卸载日志直接判为崩溃。此轮没有冷启动播放下载槽，也未验证正式 HTTPS 游戏上传。

待传机制新增原游戏／端点约束：新成绩 version=2，录像 journal 同样保存 origin；只向相同来源自动重试。原生回归覆盖首轮 503、重启恢复后保留原昵称／build／提交关联，以及其他端点、其他游戏和无来源旧格式的成绩／录像／待关联文件原样保留。该机制使用自造数据，不能代替新宿主的真实断网游玩验收。

### 客体音频队列观测

增强后的日志分析器重新读取 Black Label 双进关现场 `run-20261019T004829280Z-12940`，新报告为 `.local/analysis/collection-p4-futari-guest-audio-r1.json`。作用域为 `kiniro_abnormal_and_default_engine_and_wait`：34 个匹配队列头样本为 0.343–3.427 ms，44 个 cue 请求 API 完成样本为 0.039–0.319 ms；超过 20／100／1000 ms 的队列头样本均为零，丢弃计数最大值为零。另有 6 个有效死亡阶段探针，没有道具拾取阶段证据。

这些样本中未见秒级队列或 API 等待；同一队列头可能因重试重复采样。结果不测量扬声器发声时刻，不覆盖其他模块或道具后延迟，不能据此关闭历史音效问题。分析器的空数据、无效值、停机样本、丢弃计数及玩法探针边界由自造日志回归覆盖。

### 下载录像冷启动完整回放

干净构建 `6b47555` 的现场 `run-20261019T013724950Z-76988` 从合集启动器冷启动，明确关闭社区，不启动本机测试服务。Ver1.5／Arcade 的 REPLAY DATA 读回 No.4（Original／Reco Normal／Stage 1／Continue 0／09:19:05），选择后已捕获第一关中的回放玩家；之后不发送局内输入，录像自然完整结束并返回列表。此处验证的是上一轮实际下载保存的槽，不以原有 No.1 或本地 No.3 代替。

09:40:13 在录像列表正常关闭，launcher/run-996.log 于 09:40:14 记录 code=0，实际启动器恢复“开始游戏”。7,312 字节录像容器 SHA-256 保持 10578d79；根 INI 恢复 c10e3d96。报告 `.local/analysis/collection-p4-futari-network-cold-r1.json` 共 1,399 行、无错误／致命行，166 个输出音频样本均为 42.7 ms、静音补块为零。该次模块在列表仍驻留，未记为模式卸载或重复进入通过；音频客体队列及 cue 样本为空，不解释成零延迟。

## P5 PS4 源码与依赖

来源 `9501378a` 的 635 个普通维护文件共 7,105,443 字节，导入前核对每个 Git blob 和 SHA-256，导入后原工作树保持干净。`scripts/import_ketsui_sources.py` 的自造仓库测试覆盖选择范围、脏来源、既有文件保护、伪装二进制拒绝与报告路径。未导入旧启动器、构建／发行脚本、工具安装、二进制测试短片、ROM、准备镜像或 profile。

19 个开源依赖按 `tools/ps4-source.lock.json` 恢复到 `.deps/ps4`，本轮显式使用本机已有缓存；各自提交、官方 origin、工作树及独立 Git objects 校验通过，不引用旧仓库对象。四项机制回归另覆盖缓存独立性、版本及地址约束、大小写重名和脏目录保留。它不验证 PS4 宿主已能在新目录编译。

三份适配后的 Python 检查通过公共 source 入口运行，共 56 项，涵盖只读资源覆盖、静态接口边界和音画／帧时间报告。首轮源码迁入时共有 216 项源码回归及审计通过；后续独立构建结果如下。其他导入的原生／设备检查仍逐项适配，清单保持 integrated=false。

### PS4 原生构建及自制回归

`Prepare-Tools.ps1 -Ps4` 在合集独立安装固定 clang-cl 23、链接器和 compiler-rt，360 编译器与工具收据未改写。zstd 解码器仅安装到本地工具 Python；真实 LLVM 归档流式读取到 4,701 个普通文件及 285 个目录，未见链接或特殊项。归档、入口、链接器及 compiler-rt 哈希分别校验。

`Build.ps1 -Games ketsui -NativeOnly -Jobs 2` 已实际通过：GPU 目标在 out/ps4-gpu，宿主在 out/ps4-native；两个项目使用同一专用编译器、RelWithDebInfo 和显式 O2 优化。全新首编包含 GPU 的 374 笔和宿主的 529 笔构建任务；随后相同输入的增量构建均无需重新编译。构建不读取 ROM，不启动游戏，旧工程保持干净。

收据绑定维护源码、383 个上游／已声明本地适配文件、19 个固定依赖、工具及五对程序／PDB；GUID 和 age 实际匹配。上游哈希与维护哈希分开保留，其中 GCN 的既有 12 个维护适配已明确登记，不把原始上游哈希当成当前源码哈希。输入变化、工具损坏、缺项和符号错配由回归覆盖。

| 检查入口 | 已通过范围 |
| --- | --- |
| Source | 222 项 Python／CLI 回归及源码审计；包括新增工具恢复、zstd 成员检查、构建输入／输出和 PDB 身份边界 |
| 公共 CTest | common、launch、source 三组在 `Test.ps1 -Games ketsui` 流程通过；没有把它们重复计作 PS4 的专属机制 |
| PS4 原生检查 | 33 项，包括自造代码 ABI、格式化、堆／直接内存、同步／线程、文本／日历、显示布局与时钟、纹理写入跟踪、PCM、NP 请求／成绩／录像边界、SFO／PM4、宿主计时及 SDL 虚拟手柄。首轮报告为 .local/ps4-native-checks/collection-p5-native-r1，统一入口另生成 20261019T020726Z-33af413e |
| GPU CTest | 四项 gpu_compile_metrics、gpu_core_scheduler、gpu_runtime_draw、gpu_runtime_present；使用自制资源，检查真实 Vulkan 绘制／缓存、像素及窗口呈现，不执行游戏代码 |

这些结果仅证明新工程的原生组件和机制可构建／运行，尚未验证合集下的游戏进入、保存、媒体、实际手柄或长期流畅性。数据准备、合集启动与剩余真实流程继续接线。

### PS4 共享配置

PS4 与两款 360 游戏复用同一 common 配置库，保留 UTF-8／BOM、大小写、共享值和当前游戏覆盖的规则。新增配置适配将 16 个统一按钮位置转为 PS4 掩码，含 Touchpad、Options、肩键、扳机和摇杆按下；别名替换与保留宿主快捷键的检查不另写第二套规则。SDL 的禁用分支不初始化设备，径向死区与 360 使用同一函数；虚拟手柄验证回中、满幅、数值曲线、数字方向与已有短按／失焦／断连行为。首轮轴用例曾复用 POV-only 虚拟设备的 GUID，修正独立 fixture 身份后全部通过；未改实体设备映射。

PC 总音量在样本输出阶段与客体逐通道增益相乘，自造样本验证 0.25 倍音量、静音与通道顺序，保留客体原音量和输入数据。启动显示配置携带清单标题、全屏和 vsync；宿主与驻留 GPU 的接口同步升级至版本 5。真实 GPU 自制场景确认开启同步选择 FIFO、关闭选择 Immediate，均通过窗口重绑、缩放／最小化和多次呈现；FIFO fallback 与 Mailbox 选择另由纯机制检查覆盖。未改变客体显示时钟，不把这些结果写为游戏帧节拍或实际启动全屏通过。

`run_native_checks.py` 当前通过 34 项机制和 15 项真实配置文件检查，报告为 `.local/ps4-native-checks/20261019T022652Z-17139378`。文件用例在新的中文空格目录中验证默认值、UTF-8 BOM、覆盖、其他游戏隔离、关闭选项、合集及旧专属接口、无效按键／数值／版本／端点和缺文件；前后哈希不变，缺文件未被创建。根 config.ini 实际只读解析为 Player、联网开启、合集 Ketsui 地址；哈希仍为 c10e3d96。社区检查未创建网络 worker、未提交成绩；数据准备、真实启动、保存／录像及常态日志分级仍待接入。

### PS4 原生镜像准备与数据清单

原生 ELF 准备实现已从维护中的 Python 读取／序列化逻辑对照迁入 `backends/ps4/loader`，不调用 Python、原工程程序或模拟器。源数据只读，生成的是既有 KTPROBE1／KTMODULE 格式；26 组初始自造输入已逐字节一致，随后补充空 libc 及八组路径边界。检查覆盖可执行／模块、弱符号、相对／导入／已定义／绝对重定位、TLS／初始化约束、无效文件边界、libc RET 证明及既有输出保护；路径检查包含真实 Windows junction、源目录重叠、另一游戏数据边界、ADS、相对组件及无关游戏无效路径不阻塞。

59 个必需文件共 1,448,704,318 字节，包含 CUSA09062-app 与 CUSA09062-patch；SFO 确认为 01.00 本体／01.02 更新。数据清单只保存路径／大小／哈希，完整资源保持外部只读；零字节 CUSA09062.complete 为可选提取标记。源码回归现为 226 项，新增自造 ELF oracle 和 SFO／数据清单边界，不包含 ROM fixture。

`.local/ketsui-preparation/20261019T023812Z-2b720159` 的首次实际对照在 aeae872 的维护工作树上执行，三个原生输出均与 Python oracle 逐字节相同，源程序和 libc 哈希未变。eboot 为 3,429,952 字节（594 导入／6,245 重定位），emu_integration 为 904,244 字节（46／2,346），ketsui 模块为 71,300,492 字节（148／403）；该现场没有执行游戏。后续统一回归另记录新现场，不把这些数据作为实际进关、资源覆盖或完整 Public 首次准备通过。

自动缓存及收据、启动合同和实际游戏流程仍待接入，Ketsui 保持 integrated=false。编译器统一与虫姬二／绊地狱帧节拍专项继续按下一版本安排，不因准备工具完成而提前实施。

### PS4 原生资源覆盖准备

`backends/ps4/content/resource_prepare.*` 直接读取受支持 eboot、MDF／PSB 索引及归档范围，生成与现有 ContentView 配套的 KTFIX01 覆盖。源程序和脚本身份先核对，种子只从本地源程序读取；三项修复分别保留 BGM 设置自动保存、排行榜录像批量提交和仅作用于合格局末 producer 的本地采集门槛。原始归档、共享网络状态、查询调用方与成绩正文不被改写。

94 项原生检查包括 82 项自造数据 oracle 对照及 12 项实际路径／参数检查。覆盖 Python 整数种子的 MT19937 掩码、MDF 固定长度／零填充／错误流、PSB 的 UTF-8 名称链／循环／重复键／递归与数值、三种 LF／CRLF 脚本变换、覆盖排序与范围，以及既有输出、目录联接、数据重叠、ADS、错误扩展名、重复参数和无关游戏坏路径。重复执行结果确定一致；报告 `.local/ps4-resource-checks/20261019T030251Z-6a76da9e`。

实际数据对照报告 `.local/ketsui-preparation/20261019T030203Z-76768248` 中，music／replay／offline 三种文件分别为 5,028／8,269／40,715 字节，完整文件与 Python oracle 逐字节相同，修复前后脚本哈希及元数据一致。ContentView 对每一组合核验应用版本、原归档范围和覆盖哈希后装载成功，已存在输出不能被第二次调用覆盖。全部 59 项源文件的完整盘点前后相同。此检查没有进入客体入口或创建玩家存档，不当作游玩、联网提交或整个 Public 首次启动通过。

源码检查现为 229 项；新增资源对照程序使 PS4 收据包含六对 PE／PDB。统一 All 回归继续通过双游戏 360 的 18 组 CTest、PS4 的 5 项 GPU、49 项机制／配置及 35 项 ELF 准备检查。检查程序仅为开发工具，Public 运行链不依赖它或 Python；准备缓存和启动器接线仍需完成。

## P5 合集绊地狱轻量实战

### 公共启动合同与自动准备

合集原生构建已整理 runtime/ps4/ketsui.exe、KetsuiGpuRuntime.dll 及各自配套 PDB，六对构建输出／符号校验通过；启动器使用与 360 相同的五参数合同，运行时校验数据与程序证书并取得合集／profile 两级锁。准备配方绑定数据清单、处理选项及 EXE 哈希，缓存复核输出后复用；失败时保留现场，新建一代，不覆盖原始数据。短代目录名保留完整收据哈希校验，减少 Win32 装载路径长度。

父进程 28572 通过实际数据选择对话框只更新共用 INI 的 Ketsui 路径，未复制或改写旧数据。两次实际启动子进程 25740／33564，现场为 run-20261019T033318749Z-25740 和 run-20261019T033611911Z-33564。已见标准标题、模式菜单、ARCADE／DEATHTINY 核心加载、F11 全屏及 Alt+Enter 返回窗口；两次 Alt+F4 后呈现清理完成并写入绑定 PID／进程创建时间的 exit.ini，启动器恢复且没有误报崩溃。退出码仍为 27，guest_finalizers_run=false 与 host_cleanup_complete=false 保留，不记为完整退出清理修复。

第二次运行修正 CRT 独占日志，stdout.jsonl 和 stderr.txt 在游戏存活时可直接读取。run.json 含实际宿主／GPU 哈希、profile 和进程身份，另有配置快照和准备收据。相同发行位置 EXE 的 --prepare-game 再次执行得到 cacheHit=true。日志读回多个已存在保存槽，DEATHTINY 选择触发 0008-0000 的真实保存事务提交；仍需可见设置重启读回，不把文件存在等同于完整验收。公共 common／launch 两组 CTest、229 项源码及 49 项 PS4 原生／配置检查通过。

原有定向工具 verify_preparation 的现场 .local/ketsui-preparation/20261019T033907Z-5a98bad8 通过：三程序／三覆盖逐字节对照、无客体执行的装载验证、完整原生准备与缓存复用均成功，全量源数据盘点不变。此前失败的诊断根路径在缩短代名称后通过该同一工具的首次／复用检查；没有新增路径极限组合。

此次自动操作的 V 短按已通过 scePadRead 返回 Triangle，但街机 CREDIT 未增加，因此没有从启动器进入玩家第一关。屏幕中的 DEMONSTRATION 不计入玩家进关；没有为自动操作默认延长按键。原有直接后端玩家进关与联网上传证据继续保留，后续只补轻量实际缺口。

### 既有直接后端玩家进关与联网

现场 `logs/ketsui/play-20261019T031917416Z` 使用合集独立编译的宿主／GPU、原生准备的镜像与资源覆盖、根 config.ini，以及独立 gameplay-r1 profile。已实际打开五模式主菜单（含 IKD 2007 SPECIAL 和绊育成入口），ARCADE 投币、Tiger 选机进入玩家第一关，射击后得分 5，并自然结束。此处为合集后端直接启动的实战，不冒称已完成启动器接线或全模式回归。

局末 reference `e50ea91a1d02be1f6898fd50f2438df8` 在正式配置下完成两个 board 的 score／replay 请求，四次 HTTP 200、error=0，随后 archive 完成；原游戏对应 score／upload 操作均 delivered，result=0。该证据证明本次真实成绩与录像上传，不等同于榜单 UI、下载或重启回放已完成。系统保存事务已有 commit；重启读回和启动器联动继续作为轻量迁移检查。

### 启动器实战、保存与正式联网读回

`66b29e2` 之上的输入诊断桥接工作树构建，宿主 SHA-256 为 `77ed2b4db2721daa97450a4206a9baf80f10c484514c86ed3cc22b6fecbfdd37`。50 项原生／配置检查通过，报告 `.local/ps4-native-checks/20261019T044124Z-a8072dc3`，240 项源码回归通过。默认不启用输入延长；自造解析检查覆盖 0／120／500 和非法值，不涉及投币计数修改。

实际 GUI 启动器打开独立 `collection-r1`，`run-20261019T044148563Z-75128` 明确记录 `diagnosticInputTapMs=120`。CU 实际 V 投币、Enter 开始、Tiger 选机进入 ARCADE 玩家第一关，射击得到 380 分并自然结束。reference `39caa20f29aea62569ba8a131bb202cc` 的两个 board 均完成成绩及录像上传，四次 HTTP 200／error=0 后归档。BGM 70% 改为 69%，原游戏保存提示及 `0008-0000` 提交完成；局末录像和成长数据保存结束后关窗，启动器恢复可启动状态，退出清理限制仍保留。

无诊断环境的冷启动 `run-20261019T044706714Z-71384` 记录 `diagnosticInputTapMs=0`、准备缓存命中。Sound 设置实际显示 BGM 69%，原生在线 ARCADE 总榜显示 Player／380／1-1；下载该行录像返回 HTTP 200，保存事务 `0019-0000` 完成。My Replay 读回上次进程的 TYPE-A／380／12:45 记录，原播放器从选机、第一关、380 分及续关倒计时完整播放 00:53.45，自然返回同一列表。这里验证了本地新录像跨进程完整回放与网络下载保存，不把当前进程的下载记录宣称为另一次冷启动播放。实体手柄、其他模式及帧节拍专项按既定范围继续跟踪。

## P6 RC2 实际解压与代表性流程

以下为 `2e1e4c6` 的实际候选包，不是最终 v0.1 或已交付结果。两种 ZIP 均在新的中文空格目录实际解压；Public 校验 123 文件，Internal 校验 2571 文件，源码空 gitlink 目录保留，Git 状态干净。内部源码 `git pull --ff-only` 正常，无作者对象目录借用；G2／虫姬二构建数据解析指向包根数据。

固定工具从显式缓存复制到独立源码树后，全新公共组件和启动器构建成功，公共／启动两组 CTest 通过。该开发检查使用英文路径；中文源码路径会在 CMake 4.2.3 的隐式链接输出解析中异常退出，已作为构建环境限制说明，不等同于玩家中文路径不支持。未把这次公共构建写成三个后端全部重新从零构建。

| 候选包 | ZIP 字节数 | SHA-256 |
| --- | ---: | --- |
| Public RC2 | 56,905,531 | `8b59f1f60068de02c573bfbf3d5802d7a2cb6c0e9fa097df6589e21c8868fc96` |
| Internal RC2 | 2,975,106,459 | `bb1781f1e9073816818aad9a4847472dd5fc57b7a5633b56f2f82a842c8abd85` |

Internal 的 `run-20261019T043252588Z-74072` 由包内启动器启动，创建独立 smoke 存档，菜单识别 Black Label 和 Ver1.01；Black Label／Arcade／Original／Reco 实际进入玩家第一关，自然结束后 No.1 录像保存完成。正式合集服务返回 view 500／501 的榜单、排名估计、成绩和录像请求成功，四次成绩／录像上传均 HTTP 200、error=0；录像各 2180 字节，原游戏显示登记完成。局末已有的 XGI `0x000B0013` 未实现消息仍记录，不影响这次成功提交，不视为已修复。测试副本临时增加 CU 小键盘方向别名，结束后恢复；不变更发行模板或宣称实体设备重测。

Public RC2 的绊地狱普通启动合同完成原生准备及菜单进入。现有 `--diagnostic-tap-ms 120` 的单独对照中，真实 CU V 输入成功投币、选择 Tiger 并进入 ARCADE 玩家第一关，确认此前毫秒短按不足以触发街机核心；未改默认输入。`ketsui-tap-r2` 的真实局末 reference `c62bb5f61bfda2c762469274bffc60c5` 成功提交两榜成绩并归档；这局没有录像上传记录。此为同一候选运行时的显式诊断启动，不标为普通启动器玩家进关或实体输入已验证；既有真实录像上传证据继续沿用。

## 原 G2 的 P1 实际覆盖

以下表格是原 G2 工程的 P1 基线，不能当作新合集已经全部复测。

| 项目 | 状态 |
| --- | --- |
| 标题、主菜单与已有系统存档 | 原独立测试 profile 已通过，当前另复制为新的 P1 profile，保留原现场 |
| Black Label／Seseri 第一关、死亡复活与局末 | 新 P1 profile 已实际进入、暂停、恢复并自然 Game Over；No.2 保存完成 |
| 重复进退与设置 | 用户实测完成同进程三次进入第一关／返回／重新进入。系统 BGM 音量修改后已实际关闭重启读回并恢复；新进程观察到模式 LIFE 2，已恢复 3，用户接受模式设置现有覆盖，不再追加验证 |
| 保存录像跨进程读取 | No.1／No.2 Black Label、No.3 Xbox 360 及玩家录制的 No.8 Black Label Novice 均跨进程完整播放并卸载返回；No.8 用户确认正常。No.4／No.5 目前仅重启元数据读回 |
| 回放返回崩溃 | 最新回调声明修订通过一次实际旧录像回归，日志确认 dat15 卸载；不是所有返回路径验收 |
| 输入合并序号 | 自造数据先复现空闲高序号手柄掩盖键盘变化，修复后覆盖按下／松开、设备序号重置、扳机、轴、断连重连与玩家隔离；新版宿主键盘菜单、选人和暂停通过，实体手柄待测 |
| 其他模式 | PLAY 六种模式均有玩家第一关观察：dat00＝Arcade、dat11＝Arrange、dat12＝Xbox 360、dat15＝Black Label、dat163＝Xbox 360 Novice、dat164＝Black Label Novice。五个玩家模式模块在一轮进程内实际卸载返回通过；Black Label 教程第一课的进入和返回另通过。更多角色、训练与 dat162 的可达入口待扩展 |
| 视频节拍 | 已修复关闭 vsync 时将客体 VBlank 改为 1000 Hz 的耦合，原生计时测试与重建宿主的开启 vsync 回放通过；关闭对照仍待测 |
| 全屏 | G2 的 INI 启动全屏、Alt+Enter 切回窗口和 F11 往返均通过，不回写玩家配置 |
| G2 社区／Live | 原生空榜及未排名本人查询、成绩提交、跨进程榜单读回通过；Black Label Novice 7,705,305 分及 Xbox 360 7,614,133 分由原游戏分别写入六个 view。录像时区由 Windows XConfig 提供，旧文件字节不变。原生网络录像已在本机完成上传、六条成绩关联、GAMECLIP 下载、空槽 No.10 保存和自然回放返回；十五项 C++／本机 API 检查、66 项 Python 回归及九组原生测试通过。整数元数据映射已逐字段修正，新宿主实际读回高分为 Stage 2／Continue 0。正式服务未部署，默认开关仍关闭；XGI 0x000B0013 兼容清理及持久化离线补传继续跟踪 |
| 实体输入 | 当前未枚举到可用实体手柄，保持待测 |
| 音画与长期稳定性 | 宿主等待精度、VBlank 截止时间与空闲 UI 重绘已修正，实际出帧从基线约 45.6 FPS 恢复到 60 FPS；Xbox 360 回放约 74 秒内 4462 个间隔无超过 20 ms，Black Label 关卡约 30 秒内最大 19.1 ms。关卡切入附近仍见一次约 32 ms 间隔，保留观测；按用户要求不无限微调单个极值，新增明显实战停顿仍需处理。最终显示与长期稳定性未由平均 FPS 或单次音频队列正常代替 |
