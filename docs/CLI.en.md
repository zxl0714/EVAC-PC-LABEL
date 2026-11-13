# Command-line guide

[简体中文](CLI.md) · [English](CLI.en.md)

## Launch a game directly

Once your game data is ready, run the following in PowerShell from the collection folder.

```powershell
& '.\EVAC PC LABEL.exe' --game futari
```

Change the game ID to launch another title. Games use the same data and saves as when launched from the app.

In Command Prompt (CMD), use this form.

```bat
"EVAC PC LABEL.exe" --game futari
```

## Game IDs

| Platform | Game | ID |
| --- | --- | --- |
| Xbox 360 | Mushihimesama Futari | `futari` |
| Xbox 360 | ESPGaluda II | `espgaluda2` |
| Xbox 360 | DoDonPachi SaiDaiOuJou | `sdoj` |
| Xbox 360 | Guwange | `guwange` |
| PS4 | Ketsui Deathtiny | `ketsui` |
| PS4 | DoDonPachi DaiOuJou Re:Incarnation | `daioujou` |
| PS4 | ESP Ra.De. Ψ | `esprade` |
| PS4 | Dangun Feveron | `feveron` |
| PS4 | Battle Garegga Rev.2016 | `garegga` |
| PS2 | ESPGaluda | `espgaluda` |
| PS2 | Ibara | `ibara` |

## Optional arguments

| Option | When to use it |
| --- | --- |
| `--data-dir <folder>` | Use a different data folder for this launch. The usual location is the folder set in the launcher, or `game-data/<id>/` by default. Relative paths start at the collection folder. |
| `--profile <name>` | Use a separate save profile under `user-data/<id>/<name>/`. The default name is `default`. Use up to 64 characters from A–Z (either case), digits, underscores, or hyphens. |
| `--root <folder>` | Use a different collection installation. Defaults to the folder containing the EXE. |

Add these options after `--game <id>` as needed and quote paths containing spaces. For PS2, pass the folder containing the ISO. [Data folder examples](GAME_DATA.en.md)

```powershell
& '.\EVAC PC LABEL.exe' --game ketsui --data-dir 'D:\Game Data\Ketsui' --profile practice
```

When called from a script or frontend, the EXE waits for the game to quit and returns the game process's exit code. Launcher errors appear in a dialog and return `2`.
