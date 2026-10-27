# Game data guide

[简体中文](GAME_DATA.md) | [English](GAME_DATA.en.md)

Prepare data for the games you want to play.

## Choose a location

- **Use an existing folder:** select a game in the launcher, then choose **Game data… → Choose data folder…** and select its extracted game folder.
- **Place files in the collection:** copy the game files into `game-data/<game ID>/`, then click **Refresh**. If you previously selected an external folder, use **Choose data folder…** to select the new location.
- **Let the launcher copy them:** choose **Game data… → Copy into collection…**. The destination must be empty or contain only `README.txt`, with enough disk space for the complete game. The original files are kept.

In the examples below, select the folder shown at the top of each tree. The examples show files that identify the correct folder level; `...` represents other game resources to keep.

## Set up the game folder

1. **Extract the files.** Extract ISO or PKG images into game files. Unpack ZIP/RAR archives; if they contain an ISO or PKG, extract that too.
2. **Find the game root.** For Xbox 360 games, this is the folder directly containing `default.xex`. For PS4 games, it directly contains `eboot.bin`.
3. **Copy all contents** into `game-data/<game ID>/`, preserving filenames and internal folders. Apply updates as specified for each game below.
4. **Click Refresh**, select the game, and launch it. If using an external location, select it through **Game data… → Choose data folder…** first.

For example, Futari's files should look like this:

```text
game-data/futari/default.xex
game-data/futari/media/...
```

If the files are inside `game-data/futari/extracted-folder/`, select that folder in the launcher instead. The launcher checks file contents and versions; the archive's name and a selected external folder's name do not affect detection. Keep the default `game-data/<game ID>/` paths and the game's internal filenames as shown.

## Extract ISO, PKG, and XBLA packages

If you already have `default.xex` or `eboot.bin` alongside the complete game resources, continue to the game's section below.

### Xbox 360 ISO: extract-xiso

1. Download and unpack a [Windows Release of XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso/releases).
2. Open PowerShell in the tool's folder and substitute your input file and output folder:

```powershell
.\extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"
```

3. Select `D:\GameExtract\futari` in the launcher, or copy all its contents into `game-data/futari/`. Use the same process for G2 and SDOJ with their respective folders.

### PS4 PKG: Extract PS4

1. Double-click `tools/Extract-PS4.exe` in the program package.
2. Click **Browse…** and select the base PKG. A new output folder is suggested automatically; use **Location…** to choose another location, then click **Extract**.
3. When finished, click **Open game files**. Copy **all files and folders inside** the opened directory into `game-data/<game>/`.
4. If you have an update PKG, extract it separately with the same tool, then overlay all its game files onto the base, replacing matching files and keeping base-only resources.
5. Click **Refresh** in the launcher and start the game. For ESP Ra.De., the final folder should directly contain `game-data/esprade/eboot.bin` and `game-data/esprade/sce_sys/param.sfo`.

Use a new output folder for each extraction. Game files are inside its `uroot/`; **Open game files** opens that folder directly. Dependencies are included in `tools/pkgtool/`. Keep the entire `tools/` folder.

The PKG passcode defaults to 32 zeros. Change it only if your PKG uses another passcode; this is separate from a ZIP/RAR password. Errors appear in the window, with detailed output in `extract.log` in the new output folder. Input PKGs remain unchanged.

#### Advanced: script usage

Open PowerShell in the program package's root folder and substitute the input and output paths. The helper finds the bundled PkgTool automatically:

```powershell
.\tools\Extract-Ps4Pkg.ps1 -InputPkg "D:\Games\base.pkg" -OutputDir "D:\GameExtract\base"
.\tools\Extract-Ps4Pkg.ps1 -InputPkg "D:\Games\update.pkg" -OutputDir "D:\GameExtract\update"
```

In a source checkout, use `scripts/Extract-Ps4Pkg.ps1`; see `tools/README.md` for dependency preparation. Use `-PkgTool` to select an external tool or `-Passcode` to supply another PKG passcode.

### Xbox 360 XBLA: Guwange

Unpack the ZIP/RAR first. If it contains a large file with a long name and no extension, extract that content package with the included `tools/Extract-XBLA.exe`:

1. Double-click the tool, click **Select package...**, and select the content package.
2. Choose an output location. The tool creates a new `xbla-extracted` folder.
3. Click **Open game files** when finished and copy all contents into `game-data/guwange/`. This folder should directly contain `default.xex`, `module/`, `game_res/`, and `res/`.
4. Click **Refresh** in the launcher, select Guwange, and start it.

Use base version 0.0.0.4 without a Title Update. For scripted extraction, `scripts/Extract-Xbla.ps1` is also available in the repository and included under `tools/` in the player package.

Select `D:\GameExtract\guwange` in the launcher, or copy all its contents into `game-data/guwange/`.

## Xbox 360 games

### 虫姫さまふたり Ver.1.5 — Mushihimesama Futari (futari)

```text
game-data/futari/
  default.xex
  media/
    Module/
    ...
  ...
```

Use the complete Xbox 360 Ver.1.5 game data. Black Label and Ver.1.01 are optional DLC. For the base game alone, leave `dlc/` empty or absent.

The default DLC location is `dlc/` inside the selected base-game folder. Place the original DLC files here, keep their filenames, then start the game:

```text
game-data/futari/dlc/
  0000000000000000/
    435A07D7/
      00000002/
        496F816DD4B53DAEFF931838B6C69831AAD7663043
        9893A78CA6062B935BD1F53EA43CFD546B7246DF43
```

`496F...` is Black Label; `9893...` is Ver.1.01. Install either or both. The game extracts the files automatically on first use and reuses the prepared content on later launches.

Existing v0.7.2 extracted folders work in place, including the shorter `dlc/00000002/<package-folder>/` layout. An extracted Black Label folder contains `spa.bin`, `Rom/cg_black.bin`, `Rom/cg_blackhi.bin` and `Rom/dropprm.bl`; the Ver.1.01 folder contains `ver101_key.bin`.

The default setting follows the base-game folder selected in the launcher:

```ini
[game.futari]
dlc_dir = auto
```

For example, selecting `D:/Games/Futari` searches `D:/Games/Futari/dlc`. An explicit path in an older config remains in use; change it to `auto` to follow the base folder. To use a separate DLC location, set its path:

```ini
[game.futari]
dlc_dir = D:/Games/Futari/DLC
```

Select Futari and click **Details** at the bottom of the launcher to see the actual DLC search path and the status of Black Label and Ver.1.01. Replace missing or mismatched files with the correct resources.

### エスプガルーダII ブラックレーベル — Espgaluda II Black Label (espgaluda2)

```text
game-data/espgaluda2/
  default.xex
  dat00.xex
  dat11.xex
  ...
```

Keep all `dat*.xex` files and the original resource folders. `game-data/espgaluda2/` holds the complete game, including Black Label.

### 怒首領蜂最大往生 — DoDonPachi SaiDaiOuJou (sdoj)

```text
game-data/sdoj/
  default.xex
  CA022100.bin
  CA022110.bin
  ...
```

Use the supported original disc data. Music patches and other program updates are not supported. Keep `default.xexp`, `CA022100.binp`, and `CA022110.binp` out of this folder; if updates have already been mixed in, extract the original base game into a new folder.

Coins, the shop, and unlockable content follow the original game's progression.

### ぐわんげ — Guwange (guwange)

Use Xbox 360 XBLA **Title ID 58410A78, base version 0.0.0.4**, without a Title Update.

```text
game-data/guwange/
  default.xex
  module/
    Guwange01_x360.dll
    Guwange01_coop_x360.dll
    Guwange_arrange_x360.dll
    Guwange_blue_x360.dll
  game_res/
  res/
  ...
```

Keep all 29 files and their internal folders. These four DLLs are original Xbox game modules; keep them separate from the Windows runtime's components. Exclude `default.xexp` and `module/*.dllp` update files. For a folder that already has updates mixed in, extract the original base package into a new folder.

## PS4 games

### ケツイ Deathtiny ～絆地獄たち～ — Ketsui Deathtiny (ketsui)

Supported: **CUSA09062, base version 01.00**, or the **complete base game with update 01.02 overlaid**. Both use a single game folder:

```text
game-data/ketsui/
  eboot.bin
  ketsui_01000.prx
  ...
```

Copy all contents of the base-game folder into `game-data/ketsui/`, with `eboot.bin` directly inside it. To apply 01.02, copy all contents of the update folder over the same game root, replacing matching files and keeping the other base-game files.

Select the folder containing `eboot.bin` in the launcher. The scan identifies version 01.00 or 01.02; unknown or mixed versions fail validation.

If your extraction has separate `CUSA09062-app/` and `CUSA09062-patch/` folders, copy everything inside `CUSA09062-app/` into a new folder, then overlay the contents of `CUSA09062-patch/` if desired. Select the merged folder. To return to 01.00, extract the complete base game into a new folder. Saves and replays remain in `user-data/ketsui/`.

### 怒首領蜂 大往生 臨廻転生 — DoDonPachi DaiOuJou Re:Incarnation (daioujou)

Supported: **CUSA32997, version 01.00 base or complete merged 01.04 data**. Both support community rankings and replays; 01.00 cannot play replays recorded by a newer game version. Use the complete base folder for 01.00, or overlay the 01.04 update into that folder. Updated layout:

```text
game-data/daioujou/
  eboot.bin
  daioujou_01000.prx
  daioujou_01010.prx
  ...
```

Select the folder containing `eboot.bin` and the complete game modules and resources. Apply the update over the base game's files before selecting the folder.

### エスプレイドΨ — ESP Ra.De. Psi (esprade)

Supported: **CUSA15155, complete base version 01.00 with update 01.04 overlaid**.

```text
game-data/esprade/
  eboot.bin
  emu_integration_01500.prx
  esprade_01000.prx
  ps4data_body.bin
  ps4data_patch_body.bin
  sce_module/
  ...
```

Extract the base PKG, then copy all extracted update files over the base-game files. Place the merged contents in `game-data/esprade/`, or select that merged folder in the launcher. Keep `eboot.bin` directly inside the selected folder, along with all modules, resources, and internal folders.

Saves, downloaded replays, and settings are stored in `user-data/esprade/`.

### 弾銃フィーバロン — Dangun Feveron (feveron)

Supported: **US CUSA06735, version 01.00 base game**.

```text
game-data/feveron/
  eboot.bin
  emu_integration_01200.prx
  dfeveron_01000.prx
  dfeveron_01010.prx
  ps4data_body.bin
  ps4data_info.psb.m
  sce_module/
  sce_sys/
  ...
```

Extract the base PKG with `tools/Extract-PS4.exe`, click **Open game files**, then copy all contents into `game-data/feveron/`. Alternatively, select that extracted folder directly in the launcher. Saves, replays, and settings are stored in `user-data/feveron/`.

## Troubleshooting

### I only have an ISO, PKG, or ZIP

Extract it first, following the steps above, then select the folder containing the game files. If a ZIP contains an ISO or PKG, extract that image or package as well.

### Does the folder name matter?

For default locations, use the paths shown above, such as `game-data/futari/`. With **Choose data folder…**, the outer folder can have any name, including spaces and non-Latin characters. Preserve the game's internal filenames and relative paths.

### The files are present, but the launcher reports missing data

Check the selected folder level. If the entry is at `game-data/sdoj/extracted-folder/default.xex`, select `extracted-folder`, or move its complete contents into `game-data/sdoj/`. The launcher can suggest nearby candidate folders; select the correct one and let validation finish. Also check whether it still points to an external folder you selected previously.

### The launcher reports an unsupported version

Use one of the supported versions listed above. The scan checks required files and sizes, and launch validation checks their SHA-256 hashes. Replace missing or mismatched files with resources from the correct version. Exact file manifests are available in `games/<game ID>/data.json` in the source repository.

### Do I need to set up game data again after updating?

Extracting a program update over the existing collection keeps its game data. If you move the collection, either move `game-data/` too or select the existing external folders again. Keep the entire `user-data/` folder for settings, saves, and replays. If an older installation still has `config.ini` at the root, keep it as well; the launcher moves it into `user-data/` automatically.
