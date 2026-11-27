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

## Big Picture mode

Select **Big Picture** at the bottom of the launcher or press `F11` in the game library. In Big Picture, select **Desktop mode** or press `F11` again to return. The launcher reopens in the mode you last used.

In either mode, use the left stick, D-pad, or arcade stick to select a game and press Confirm to launch it. In Big Picture, press Back to open the exit menu.

Big Picture uses a 2×2 cover grid on portrait monitors. Move left or right to select a game, or continue past either edge to turn the page. Use Up, Down, or the mouse wheel to move between rows. Under `[collection]` in `user-data/config.ini`, set `big_picture_layout` to `vertical` or `horizontal` to choose a layout, or `auto` to follow the monitor.

When playing from the launcher, press `Esc`, or hold `Start` and press D-pad Up, to return to the selected game card. In DonPachi Recomp and DoDonPachi Recomp, `Esc` opens the game's pause menu by default. Use the controller shortcut or set your own exit key in **Controls**.

To change the shortcut, open **Controls → All games** and double-click **Exit game**. Tap one input for a single bind, or hold one and press another for a combo. This works with keyboards, controllers, and arcade sticks. Use **Clear** to disable the shortcut.

On a cabinet, use the following command to open Big Picture directly.

```powershell
& '.\EVAC PC LABEL.exe' --big-picture
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
| PS2 | DaiOuJou (PS2) | `daioujou-ps2` |
| PS2 | R-Type Final | `rtype-final` |
| Arcade | DonPachi | `donpachi` |
| Arcade | DoDonPachi | `dodonpachi` |

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
