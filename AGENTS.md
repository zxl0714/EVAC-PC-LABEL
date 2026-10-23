# Development entry

Active workspace: E:/Projects/EVAC-PC-LABEL. Develop/build here. The former
E:/Projects/CAVE-PC-Collection is retained as a recovery/evidence copy, not the
active build root. Older .local evidence referenced below remains there.

## Current objective

### Next-package policy — unified Public and flexible Internal

The October 23 user decision supersedes older four-flavor delivery plans for
NEW packages. Keep two routine deliverables: Public, a full program ZIP usable
both for first installation and overlay updates; and Internal, with explicitly
selected complete game data, optional DLC and optional matching symbols.
Neither includes source/. Do not routinely create a separate source attachment
or Update ZIP. Source delivery moves to the matching GitHub commit/tag, with
fixed dependencies and build instructions available to recipients. Repository
publication is a later user action, not permission to change visibility now.

Unified Public must be flat, include the complete supported runtime and six
placement guides, and exclude config.ini and all user-data/, game payloads,
prepared/, cache/, logs/, source/ and symbols. The launcher already generates
missing configuration from its embedded template and preserves existing text.
The format-2 scripts now implement this policy; filenames are
EVAC-PC-LABEL-v<version>-win-x64.zip and ...-win-x64-internal.zip. Release.ps1
defaults to Public. Internal accepts DataGames, FutariContent and Symbols;
all data is manifest-only, no source clone. The byte-identical old launcher
alias refreshes existing shortcuts. Tiny authored package/extraction tests
pass (34 packaging, one file-link privilege skip; 6 source-package tests).
Full source regression now passes 497 tests (same single privilege skip), and
source audit passes; log: .local/v04-package-format2-source-r1.log.
Actual format-2 packages are built/accepted at ddf71cfb5f1e015fdd634c8f561ddd85065521ef.
Each was extracted exactly once; 136 Public files, 602 Internal files, nine
preservation sentinels per package, 36 shared runtime identities match. Internal
contains all five games, both Futari DLC and symbols, no source. The new five-game
incremental build verified 56/6/2 PE/PDB pairs and 27 native CTests. No game logic
changed from 091c15c; reuse accepted gameplay and identical-launcher screenshot.
Public (78205835 bytes, SHA 6aa98f48759d291787d11f9e7619ca9429e6f0278f7d708f07cc52d128d945aa)
is uploaded/verified. Internal (7085755632 bytes, SHA
44c94ad6bd3643ab83ab4291858206d9138085b1024bea92fd4383031ecbdefe) is also uploaded/
verified. Both have matching remote length, SHA metadata, public checksum and
bounded 32-byte ZIP header. Upload session 87002 exited 0; no job remains live.
Evidence is .local/v04-package-format2-*; current source ddf71cf is pushed.
The frozen format-1 Public still ships user-data/config.ini and must not be
described as overlay-safe.
Do not add first-install/update classification to player-facing copy.
README win-x64 program/checksum URLs are now verified. Group text uses the new
names at .local/v04-win-x64-group-message.txt; no new group announcement yet.
Latest follow-up: actual 1V held-input acceptance now passes in
.local/v04-sdoj-held-r1/logs/sdoj/run-20261023T041238915Z-102632 using frozen
ddf71cf runtime and an independent offline profile. The existing TestPad holds
logical buttons for 1..600 guest input frames; it is not limited to short taps.
Eight steps cover movement+shooting, releasing either component, diagonals and
neutral state: all 750 actual gameplay logic samples match, with same-frame
core mask copies. 6285 early/prefix/driver receipts match, zero timeouts, 6140
low publications; one bootstrap recovery. Six full present windows/722 intervals
max at 17.307 ms, none over 20 ms; 33 audio windows have no silence blocks.
Normal Alt+F4 shutdown completed; no game remains. Evidence and reproducible
local controller/analyzer are in .local/v04-sdoj-held-r1. Shared config stays 3V.
This is game-side logical-input acceptance, not a physical-device result. The
latest scope decision removes held focus-loss testing from this round; do not wait for manual
input or expand the boundary-test scope. Reuse already accepted training reentry,
replay and A/V results; no new full campaign or package build.
QQ's actual existing executable is C:/Program Files/Tencent/QQNT/QQ.exe, not the
stale QQScLauncher installed-app entry. One launch through sky exposed its login
window (2026-10-23); no messages sent. Authentication must be done by the human.
Do not repeatedly launch QQ or automate authentication. Taskmgr was not opened.
Existing v0.4 archives, source attachment, URLs and acceptance records remain
unchanged. Do not rebuild them merely for the policy or documentation changes.

### Latest handoff — v0.4 artifacts are frozen and uploaded

Player/source artifacts are fixed at 091c15c76b9fb51919332310fc802280026cd613.
Five-game build receipts share that commit; 56/6/2 PE/PDB pairs and five runtime
certificates pass. Final SDOJ modules and PS4 runtime match the actual accepted
candidates. The last 27 common/360 CTests pass. Do not repeat accepted gameplay,
builds or extraction for the post-release documentation commit.

All four final ZIPs under releases/v0.4-*-091c15c were really extracted ONCE;
receipts are under .local/release-acceptance-v04/<zip-sha-prefix>/accepted.json.
Public 136 files, Update 130, Internal 2915, corresponding source 37205 pass.
Update overlays an existing extracted v0.3 copy, preserves six data/config/save/
replay/source sentinels, and its old-name native launcher opens correctly.
Internal's actual extracted launcher also opens correctly and is now closed;
its screenshot is .local/v04-release-media/launcher-v0.4.jpg. All 35 shared
player runtime files match across flavors. Corresponding source includes 25
SDK and 19 PS4 fixed dependencies; Git verification in the deep test directory
needed process-only core.longpaths=true for seven ext-boost HTML paths. Files
already matched the ZIP inventory; no global setting or archive was changed.

Internal 7105602167 bytes, Public 72888170, Update 78200361 and Source 288246758
are uploaded under OSS v0.4/ with EVAC-PC-LABEL-v0.4-<flavor>.zip names and
matching .sha256 files. All remote lengths, public checksums and bounded 32-byte
ZIP range requests pass; .local/v04-oss-*-verified-r1.json has the evidence.
Do not overwrite/re-upload/re-extract frozen files. No production service was
modified. No games or owned loopback services remain running.

On October 23 the user approved proceeding with default 3V and requested more
1V testing. Physical held-direction/shoot-release acceptance remains open as
a focused follow-up, not a release blocker; do not mark it passed. Cover held
movement plus shooting, release, focus loss/recovery while held, and a short
training re-entry with audio/frame-pacing observation. Reuse accepted replay
and mechanism evidence instead of restarting full campaigns. Keep the shared
configuration at 3V and use a separate profile for 1V tests.
The current v0.4 release tag belongs to the new win-x64 source ddf71cf, not the
older frozen 091c15c archives. QQ announcement remains pending.
Native CU found no targetable QQ window; do not open Taskmgr or
start repeated QQ instances. Draft group text is .local/v04-group-message.txt.
The updated public README links and artifact evidence are documentation only;
the corresponding source of these packages remains 091c15c.

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
The earlier 3fc6625 dat163 SHA was
1156b4264d04779d43d9abffadecccf92a8b7821632b8919cab5f9fe47125a29.
Xbox360 Novice player-entry regression now passes at baseline 8fab1ce: run
logs/espgaluda2/run-20261022T115223367Z-81172 entered dat163, selected Ageha,
showed PLAYER-1 stage 1 and score growth, paused, returned via Previous Menu,
unloaded dat163, then closed normally. Do not repeat that accepted short test.

Earlier CU attempts were blocked by an elevated foreground Taskmgr. Authorized
close/kill/minimize attempts failed. Do not repeat unchanged recovery, blind
occluded-menu guesses, or bypass OS permissions. The user has since authorized
closing the two old launchers; both are now closed and their files preserved.
The bounded SDOJ runs 97224/48620 closed by diagnostic quit; they exercised
module 2, not the failing module 1. .local/v04-sdoj-metrics-r1/test-pad.txt is
still '1 quit'; reset it before reusing that independent diagnostic root.
An offline test pad proves InputSystem behavior, not physical desktop input.

Latest bounded SDOJ attempt: .local/v04-sdoj-metrics-r2, own PID 78892, runtime
source 3fc6625, offline fresh profile and 1V. Logical A/Start were acknowledged;
it reached title/first save-file creation prompts, still partly covered by the
same Taskmgr. No target mode or module-1 gameplay was entered. Do not repeat this
occluded-menu loop. Ticket 5 quit used normal guest stop/window-close; process
is confirmed gone. summary.json and logs/sdoj/run-20261021T162641799Z-78892 retain
the result. test-pad.txt is now '5 quit'; reset it before any authorized reuse.

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

- Captions, Windows properties, EVAC PC LABEL.exe, BAT, build receipts, v0.4
  binary/source archive names and current bilingual docs are implemented.
  Actual EXE version properties and two launcher PE/PDB pairs pass; 226 hidden
  UI, 386 common, 139 launch/copy and 27 package tests pass (one link-privilege
  skip). Full source suite has 486 tests (one privilege skip), source audit passes.
  Authored Internal/Public/Update assembly/extraction verifies new names;
  Update's old shortcut filename is byte-identical to the certified new native
  EXE, never a wrapper. Fresh full packages have only the new launcher name.
  Old archive profiles, game IDs, saves, config and API namespaces stay stable.
- GitHub was renamed by the user; origin is now
  git@github.com:zxl0714/EVAC-PC-LABEL.git. SSH main matches the previous history.
  Package source remotes/notices and the development clone command use it.
  Old launchers 90668/91688 were closed after exact path validation and explicit
  user authorization; files remain. An idle authored local DaiOuJou fixture
  server was stopped after checking no active clients; production was untouched.
  Direct physical rename failed; split files were restored and verified. Do
  not retry Move-Item, change ACLs, create old-path junctions, or kill unrelated
  processes. Migration instead uses a fresh independent SSH clone at the NEW
  active root E:/Projects/EVAC-PC-LABEL, same history, no Git alternates. Original
  root and all historical artifacts remain intact. 25 SDK dependency checkouts
  (22 top-level) and 19 PS4 dependencies were restored independently from verified
  local seeds, with official upstream remotes. Pinned tools were copied/verified;
  tool Python was newly created, not moved. Existing installed SDK binaries were
  copied, not called through the old root; three pkg-config prefix metadata files
  were relocated and SDK source/tool/binary receipt verification passes. This is
  installed SDK reuse, NOT a clean SDK compilation. Fresh common/launcher build
  and all three common/launch/source CTest groups pass. Initial source: 80edd1c;
  evidence in NEW root
  .local/v04-new-root-{tools-r1.log,sdk-deps-r1.log,ps4-deps-r1.log,
  common-build-r1.log,common-native-r1.log,data-r1.json,inspect-r2.json}.
  All five supported data sets plus Futari DLC were copied by exact manifests;
  user-data copied with identity checks, then only four config data/DLC paths
  changed to the new root's relative directories. Saves/replays untouched. Old
  root config still unchanged. Browser URL safety stop remains in force.
- The complete wordmark is the EXE icon, not the rejected standalone star.

PS4 branding regression r1 was 72/73: a timing assertion expected zero budget
even when Address wait legitimately returned success at ~199069 us of 200000.
Short-circuited Delete then poisoned registry checks. Keep r1 failure evidence.
Only the self-test was corrected to check budget against actual elapsed time
and always delete; production Wait/Signal/Grant are unchanged. r2 is 73/73.
See .local/v04-brand-ps4-native-r{1,2}, properties-r1.json and TEST_MATRIX.
Xbox 360 common host library compiles with the new title, but final same-commit
player builds/certificates remain pending. Do not confuse these with a release.

Earlier five-game component baseline was built/certified at 3fc6625: host,
PS4 and launcher receipts all name that same commit; all five runtime INIs match
their certified file hashes. 56 Xbox PE/PDB pairs, 6 PS4 and 2 launcher; 27
common/360 CTests pass. This is not final release/gameplay acceptance. Evidence:
.local/v04-five-game-{certified-set-r1.json,xbox-build-r1.log,
xbox-finalize-r1.log,ps4-build-r1.log,xbox-native-r1.log}. Four superseded out/
launcher EXE/PDB files were recoverably moved to
.local/v04-retired-launcher-build-outputs-21f5e359094e4e26b1ee26faf08a4a6a;
root/pre-existing launchers and frozen packages stayed unchanged. No ROM copy.

Latest NEW-root baseline is built/certified at
8fab1ce9f2aeea899a7001fc6221ba56774e03fe: PS4, all three Xbox games and launcher
receipts share that commit; 56/6/2 PE/PDB pairs and all five runtime INIs verify.
PS4 completed 375 GPU and 555 native build tasks, Xbox/common 508. Five PS4 GPU
CTests, 73 PS4 native checks and 27 common/360 CTests pass; the latter includes
1122 SDOJ frame-delay ownership checks. New-root Test-G2Callback passes all 12
ABI checks using the newly generated entry. Program-level inspection finds no
missing runtime files, but correctly still requests full resource verification;
full data inspection was already recorded before staging. No game was run.
All build/test sessions have exited successfully. Evidence in NEW root:
.local/v04-new-root-{ps4-build-r1.log,xbox-build-r1.log,ps4-gpu-tests-r1.log,
ps4-native-r1/result.json,xbox-native-r1.log,g2-callback-r1.log,
certified-set-r1.json,inspect-r3-programs.json,user-data-r1.json}.
Current follow-up documentation is newer than that baseline, not a new player
release. Do not recertify/rebuild just for this status record. Final release
certification still waits for actual gameplay gates and the final source commit.

GitHub integration was discovered and suggested, not installed/connected at
inspection. Do not resuggest within seven days or claim it supplies repository
administration until connection/capabilities are confirmed. GitHub management
is no longer a blocker: the human renamed it and the new SSH address works.
The user closed Taskmgr on October 22; native CU now activates the launcher and
game windows successfully. Never reopen Taskmgr or repeat prior recovery detours.
G2 Novice's accepted desktop run is above. The actual launcher desktop smoke
also passes: choose Ketsui, minimize, launch again (same PID 99140, second PID
71660 exits), normal close, cold reopen (PID 100576) restores Ketsui. A Hidden
shell launch initially hid the new window; a normal GUI launch restored that
same process via the mutex message. Config now contains selected_game=ketsui.
SDOJ and Ketsui actual gates remain. Temporary input aliases are restored to
plain arrows and the local frame_delay is back to 3; no game remains running.

SDOJ baseline run logs/sdoj/run-20261022T115752086Z-101808 reproduced the metrics
exception in Arcade HD / Type-C / Shot, at logic 19627. The full diagnostic ring
shows 19624 sampled, 19625 unsampled (due 19957), then 19626 sampled; the frozen
source is 19625 at calls 19958. There was one budget skip, no timeout. The earlier
25f3be9 fix alone is insufficient. Preserve this dump and module snapshot locally.

Current uncommitted candidate adds a per-completion Sampled guard before the
bootstrap/early wake. 1134 ownership checks and five source-contract tests pass;
both module DLLs compile. Actual candidate run
logs/sdoj/run-20261022T121639607Z-10556 entered Arcade HD / Type-C / Shot, fired,
scored 12620, and reached natural result/replay selection without the exception.
However after its first timeout early_requests stopped at 3303: ordinary late
wakes keep producing another unparsed batch, so this candidate can remain on
balanced timing. It is NOT accepted as the completed 1V fix. Retain the gate;
resolve recovery or explicitly revisit the manual-policy contract, not fake
metrics, suppress assertions, or call the requested policy an actual latency.
The prior out-tree run 91536 never loaded its module because the expected module
directory was missing. Actual candidate DLLs are now copied from out/xbox360/bin
to out/xbox360/games/sdoj; process-module inspection confirmed that path. run.json
still inventories the staged baseline, so do not use it as candidate DLL identity.
The certified runtime/ tree is unchanged. No v0.4 release has been produced.

Recovery r5 drew the unseen batch first and deferred exactly one pending
late wake to the following early slot, carrying its frozen native metrics.
Actual r4 established that the late driver runs after ReadEnd but before the
original metric stores: Capture now runs at parser return, with equality
verification at those original stores, never a second Capture there.
1345 C++ checks and seven source-contract tests pass. r5 player run
logs/sdoj/run-20261022T125057047Z-96616 reached Arcade HD / Type-C / Shot stage 1
and natural result, normal close; one recovery, then 10553 early requests,
10416 low publications and 10615 matching store verifications, no timeout.
Do not claim shooting/focus/held-key acceptance from that run. DLL identities
and failed predecessors are in TEST_MATRIX. Short replay runs are complete under
.local/v04-sdoj-recovery-replay-{balanced,low}, PID 48960/34332; both closed.
They fall in the known 5504/5505 initialization classes. Direct cross-class
comparison fails and is preserved; each complete window matches its historical
same-class opposite-policy reference exactly, including raw command SHA, native
states, depth and both slowdown operands. Both have 131 depth=2 updates. Do not
repeat runs merely to obtain the same initialization class. Low has one recovery,
5638 matched early/prefix/driver receipts, 5506 low publications, 7307 metric
verifications, zero timeouts, one pre-wake budget skip. Staged runtime and main
config (3V, ordinary arrows) remain untouched. r5 later failed training re-entry;
it is superseded by r7 below. Do not repeat accepted G2/launcher tests.

Training re-entry r5/100092 and diagnostic r6/101048 exposed a legitimate
native post-parse reset: sub_88046330 clears renderer+544/+548 when control+720
is set. Parser values 1688688,0 become final stored 0,0. Current r7 pins only
the parsed identity at ReadEnd; Capture uses the original final-store operands.
Recovery defers the exact late wake, then freezes its selection after final
Capture. Do not restore the r5 parser-time value assumption or remove guards.
1371 mechanism checks, seven source contracts and 27 native CTests pass; both
DLLs built. Training run 43740 under .local/v04-sdoj-recovery-replay-low passes
the previously crashing re-entry and closes normally. Both native resets were
observed. 23166 paired early/prefix/driver receipts, five recoveries, four budget
skips, no timeouts. Confirm/back and focus-out/in pass; held directions/shoot
release remain unverified. Final fixed replay PID 86580 has naturally finished
and closed: Arcade HD SECOND slot (REPLAY_XB02.DAT despite the filename),
module 1/generation 2/logic 1..5505, matches historical balanced/98276 exactly,
including both slowdown operands and 131 depth=2 updates. Reports are
.local/v04-sdoj-native-final-r7-{closed-exact,slowdown}.json. One recovery,
5639 paired receipts, no timeout or budget skip. The earlier Xbox 360 menu
selection in the same run was module 2 and is not the target comparison.
Main config remains 3V. Preserve r5/r6 crashes. Final release still pending.

## Documentation and release

Ketsui actual base regression now passes in NEW-root
.local/v04-ketsui-desktop-r1/base-loopback-r3, PID 103252 (closed): ARCADE/Type-A
player entry, natural result, saved credit-unlock progress cold-read 5 then 6,
actual two-board loopback score/replay delivery, visible Online Ranking,
download and full ~62-second Download Replay returning naturally. No production
scores were written. Native replay data version is 0x10000 for base, not 0x10001;
this exposed a real capture/envelope rejection in baseline PID 67116. Current
candidate binds capture to each executable's replay revision, accepts only the
two known envelope revisions, preserves raw bytes and native replay checks.
The original 1.02 viewer already has a minor-version 0/1 branch. 49 capture,
38 journal, 19 audit, 24 envelope, 33 profile and 10 HTTP checks pass; 489 source
tests pass with one privilege skip. EXE SHA is
9abbe1c885844980604ce79b940114228701df72fbd0b4a223b573939adcef30.
Candidate PS4 runtime/ is staged; the Xbox final release is not yet rebuilt.
Updated single-root actual regression has passed; PID 102668 is closed. Under
updated-loopback-r1, a fresh COPY of base's native profiles, not originals,
read prior unlock progress, entered ARCADE/Type-A, used a bomb and naturally
finished at 1130. Both boards and replays reached loopback with HTTP 200 and
native replay proof 0. Its original Download Replay viewer played the inherited
base record for the full ~62 seconds and returned naturally. Normal Alt+F4
completed presentation cleanup; existing incomplete finalizer/exit 27 remains.
Its prepared cache is cache-91a4ac91863f32b7-a2cbdc24b033c45a6f9466885b045b46.
Loopback server PID 38964 was stopped after confirming its client had exited
and its exact database/port command line. These CLI tests explicitly use 120 ms
diagnostic taps; do not label them physical held-input acceptance.

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
