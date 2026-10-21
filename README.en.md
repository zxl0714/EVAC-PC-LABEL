# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

Bringing CAVE's classic bullet-hell shooters to Windows PC.

EVAC PC LABEL is an unofficial collection of CAVE PC ports with a unified launcher, shared configuration, saves, replays, and community leaderboards. Install game data only for the titles you want to play.

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

## Getting started

### System requirements

Use Windows x64 with your GPU vendor's graphics drivers. Futari, G2, and SDOJ require Direct3D 12; Ketsui and DaiOuJou require Vulkan. Application runtime libraries are included in the player package.

### First launch

1. Extract the complete program package into a writable folder. Supply your own game data.
2. Run `CAVE-PC-Collection.exe` in the collection's root folder. `启动游戏.bat` is an alternative entry point. Select `EN` at the top right of the launcher to use English.
3. Select a game, then choose **Game data… → Choose data folder…** at the bottom. Select the folder containing the extracted game files. Alternatively, place the data in the default location shown below.
4. Click **Launch game**. The launcher verifies the data before starting; PS4 games also prepare their runtime files on first launch. Allow this process to finish.

### Where to put game data

Use **Choose data folder…** to use an existing folder without modifying its files, or **Copy into collection…** to copy it to a default location below. The copy destination must be empty.

| Game | Default folder | Files or folders directly inside it |
| --- | --- | --- |
| Futari | `game-data/futari/` | `default.xex`, `media/`, and the remaining resources |
| G2 | `game-data/espgaluda2/` | `default.xex`, `dat*.xex`, and the remaining resources |
| Ketsui | `game-data/ketsui/` | `CUSA09062-app/` and `CUSA09062-patch/` |
| SDOJ | `game-data/sdoj/` | `default.xex`, `CA022100.bin`, `CA022110.bin`, and the remaining resources |
| DaiOuJou | `game-data/daioujou/` | `eboot.bin`, the `.prx` modules, and the remaining resources |

Create folders as needed and preserve all game files and their internal folder structure.

Extract ISO or PKG images into game files first. For ZIP archives, unpack them and select the actual game root.

See the [game data guide (Chinese)](docs/GAME_DATA.md) for complete folder examples, DLC placement, and data detection help.

## Settings and controls

Use the launcher header to change fullscreen mode and your leaderboard nickname. Select `CN / EN / JP` at the top right to change the launcher's language. In-game languages depend on the original game.

The **Config file** action at the bottom opens `user-data/config.ini`. Edit audio, keyboard, controller, and online settings there. Save the file, click **Refresh** in the launcher, and start the game for the changes to take effect. See the [default configuration template](config/default.ini) for available options.

For SDOJ, set `frame_delay = 3` under `[game.sdoj]` to use the original three-frame path. This will be the default in the next release. Change an existing 1V or 2V setting to 3 manually.

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

Online features are enabled by default, with no registration required. Set your nickname in the launcher, then use the community service through the game's leaderboard and replay screens. Each title has separate leaderboards, with score eligibility following the game's rules.

```ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
```

Set `enabled` to `false` to play offline.

## Updates and backups

Current version: **v0.3.2**. See the [changelog (Chinese)](docs/CHANGELOG.md) for release notes.

[Download the v0.3.2 update package](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip) · [SHA-256 checksum file](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip.sha256)

The update package is for an existing v0.3, v0.3.1, or earlier v0.3.2 installation. Close the games and launcher, then extract the ZIP into your existing collection folder, replacing files with the same names. First-time installations need the complete program package.

- `user-data/`: configuration, per-game saves, replays, and other player data. Keep the whole folder when backing up or moving an installation.
- `game-data/`: the game files you provide. Keep them during an in-place update. When moving the installation, either move them too or select their existing location again.
- `logs/`: run logs. Keep the logs from any session in which you encounter a problem.

An older root-level `config.ini` is automatically migrated to `user-data/config.ini`; an existing configuration at the new location takes precedence. Relative data paths are resolved from the collection's root folder.

## Troubleshooting and feedback

For missing data or unsupported-version messages, check the folder level and version requirements in the [data troubleshooting guide (Chinese)](docs/GAME_DATA.md#无法识别时怎么检查).

When reporting a crash, stutter, audio issue, or network problem, include:

- The collection version (see `release.json`), game, mode, and character or ship.
- Steps to reproduce the issue and what happened; a screenshot or short video can help.
- A ZIP of the complete corresponding `run-*` folder under `logs/<game ID>/`.

Press F8 when the problem occurs to mark its time, then close the game and ZIP the logs. Check them for personal information before sharing publicly.

## Development

See [FOR_AGENT.md (Chinese)](FOR_AGENT.md) for environment setup, building, running, and packaging. Read [AGENTS.md](AGENTS.md) before making changes. Upcoming work is in the [implementation plan (Chinese)](docs/IMPLEMENTATION_PLAN.md).

## Credits and licenses

The project builds on ReXGlue/Xenia-related components, graphics components derived from shadPS4, SDL, and other open-source work. See the [source origins](docs/SOURCE_ORIGINS.json), [ReXGlue license](third_party/rexglue/LICENSE), [PS4 third-party notices](third_party/ps4/THIRD_PARTY_NOTICES.txt), and [banner artwork sources](launcher/artwork.json). Components retain their respective licenses.

Release packages include licenses in `licenses/` and instructions for obtaining matching source and build documentation in `SOURCE.txt`.
