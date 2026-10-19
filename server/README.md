# 合集社区服务

玩家无需注册，在根 `config.ini` 设置联网开关、API 根地址和昵称即可。昵称用于区分榜单上的玩家；相同昵称共享个人最高分。当前服务不提供身份认证或反作弊，不能把昵称当作账号所有权证明。

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

虫姬二和绊地狱两条是反向代理别名，旧 `/futari/api/v1/`、`/ketsui/api/v1/` 入口继续保留，不需要搬迁数据库。未知游戏路径返回 404，不能落入另一款游戏的榜单。

## G2 服务与开发测试

需要 Python 3.10 或更高版本，运行时仅使用标准库和 SQLite。G2 按原游戏的顺序先提交成绩，再把录像关联到同一局成绩；未分享的本地录像不自动成为网络附件。录像最大 8 MiB，附件总存储上限为 512 MiB，成绩数上限为 100,000。服务只监听 loopback，由 HTTPS 代理提供外部访问。

```sh
python3 -B -m server.games.espgaluda2 --database /path/to/new/community.sqlite3 --port 18086
python3 -B -m unittest discover -s tests -p 'test_*community*.py' -v
```

测试使用临时数据库和本机随机端口，不访问线上榜单。C++ `espgaluda2_community_service_tests --probe` 只接受隔离的 loopback 服务，避免把自造高分写到公开榜单；`--check-endpoint https://xiaolu.life/cave/api/v1` 仅通过 Windows HTTP 和原生适配读取公开榜单及本人状态，不提交成绩。真实联网验收使用游戏本身的成绩与录像流程。

## 大往生开发适配

`server/games/daioujou.py` 定义 CUSA32997／1.04 的独立协议、昵称映射和榜单，复用 PS4 的 HTTP／SQLite 存储。服务已独立部署，客户端能力仍关闭，游戏内流程尚待验收。每组编号 0–5 对应三个人偶与两种机体，6 为综合榜；传输成绩是显示分数除以十的原生整数。CUSTOM 不接受提交，也不套用绊地狱的 CUSTOM 编号规则。

构建 PS4 组件后，运行 `py -3.12 scripts/ps4/run_community_checks.py`，会创建新的 `.local` 现场和两个随机 loopback 服务，使用真实 Windows HTTP 客户端分别核对两款协议的测试成绩与读榜；不访问正式服务、不执行游戏。`scripts/ps4/run_native_checks.py` 包含协议、配置及关闭联网的原生自测。

大往生开发服务已实现本作录像结构、双版本、快照／输入边界和成绩／角色／模式匹配，先前独立测试的真实 420 分记录已通过原生与服务端结构检查。可用 `python -B -m server.games.daioujou --database <独立数据库> --port 18088` 在 loopback 启动，不自动部署。游戏内提交、下载和回放仍需闭环验收；结构通过不等同于 PSN 功能已接通。接口与记录布局参考在 `games/daioujou/community-references.json`。

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

## 服务器目录与部署

`/opt/cave-community/releases/<版本>/` 保存不可变的服务源码，G2 的 `current`、SDOJ 的 `sdoj-current` 及大往生计划使用的 `daioujou-current` 分别选择自己的版本。服务程序属于 root，各游戏的 `/var/lib/cave-community/<游戏>/` 由低权限 `cave-community` 用户写入。发行源码不带数据库、服务账号信息、TLS 私钥或凭据。以下先说明 G2 的已有部署，各标题增量步骤见后文。模板列出的服务不代表均已部署；当前线上范围见本文开头。

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

当前服务版本为 b9a8f4f，`daioujou-current` 指向 `/opt/cave-community/releases/b9a8f4f`，18088 仅监听 loopback。三个 `cave-daioujou*.in` 模板提供独立服务、每日备份与 timer；数据库及备份均在 `/var/lib/cave-community/daioujou/`，只由低权限服务用户写入。nginx 只新增本作路由，旧四款 upstream、PID、G2／SDOJ 版本指针保持不变。

部署前通过目标 Python 3.10.12 的 15 项 profile／备份／部署测试及三个 systemd 单元校验；部署后五款合集与两条旧入口的 HTTPS 健康检查通过，未知游戏返回 404。本作 49 个榜单已可读，初始为空，没有提交 fixture 分数。服务和每日备份 timer 已启用，首次快照的 Title ID、application_id 与完整性已核对；实际游戏联网仍待验收。以下为首次部署步骤与更新边界。

1. 从干净提交准备仅含 `server/__init__.py`、`server/games/__init__.py`、`server/games/ketsui.py`、`server/games/daioujou.py`、`server/backup.py`、部署模板及 profile／备份／部署测试的归档。测试还需 `scripts/ps4/replay_fixture.py`，它只生成不可游玩的自造记录，不依赖 Windows 构建工具或 ROM。记录提交、成员和哈希；不携带数据库、游戏数据或本机配置。
2. 上传到新的不可变版本目录，核验后用服务器 Python 执行 `test_ps4_community_profiles.py`、`test_community_backup.py`、`test_community_deployment.py`。这一步只使用随机 loopback 和临时 SQLite，不改玩家榜单。先记录原四款服务 PID、各版本指针与 nginx 片段哈希。
3. 确认 18088、新服务单元、版本指针及大往生数据库均不存在；有已有安装时先检查，不能当作首次部署覆盖。新建 `daioujou-current`，安装本作三个单元，执行 `systemd-analyze verify`、`daemon-reload`，只启动大往生。确认仅监听 127.0.0.1，health 为 CUSA32997／1.04。
4. 独立备份 nginx 片段，只加入大往生 location。`nginx -t` 成功才 reload，并检查五款合集与两条旧入口的身份，以及未知游戏 404。不要替换或重启绊地狱的旧 Python 服务；本作只在自己的版本目录复用其维护源码。
5. 启用本作服务和备份 timer，执行一次备份，核对 SQLite 完整性、Title ID CUSA32997 和本作 application_id。确认原四款 PID 与版本指针不变，保留部署、健康和备份记录。游戏内真实提交、榜单、录像收发与播放仍是单独验收条件。

更新只切换 `daioujou-current` 并重启本作；回滚切回旧链接。首次上线失败时停止本作，恢复 nginx 片段并先校验，不删除数据库或旧目录。不要用对本作数据库的测试结果替代其他标题的服务验收，也不要向线上写入 fixture 分数。

### 日常操作

```sh
systemctl status cave-g2.service cave-g2-backup.timer
journalctl -u cave-g2.service -n 50 --no-pager
systemctl start cave-g2-backup.service
```

日志只记录路由类别、状态和耗时，不记录请求正文、昵称、IP 或完整 URL。nginx 站点沿用关闭访问日志的设置。客户端日志仍由玩家自行提交，不由该服务收集。

每天生成一致的在线 SQLite 快照，执行完整性检查后保留最新七份。备份位于 `/var/lib/cave-community/espgaluda2/backups/`；不会复制整个 `/var/lib`，也不清理其他目录或文件。恢复前停止 G2，另存当前数据库及 WAL／SHM 现场，再把选定快照恢复为新的工作数据库，核对 identity 后启动。不要直接覆盖运行中的 SQLite 文件。

正式服务仅存放玩家主动提交的成绩与录像，不存放 ROM、资源或解压游戏程序。
