# Development entry

## Current objective

Implement the complete v0.4 plan in docs/IMPLEMENTATION_PLAN.md step by step.
The latest user goal authorizes implementation, including crash fixes, game-data
layout, launcher UX, EVAC PC LABEL branding, repository/local-directory rename,
light regression and verified release artifacts. Earlier question-only scopes
and historical defaults are not current instructions. Do not redefine completion
around a passing subset. Read README.md and the plan before further changes.

Use docs/TEST_MATRIX.md for actual acceptance, docs/CHANGELOG.md for player-facing
release notes, and Git history for older development checkpoints. Do not repeat
accepted campaigns or treat old status paragraphs as new work requirements.

## Working rules

- Preserve original standalone projects, original game data and frozen releases.
  Use fresh copies for merged-data and test profiles; never auto-move, overwrite
  or delete the user's source extraction. Current development profiles are test
  data; resetting/restarting those test sessions is authorized.
- Keep ROMs, generated game bodies, prepared images, tools, logs, dumps, profiles
  and credentials out of Git. Crash module snapshots also contain original code.
- Fix 360 translation through declarations/hooks, not generated C++ bodies.
  Maintain source origins and licenses. Do not silently replace game logic,
  native slowdown, replay formats, buffer ownership or synchronization.
- Build with two jobs at BelowNormal priority by default. A build must not launch
  games. Reuse pinned tools/dependencies under this project, not old checkouts.
- Source and mechanism checks prove their own scope, not actual gameplay,
  physical input, scanout, or all-mode stability. Keep failed evidence.
- Use a small relevant regression set. No new full-mode campaign, extreme-case
  matrix, uncapped FPS experiment or indefinite millisecond tuning.
- Keep game IDs, user-data, saves/replays and deployed API paths stable across
  branding changes. Default community nickname is Player; shared API base is
  https://xiaolu.life/cave/api/v1. Never submit authored fixtures to production.

## Current v0.4 crash work

SDOJ defaults to frame_delay=3 in Settings, the template and missing-key reads.
Preserve explicit 1/2/3. 1V/2V are manual test choices, not stability claims.
The local shared INI explicitly sets 3; its prior-text backup is
.local/config-before-default-3v-8e1d663393e140e6920fcb61bd0fd0e4.ini.

Friend run 71556, 1V/CA022100, throws missing original slowdown metrics at logic
3187. Its dump lacks the complete RenderMetrics ring, so the exact source of
that incident is not proved. A definite late-parse defect is fixed at 25f3be9:
only the Tick-frozen source's real Capture may resolve the same-frame missing
selection; newer/other parses cannot retarget it. Low defers before input/wake
while that source is unknown, leaving ordinary parsing and the late driver.
Keep the original exception and detailed fault logs; do not guess zero metrics.
The authored sequence failed before the fix and passes now (1122 mechanism
checks, 27 common/360 CTests, both modules built). Actual incident regression
and ordinary physical keyboard/focus/A-V checks remain open. Prior full replay
comparisons and output-chain results are preserved in TEST_MATRIX, not restarted.

G2 dat163's real 12-byte forwarding entry 0x880EEB00 is declared, generated and
registered at 30fe902. Test-G2Callback.ps1 passes 12 ABI checks on the real
locally generated forwarder; neighboring entries were already registered.
The built dat163 SHA is
1156b4264d04779d43d9abffadecccf92a8b7821632b8919cab5f9fe47125a29.
Target Xbox360 Novice player-entry regression remains open: the new run reached
only the title before desktop activation failed. Do not call that a mode pass.

Last CU attempts were blocked by an elevated foreground Taskmgr. Authorized
close/kill/minimize attempts failed. Do not repeat unchanged recovery, blind
occluded-menu guesses, or bypass OS permissions. Leave pre-existing launchers
alone. The bounded SDOJ runs 97224/48620 closed by diagnostic quit; they exercised
module 2, not the failing module 1. .local/v04-sdoj-metrics-r1/test-pad.txt is
still '1 quit'; reset it before reusing that independent diagnostic root.
An offline test pad proves InputSystem behavior, not physical desktop input.

## Current game-data implementation

The code now supports Ketsui CUSA09062 01.00 base (41 files) and complete merged
01.02 (47 files) in ONE root with eboot.bin directly inside. Manifest variants
are selected by actual root-program size/SHA, then checked against their own
full file list. Unknown/mixed versions and old CUSA09062-app/patch pairs are
rejected. Do not add a legacy fallback. Base data rejects patch-only remnants.
No physical source extraction or shared data path was changed by this work.

Base-specific community sites and archive seed-load offsets are in
backends/ps4/ketsui_version.h, bound to exact executable SHA-256. They came from
read-only instruction/relocation mapping, not a blanket address delta. Resource
transforms are separately bound to real base/update script hashes. Prepared
cache recipes bind variant/version and the correct base/merged content mount.
Internal packaging selects the inspector-confirmed variant's complete manifest.

Two fresh ignored input copies are under
.local/inputs/ketsui-v04-single-root-r1/{base,update-102}; originals remain at
E:/Projects/Ketsui-PC/game-data. Both actual inputs pass full CLI validation,
all three ELF preparations and Python byte comparison, music/replay/offline
read-fix comparison and content validation, and cold/warm cache verification
without game entry. Reports: .local/v04-ketsui-{base,updated}-prepare-r2/result.json.
479 source tests (one file-link privilege skip), 389 common checks, 118 hidden
launcher checks, 45 native round cases, 73 PS4 checks and 107 resource checks
pass. Build/PS4 and launcher symbol receipts pass. Logs use
.local/v04-ketsui-single-root-*, v04-ketsui-native-checks-r1 and
v04-ketsui-resource-checks-r1. The first preparation attempt failed solely on
Windows path-sort order; r1 failure is retained, r2 uses canonical text ordering.
This is NOT actual base/update gameplay, save or community replay acceptance.

Bounded read-only nested-root hints also work for root default.xex/eboot.bin:
two wrapper levels, bounded enumeration, no reparse-point traversal, no auto
selection or hash-validation claim. Placement-only folders remain uninstalled.
v0.4 full-package placement templates and template-aware import are implemented:
six bilingual config/game-data/*.txt guides go to the fixed per-game paths.
Public permits only those exact documentation identities under game-data;
Update still forbids all game-data payloads. Tiny authored Internal/Public ZIPs
were really assembled/extracted, not actual release archives. Import preserves
a placement-only destination, retaining its original directory under
cache/<id>/contract-1/import-guides before non-overwriting publication; it rejects
other files and conflicting source guides. 481 source tests (one privilege skip),
139 launch/config/copy checks and 226 hidden UI checks pass. Evidence:
.local/v04-placement-{build-r2,source-r1,native-r1,launcher-r1}.log. Real release
artifacts and game regression remain pending; originals and frozen ZIPs unchanged.

## Remaining launcher and branding work

Single-instance GUI, selected-game memory, bottom alignment and compact language
layout are implemented. 226 hidden production UI checks pass, with actual
cross-process mutex/message coverage of concurrent startup, minimized restore
requests, abandoned-owner recovery and root isolation. Reactivation preserves
selection, dirty form and fixture child handle; desktop restore/foreground calls
are intercepted in tests, not actual desktop focus evidence. Normal close saves
only [collection].selected_game into latest disk text, preserving editor changes
and unsaved nickname; a fresh process restores selection. --select chooses the
initial GUI item, --game still launches directly. Unknown ID defaults to Futari;
known missing/Coming Soon IDs remain valid selections. Six native renderer
previews at 96/144 DPI and two sizes pass pixel alignment and self-review, with
explicit example-state labels. See .local/v04-launcher-ux-r4.log and
.local/v04-launcher-visual-r1. Common/launch CTests and launcher symbol receipt
pass. Existing root launchers were not replaced. Remaining:

- Uniform EVAC PC LABEL captions/Windows properties, EVAC PC LABEL.exe, BAT,
  build/release/docs and EVAC-PC-LABEL archive/repository/local-directory names.
  GitHub/local rename has not happened. Inspect processes, submodules, absolute
  paths and build caches before moving; validate SSH remote and clean rebuild.
- The complete wordmark is the EXE icon, not the rejected standalone star.

## Documentation and release

Maintain README.md (Chinese) and README.en.md (English) together, reciprocal
links and matching actual names/versions/paths. Write for players: no former-name
history, internal diary or repeated defensive explanations. Keep required setup,
known issues/remedies and license/source information. English UI labels match
launcher/strings.h. The current public README download is the frozen v0.3.2
Update only, not a complete first-install package; synchronize to v0.4 on release.

At v0.4 release provide Internal (complete supported data, runnable components,
symbols, self-contained source/.git), Public (no ROM/source) and matching no-ROM
source attachment, with Update as planned. Source Git has no alternates/shared
objects; restore all pinned dependencies. Preserve GPL/source access. Never put
internal ROM-containing downloads on the public README. Publish only verified
artifacts, not unfinished candidates.

Use the lean workflow: certify components once at the release source commit,
assemble each final flavor once, one real extraction acceptance per final ZIP.
Reuse acceptance by ZIP SHA while unchanged. Shared runtime hashes avoid repeated
game campaigns across flavors. Verify OSS upload by local identity, remote
size/metadata, public checksum and bounded header GET, not whole re-download.
Post-release docs/delivery records do not trigger frozen-package rebuilds.
Keep old tags, ZIPs and OSS objects. Latest accepted v0.3.2 CUSTOM source is
a8a75fb9d86a11003c8fa9f3e1fb38521bcb2a07 (tag v0.3.2-custom), SHA
9bb302546e3f42c9050a4180e1cbee0d3b7784a272979fa71825835173b7ca99:
https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip
Reuse its accepted overlay/delivery records; do not re-extract or overwrite it.
Service deployments and group delivery are separate from builds. Do not restart
peer services, write synthetic production scores, or repeat old QQ delivery.
