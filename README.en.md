# EVAC PC LABEL

[简体中文](README.md) | [English](README.en.md)

Bringing CAVE's classic bullet-hell shooters to Windows PC.

EVAC PC LABEL is an unofficial collection of CAVE PC ports with a unified launcher, shared configuration, saves, replays, and community leaderboards. Install game data only for the titles you want to play.

[Getting started](#getting-started) · [Game data guide](docs/GAME_DATA.en.md) · [Changelog (Chinese)](docs/CHANGELOG.md) · [Development guide (Chinese)](FOR_AGENT.md)

Download links are listed under “Downloads and backups”.

## Latency and performance

Lower CPU usage, with 1V low-latency options for G2 and SaiDaiOuJou. Estimated latency and measured CPU usage in representative Xbox 360 scenes:

| Game | Port latency/ms | Xenia latency/ms | Port CPU/cores | Xenia CPU/cores |
| --- | ---: | ---: | ---: | ---: |
| Mushihimesama Futari | 26 | 36 | 0.08 | 1.26 |
| ESPGaluda II | 26 | 51 | 0.38 | 1.37 |
| DoDonPachi SaiDaiOuJou | 26 | 69 | 0.43 | 3.46 |

All three ports use their default 1V setting. Xenia retains the games' original 1V, 2V and 3V settings, respectively. G2 can return to 2V through its [Frame Delay setting](#g2-frame-delay).

Latency combines game-internal measurements, an average 8.3 ms sampling wait and historical same-PC presentation data, assuming independent flip for the ports and desktop composition for Xenia. Device input, display scanout and panel response are excluded. CPU figures are equivalent logical cores from replay workloads on an i7-13700KF / RTX 4080; one core equals 1,000 CPU ms/s. [Method and data (Chinese)](docs/ANALYSIS.md#360-统一条件比较).

## Getting started

### System requirements

Use Windows x64 with your GPU vendor's graphics drivers. Futari, G2, SDOJ, and Guwange require Direct3D 12; Ketsui, DaiOuJou, ESP Ra.De., and Dangun Feveron require a graphics driver supporting Vulkan 1.3. Application runtime libraries are included in the player package.

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
| 怒首領蜂 大往生 臨廻転生 | DoDonPachi DaiOuJou Re:Incarnation / DaiOuJou | PS4, CUSA32997; 01.00 base or complete merged 01.04 data |
| エスプレイドΨ | ESP Ra.De. Psi | PS4, CUSA15155; complete 01.00 base data with the 01.04 update overlaid |
| ぐわんげ | Guwange | Xbox 360 XBLA, 58410A78; base version 0.0.0.4 |
| 弾銃フィーバロン | Dangun Feveron | PS4, US CUSA06735, version 01.00 base game |

### Extracting ISO / PKG / XBLA files

- **Xbox 360 ISO**: download a [Windows Release build of extract-xiso](https://github.com/XboxDev/extract-xiso/releases), then run `extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"`. The output should contain `default.xex` directly.
- **PS4 PKG**: double-click `tools/Extract-PS4.exe` in the program package. Select the base PKG and a new output folder, then click **Extract**. When finished, click **Open game files** and copy all contents into `game-data/<game>/`. Extract an update PKG separately with the same tool, then overlay all its extracted contents onto the base, replacing matching files.

Keep the entire `tools/` folder; extraction dependencies are included. The tool extracts game resources and `sce_sys` metadata. **Open game files** opens `uroot/` directly, so copy its contents. See the [extraction guide](docs/GAME_DATA.en.md#extract-iso-pkg-and-xbla-packages) for details and advanced script usage.

**Guwange XBLA**: unpack the ZIP/RAR, then double-click `tools/Extract-XBLA.exe`. Click **Select package...**, select the content package inside it (usually a long filename without an extension), and choose an output location. Click **Open game files** when finished and copy all contents into `game-data/guwange/`, with `default.xex` directly inside that folder. Use base version 0.0.0.4 without a Title Update.

### Where to put game data

The complete program package creates the folders below, each with a bilingual `README.txt` placement guide. Use **Choose data folder…** to use an existing folder without modifying its files, or **Copy into collection…** to copy it to a default location. The destination must be empty or contain only this guide.

| Game | Default folder | Files or folders directly inside it |
| --- | --- | --- |
| Futari | `game-data/futari/` | `default.xex`, `media/`, and the remaining resources |
| G2 | `game-data/espgaluda2/` | `default.xex`, `dat*.xex`, and the remaining resources |
| Ketsui | `game-data/ketsui/` | `eboot.bin`, the `.prx` modules, and the remaining resources |
| SDOJ | `game-data/sdoj/` | `default.xex`, `CA022100.bin`, `CA022110.bin`, and the remaining resources |
| DaiOuJou | `game-data/daioujou/` | `eboot.bin`, the `.prx` modules, and the remaining resources |
| ESP Ra.De. | `game-data/esprade/` | `eboot.bin`, the `.prx` modules, and the remaining resources |
| Guwange | `game-data/guwange/` | `default.xex`, `module/`, `game_res/`, `res/`, and the remaining resources |
| Dangun Feveron | `game-data/feveron/` | `eboot.bin`, the `.prx` modules, and the remaining resources |

Install only the games you want and preserve all game files and their internal folder structure. Leave other folders empty. Ketsui works with the base game alone; to apply 01.02, copy the update folder's contents over the same game root. See the [data guide](docs/GAME_DATA.en.md).

Extract ISO or PKG images into game files first. For ZIP archives, unpack them and select the actual game root. Archive filenames do not affect detection; changing a file extension does not extract an image or package.

Copy **all contents inside** the extracted game folder, not the outer folder itself. For example, Futari's entry should be `game-data/futari/default.xex`, not `game-data/futari/extracted-folder/default.xex`. The filenames in the table identify the correct folder level, not a complete list of required files. Keep every resource, its original name, and the internal folder structure.

Futari's Ver.1.5 base game works without DLC. Leave `game-data/futari/dlc/` empty or absent; no configuration change is needed. Only Black Label and Ver.1.01 require their respective DLC. Use the default folder names shown above, or choose an external data folder with any outer folder name.

### Futari DLC

Place the original DLC files here, keeping their filenames:

```text
game-data/futari/dlc/0000000000000000/435A07D7/00000002/
  496F816DD4B53DAEFF931838B6C69831AAD7663043
  9893A78CA6062B935BD1F53EA43CFD546B7246DF43
```

The first file is Black Label; the second is Ver.1.01. Install either or both, then start the game. Extraction is automatic on first use. Existing v0.7.2 extracted folders also work in place, including the shorter `dlc/00000002/<package-folder>/` layout. If using extracted Black Label files, keep `spa.bin` and all three `Rom/` files together.

The default `[game.futari] dlc_dir = auto` searches the selected base game's `dlc/` folder. For example, selecting `D:/Games/Futari` searches `D:/Games/Futari/dlc`. An explicit path in an older config is preserved; change it to `auto` to follow the base folder, or set it to your own DLC directory. Select Futari and click **Details** to see the actual search path and each DLC's status.

See the [game data guide](docs/GAME_DATA.en.md) for other games' folder examples and troubleshooting.

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

Close leaderboard player cards with `CLOSE`, Enter or Esc.

Actions such as shooting, bombing, and switching characters depend on each game's own control settings. Controllers and arcade sticks are handled through SDL, including the XInput and PlayStation devices it supports.

Guwange's **Arrange Xbox 360** mode fires automatically. Use the arrow keys / left stick to move your character, `I / K / J / L` (up / down / left / right) or the right stick to move the Shikigami, `W / RT` to summon it, and `X / B` to bomb. Customize the Shikigami keys with `right_stick_up/down/left/right` under `[game.guwange.keyboard]`; other games' bindings are unchanged.

Face-button reference:

| Position | Xbox 360 | PS4 | Keyboard |
| --- | --- | --- | --- |
| South | A | × | Z |
| East | B | ○ | X |
| West | X | □ | C |
| North | Y | △ | V |

Keep the whole `user-data/` folder when backing up or moving the collection. It contains configuration, saves, and replays. Wait for saving to finish before closing after changing in-game settings.

## Checking for updates

The launcher checks GitHub stable releases in the background and automatically announces each new version once. It does not interrupt a running game. Open the release page from the notification for notes and downloads.

Successful checks are cached for 24 hours. To disable automatic checks, set this in `user-data/config.ini`:

```ini
[collection]
check_for_updates = false
```

This setting is separate from community networking. Reminder history is stored in `user-data/update-check.json`; download and replace update files yourself.

## Community leaderboards

Online features are enabled by default, with no registration required. Set your nickname in the launcher, then use the community service through the game's leaderboard and replay screens. Each title has separate leaderboards, with score eligibility following the game's rules.

Within a game, the same nickname represents the same community identity. Change the default `Player` to your own distinctive nickname before playing, and keep using it. The service checks submission formats and replay integrity, but does not authenticate nickname ownership. SDOJ team scores and DaiOuJou CUSTOM score submissions are not integrated. Guwange supports Arcade / Blue / Arrange leaderboards and replays; online co-op is not integrated, and local two-player play has not been validated.

Other players can view nicknames, scores, and replays submitted to the community. Local replays you have not shared are not automatically made public.

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

## G2 Frame Delay

Set the following in `user-data/config.ini`:

```ini
[game.espgaluda2]
frame_delay = 1
```

`1` is the default low-latency path; `2` selects the original path. Existing explicit settings are preserved. Restart the game after changing it.

## Downloads and backups

Windows x64. See the [changelog (Chinese)](docs/CHANGELOG.md) for release notes.

- [Download the latest program package and SHA-256](https://github.com/zxl0714/EVAC-PC-LABEL/releases/latest). Game data is not included.
- Development: [GitHub repository](https://github.com/zxl0714/EVAC-PC-LABEL). See the package's `SOURCE.txt` for the matching source commit, and [FOR_AGENT.md (Chinese)](FOR_AGENT.md) for dependency setup and build instructions.

To update, close the games and launcher, then extract over the existing folder and replace matching files. The program package does not include `user-data/`; existing settings, saves, and replays are preserved.

- `user-data/`: configuration, per-game saves, replays, and other player data. Keep the whole folder when backing up or moving an installation.
- `game-data/`: the game files you provide. When moving the installation, either move them too or select their existing location again.
- `logs/`: run logs. Keep the logs from any session in which you encounter a problem.

An older root-level `config.ini` is automatically migrated to `user-data/config.ini`; an existing configuration at the new location takes precedence. Relative data paths are resolved from the collection's root folder.

## Troubleshooting and feedback

For missing data or unsupported-version messages, check the folder level and version requirements in the [data troubleshooting guide](docs/GAME_DATA.en.md#troubleshooting).

If a PS4 game reports an unsupported Vulkan version, update the graphics driver and restart Windows. For AMD GPUs, select your model on the [official driver support page](https://www.amd.com/en/support/download/drivers.html).

When reporting a crash, stutter, audio issue, or network problem, include:

- The collection version (see `release.json`), game, mode, and character or ship.
- Steps to reproduce the issue and what happened; a screenshot or short video can help.
- Text logs from the corresponding `logs/<game ID>/run-*/` folder, such as `game.log`, `stdout.jsonl`, `stderr.txt`, and `startup-error.txt`, plus `run.json`. Include the files that exist for that session.

Press F8 when the problem occurs to mark its time, then close the game and copy the relevant logs, keeping the originals. Before posting publicly, replace your Windows username, private paths, nickname, and custom server addresses in those copies with placeholders.

Do not attach an entire `run-*` folder to a public issue: `config.ini` is a configuration snapshot, and `.dmp` or `*-module.bin` files may contain memory and original game code. A maintainer will arrange a separate transfer if these are needed. Game data, saves, and replays are not default public attachments either.

## Development

See [FOR_AGENT.md (Chinese)](FOR_AGENT.md) for environment setup, building, running, and packaging. Read [AGENTS.md](AGENTS.md) before making changes. Upcoming work is in the [implementation plan (Chinese)](docs/IMPLEMENTATION_PLAN.md).

## Credits and licenses

The project builds on ReXGlue/Xenia-related components, graphics components derived from shadPS4, SDL, and other open-source work. See the [source origins](docs/SOURCE_ORIGINS.json), [ReXGlue license](third_party/rexglue/LICENSE), [PS4 third-party notices](third_party/ps4/THIRD_PARTY_NOTICES.txt), and [banner artwork sources](launcher/artwork.json). Components retain their respective licenses.

Release packages include licenses in `licenses/` and instructions for obtaining matching source and build documentation in `SOURCE.txt`.
