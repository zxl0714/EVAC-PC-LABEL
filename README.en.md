# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

Bringing CAVE's classic bullet-hell shooters to Windows PC.

EVAC PC LABEL is an unofficial collection of PC ports, formerly known as CAVE PC Collection. Choose a game from one launcher and use a shared configuration for display, audio, keyboard, controllers, and your community nickname. Install only the game data for the titles you want to play.

Five games are currently integrated, with saves, replays, and community leaderboards. Each game uses a dedicated PC runtime; no separate Xbox 360 or PS4 emulator installation is needed.

[Getting started](#getting-started) · [Game data guide (Chinese)](docs/GAME_DATA.md) · [Changelog (Chinese)](docs/CHANGELOG.md) · [Development guide (Chinese)](FOR_AGENT.md)

## Supported games

| Game | English name / shorthand | Supported game data |
| --- | --- | --- |
| 虫姫さまふたり Ver.1.5 | Mushihimesama Futari Ver.1.5 / Futari | Xbox 360; optional Black Label and Ver.1.01 DLC |
| エスプガルーダII ブラックレーベル | Espgaluda II Black Label / G2 | Xbox 360 |
| ケツイ Deathtiny ～絆地獄たち～ | Ketsui Deathtiny / Ketsui | PS4, CUSA09062; base game and 1.02 update in separate folders |
| 怒首領蜂最大往生 | DoDonPachi SaiDaiOuJou / SDOJ | Xbox 360, original disc data |
| 怒首領蜂 大往生 臨廻転生 | DoDonPachi DaiOuJou Re:Incarnation / DaiOuJou | PS4, CUSA32997; complete merged 01.04 data |

エスプレイドΨ (ESP Ra.De. Psi) is planned for a future version and marked Coming Soon in the launcher.

Data may differ between platforms, regions, and updates, even for the same title. Use the versions listed above and follow the [game data guide (Chinese)](docs/GAME_DATA.md). DaiOuJou and SaiDaiOuJou are different games.

## Getting started

### System requirements

Use Windows x64 with your GPU vendor's graphics drivers. Futari, G2, and SDOJ require Direct3D 12; Ketsui and DaiOuJou require Vulkan. Application runtime libraries are included in the player package.

### First launch

1. Extract the complete **Public package** into a writable folder, keeping all included files. Supply your own game data. GitHub's source ZIP is for development, not a ready-to-play package.
2. Run `CAVE-PC-Collection.exe` in the collection's root folder. `启动游戏.bat` is an alternative entry point. Select `EN` at the top right of the launcher to use English.
3. Select a game, then choose **Game data… → Choose data folder…** at the bottom. Select the folder containing the extracted game files. Alternatively, place the data in the default location shown below.
4. Click **Launch game**. The launcher verifies the data before starting; PS4 games also prepare their runtime files on first launch. Allow this process to finish.

The current release still uses the filename `CAVE-PC-Collection.exe`. Renaming the executable and release packages to EVAC PC LABEL is planned for v0.4; existing versions remain usable.

### Where to put game data

Use **Choose data folder…** to point to an existing folder without duplicating the data. Original files are not moved or modified. To keep everything in the collection, use **Copy into collection…** to copy the files to the default location; the destination must be empty.

| Game | Default folder | Files or folders directly inside it |
| --- | --- | --- |
| Futari | `game-data/futari/` | `default.xex`, `media/`, and the remaining resources |
| G2 | `game-data/espgaluda2/` | `default.xex`, `dat*.xex`, and the remaining resources |
| Ketsui | `game-data/ketsui/` | `CUSA09062-app/` and `CUSA09062-patch/` |
| SDOJ | `game-data/sdoj/` | `default.xex`, `CA022100.bin`, `CA022110.bin`, and the remaining resources |
| DaiOuJou | `game-data/daioujou/` | `eboot.bin`, the `.prx` modules, and the remaining resources |

Create the default folders as needed; you only need data for the games you want to play. The files listed above identify the correct folder level, not the complete file list. Preserve all game resources. The launcher checks file contents, not just folder names.

Extract ISO or PKG images into game files first: the launcher accepts extracted folders. If an ordinary ZIP contains already-extracted game files, unpack it and select the actual game root, rather than an extra enclosing folder.

**Ketsui v0.3.2 still requires the two-folder layout and the 1.02 update.** Base-only data and a merged base/update folder are not supported by this release. The single-folder approach is planned for v0.4; see the [implementation plan (Chinese)](docs/IMPLEMENTATION_PLAN.md). Folder examples, DLC placement, and data detection help are in the [game data guide (Chinese)](docs/GAME_DATA.md).

## Settings and controls

Use the launcher header to change fullscreen mode and your leaderboard nickname. Select `CN / EN / JP` at the top right to change the launcher's language. In-game languages depend on the original game.

The **Config file** action at the bottom opens `user-data/config.ini`. Edit audio, keyboard, controller, and online settings there. Save the file, click **Refresh** in the launcher, and start the game for the changes to take effect. See the [default configuration template](config/default.ini) for available options.

| Default keyboard input | Function |
| --- | --- |
| Arrow keys | Directional input |
| Z / X / C / V | Bottom / right / left / top controller face button |
| Enter / Backspace | Start / Select (Touchpad for PS4) |
| A / S, Q / W | L1 / R1, L2 / R2 |
| F11 or Alt+Enter | Toggle fullscreen |
| Alt+F4 | Close the game |
| F8 | Add a timestamp marker to the log for troubleshooting |

Actions such as shooting, bombing, and switching characters depend on each game's own control settings. Controllers and arcade sticks are handled through SDL, including the XInput and PlayStation devices it supports.

## Community leaderboards

Online features are enabled by default, with no registration required. Change the default nickname `Player` to a recognizable name in the launcher, then use the game's leaderboard and replay screens. Your nickname is displayed on the community leaderboards.

These features use the collection's community service; no Xbox Live or PlayStation Network login is needed. Each title has separate leaderboards. Score eligibility and applicable replay workflows follow the game's rules.

```ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
```

Set `enabled` to `false` to play offline. SDOJ's coins and shop unlocks follow the original game's progression.

## Updates and backups

The current maintenance version is **v0.3.2**. It fixes DaiOuJou's AC-CHALLENGE leaderboard and adds Ketsui's CUSTOM overall, Type-A, and Type-B leaderboards with their replay features.

[Download the v0.3.2 update package](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip) · [SHA-256 checksum file](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip.sha256)

This link is an **update for an existing v0.3, v0.3.1, or earlier v0.3.2 installation**, not a complete first-install package. Close the games and launcher, then extract the ZIP directly into your existing collection folder, replacing files with the same names. It includes the previous v0.3.1 changes; see the [changelog (Chinese)](docs/CHANGELOG.md).

- `user-data/`: configuration, per-game saves, replays, and other player data. Keep the whole folder when backing up or moving an installation.
- `game-data/`: the game files you provide. Keep them during an in-place update. When moving the installation, either move them too or select their existing location again.
- `logs/`: run logs. Keep the logs from any session in which you encounter a problem.

An older root-level `config.ini` is automatically migrated to `user-data/config.ini` on first launch. If the new location already has a configuration, that file takes precedence. Relative data paths are always resolved from the collection's root folder.

## Troubleshooting and feedback

For missing data or unsupported-version messages, check the folder level and version requirements in the [data troubleshooting guide (Chinese)](docs/GAME_DATA.md#无法识别时怎么检查). Renaming files or removing validation entries does not resolve a version mismatch.

When reporting a crash, stutter, audio issue, or network problem, include:

- The collection version, game, mode, and character or ship.
- Steps to reproduce the issue and what happened; a screenshot or short video can help.
- A ZIP of the complete corresponding `run-*` folder under `logs/<game ID>/`.

Press F8 when the problem occurs to mark its time, then close the game and collect the logs. `release.json` identifies the program version and can be included if the build is unclear. Check logs for personal information such as local paths before sharing them publicly. Game data is not needed in a bug report.

## Development

The project shares its launcher, configuration, and common components, with separate Xbox 360 and PS4 backends and per-game adaptations. The source repository does not include game data, generated game program bodies, or player saves.

Start with [FOR_AGENT.md (Chinese)](FOR_AGENT.md) for environment setup, building, running, and packaging. Read [AGENTS.md](AGENTS.md) before making changes. The [implementation plan (Chinese)](docs/IMPLEMENTATION_PLAN.md) tracks upcoming work, and the [verification matrix (Chinese)](docs/TEST_MATRIX.md) records tested behavior. Include the affected game and program version in bug reports and contributions.

## Credits and licenses

The project builds on ReXGlue/Xenia-related components, graphics components derived from shadPS4, SDL, and other open-source work. See the [source origins](docs/SOURCE_ORIGINS.json), [ReXGlue license](third_party/rexglue/LICENSE), [PS4 third-party notices](third_party/ps4/THIRD_PARTY_NOTICES.txt), and [banner artwork sources](launcher/artwork.json). Components retain their respective licenses.

Release packages include `licenses/` and `SOURCE.txt`. The latter explains how to obtain the matching maintained source and build instructions for that program version. Public program packages and source attachments do not include game data.
