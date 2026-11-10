# Settings and controls

[简体中文](SETTINGS.md) | [English](SETTINGS.en.md) · [Back to overview](../README.en.md)

## Launcher and controls

Choose fullscreen or a 1280×720, 1600×900, 1920×1080, 2560×1440 or 3840×2160 window above the launch button; the launcher remembers your selection. Edit your leaderboard nickname at the top right. Select `CN / EN / JP` to change the launcher's language. If a game cannot display characters in your nickname, saving shows the affected games and the names passed to them. In-game languages depend on the original game.

The launcher remembers its window size when closed and fits it within the available screen area when the display resolution or scaling changes.

Open **Controls** and choose Keyboard or Controller / stick. Double-click a binding, press a key or move the stick, then select **Save**. Use **All games** or the selected game to choose the scope. Assigning an input that is already in use moves it to the new binding. **Reset to defaults** restores all shared keyboard and controller bindings. **Use shared bindings** removes all overrides for the selected game. Changes apply the next time you start a game.

**Config file** opens `user-data/config.ini` for audio, deadzone and online settings. Click **Refresh** after editing the file. See the [default configuration template](../config/default.ini) for available options.

In `[display]`, `vsync = false` enables low-latency presentation while preserving the original game timing; tearing is possible. Set it to `true` for vertical synchronization. Updates preserve your configuration, so an existing `true` value can be changed here.

| Default keyboard input | Function |
| --- | --- |
| Arrow keys | Directional input |
| Z / X / C / V | A / ×, B / ○, X / □, Y / △ |
| Enter / Backspace | Start / Select (Touchpad for PS4) |
| A / S, Q / W | L1 / R1, L2 / R2 |
| F11 or Alt+Enter | Toggle fullscreen |
| Alt+F4 | Close the game |
| F8 | Add a timestamp marker to the log for troubleshooting |

Close leaderboard player cards with `CLOSE`, Enter or Esc.

Actions such as shooting, bombing, and switching characters depend on each game's own control settings. Controllers and arcade sticks are handled through SDL, including the XInput and PlayStation devices it supports.


Battle Garegga defaults: `V` for Credit, `Enter` for Start, `C / S` for Shot, `Z / A` for Weapon, `X` for Formation, and `Backspace` for the pause menu. Quick-save slots are separate for each game mode.

Guwange's **Arrange Xbox 360** mode fires automatically. Use the arrow keys / left stick to move your character, `I / K / J / L` (up / down / left / right) or the right stick to move the Shikigami, `W / RT` to summon it, and `X / B` to bomb. Customize the Shikigami keys with `right_stick_up/down/left/right` under `[game.guwange.keyboard]`; other games' bindings are unchanged.

Face-button reference:

| Position | Xbox 360 | PS4 | Keyboard |
| --- | --- | --- | --- |
| South | A | × | Z |
| East | B | ○ | X |
| West | X | □ | C |
| North | Y | △ | V |

Keep the whole `user-data/` folder when backing up or moving the collection. It contains configuration, saves, and replays. Wait for saving to finish before closing after changing in-game settings.

## NEO mode

PS4 games enable NEO (PS4 Pro) mode by default, using 1080p composition. It brings Dangun Feveron's first response forward by about one frame. Matched-scene latency is essentially unchanged in Ketsui, DaiOuJou and ESP Ra.De., with slightly higher GPU cost.

To disable NEO, click **Config file** at the bottom of the launcher and add or change `neo=false` in the game's existing section in `user-data/config.ini`, then restart the game. For Ketsui:

```ini
[game.ketsui]
neo = false
```

Use `[game.daioujou]`, `[game.esprade]`, `[game.feveron]` or `[game.garegga]` for DaiOuJou, ESP Ra.De., Dangun Feveron or Battle Garegga. Set `neo=true` to enable it again.

## SDOJ Frame Delay

Set the following under `[game.sdoj]` in `user-data/config.ini`:

```ini
[game.sdoj]
data_dir = game-data/sdoj
frame_delay = 0
```

| Value | Timing policy |
| --- | --- |
| 0 | Present before waiting for the next VBlank (default) |
| 1 | Low latency |
| 2 | Balanced |
| 3 | Original path |

These select the game's internal frame scheduling policy. Total input-to-display latency also depends on the controller and display chain. Explicit settings are preserved; restart the game after changing the value.

## G2 Frame Delay

Set the following in `user-data/config.ini`:

```ini
[game.espgaluda2]
frame_delay = 0
```

The default, `0`, presents before waiting for the next VBlank. Choose `1` for the low-latency path or `2` for original timing. Existing explicit settings are preserved. Restart the game after changing it.

## AMD Radeon Anti-Lag 2

For Xbox 360, PS4 and PS2 games, set `amd_anti_lag = true` under `[display]` in `user-data/config.ini` and restart the game. Set it to `false` to turn it off. The standard package defaults to off and the `-amd-anti-lag2` test package defaults to on. An explicit setting takes precedence.

The runtime checks the active GPU and driver. Look for `AMD_ANTI_LAG` in the run log for the detection result. NVIDIA GPUs use the separate `reflex` setting.

## Futari, Guwange and NVIDIA Reflex

In Futari, go to **Option → Screen Setting → “Maniac” Menu Mode → Extra Setting → FrameDelay → 0V(Tearing)**, then choose **EXIT** to save. If the screen already says **Screen Setting (Maniac)**, go straight to **Extra Setting**. Guwange defaults to `frame_delay = 0` under `[game.guwange]`, removing two buffered response frames in Arcade, Blue and Arrange single-player modes. Set it to `2` to restore the original timing.

Xbox 360, PS4 and PS2 titles automatically enable Reflex on supported NVIDIA hardware. To turn it off, set `reflex = off` under `[display]` and restart the game. Set it back to `auto` to enable detection.

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

## Checking for updates

The launcher checks GitHub stable releases in the background and automatically announces each new version once. It does not interrupt a running game. Open the release page from the notification for notes and downloads.

Successful checks are cached for 24 hours. To disable automatic checks, set this in `user-data/config.ini`:

```ini
[collection]
check_for_updates = false
```

This setting is separate from community networking. Reminder history is stored in `user-data/update-check.json`; download and replace update files yourself.

## Updating and backups

To update, close the games and launcher, then extract over the existing folder and replace matching files. The program package does not include `user-data/`; existing settings, saves, and replays are preserved.

- `user-data/`: configuration, per-game saves, replays, and other player data. Keep the whole folder when backing up or moving an installation.
- `game-data/`: the game files you provide. When moving the installation, either move them too or select their existing location again.
- `logs/`: run logs. Keep the logs from any session in which you encounter a problem.

Verification results are cached in `user-data/cache/`. Keep this folder to reduce startup waits after updates. Changed files are checked again automatically.

An older root-level `config.ini` is automatically migrated to `user-data/config.ini`; an existing configuration at the new location takes precedence. Relative data paths are resolved from the collection's root folder.

## Logs and startup problems

After closing the game, click **Upload logs**. It selects your latest session; you can choose another run. Describe the mode, character and what happened, then click **Upload**. Include the resulting report ID when reporting the issue. **Open local logs** opens the selected session folder.

Logs can also be uploaded when a game cannot start. If the runtime is unavailable, click **Details** for the affected files and how to restore them. For missing data or unsupported versions, see the [data guide](GAME_DATA.en.md#troubleshooting).

If a PS4 game reports an unsupported Vulkan version, update your graphics driver and restart Windows. AMD users can select their model on the [official driver support page](https://www.amd.com/en/support/download/drivers.html).
