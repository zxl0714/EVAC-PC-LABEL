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

后两条是反向代理别名，旧 `/futari/api/v1/`、`/ketsui/api/v1/` 入口继续保留，不需要搬迁数据库。未知游戏路径返回 404，不能落入另一款游戏的榜单。

## G2 服务与开发测试

需要 Python 3.10 或更高版本，运行时仅使用标准库和 SQLite。G2 按原游戏的顺序先提交成绩，再把录像关联到同一局成绩；未分享的本地录像不自动成为网络附件。录像最大 8 MiB，附件总存储上限为 512 MiB，成绩数上限为 100,000。服务只监听 loopback，由 HTTPS 代理提供外部访问。

```sh
python3 -B -m server.games.espgaluda2 --database /path/to/new/community.sqlite3 --port 18086
python3 -B -m unittest discover -s tests -p 'test_*community*.py' -v
```

测试使用临时数据库和本机随机端口，不访问线上榜单。C++ `espgaluda2_community_service_tests --probe` 只接受隔离的 loopback 服务，避免把自造高分写到公开榜单；`--check-endpoint https://xiaolu.life/cave/api/v1` 仅通过 Windows HTTP 和原生适配读取公开榜单及本人状态，不提交成绩。真实联网验收使用游戏本身的成绩与录像流程。

## 虫姬二客户端与本机验证

虫姬二保留自身原生录像／成绩时序。`scripts/futari/local_service.py` 可在全新 `.local` 目录启动随机端口的本机服务，使用相同协议而不改正式路由或数据库；命令和配置方法见 [分析工具](../docs/ANALYSIS.md#虫姬二本机联网现场)。无需 ROM 的传输与故障注入检查由 `tests/futari/run_native_community.py` 启动独立临时服务。

客户端新成绩 journal 使用 version=2，成绩、待关联录像及录像重试文件均保存 `origin.game` 和原始游戏 API 端点。只有当前游戏与端点完全匹配时才自动重试，重试继续使用原昵称、构建及提交／成绩引用；端点末尾斜杠统一去除，其他别名不自动视为同一服务。`origin` 只用于本地 journal，不增加网络协议字段。

修改 endpoint 后，其他服务的任务仍保留在原目录但不会发送。缺少来源的旧版 journal 同样保留，不按新配置猜测归属；旧记录的显式迁移尚未实现。已完成的 history 不受影响。改昵称不会改写已排队成绩或使本地存档改目录。

## 服务器目录与部署

`/opt/cave-community/releases/<版本>/` 保存一份不可变的服务源码，`current` 指向启用版本。服务程序属于 root，仅 `/var/lib/cave-community/espgaluda2/` 由低权限 `cave-community` 用户写入。发行源码不带数据库、服务账号信息、TLS 私钥或凭据。

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

```sh
systemctl status cave-g2.service cave-g2-backup.timer
journalctl -u cave-g2.service -n 50 --no-pager
systemctl start cave-g2-backup.service
```

日志只记录路由类别、状态和耗时，不记录请求正文、昵称、IP 或完整 URL。nginx 站点沿用关闭访问日志的设置。客户端日志仍由玩家自行提交，不由该服务收集。

每天生成一致的在线 SQLite 快照，执行完整性检查后保留最新七份。备份位于 `/var/lib/cave-community/espgaluda2/backups/`；不会复制整个 `/var/lib`，也不清理其他目录或文件。恢复前停止 G2，另存当前数据库及 WAL／SHM 现场，再把选定快照恢复为新的工作数据库，核对 identity 后启动。不要直接覆盖运行中的 SQLite 文件。

正式服务仅存放玩家主动提交的成绩与录像，不存放 ROM、资源或解压游戏程序。
