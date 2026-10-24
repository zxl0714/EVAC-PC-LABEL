# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

Bringing CAVE's classic bullet-hell shooters to Windows PC.

EVAC PC LABEL is an unofficial collection of CAVE PC ports with a unified launcher, shared configuration, saves, replays, and community leaderboards. Install game data only for the titles you want to play.

[Getting started](#getting-started) · [Game data guide (Chinese)](docs/GAME_DATA.md) · [Changelog (Chinese)](docs/CHANGELOG.md) · [Development guide (Chinese)](FOR_AGENT.md)

This page describes v0.6.1, currently shared with a limited group. Public downloads are listed under “Downloads and backups”.

## Getting started

### System requirements

Use Windows x64 with your GPU vendor's graphics drivers. Futari, G2, and SDOJ require Direct3D 12; Ketsui, DaiOuJou, and ESP Ra.De. require Vulkan. Application runtime libraries are included in the player package.

### Launch

1. Extract the program package into a writable folder. Supply your own game data.
2. Run `EVAC PC LABEL.exe` in the collection's root folder. `Launch.bat` is an alternative entry point. Select `EN` at the top right of the launcher to use English.
3. Select a game, then choose **Game data… → Choose data folder…** at the bottom. Select the folder containing the extracted game files. Alternatively, place the data in the default location shown below.
4. Click **Launch game**. The launcher verifies the data before starting; PS4 games also prepare their runtime files on first launch. Allow this process to finish.

## Game data

Prepare only the games you want:

| Game | English name / shorthand | Supported game data |
| --- | --- | --- |
| 虫姫さまふたり Ver.1.5 | Mushihimesama Futari Ver.1.5 / Futari | Xbox 360; optional Black Label and Ver.1.01 DLC |
| エスプガルーダII ブラックレーベル | Espgaluda II Black Label / G2 | Xbox 360 |
| ケツイ Deathtiny ～絆地獄たち～ | Ketsui Deathtiny / Ketsui | PS4, CUSA09062; 01.00 base game or complete data with the 01.02 update overlaid in one folder |
| 怒首領蜂最大往生 | DoDonPachi SaiDaiOuJou / SDOJ | Xbox 360, original disc data |
| 怒首領蜂 大往生 臨廻転生 | DoDonPachi DaiOuJou Re:Incarnation / DaiOuJou | PS4, CUSA32997; complete merged 01.04 data |
| エスプレイドΨ | ESP Ra.De. Psi | PS4, CUSA15155; complete 01.00 base data with the 01.04 update overlaid |

### Extracting ISO / PKG files

- **Xbox 360 ISO**: download a [Windows Release build of extract-xiso](https://github.com/XboxDev/extract-xiso/releases), then run `extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"`. The output should contain `default.xex` directly.
- **PS4 PKG**: download and fully unpack [PkgTool from LibOrbisPkg v0.2](https://github.com/maxton/LibOrbisPkg/releases/tag/v0.2). The player package includes `tools/Extract-Ps4Pkg.ps1` to extract both game files and `sce_sys` metadata. Open PowerShell in the collection folder and replace these example paths:

```powershell
.\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\base.pkg" -OutputDir "D:\GameExtract\base"
.\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\update.pkg" -OutputDir "D:\GameExtract\update"
```

Each output folder must not already exist. The game files appear inside its `uroot/`. Copy **all contents inside** `base/uroot/` into the game folder, then overlay all contents of `update/uroot/`, replacing matching files. Skip the update step for base-only installations. The script defaults to the common backup-package passcode of 32 zeros; use `-Passcode` for a different one. This is not a ZIP/RAR password. Encrypted retail PKGs require an extractable backup first.

In a source checkout, the helper is at `scripts/Extract-Ps4Pkg.ps1`.

### Where to put game data

The complete program package creates the folders below, each with a bilingual `放置说明.txt` placement guide. Use **Choose data folder…** to use an existing folder without modifying its files, or **Copy into collection…** to copy it to a default location. The destination must be empty or contain only its placement guide.

| Game | Default folder | Files or folders directly inside it |
| --- | --- | --- |
| Futari | `game-data/futari/` | `default.xex`, `media/`, and the remaining resources |
| G2 | `game-data/espgaluda2/` | `default.xex`, `dat*.xex`, and the remaining resources |
| Ketsui | `game-data/ketsui/` | `eboot.bin`, the `.prx` modules, and the remaining resources |
| SDOJ | `game-data/sdoj/` | `default.xex`, `CA022100.bin`, `CA022110.bin`, and the remaining resources |
| DaiOuJou | `game-data/daioujou/` | `eboot.bin`, the `.prx` modules, and the remaining resources |
| ESP Ra.De. | `game-data/esprade/` | `eboot.bin`, the `.prx` modules, and the remaining resources |

Install only the games you want and preserve all game files and their internal folder structure. Leave other folders empty. Ketsui works with the base game alone; to apply 01.02, copy the update folder's contents over the same game root. See the [data guide](docs/GAME_DATA.md).

Extract ISO or PKG images into game files first. For ZIP archives, unpack them and select the actual game root. Archive filenames do not affect detection; changing a file extension does not extract an image or package.

Copy **all contents inside** the extracted game folder, not the outer folder itself. For example, Futari's entry should be `game-data/futari/default.xex`, not `game-data/futari/extracted-folder/default.xex`. The filenames in the table identify the correct folder level, not a complete list of required files. Keep every resource, its original name, and the internal folder structure.

Futari's Ver.1.5 base game works without DLC. Leave `game-data/futari/dlc/` empty or absent; no configuration change is needed. Only Black Label and Ver.1.01 require their respective DLC. Use the default folder names shown above, or choose an external data folder with any outer folder name.

See the [game data guide (Chinese)](docs/GAME_DATA.md) for complete folder examples, DLC placement, and data detection help.

## Settings and controls

**Start fullscreen** is above the launch button; edit your leaderboard nickname at the top right. Select `CN / EN / JP` to change the launcher's language. If a game cannot display characters in your nickname, saving shows the affected games and the names passed to them. In-game languages depend on the original game.

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

Face-button reference:

| Position | Xbox 360 | PS4 | Keyboard |
| --- | --- | --- | --- |
| South | A | × | Z |
| East | B | ○ | X |
| West | X | □ | C |
| North | Y | △ | V |

Keep the whole `user-data/` folder when backing up or moving the collection. It contains configuration, saves, and replays. Wait for saving to finish before closing after changing in-game settings.

## Community leaderboards

Online features are enabled by default, with no registration required. Set your nickname in the launcher, then use the community service through the game's leaderboard and replay screens. Each title has separate leaderboards, with score eligibility following the game's rules.

Within a game, the same nickname represents the same community identity, so choose a distinctive name and keep using it. The service checks submission formats and replay integrity, but does not authenticate nickname ownership. SDOJ team scores and DaiOuJou CUSTOM score submissions are not integrated.

```ini
[community]
enabled = true
endpoint = https://xiaolu.life/cave/api/v1
nickname = Player
```

Set `enabled` to `false` to play offline.

## SDOJ Frame Delay

Set the following under `[game.sdoj]` in `user-data/config.ini`:

```ini
[game.sdoj]
data_dir = game-data/sdoj
frame_delay = 1
```

| Value | Timing policy |
| --- | --- |
| 1 | Low latency; default |
| 2 | Balanced |
| 3 | Original path |

These select the game's internal frame scheduling policy. Total input-to-display latency also depends on the controller and display chain. Explicit settings are preserved; restart the game after changing the value.

## Downloads and backups

Public download: **v0.4**. The maintainer shares v0.6.1 directly with a limited group. See the [changelog (Chinese)](docs/CHANGELOG.md) for release notes.

- [Windows x64](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.4/EVAC-PC-LABEL-v0.4-win-x64.zip) · [SHA-256](https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.4/EVAC-PC-LABEL-v0.4-win-x64.zip.sha256). Game data is not included.
- Development: [GitHub repository](https://github.com/zxl0714/EVAC-PC-LABEL). See the package's `SOURCE.txt` for the matching source commit, and [FOR_AGENT.md (Chinese)](FOR_AGENT.md) for dependency setup and build instructions.

- `user-data/`: configuration, per-game saves, replays, and other player data. Keep the whole folder when backing up or moving an installation.
- `game-data/`: the game files you provide. When moving the installation, either move them too or select their existing location again.
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
