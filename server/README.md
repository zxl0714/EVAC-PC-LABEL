# 合集社区服务

玩家无需注册，在 `user-data/config.ini` 设置联网开关、API 根地址和昵称即可。昵称用于区分榜单上的玩家；相同昵称共享个人最高分。当前服务不提供身份认证或反作弊，不能把昵称当作账号所有权证明。

```ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
```

客户端在 API 根地址后追加游戏路径。HTTP 使用整数或十进制字符串传递 64 位成绩，不使用浮点数。各游戏的原生成绩、录像格式和数据库独立管理。

| 路径 | 本机上游 | 数据库 |
| --- | --- | --- |
| `/cave/api/v1/games/espgaluda2/` | `127.0.0.1:18086` | `/var/lib/cave-community/espgaluda2/community.sqlite3` |
| `/cave/api/v1/games/futari/` | `127.0.0.1:18084/futari/api/v1/` | 沿用虫姬二服务的数据目录 |
| `/cave/api/v1/games/ketsui/` | `127.0.0.1:18085/ketsui/api/v1/` | 沿用绊地狱服务的数据目录 |
| `/cave/api/v1/games/sdoj/` | `127.0.0.1:18087` | `/var/lib/cave-community/sdoj/community.sqlite3` |
| `/cave/api/v1/games/daioujou/` | `127.0.0.1:18088` | `/var/lib/cave-community/daioujou/community.sqlite3` |
| `/cave/api/v1/games/esprade/` | `127.0.0.1:18089` | `/var/lib/cave-community/esprade/community.sqlite3` |
| `/cave/api/v1/games/guwange/` | `127.0.0.1:18090` | `/var/lib/cave-community/guwange/community.sqlite3` |

Guwange 已部署独立服务及每日备份，源码固定为 `2bf184f41fb5`，版本指针为 `/opt/cave-community/guwange-current`。七款合集入口、两条旧入口及首次备份检查通过，原六款服务进程和版本指针保持不变。

虫姬二和绊地狱两条是反向代理别名，旧 `/futari/api/v1/`、`/ketsui/api/v1/` 入口继续保留，不需要搬迁数据库。未知游戏路径返回 404，不能落入另一款游戏的榜单。

## G2 服务与开发测试

需要 Python 3.10 或更高版本，运行时仅使用标准库和 SQLite。G2 按原游戏的顺序先提交成绩，再把录像关联到同一局成绩；未分享的本地录像不自动成为网络附件。录像最大 8 MiB，附件总存储上限为 512 MiB，成绩数上限为 100,000。服务只监听 loopback，由 HTTPS 代理提供外部访问。

```sh
python3 -B -m server.games.espgaluda2 --database /path/to/new/community.sqlite3 --port 18086
python3 -B -m unittest discover -s tests -p 'test_*community*.py' -v
```

测试使用临时数据库和本机随机端口，不访问线上榜单。C++ `espgaluda2_community_service_tests --probe` 只接受隔离的 loopback 服务，避免把自造高分写到公开榜单；`--check-endpoint https://xiaolu.life/cave/api/v1` 仅通过 Windows HTTP 和原生适配读取公开榜单及本人状态，不提交成绩。真实联网验收使用游戏本身的成绩与录像流程。

## 绊地狱 CUSTOM

原作 CUSA09062／1.02 为 CUSTOM 定义综合／Type-A／Type-B 三个榜，编号
60／61／62，录像上传名次限制分别为 100／10／10。客户端与服务统一使用
`rules=standard`，不额外按难度或自机数拆榜；这些参数随原始录像快照保留。
CUSTOM 录像的原生 mode 为 3，不能发到 ARCADE 等其他模式榜。是否可以登记
仍由游戏原有资格判断决定，包括续关和双人限制。

历史 API 显式传入的 `custom-<64位十六进制摘要>` 分区保持可读写，数据不会
并入原生榜。`scripts/ps4/run_community_checks.py` 包含三个 CUSTOM 榜的
原生 ABI／HTTP 成绩与录像收发，以及整局 worker 的持久化、附件原字节证明和
归档检查；均使用临时 SQLite 与随机 loopback，不向正式榜单写入自造记录。
原生定义的来源和哈希见 `games/ketsui/community-references.json`。

当前绊地狱 `current` 指向 `/opt/ketsui-live/releases/a8a75fb`，继续使用原
`ketsui-live.service`、18085 端口和 `/var/lib/ketsui-live/community.sqlite3`。
旧 adb46f8 发行目录和 `pre-v032-a8a75fb/community.sqlite3` 一致性快照保留。
更新经过 44 项目标 Python 测试及已有数据库副本兼容检查，原有成绩录像不变；
旧入口和合集入口的 CUSTOM 榜均可读，其他四款服务未重启。回滚只切回旧
`current` 并重启绊地狱服务，不覆盖工作数据库。

## 大往生开发适配

`server/games/daioujou.py` 定义 CUSA32997 的独立协议、昵称映射和榜单，复用 PS4 的 HTTP／SQLite 存储，同时接受 1.00 与 1.04 的原生录像。每组编号 0–5 对应三个人偶与两种机体，6 为综合榜，99 为 AC-CHALLENGE；传输成绩是显示分数除以十的原生整数。CUSTOM 不接受提交，也不套用绊地狱的 CUSTOM 编号规则。各版本录像保留原始字节，由游戏执行原生版本兼容检查。

AC-CHALLENGE 使用独立的 99 号榜，累计 66 场景、两种机体和三个人偶的奖牌积分，上限 3,960,000。金／银／铜牌分别为 10,000／3／1 分，按原生整数传输，不乘普通分数的十倍单位。原游戏榜单定义的 `uploadable=0`，只登记积分而不上传占位录像。本作共 50 个可读榜单；普通 CUSTOM 提交限制保持不变。

构建 PS4 组件后，运行 `py -3.12 scripts/ps4/run_community_checks.py`，会创建新的 `.local` 现场，共十二项检查：三作普通成绩／录像、大往生 AC-CHALLENGE、绊地狱三个 CUSTOM 榜的原生 ABI／HTTP 收发、CUSTOM 整局 worker 及本体版本录像。每项使用独立随机 loopback 与 SQLite；录像是不可游玩的自造结构。还会核对新高分出现后旧附件传输不串项、分值不符拒收、字节哈希与服务存储。该入口不访问正式服务、不执行游戏，也不验证原生回放。`scripts/ps4/run_native_checks.py` 另有协议、配置及关闭联网的原生自测。

大往生服务校验本作网络结构、双版本、快照／输入边界和成绩／角色／模式匹配。本地存档记录与网络附件的尺寸不同；原生临时 CUSTOM=3 归入合资格 ARCADE 榜的行为单独验证，未开放 CUSTOM 榜或重写录像字节。可用 `python -B -m server.games.daioujou --database <独立数据库> --port 18088` 在 loopback 启动，不自动部署。接口、记录布局和代表性真实收发／回放身份在 `games/daioujou/community-references.json`，完整范围见验收矩阵。

## ESP Ra.De. 开发适配

`server/games/esprade.py` 适配 CUSA15155／01.04，使用 `/cave/api/v1/games/esprade/`、独立数据库和昵称映射。ARCADE、SUPER EASY、ARCADE PLUS（原生 `premium`）与 CUSTOM 各有综合及四角色榜；88／99 为仅提交进度的 OSARAI／いろりの部屋榜，不接受录像。成绩沿用本作原始整数，不套用大往生的十倍显示单位。

运行本机服务：

```sh
python3 -B -m server.games.esprade --database /path/to/new/esprade.sqlite3 --port 18089
python3 -B -m unittest discover -s tests -p 'test_esprade_community.py' -v
```

正式服务已部署，使用 `cave-esprade` 服务和每日备份单元，版本指针 `/opt/cave-community/esprade-current`，状态目录 `/var/lib/cave-community/esprade/`，本机监听端口 18089。共享 endpoint 自动分流到本作路径。首次备份、六款健康检查和旧入口检查通过，部署前后原五款服务 PID 不变。

原生 1390 分的提交、读榜、录像上传／下载及冷启动离线播放通过独立 loopback。正常合集启动器的正式 HTTPS 测试实际打出 3300 分，综合／祐介榜提交、录像上传、游戏内读榜及下载通过；部署源码为 `d66c11afdee8`，记录见 `docs/TEST_MATRIX.md`。不得向正式榜单写入自造测试成绩。

部署先核对 18089、服务单元、版本指针和数据库状态；在新的源码目录完成哈希与测试核验后，仅启用 ESP 服务。独立备份 nginx 片段，只追加本作路由，校验通过后 reload。最后核对六款健康身份、原五款 PID 不变和首次 SQLite 备份完整性。测试使用临时数据库，不向正式榜单写入自造记录。

## SDOJ 服务与验收范围

SDOJ 的独立适配位于 `server/games/sdoj.py`，复用有界 HTTP／SQLite 存储而保留本作的 39 个个人榜单、属性和原生录像格式。服务监听 `127.0.0.1:18087`，路径为 `/cave/api/v1/games/sdoj/`，Title ID 为 `435A07E7`。独立服务及每日备份已部署，v0.2 的游戏内真实提交、读榜、录像收发和播放已验收，个人榜单能力开放；团队评分未接入。开发时可用下面的命令另启本机数据库。

```sh
python3 -B -m server.games.sdoj --database /path/to/new/sdoj.sqlite3 --port 18087
```

原生录像为 304 字节头，加权半字校验和与元数据核对后保持完整原字节；不转换为 G2 格式，也不重写录像时间。成绩先提交、录像随后关联；服务确认附件后才对榜单公开可下载大小。未完成个人网络流程或未覆盖的团队评分不声明支持。

`sdoj_community_tests --check-endpoint https://xiaolu.life/cave/api/v1` 仅通过实际 Windows HTTP 客户端读取 Shot 榜单和本人状态，不提交成绩、不执行游戏，也不代替游戏内验收。包含写入的 `--probe` 仍只接受独立 loopback 服务。

## 虫姬二客户端与本机验证

虫姬二保留自身原生录像／成绩时序。`scripts/futari/local_service.py` 可在全新 `.local` 目录启动随机端口的本机服务，使用相同协议而不改正式路由或数据库；命令和配置方法见 [分析工具](../docs/ANALYSIS.md#虫姬二本机联网现场)。无需 ROM 的传输与故障注入检查由 `tests/futari/run_native_community.py` 启动独立临时服务。

客户端新成绩 journal 使用 version=2，成绩、待关联录像及录像重试文件均保存 `origin.game` 和原始游戏 API 端点。只有当前游戏与端点完全匹配时才自动重试，重试继续使用原昵称、构建及提交／成绩引用；端点末尾斜杠统一去除，其他别名不自动视为同一服务。`origin` 只用于本地 journal，不增加网络协议字段。

修改 endpoint 后，其他服务的任务仍保留在原目录但不会发送。缺少来源的旧版 journal 同样保留，不按新配置猜测归属；旧记录的显式迁移尚未实现。已完成的 history 不受影响。改昵称不会改写已排队成绩或使本地存档改目录。

## Guwange 开发适配（v0.7）

`server/games/guwange.py` 对应 XBLA 本体 0.0.0.4，Title ID `58410A78`。普通、Blue、Arrange 分别使用 1–4、5–8、9–12 号榜；每组依次为综合、Shishin、Gensuke、Kosame。保留原生分数及游戏自身的提交资格，不提供在线合作。

```sh
python3 -B -m server.games.guwange --database /path/to/new/guwange.sqlite3 --port 18090
python3 -B -m unittest discover -s tests -p 'test_guwange_community_server.py' -v
```

客户端沿用共享 endpoint、联网开关和昵称。原生流程先提交成绩，再上传合资格录像；服务器将模式、角色、分数、关卡与对应榜单绑定，完整保存原录像字节。成绩时间与录像时间由游戏分别生成，不要求相等。新昵称的单条个人最佳查询返回未上榜的零记录，避免原游戏直接解引用空行；该兼容结果不写入数据库，也不出现在公开榜单。

离线协议、HTTP 收发和备份恢复检查使用临时数据库。本机实际 Blue 2370 分已完成完整联网流程；正式 HTTPS 也通过正常合集入口的 Arrange／Shishin 5336 分提交、综合与角色榜读回、录像上传下载、保存及完整回放。两榜关联同一份 183230 字节原生录像。详细身份见 [验收矩阵](../docs/TEST_MATRIX.md)。

## 服务器目录与部署

`/opt/cave-community/releases/<版本>/` 保存不可变的服务源码。G2 使用 `current`，SDOJ、大往生和 ESP Ra.De. 分别使用 `sdoj-current`、`daioujou-current`、`esprade-current`；Guwange 预留 `guwange-current`。各指针独立选择版本。服务程序属于 root，各游戏的 `/var/lib/cave-community/<游戏>/` 由低权限 `cave-community` 用户写入。发行源码不带数据库、服务账号信息、TLS 私钥或凭据。以下先说明 G2 的已有部署，各标题增量步骤见后文。模板列出的服务不代表均已部署；当前线上范围见本文开头。

从已提交版本准备一个仅含下列文件的部署归档，记录 commit 与每个文件的 SHA-256，再上传并解压到新的版本目录：

- `server/__init__.py`、`server/games/__init__.py`、`server/games/espgaluda2.py`、`server/backup.py`。
- `server/deploy/` 下的服务、定时器和 nginx 模板。
- `tests/test_g2_community_server.py`、`tests/test_community_backup.py`，用于目标 Python 版本的上线前回归。

在目标目录运行上述 22 项测试，然后按以下步骤启用。`.in` 模板已经是有效配置，安装时去掉扩展名，不做文本变量替换。

1. 确认 `18086` 未占用，三个新 systemd 单元和 nginx 片段不存在。建立系统用户 `cave-community`，不创建可登录账号或密码。
2. 将 `current` 指向新版本。安装 `cave-g2.service.in`、`cave-g2-backup.service.in`、`cave-g2-backup.timer.in` 到 `/etc/systemd/system/`，并安装 nginx 片段为 `/etc/nginx/snippets/cave-live-locations.conf`。
3. 在既有 `xiaolu.life` 的 HTTPS server 块内加入 `include /etc/nginx/snippets/cave-live-locations.conf;`。先保存该站点配置的独立备份，不改原有 location、TLS 或其他域名配置。片段复用该站点已经定义的 `futari_requests`、`ketsui_requests` 限流区。
4. `systemctl daemon-reload` 后启动 `cave-g2.service`，先从本机检查 G2 `/health`。确认服务正常后执行 `nginx -t`，成功才 reload nginx。
5. 通过 HTTPS 分别检查三款合集 `/health`，以及两条旧入口。对照 Title ID：G2 为 `435A07D8`，虫姬二为 `435A07D7`，绊地狱为 `CUSA09062`；旧入口仍应返回原服务。
6. 启用 G2 服务和备份定时器，运行一次备份，并核对数据库身份及快照完整性。上线后再用游戏实际验证榜单、成绩和适用录像，不以健康检查代替游戏内验收。

更新时先创建、测试新版本目录，再切换 `current` 并仅重启 G2 服务。保留旧版本；不重建数据库，不重启虫姬二或绊地狱。失败时切回旧链接并重启 G2；首次部署若 nginx 校验失败，恢复站点备份，不 reload 无效配置。

## 运维与备份

### 增量部署 SDOJ

SDOJ 使用单独的 `sdoj-current` 链接、`cave-sdoj.service`、18087 端口和 `/var/lib/cave-community/sdoj/`。不切换 G2 的 `current`，不重启另外三款服务。将 SDOJ 服务启用与客户端游戏内验收分开记录。

1. 确认 SDOJ 三个单元、`sdoj-current`、数据库和 18087 监听均不存在。将已提交的 `server/games/sdoj.py`、共用的 `espgaluda2.py`／包入口／`backup.py`、部署模板及两款服务与备份测试打成仅含源码的归档，记录提交和 SHA-256。
2. 上传到新的 `/opt/cave-community/releases/<提交>/`，先对归档与文件逐一核验，使用服务器 Python 执行服务、备份和部署模板测试。目录属于 root，不能由 `cave-community` 改写。
3. 创建 `sdoj-current` 指向该版本，将 SDOJ 三个模板安装为无 `.in` 扩展的 systemd 单元。执行 `systemd-analyze verify`、`daemon-reload`，只启动 SDOJ。检查本机健康响应为 `435A07E7`，监听仅限 127.0.0.1。
4. 保存当前 nginx 片段的独立备份，确认原三款 location 与现用配置完全相同，只增加模板中的 SDOJ location，不安装尚未部署作品的路由。安装片段并执行 `nginx -t`；失败时恢复片段备份，不 reload。成功后只 reload nginx，检查四款合集入口与两条旧入口的健康响应；未知游戏仍返回 404。
5. 启用 SDOJ 服务／每日备份 timer，手动运行一次备份，核对快照的数据库身份与 SQLite 完整性。原三款服务的 PID 和 G2 的 `current` 应保持不变。

更新仅切换 `sdoj-current` 并重启 SDOJ，保留旧发行目录和数据库。回滚切回旧链接并重启该服务，不覆盖工作数据库。第一次上线失败时可以停止 SDOJ 并恢复 nginx 片段，不删除数据库现场；服务端健康和空榜读回不是游戏内提交／录像播放验收。

### 增量部署大往生

当前 `daioujou-current` 指向 `/opt/cave-community/releases/daioujou-v076-9393c10ee402`，支持 1.00 与 1.04，18088 仅监听 loopback。数据库和每日备份目录不变；更新前的一致性快照位于 `/var/lib/cave-community/daioujou/pre-v076-9393c10ee402/community.sqlite3`。旧 `a865369` 发行目录保留，可切回链接回滚。三个 `cave-daioujou*.in` 模板提供独立服务、每日备份与 timer。

服务和每日备份 timer 已启用。v0.7.6 在目标 Python 3.10.12 通过 23 项 profile／备份／部署测试；快照的 Title ID、application_id、完整性及既有记录逐行对照通过。七款 HTTPS 健康检查和本作普通／综合／挑战榜读取正常，其他六款服务和 nginx 未修改。以下为首次部署步骤与更新边界。

1. 从干净提交准备仅含 `server/__init__.py`、`server/games/__init__.py`、`server/games/ketsui.py`、`server/games/daioujou.py`、`server/backup.py`、部署模板及 profile／备份／部署测试的归档。测试还需 `scripts/ps4/replay_fixture.py`，它只生成不可游玩的自造记录，不依赖 Windows 构建工具或 ROM。记录提交、成员和哈希；不携带数据库、游戏数据或本机配置。
2. 上传到新的不可变版本目录，核验后用服务器 Python 执行 `test_ps4_community_profiles.py`、`test_community_backup.py`、`test_community_deployment.py`。这一步只使用随机 loopback 和临时 SQLite，不改玩家榜单。先记录原四款服务 PID、各版本指针与 nginx 片段哈希。
3. 确认 18088、新服务单元、版本指针及大往生数据库均不存在；有已有安装时先检查，不能当作首次部署覆盖。新建 `daioujou-current`，安装本作三个单元，执行 `systemd-analyze verify`、`daemon-reload`，只启动大往生。确认仅监听 127.0.0.1，health 为 CUSA32997／1.04。
4. 独立备份 nginx 片段，只加入大往生 location。`nginx -t` 成功才 reload，并检查五款合集与两条旧入口的身份，以及未知游戏 404。不要替换或重启绊地狱的旧 Python 服务；本作只在自己的版本目录复用其维护源码。
5. 启用本作服务和备份 timer，执行一次备份，核对 SQLite 完整性、Title ID CUSA32997 和本作 application_id。确认原四款 PID 与版本指针不变，保留部署、健康和备份记录。游戏内真实提交、榜单、录像收发与播放仍是单独验收条件。

更新只切换 `daioujou-current` 并重启本作；回滚切回旧链接。首次上线失败时停止本作，恢复 nginx 片段并先校验，不删除数据库或旧目录。不要用对本作数据库的测试结果替代其他标题的服务验收，也不要向线上写入 fixture 分数。

### 增量部署 Guwange

完成本机实际成绩／录像闭环后，再部署正式服务。准备一个干净提交的源码归档，包含 `server/__init__.py`、`server/games/__init__.py`、`server/games/espgaluda2.py`、`server/games/guwange.py`、`server/backup.py`、`server/deploy/` 以及 `tests/test_guwange_community_server.py`、`tests/test_community_backup.py`、`tests/test_community_deployment.py`。保存提交号、归档和成员 SHA-256。

1. 记录现有六款服务的 PID、版本指针及 nginx 片段哈希，确认 18090、Guwange 三个单元、版本指针和数据目录没有已有安装。已有安装应走更新流程，不能覆盖当作首次部署。
2. 解压到新的只读发行目录，核验文件身份，在服务器 Python 上运行上述三组测试；测试仅使用临时数据库。建立 `guwange-current` 指针，安装三个 `cave-guwange*.in` 模板，去掉 `.in` 后缀。执行 `systemd-analyze verify` 和 `daemon-reload`，只启动 `cave-guwange.service`。
3. 确认仅监听 `127.0.0.1:18090`，本机 `/cave/api/v1/games/guwange/health` 返回 `58410A78`。独立备份 nginx 片段，只追加 Guwange location；`nginx -t` 成功才 reload。检查七款合集入口、两条旧入口和未知游戏 404，核对原六款 PID 与版本指针不变。
4. 启用服务和每日备份 timer，运行一次 `cave-guwange-backup.service`，核对快照的 `identity`、SQLite 完整性及外键。保留最新七份一致性快照。最后使用真实游玩记录完成正式 HTTPS 的提交、读榜、录像收发和回放；不提交自造成绩。

后续更新只切换 `guwange-current` 并重启本作。回滚切回旧指针，不覆盖工作数据库；首次安装失败时停止本作、恢复并校验 nginx 片段，保留数据库现场。

### 日常操作

```sh
systemctl status cave-g2.service cave-g2-backup.timer
journalctl -u cave-g2.service -n 50 --no-pager
systemctl start cave-g2-backup.service
```

日志只记录路由类别、状态和耗时，不记录请求正文、昵称、IP 或完整 URL。nginx 站点沿用关闭访问日志的设置。客户端日志仍由玩家自行提交，不由该服务收集。

每天生成一致的在线 SQLite 快照，执行完整性检查后保留最新七份。备份位于 `/var/lib/cave-community/espgaluda2/backups/`；不会复制整个 `/var/lib`，也不清理其他目录或文件。恢复前停止 G2，另存当前数据库及 WAL／SHM 现场，再把选定快照恢复为新的工作数据库，核对 identity 后启动。不要直接覆盖运行中的 SQLite 文件。

正式服务仅存放玩家主动提交的成绩与录像，不存放 ROM、资源或解压游戏程序。
