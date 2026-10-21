# Development entry

The active user goal now authorizes implementing the complete v0.4 plan step
by step, including the reported crashes, single-directory game data, launcher
UX/single-instance behavior, project/repository rename and verified release
artifacts. Earlier question-only/documentation-only restrictions below are
historical. Keep default SDOJ 3V, explicit preferences and frozen releases.
Prioritize the two real crashes and use targeted evidence, not another full
mode campaign. Do not publish internal ROM-containing packages on the README.

G2's dat163 0x880EEB00 declaration is now implemented/generated/registered and
the real generated forwarder passes 12 isolated native ABI checks. The 360
build verified 56 binary/PDB pairs. Target-mode regression is still pending:
run 94912 reached the title, but CU input failed to activate the game while the
same Taskmgr remained foreground. That new game was stopped; do not repeat the
unchanged Taskmgr/focus recovery. Old launchers were left alone. The dat163
DLL SHA is 1156b4264d04779d43d9abffadecccf92a8b7821632b8919cab5f9fe47125a29.
SDOJ now logs the eight metrics entries and exact counters before the existing
missing-metrics exception. Both modules compile, 1112 mechanism checks pass;
this is diagnostic progress, NOT a slowdown fix. Default remains 3V.
Final source suite: 469 tests, one symlink-privilege skip, source audit passed;
27 current common/360 CTests pass. Logs: .local/v04-crash-source-r1.log and
out/xbox360/.local-v04-crash-native-r1.xml. No game process remains. Continue
SDOJ's missing-statistics mechanism next, then the remaining full v0.4 scope.

One bounded SDOJ diagnostic attempt is now closed: .local/v04-sdoj-metrics-r1
runs 97224 and 48620 both received the offline test-pad quit and terminated.
The first accidentally exercised module 2 / Xbox360, not the failing module 1;
do not treat its lack of a metrics error as a regression pass. Its title-menu
EXIT returns to the outer mode list; Back toggles 1P/2P and B only leaves the
submenu. Taskmgr covers the outer menu's left list, including in windowed
capture. Fresh windowed activation still failed. Do not repeat blind menu
guessing or unchanged desktop recovery. The independent test configuration and
copied profile remain local; shared config stays 3V and original data untouched.

Latest user decision: restore SDOJ's default Frame Delay to 3V after the friend
crash. Update Settings, the fresh INI template, configuration/cold-start tests
and current developer/v0.4 player docs. Preserve explicit 1/2/3 preferences;
1V/2V remain manual test options, not stability claims. Keep old v0.3.1/v0.3.2
release records and frozen packages intact. No crash fix or new release is
implied by changing the default. GitHub rename is discussed only, not authorized
for execution by the current question; origin/local directory stay unchanged.
The default change is verified: 465 source tests (one privilege skip), common
and launch CTests, and 118 hidden launcher checks including three cold locales
pass. Only common/launcher were built, root launcher and game runtimes retained.
Local config now explicitly sets 3 with all prior text preserved; backup is
.local/config-before-default-3v-8e1d663393e140e6920fcb61bd0fd0e4.ini. No new package.

v0.4 prioritizes the two friend crashes recorded in IMPLEMENTATION_PLAN before
UI/release work. SDOJ run 71556 throws missing slowdown metrics at logic 3187
in 1V / SdojDelaySlowdownInputs; the exact render-metrics state was not captured.
Do not guess zero metrics or remove the check; 2V shares this tracker, 3V skips
it. G2 run 84416 / dat163 Xbox360 Novice calls unregistered 0x880EEB00, a valid
12-byte forwarding entry to 0x880EE9E8. Add declarations, not generated edits.
Both matching game DLLs persist in the current release. Diagnostics and raw
inputs stay ignored under .local; analysis is complete, fixes are NOT done.

v0.4 also includes a single-instance GUI launcher per installation: subsequent
opens restore/focus the existing window without starting another launcher or
game. Preserve selection, unsaved settings and the running game; keep distinct
installation roots isolated. Recorded in IMPLEMENTATION_PLAN; not implemented.

Maintain README.md (Simplified Chinese) and README.en.md (English) together,
with reciprocal language links at the top. Keep release versions, requirements,
download URLs, data layouts and feature status consistent in both. English UI
labels must match launcher/strings.h; label Chinese-only linked guides clearly.

Public READMEs should describe the product and how to use it. Omit former-name
history, renaming schedules, internal decision explanations and repeated caveats.
Keep actual executable names, supported data versions, update prerequisites,
troubleshooting essentials and license notices. Put detailed edge cases and
development history in their dedicated guides, not the front page.

README is now a public-facing player entry rather than a development diary.
docs/GAME_DATA.md describes the CURRENT v0.3.2 paths/versions and data import;
docs/CHANGELOG.md holds concise player-visible release notes. Keep future v0.4
branding and single-directory/base-only Ketsui separate from shipped behavior.
The README's verified public download is Update only; an actual no-ROM full
Public download and matching source entry must be provided at public release.
Do not link internal ROM-containing packages on the public front page or invent
a renamed repository/release URL. Documentation edits do not rebuild frozen ZIPs.

Project brand is now EVAC PC LABEL (formerly CAVE PC Collection). README and
FOR_AGENT use the new name; whole-code/build/release branding is planned for
v0.4, including EVAC PC LABEL.exe and EVAC-PC-LABEL archive/repository naming.
Actual GitHub/local checkout renaming is pending; do not claim it occurred.
Preserve game IDs, user-data, save/replay formats, existing API URLs, old tags,
ZIPs and OSS objects. This turn is planning/documentation, not a new build.

User-approved data UX: precreate game-data/<id> folders with per-game placement
instructions in new full packages, use one directory per game, and let Ketsui
run from supported unpatched base data or supported base+1.02 merged data.
Updates are extracted over the actual base root. User explicitly rejects
legacy CUSA09062-app/CUSA09062-patch compatibility; do not implement a legacy
fallback. This requires real base-version and merged-layout runtime support,
not removing validation. No runtime or manifests changed yet; released v0.3.2
still needs its old layout. Do not physically merge/delete/move current data.
Full packages include empty templates/docs, Update preserves existing data.
Detailed tasks are in docs/IMPLEMENTATION_PLAN.md under v0.4.

New launcher requests are recorded in docs/IMPLEMENTATION_PLAN.md under v0.4:
persist the selected game at normal launcher close and restore it; bottom-align
the launch button with the left tools and remove its permanent arrow/Enter
caption (retain keyboard behavior); use EVAC PC LABEL for the launcher Windows
title and EVAC PC LABEL.exe for the default entry; tighten CN/EN/JP spacing and
align the language group to the visible logo top. This turn records these
requirements only: no launcher code/build/package change has been made. Keep
the accepted v0.3.2 ZIP, tag and OSS object unchanged. Game/data identities must
not change; the new project-wide naming request above supersedes the earlier
launcher-only scope and includes planning the repository rename.

The latest v0.3.2 CUSTOM revision is now uploaded and publicly verified:
https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip
Its .zip.sha256 companion matches; public HEAD=200, byte count/source/revision
and SHA-256 metadata agree, bounded 32-byte ZIP-header GET=206. Receipt is
.local/v032-custom-oss/receipt.json. No rebuild/re-extraction or QQ post.
Use this URL for the revision containing BOTH ranking fixes, not the older
local-only AC-CHALLENGE archive. Existing OSS objects were not overwritten.

Latest v0.3.2 revision includes BOTH DaiOuJou AC-CHALLENGE and Ketsui CUSTOM.
Binary source a8a75fb9d86a11003c8fa9f3e1fb38521bcb2a07, tag v0.3.2-custom.
Accepted ZIP: releases/v0.3.2-custom-a8a75fb/CAVE-PC-Collection-v0.3.2-update.zip,
72,777,904 bytes; SHA-256 9bb302546e3f42c9050a4180e1cbee0d3b7784a272979fa71825835173b7ca99.
465 source tests (one privilege skip), 29 replay checks, nine HTTP cases,
36 native round and 37 journal cases pass. General native suite first run was
72/73: event semaphore stale-identity cleanup assertion failed; unchanged
isolated semaphore rerun passed. Retain both logs; no synchronization code
changed and do not rewrite the first run as 73/73. One real v0.3 overlay
verified 129 payloads, 12 preservation sentinels and installed native CUSTOM
replay/profile checks. Reuse .local/v032-custom-overlay-a8a75fb/accepted.json;
do not rebuild/re-extract for documentation follow-ups. Ketsui service alone
now points to /opt/ketsui-live/releases/a8a75fb after 44 Ubuntu tests and a
database-copy compatibility check. Old adb46f8 release and pre-v032 database
snapshot retained; five players/20 scores/18 replays preserved, peer PIDs,
nginx and unit unchanged. Public CUSTOM 60/61/62 reads work through old and
collection endpoints; DaiOuJou a865369/board99 remains working and untouched.
No synthetic production submissions, new desktop/menu acceptance, OSS or QQ.
Old v0.3.2/a865369 ZIP and tag remain intact; use the custom revision above.

Current user extension: also fix Ketsui CUSTOM in v0.3.2. Native CUSA09062
1.02 defines boards 60/61/62 with upload limits 100/10/10; CUSTOM mode=3
keeps its settings in the original replay snapshot. The native game does NOT
partition CUSTOM by a configuration fingerprint. Remove that extra restriction
for standard native requests, retaining old explicit hashed partitions without
merging rows. Extend native score, round capture/journal and replay matching;
keep native continue/co-op/practice eligibility and ordinary boards unchanged.
Deploy ONLY Ketsui's existing service after backup, preserve DaiOuJou a865369
and all peer PIDs/config. Build a new v0.3.2 revision, retain the old ZIP/tag;
no OSS or QQ is requested. Existing Taskmgr/focus failure remains deferred,
not grounds to loop desktop recovery or claim a new menu test passed.

v0.3.2 is built from a865369bfdfd880789d7ca1de00f4f0a121fe77d. The no-ROM
Update ZIP is releases/v0.3.2-update-a865369/CAVE-PC-Collection-v0.3.2-update.zip,
72,773,377 bytes; SHA-256 e7349d8c50fa1072f1fe940bc85953a8aa03b141b0c095ba12885554e0f38e7f.
440 source tests (one privilege skip), 73 PS4 checks, five independent HTTP
cases including native challenge scoring, and one actual v0.3 overlay pass.
The overlay verified 129 payloads, 12 preservation sentinels and the installed
native profile self-test. Reuse these reports; do not repeatedly extract or
rebuild an unchanged accepted ZIP. .local/v032-overlay-a865369/accepted.json
records the archive identity. DaiOuJou service now points to releases/a865369;
its previous release and pre-v032 database snapshot are retained, existing
score/replay rows preserved, peer PIDs/nginx unchanged, public board99 GET=200.
No synthetic production scores were submitted. Desktop launcher activation
failed on both refreshed selections; the in-game challenge menu was NOT newly
verified. Do not loop Taskmgr/focus recovery. No OSS upload or QQ post occurred.

Current task: fix DaiOuJou AC-CHALLENGE RANKING in v0.3.2. Its native board is
99, cumulative medal points (maximum 3,960,000), with uploadable=0 in the
verified native definition. Add it independently to the client/server profile;
do not rescale points, fabricate a replay, or change ordinary score/replay
semantics. Verify through isolated ABI/HTTP tests and a bounded menu check.
The v0.3.2 Update is cumulative over v0.3/v0.3.1, no ROM/source/config/user-data.
Retain default SDOJ 1V, branding and accepted old archives/tags. Only deploy the
DaiOuJou service with a consistent backup; no synthetic production scores or
other-title restarts. No v0.3.2 OSS upload or QQ post is requested.

For the user's requested QQ announcement draft, the accepted logo build has
now been uploaded under a NEW prefix, preserving the earlier public object:
https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.1-logo/CAVE-PC-Collection-v0.3.1-update.zip
Its .zip.sha256 companion, public HEAD=200, identity metadata and checksum
match; the bounded ZIP header request is 206. Receipt is
.local/v031-logo-oss/receipt.json. Use this new URL when describing the logo
revision, not the older root-level URL. The user will post the text; no QQ
message was sent by the agent. Do not reupload or re-extract this same archive.

Current user request supersedes the frozen-no-rebuild note for this new content:
use the supplied EVAC PC LABEL logo for the launcher EXE icon and top-left UI,
then create a fresh v0.3.1 Update ZIP. The user rejected the standalone star;
use the complete EVAC + PC LABEL wordmark for the icon, and the full logo with
tagline in the launcher. The JPEG attachment had no alpha; transparent PNGs
were prepared with the built-in image tool and copied into launcher/branding/.
Keep default 1V and preserve explicit modes, data, config and profiles. Validate
resources/alpha/icon sizes and layout, do one final overlay acceptance, and keep
all earlier ZIPs/tags intact. No new gameplay campaign, OSS upload or QQ post is
requested. Root launchers may remain open; use -SkipLauncherStage as before.

The wordmark correction is built and accepted from source
f26c9e73a2f89cb6bca8060a3cf458100b12f942, tag v0.3.1-logo:
releases/v0.3.1-logo-f26c9e7/CAVE-PC-Collection-v0.3.1-update.zip,
72,764,656 bytes, SHA-256 ae987c08b26e3b3e2ed082b5e199edf58b183c63477e61a45aa5f633f9137ac4.
437 source tests (one privilege skip), 118 hidden launcher checks, one actual
overlay with 129 payload hashes/11 sentinels/nine cold config checks, and a
read-only check of the actual overlaid PE's logo plus nine icon frames pass.
All icon frames are the full wordmark, not the star draft. The final native
visual preview is .local/v031-branding-preview/final-zh.png; its installation
states are examples. Old star drafts and failed alpha-test logs remain local.
No game/input campaign was restarted. This new ZIP is local only; the existing
public v0.3.1 OSS URL still serves the earlier default-1V build without the new
branding. Do not claim that URL contains the logo build. Keep older ZIPs/tags
and do not rebuild this accepted package for documentation follow-ups.

The user subsequently requested OSS upload of the accepted default-1V Update.
Upload and public verification are complete at
https://xiaoluoss.oss-cn-beijing.aliyuncs.com/CAVE-PC-Collection-v0.3.1-update.zip
with its .zip.sha256 companion. Public HEAD=200, bytes/hash/source metadata and
checksum match; a 32-byte range GET=206 confirms the ZIP header. Receipt:
.local/v031-1v-oss/receipt.json. Do not reupload or re-extract the unchanged
package. No QQ announcement was requested for v0.3.1.

Latest user request: make SDOJ default to 1V and replace the local v0.3.1
Update with a matching new-build ZIP. This overrides older default-3 and
input-check-before-default restrictions. Change the built-in missing-key
default and new-config template, preserve explicit 1/2/3 preferences, update
tests/player docs and perform a bounded cold configuration/overlay check.
Ordinary desktop input/focus remains deferred, not newly passed. Do not resume
the replay campaign or retry Taskmgr. Preserve the earlier v0.3.1 archive and
tag; put the corrected build in a new release directory and use a new tag.
No OSS upload or QQ post is requested.

The default-1V corrected Update is complete and accepted at source commit
38717a1f2d7e3479415ff13f1d753a047cd0aaed (tag v0.3.1-1v):
releases/20261021T091643Z-a185ee75/CAVE-PC-Collection-v0.3.1-update.zip,
72,159,381 bytes, SHA-256 2330d6078cc0327b9a480367e8e89c41cf21ac3acd2f68a0d7c7a38f30930ff9.
433 source tests (one privilege skip), five targeted CTests and 95 launcher
checks pass. Its single actual v0.3 overlay passes 129 payload hashes, 11
preservation sentinels and nine hidden cold App checks, including resolved
SDOJ default 1 from the byte-identical migrated legacy config. Explicit modes
1/2/3 survive migration in native tests. Ordinary desktop input/focus is still
deferred, not passed. Frozen first-batch archives/tags remain unchanged; the
older source ZIP is not corresponding source for this new commit. SOURCE.txt
points to the new maintenance commit; create its matching source ZIP on demand.
Do not rebuild or re-extract this accepted ZIP for documentation follow-ups.

Current user request supersedes the older no-package restriction: produce a
v0.3.1 Update ZIP that extracts directly over an installed v0.3 directory.
Ship certified runnable components and player documentation, with no ROM,
source, symbols, config.ini or user-data payload. Preserve old data/settings/
saves/replays, and verify the actual overlay plus legacy config migration.
Frame Delay's ordinary-input/focus check remains deferred to v0.4; keep default
3 and preserve explicit 1/2/3 selection. Do not resume that desktop test now.
Commit the release source and build the matching components before packaging.
The old release ZIPs and acceptance directories remain unchanged. The current
request is local package delivery; do not infer an OSS upload or QQ post.

v0.3.1 Update is now built from d1b32cdb9d5f74ba7fcdf7d45505401ea83fcef6:
releases/v0.3.1-update-d1b32cd/CAVE-PC-Collection-v0.3.1-update.zip,
72,159,515 bytes, SHA-256 1fa5e982021435445559ec061d37965a8d195e6059138806e818051be2593fc1.
Its one actual overlay acceptance passed; 129 payload hashes, 11 preservation
sentinels and 8 hidden production App cold-start/config migration checks pass.
The no-ROM fixture is not a new gameplay check. Reuse this result rather than
re-extracting or replaying accepted campaigns. Record delivery separately;
do not rebuild the frozen package for documentation-only follow-ups. Keep the
v0.3.1 tag on the binary source commit, not a later acceptance-record commit.
The development root launcher was deliberately not restaged while open;
the package uses certified out/ outputs with matching receipts and symbols.

Current goal: implement SDOJ Frame Delay per docs/SDOJ_LATENCY_PLAN.md, including
3/original, 2/balanced and 1/low; preserve cadence, slowdown, input/replay semantics
and resource ownership. Earlier documentation-only restrictions are superseded.
Do not package or publish. Existing v0.4 launcher/configuration work is accepted;
keep it and unrelated dirty changes intact. Implement and verify 2V first, then
1V, using bounded mechanism tests and the planned representative game checks.

Latest acceptance (2026-10-21) takes priority over historical notes:
All five map-init-r1 replay runs are CLOSED; no SDOJ process remains. Do not
restart the accepted replay campaign. The frozen runtime is
.local/sdoj-frame-delay-map-init-r1; all five run.json manifests agree on 13
runtime/symbol files, and current files plus six replay copies were rehashed.
Normal module SHA is 5b2cf417bd85cf2f4dc2fa8f97c1e315842075e692f7781cc2c1c6661b2f2903.
Module-1 first-stage normal-speed output chains are complete for 2125/2125
results per policy: 3V/2V/1V medians 49.957/33.266/16.847 ms and maxima
51.585/34.002/17.690 ms. These are copy-to-Present CPU returns, not scanout.
Fine balanced 26132 and normal low 98776 match all 5504 native-end updates;
normal balanced 98276 and fine low 97596 match all 5505, including slowdown
operands, commands, state and depth (131 depth-2 in each). Two native readiness
timings occur within BOTH policies. Preserve failed cross-class comparisons;
do not shift/drop updates or alter native readiness polling to force equality.
Original 94360 vs low 97596 also match all 5505 commands/state/depth; four
slow_stat0 differences are adjacent publication timing, without depth changes.
Normal balanced/low retained logic_trace but disabled fine latency/output/audio
and content tracing. Stage intervals 2290/2170 average 16.667 ms, max
17.430/17.530 ms, no >20ms intervals; audio silence 0, queue peaks both 8 blocks.
Low has no timeouts/draining/ownership skips, max wait 999 us. Its one budget
skip occurred before Stage 1 and did not increase during gameplay; not zero.
Reports: .local/analysis/sdoj-map-init-r1-normal-pacing-audio.json,
sdoj-map-init-r1-normal-balanced-low-exact.json and
sdoj-map-init-r1-balanced-normal-low-exact.json. TEST_MATRIX has full scope.

User update: ordinary keyboard short taps, live-craft movement and focus loss/
restore are explicitly deferred to the v0.4 TODO list. Do not keep launching
games, retrying Taskmgr or asking for the same manual intervention now. Include
a brief A/V observation in that later check. The recommended release default
is now 1V after that acceptance, retaining 2V as the compatibility fallback and
3V as original. Then update/verify settings, template, tests, player docs and
actual cold startup. Current default remains 3; this documentation-only request
does not authorize changing it or marking deferred tests passed.
Taskmgr PID 63148 is still elevated/foreground. Authorized close, kill and
minimize attempts did not work. Do not repeat unchanged or bypass OS permissions.
The offline test pad proves logical InputSystem behavior, not desktop input.
No full-mode, network, other-game or packaging campaign is required here.
Do not manufacture further offline tests as substitutes for the remaining
normal input gate. Once independent work is exhausted, report the real blocker
and apply the goal's repeated-blocker audit rather than looping indefinitely.

Historical implementation notes (superseded where the current results differ):
content-r1 balanced PID 86068 and low PID 54144 naturally returned and closed;
all 5504 logical updates through native replay end match in command bytes/hash,
state, slowdown operands and depth (131 depth-2 waits). Low has zero timeouts
or ownership skips, maximum wait 1082 us. Its exact report is
.local/analysis/sdoj-content-r1-balanced-low-exact.json. Original PID 82044
matches their first 5014 commands/state/depth; three operand differences are
original worker reads just before the new renderer metrics publication, with
no depth change. Original A/A PID 87968 also naturally returned/closed, but has
212 depth-2 waits: the original itself can produce the former 131/212 mismatch.
Same frozen binary/profile inputs, no audio trace; no logical-frame alignment.
At logic 2845 the A/A difference is exactly 36 extra 64x64 background commands,
otherwise identical packets. Do not normalize this into a pass or blame the
Frame Delay policy/audio tracing alone. Native tilemap emitter 88032CA8 now
has a default-off, one-selected-frame map/index observer at 88032D68.
tilemap-r1 original-a 96968 and original-b 93144 both naturally returned/closed,
with 131 depth-2 waits and zero first columns in all four maps at logic 2845.
The native allocator leaves these 32KB maps uninitialized; the rectangle writer
fills 12 columns x10 rows, while the emitter also reads the preceding column.
.local/analysis/sdoj-background-border-r1.json reconstructs the older A/A's
entire 36 extra commands using ONLY those four border columns, with all other
551 commands identical. These are inferred used bits, not a memory dump of
the terminated old process. Normal-core compat/tilemap.cpp now clears each
new map at 88051C34, before the original pointer store; all three policies
share this one-time fix. It does not clear existing scrolling maps or filter
commands/metrics. 83 source tests and the three targeted CTests pass.
Build sessions 68584/78946 verify 18 PE/PDB pairs and 13 staged files.
Frozen .local/sdoj-frame-delay-map-init-r1 completed the fixed replay comparison
and normal pacing checks described above. Tilemap/content-r1 binaries/logs are
preserved. No new default is accepted (still 3); ordinary keyboard/focus remains.
Candidate RenderMetrics models ORIGINAL render publication using the native
completion baseline+depth-1, once per video frame. It does NOT assume logic N-2:
resource waits add video ticks without another logic update. Actual samples
are captured only after parser 880387B0 accepts ready commands, not at the
wrapper pointer setter (which also runs on held frames). Low now resumes a
known intermediate resource wait early on the SAME logic identity, rather
than skipping its completed result during the next bootstrap. Depth>1 low
results publish immediately but retain their original due-counter late receipt.
Two failed candidates are preserved: slowdown-r1 PID 98008 throws inconsistent
metrics at logic 23 (false parse attribution); slowdown-r2 PID 97028 throws
missing metrics at logic 2844 (resource-resume/bootstrap skipped logic 2842).
The older r1 binary's original PID 89092 fully returned/closed, matching all
5504 native-end iterations and 131 depth-2 waits; original AA report is
.local/analysis/sdoj-slowdown-r1-original-aa.json. Next compare the current
candidate's exact slowdown operands/depth/command hashes with that original
using .local/analysis/sdoj-slowdown-compare.py (explicit --frames). Current
candidate correctness is NOT established just by the mechanism tests.

The earlier replay-r2 evidence remains:
both replay processes are CLOSED. Original run 045659555Z-89024 and low run
050359219Z-91880 use the same replay-r2 runtime. Old Xbox360 replay matches all
4533 iterations through the native end request. New Arcade HD replay cold-loads
and naturally returns, with matching commands/state/audio through iteration 5504,
BUT 141 slowdown-depth differences (131 versus 102 depth-2 iterations). Do NOT
accept low, change defaults or ignore depth to make the comparison pass. Exact
reports: .local/analysis/sdoj-replay-r2-{pair-audit,3v-1v-module1-through-end}.json.
The normal core's 88123B98 slowdown formula reads render-derived globals
888791E0/E4 at 88123E3C/40; renderer +544/+548 are copied there at 880315C0/C4.
Earlier publication changes their logical age, so balanced module 1 also
needs checking. Default-off SDOJ_METRICS records real parsed batches and the
formula operands; production preservation now lives separately in RenderMetrics.
Keep the original slowdown formula. Do not revive the rejected fixed N-2
selection or turn a missing parsed batch into guessed zero statistics.
Old/new native audio request and dispatch sequences match (141/31 each).
Post-end housekeeping iteration counts differ by one; native terminal state and
request appear on exactly the same logical iteration in both paths.
Taskmgr PID 63148 is still open/elevated. User-authorized caption close, Alt+F4
and minimize also had no effect. Do not repeat these unchanged or bypass OS
permissions. Normal keyboard/focus acceptance remains separate and pending.
.local/sdoj-frame-delay-smoke-r1 was prepared from replay-r2, NOT launched;
defer no-trace pacing until the slowdown discrepancy is addressed. Its test pad
is "0 idle". No game process should remain; revalidate before any new launch.

Earlier implementation evidence follows; pending replay statements below are
superseded by the explicit results and new slowdown issue above.

Frame Delay WIP lives in games/sdoj/latency. The development host accepts 3/2/1
and defaults to 3. Low's native prefix/early driver/deadline wait/drain/latch are
implemented. The latest resource-yield fix passes targeted 1V gameplay in both
modules; full input/replay/pacing acceptance remains pending.

Latest targeted regression supersedes the older CU-blocked-only status below:
the r3 same-binary 1V/2V comparison isolates a real 1V resource-load deadlock.
The worker can yield in the middle of an update (normal return 8806F384,
arrange 8806FDBC); suppressing native ticks until a completed frame deadlocks
that retry loop. IntermediateWait now releases the wait/drain without marking
partial commands complete, preserves late receipts and resumes native ticks.
R4 run-20261021T034707421Z-96188 in .local/sdoj-frame-delay-debug-r4 passes
Xbox360 and Arcade HD selection, Stage 1, logical-pad movement/shooting, pause
and return, plus module 2->1 switching. Both remain effective_policy=1 with
zero timeouts/ownership skips; module 1 also records native slowdown. Its
accepted module-2 output prefix has 5526 first results, median/p95/max
16.845/17.302/18.249 ms; the fine trace cap excludes later full-chain coverage.
The handoff model now passes 821 checks; three source-contract tests pass.
Physical keyboard/gamepad input is NOT covered: CU window activation fails,
with a high-integrity Taskmgr foreground and medium-integrity game/helper.
User-authorized Taskmgr close returned access denied; do not retry unchanged
or bypass permissions. Explicit offline-only sdoj_test_pad_file uses the real
game InputSystem without desktop injection; see FOR_AGENT. R3/r4 diagnostic
quit used UI-loop exit and crashed during teardown even at 2V; it now requests
the normal game/window close path. The r5 startup/quit-only check
run-20261021T040015665Z-79596 exits with code 0 and no crash artifacts; do not
call the older r3/r4 runs clean-exit evidence.
Old launcher windows and r3/r4 evidence must remain intact. No default change,
packaging, commit or push. Continue with the small outstanding acceptance set,
not another full-mode campaign.

Two module-specific final TOML includes explicitly compose the existing probes.
SDK midasm context/memory arguments carry the actual PPCContext/base; optional
ReXModule_OnLoad/OnUnload callbacks distinguish guest reload from retained PE
mappings. Full native 88033070 runs outside the state mutex with saved context,
stack linkage space and host FPSCR; a receipt skips only its matching late BL.
Pause matching uses native wake count/serial, NOT host engine-frame equality.
R1 crashed on that old equality; r2 fixes it and passes actual replay pause/
resume, natural return, Arcade HD player entry/new replay save and 2->1->2 module
reload (module 2 generation=2). Those r2 games are closed. Current root launcher
cannot activate via CU and remains open; do not kill it or the old v0.3 launcher.
The latest batch passed 423 source tests (one skip), nine CTests; the expanded
handoff model now passes 784 checks. R2 first
result median/p95 is 33.134/33.584 ms (4536 results); four >40 ms outliers still
need original-path comparison. Trace hit its 131072 cap, so later module 1 only
has normal summaries/visual evidence, not a full output chain. First-result
stats separate held-picture repeats. Do not mark balanced accepted/default yet.
New native REPLAY_XB02.DAT is in .local/sdoj-frame-delay-r2's independent profile;
old 9786 replay and that new replay still need logical-frame comparison and new
cold playback. The r2 run.json binds runtime hashes; scope is in TEST_MATRIX.
After r2, host Stop additionally calls SdojFrameDelayStop on retained modules,
retiring new publications without discarding successful late receipts. This
stop bridge has mechanism/compile evidence, not a new gameplay run yet.

Continue low's actual checks, not another large 2V mode campaign. Native
88044960's input prefix ends at
880451D4, before 880386F0 parsing; its normal epilogue is 88045DC0, arrange
88045D70. The implementation reuses the original prefix once early and skips it
once late, avoiding a second production action mapper. Live-through constants
differ (normal zero register r29, arrange r28); prove liveness/stack/FPSCR and
input equivalence, don't blindly jump across the prologue. Main early site
8211F0DC is in 8211EEB0, controller ctx.r31+152 selects arrange when 2. The
native callback/wake driver 88030D90 increments at 88030DEC and calls peripheral
88033E88 at 88030DF0; preserve that peripheral work at its original late point.
Low retains a parked token after publication and per-step late receipts. A due
wake requires a successfully published old result and valid next writer; middle
slowdown ticks retain the native countdown. Low summaries separate effective
policy, completed requests, current-result publications and fallback reasons.
Test-SdojPrefix.ps1 passes 1292 synthetic native-prefix cases per module, including
all action IDs, mappings, thresholds, two players, join effects and the actual
production prefix restore. Derived code stays local; the test has no devices,
gameplay/GPU/audio loop or real replay source. Do not call this 1V acceptance.
The local liveness audit confirms the five nonvolatile live-ins and no prefix
stack locals used by the parser tail. Actual input/replay/FPSCR/output/pacing
checks remain. The low-r1 test game (run-20261021T030408351Z-75816) was launched
but CU activation failed twice; no inputs were sent. Revalidate its process and
window, do not launch another instance just because activation failed. This
test copy predates the latest due-wake gate and counters. Old launchers stay open.
The next continuation confirmed PID 75816 alive/responding; even a click in the
visible game region failed activation. Do not repeatedly retry unchanged CU.
Default-off sdoj_logic_trace now records true parked-frame inputs/command SHA-256/
depth independently of the fine trace cap. sdoj_logic_compare.py requires explicit
contiguous windows and rejects missing/duplicate/invalid rows, without alignment
or normalization. The native observer and Python comparator have authored tests;
no actual replay comparison exists yet. Test-SdojPrefix now uses production
GuestCall; both modules pass 1292 cases / 338282 field/getter/ABI checks including
complete context and host FPSCR restoration on return and exception. Preserve
these results; actual 1V output, replay equivalence and no-trace pacing are still
the completion gates. Do not turn this into an endless offline-only campaign.

The current task implements the four v0.4 launcher/configuration requirements:
text-only CN / EN / JP switching, user-data/config.ini with byte-preserving
legacy migration, a new SDOJ banner and bottom-row utility actions. Validate
the actual launcher and both backend configuration consumers, then stop before
release packaging. Frame Delay remains design-only. Older documentation-only
or v0.3 release instructions below do not expand this task.

The SDOJ work remains documentation only: name it "SDOJ Frame Delay
improvement". docs/SDOJ_LATENCY_PLAN.md defines 3V original, 2V balanced first,
and 1V low using Futari-style complete-result handoff; 0V is out of scope.
These are software input-to-Present targets, not physical input-to-photon or
DXGI sync intervals. Both improvements retain the final native wait.
Proposed player config is [game.sdoj] frame_delay = 3 / 2 / 1 (one integer);
it replaces the unimplemented latency_mode proposal, not an existing setting.
No Frame Delay runtime/config-template implementation is requested yet. Accept 2V/1V only
with preserved logic counts, slowdown, replay/save compatibility, complete
graphics ownership, audio/visual continuity and frame pacing. Use the small
mechanism-based regression set, with actual fallback policy visible in logs.

Futari/SDOJ input-latency comparison is now documented in docs/ANALYSIS.md.
Static checks of Ver1.5 Xbox360 (reco_abnormal) and Black Label Xbox360
(kiniro_abnormal) show native clear-ready / wake-core / wait-ready / draw order.
After waking, the core calls 821C5618 -> 821915C0 -> 821905A0 -> XInput to
refresh input before logic; completed draw data is prepared before signaling
ready. This differs from SDOJ's proven late publication/read pipeline. Native
FrameDelay really maps 0V/1V/2V/3V choices to frame-tail policy; mode 1 waits
before swap, mode 0 after. Native new/reset configuration writes mode 1 via
82185E80 (config+12); current player's setting and own Futari causal
input->Present latency have NOT been measured. Do not turn static one-period
eligibility or old 17.477 ms pacing maximum into a one-frame latency result.
54 static sites, raw export/dispatch pointers and original input hashes pass
in .local/analysis/futari-latency-static-r1.json. No gameplay/config/behavior
changes, game rebuild, commit or push were made for this comparison. Other
six cores and physical device-to-photon latency are not covered. Preserve
native slowdown; prioritize setting-aware tracing/late sampling over blindly
transplanting SDOJ's early-publication patch.

The Frame Delay design evaluates two SDOJ latency options, not implementation
yet. Read docs/SDOJ_LATENCY_PLAN.md: first an
independently shippable balanced two-frame publication-only path, then a low
one-frame path with current-input conversion and early worker handoff. The
design retains the original final native VBlank wait and authored slowdown.
The new read-only audit finds all 2216 accepted depth-1 baseline chains have
completed commands before the proposed earlier publication/read, with at least
13.524 ms of observed slack. This is eligibility, not a measured patch gain.
Crucial static findings: active_writer == 0xFFFFFFFF is set BEFORE writing and
is not a completion fence; BuildInputMasks reads cached renderer input ports,
so simply moving that call earlier can reintroduce stale input. Add a genuine
completion/park acknowledgement after the native wait baseline is captured.
Full swap metadata, one-time late-call receipts, drain-on-timeout and module
lifetimes are required. No behavioral hooks/config/build/gameplay were changed
for this design. Local evidence is sdoj-latency-design-audit-r2.json. The four
recorded v0.4 UI/config requirements are implemented and accepted in the current
worktree; production message-handler and cold-readback checks pass as described
in TEST_MATRIX. Frame Delay remains unimplemented.

The SDOJ input-to-display investigation now has a causal software result. Read
"原生渲染队列与最终输出：软件链路结论" in docs/ANALYSIS.md before further work.
Actual r7 Xbox360/Shot Stage1 training has live Right AND Left, changed first
command batches, exact native kind-0 draw-list and kind-1 frame-end FIFO messages,
physical XE_SWAP packet provenance and same-mailbox successful Present return.
2216 complete local chains cross three guest epochs: copy->renderer source about
32.982 ms, source->Present return about 16.886 ms, copy->Present median 49.918 ms.
The original native callback wait accounts for median 15.618 ms of the last
stage; submit->Present CPU return is median 0.325 ms, not GPU completion/scanout.
Live keys->Present are 54.964/58.383 ms; candidate 0221 changes 82->85/85->82
already in those first input-consuming batches. This is software latency, not
physical device-to-photon measurement or an implemented lag fix.
The collection still has the original late publication/read ordering; fresh
same-epoch masks did not eliminate it. Prioritize a coherent early wake/build/
publish/read design, preserving cadence, slowdown, replay and module lifetimes.
Do not claim re-sampling alone saves a frame or simply remove the native wait.
User requested analysis, not behavioral patch implementation. All new probes
remain init-only/default-off and read-only. No mode/network/release campaign,
uncapped FPS, clock/power/priority changes, source commit or push is needed.
R6 PID 92744 and r7 PID 88476 closed normally via Alt+F4; no game process remains.
R4's exhausted diagnostic frame tag and r5/r6 incomplete dispatch associations
are rejected history, not full-chain results. SDK output tags now use a dedicated
GPU-worker serial, never the shared VBlank counter. Preserve the accepted data.
68 SDOJ source tests and native input/render CTest pass; 15 host binary/PDB pairs
and 12 staged runtime/symbol file hashes were verified for r7.
Evidence: .local/analysis/sdoj-latency-conclusion-r1.json, r7-output-r1.json,
r7-content-r1.json and sdoj-latency-status.json. Physical scanout/device latency
and measured patch gains are separate follow-up work, not part of these numbers.

The user has recorded four v0.4 requirements in docs/IMPLEMENTATION_PLAN.md:
text-only CN / EN / JP selection with active highlighting (no button styling,
no new languages), shared user-data/config.ini with legacy-root migration,
replacement SDOJ banner art matching the other games, and low-frequency config/
log actions in the bottom utility row with game-data/refresh. This implementation
is now built and accepted. Source/native, real migration and visual checks pass.
Desktop computer-use could not activate the window; scripts/Test-Launcher.ps1
instead passes 95 in-process checks through the production window handlers,
including three independent cold-start readbacks, unsaved form preservation,
bottom-row bounds, correct shell-open targets and embedded artwork bytes.
Only external shell/dialog effects are intercepted in that test. It uses hidden
fixture windows, not desktop input or game execution. Do not ask the human for
more manual testing of this completed batch. The new root launcher is open;
an older v0.3 acceptance-copy launcher was already open and must be left alone.
Do not produce release archives or implement Frame Delay in this task.

v0.3 is now frozen at tag v0.3 / b8807ca623f0d72bb0f7707e62a7e700c0d09ec8.
Its actual Internal/Public/source ZIPs passed their one extraction acceptance,
including 25 SDK and 19 PS4 source dependencies. The final Public package also
passed normal SDOJ GUI startup, fresh native save creation and return to the
launcher with only that game's data. The actual Internal launcher screenshot
is .local/v03-delivery/launcher-v0.3.jpg. Reuse these records; do not rebuild or
re-extract the archives for the following documentation/tooling commit.
OSS upload and public verification are complete (7,129,056,312 bytes, c66a5f53;
public checksum and a bounded ZIP download verified). Do not upload it again.
The user took over QQ posting; verified links, release text and the actual
launcher image were handed off. Do not retry QQ delivery or claim group sends
were observed. No game is running.

Release communication should describe player-visible additions, improvements
and usage. Include only concrete known issues that affect use and their remedy;
keep coverage caveats and detailed evidence in development documents instead
of repeating defensive language in announcements.

The user requires a lean release workflow. Certify shared components once,
assemble each final flavor once, and perform one fresh extraction acceptance
per final ZIP. Keep successful acceptance by archive SHA-256 and reuse it while
that archive is unchanged. Compare shared runtime hashes across flavors instead
of rerunning the same games in each. Revalidate only changed/failed artifacts.
Uploading a frozen accepted ZIP does not require downloading or extracting it
again; verify local identity, remote size/metadata and the public checksum.
Post-release documentation or delivery records do not trigger package rebuilds.

The v0.3 SDOJ latency research now has a bounded result, not a claimed fix:
poll/copy, original render/wait observations, upstream code comparison and version
mapping are documented. Both upstream latency patches remain unimplemented;
same-epoch masks do not prove a deliberate collection fix. Further behavioral
early-render/resampling work is follow-up optimization, not to be silently
invented as an already-done v0.3 feature. The player README states this limit.
PS4's current root-cause fix and representative replay acceptance are now also
reconciled with the plan checkboxes; older candidate paragraphs are history.
Continue final same-commit five-game recertification, actual Internal/Public and
no-ROM source ZIPs, extraction acceptance, tag/OSS and both exact QQ notices.
Do not add another DaiOuJou mode or SDOJ audio campaign before delivery.

SDOJ now also has default-off --sdoj_render_trace=true read-only module probes.
The closed .local/sdoj-render-trace-r1 run reached Xbox360/Shot training Stage1;
Right was observed with a live craft before/after, Left's post-action was Continue.
381 source / nine deduplicated CTests and 27 input/render subchecks pass.
460 render observations include 114 paired depth-1 waits, all one guest epoch.
The live Right edge took 4900 us to raw copy, 360 us to core copy, then 16515 us
to the next observed buffer publication. That is NOT buffer lineage or scanout.
Do not claim either upstream lag patch is implemented or that fresh same-epoch
masks prove no input lag / a deliberate collection fix. Original late rendering
is unchanged. DXGI returns S_OK with all-zero display feedback here: usable
samples are zero, not a 10,000-frame queue. Use the valid-display-r2 report only.
See the render-order section in docs/ANALYSIS.md before acting on this data.

The unified five-game candidate built successfully at 0dcefea: 53 Xbox 360,
six PS4 and two launcher PE/PDB pairs; all three receipts verified. The 24
deduplicated Xbox 360/common CTests pass. Actual normal-contract offline windows
in .local/v03-five-game-check-r1 reached Futari and G2 title screens with the
official Japanese captions; both processes and launch wrappers are closed.
This closes the outstanding window-title observation, not gameplay retesting
or package acceptance. This documentation commit still needs final same-commit
recertification before packaging. Do not restart DaiOuJou's accepted mode tests.

The user has accepted DaiOuJou as sufficient for this release ("大往生可以了").
Stop adding mode/death/respawn tests. Preserve the actual coverage below;
unobserved details are deferred, not newly passed. The just-started native-hold-r4
was closed without adding gameplay coverage. Continue the remaining collection
checks and release gates, not another DaiOuJou mode campaign.

After the music-patch analysis the user considers that the SDOJ sound may already
be normal. Keep the small-enemy sound as unconfirmed feedback, make no speculative
audio changes and do not expand that investigation for v0.3. This is not proof of
normal sound or a claimed fix. The music patch remains uninstalled/deferred.

SDOJ now has init-only, default-off --sdoj_audio_trace=true probes for module
sound requests and the actual 44-byte engine push/pop/native-play path. The
offline 9786-point Xbox360 replay returned naturally; both diagnostic processes
are closed. 380 source / nine deduplicated CTests pass (12 audio subchecks).
Slot-aware analysis invalidates pending observations at a proven-empty queue:
use sdoj-audio-engine-r2-slot-r3.json, not the earlier false seconds-long pairing.
Channel 4 cues have 103–122 ms queue ages; exact small-enemy death is unconfirmed.
The user's music-patch analysis request is now scoped in: read-only inspection
found a newer main XEX and raw-file loop-length changes, not replacement SDSE
resources. No patch was installed, no normal audio/queue/input behavior changed.
See docs/ANALYSIS.md before continuing. Do not call structural matches semantic
equivalence or attribute TU/custom differences without a clean TU reference.

SDOJ's opt-in read-only input observer now includes both disc-base gameplay
modules. Actual Xbox360/Shot training and Arcade HD training launches show
fresh direction masks built and copied in the same guest VBlank epoch (10–23 us
for the four captured direction edges), not evidence of a stale one-frame mask.
See docs/ANALYSIS.md for sites, identities, aggregate/menu limits and pairing.
Do not blindly transplant TU re-sampling patches. Later logic/render/scanout and
the missing small-enemy sound cue remain open; this is not a latency fix.
378 source / eight deduplicated native CTests (20 input subcases) pass. The
diagnostic process is closed. All five Japanese game window titles now have
actual observations, including the normal-contract Futari/G2 candidate check.

All visible DaiOuJou principal game entries now have representative player
segments. New EX, Black Label, extra III, Challenge and Osarai entries/returns
and CUSTOM cold settings readback are recorded in docs/TEST_MATRIX.md. Smaller
death/respawn or applicable miss gaps remain as deferred coverage after acceptance.
The opt-in native --enter diagnostic supports Shift+H bounded Circle hold;
ordinary player input stays unchanged. Do not redo accepted online baselines.

Current performance batch has fixed the hard host-affinity restriction rather
than reducing the game's six task workers. Read the current-result section in
docs/PS4_SYNCHRONIZATION.md first. Same-binary pinned/inherited/inherited/pinned
offline controls all pass correctness; pinned cadenced maxima are 26–37 ms,
inherited maxima 67–86 us. Normal guest threads now retain their inherited host
eligible set; guest CPU masks remain validated/snapshotted metadata. No priority,
power, task-count, credit, FIFO or completion-barrier changes. Pinned mapping is
only an explicit offline diagnostic, not a production default to restore.

375 source and 73 native checks pass. Actual Ketsui r10 and DaiOuJou r7 fixed
replays return naturally: 1860/1800 continuous gameplay frames, maxima
17.185/17.188 ms, outside-submission maxima 2.400/2.842 ms. Full-replay windows
estimated from screenshots have no >20 ms interval, but are not exact guest-frame
traces. This is a bounded, controlled root-cause fix and representative regression,
not an all-hardware/mode guarantee or v0.3 completion. Proceed with remaining
DaiOuJou main modes/CUSTOM, SDOJ audio/input investigations and 360 titles, then
the same-commit five-game/package/delivery gates. Reuse accepted network results.
The older unresolved-performance paragraphs below describe prior candidates;
do not let them restart blind primitive swaps or discard the remaining scope.

Current human instruction additionally requires a full review of wakeup,
synchronization, waiting and locking, not repeated primitive substitutions.
Read docs/PS4_SYNCHRONIZATION.md for the audited lock/lifetime chain and exact
waiter/notifier timestamp meanings. Ketsui r9 still has a 33.508 ms frame with
a 15.987 ms batched completion wait. The event transport did not reliably cure
tails and is diagnostic-only; normal default remains address. New offline
waiter-side timestamps put a 6.115 ms tail before WaitOnAddress returns, with
3 us gate acquisition and less than 1 us return-to-resume. Do not call this
pure OS Ready time without scheduler evidence. Source changes to GPU resource
reads/callback prefix have bounded gains but do not close the performance gate.
No mode or release work until this priority is handled; retain all v0.3 scope.

Current human priority: resolve the performance issue before resuming mode
coverage or packaging. Do not treat a short smooth replay as closure or switch
back to modes while the known tail remains unexplained. df79594's mode run is
stopped: SUPER EASY player entry/shooting/death/respawn/return passed; LUNA TOUR
Casual/Shotia/Type-A cleared its first area and returned to the tour board,
without death coverage. Other main modes/CUSTOM remain pending, not cancelled.

The next software candidate uses per-waiter WaitOnAddress with atomic result
publication: success no longer reacquires the shared semaphore gate. Original
CV remains available only for diagnostic A/B. Both transports preserve queue,
credits, timeout/cancel/delete and notifier lifetime; 52 semaphore subcases,
73 native checks and 373 source tests pass. Same-binary A/B/B/A offline tests
show CV wake-return-to-resume tails of 22.392/18.177 ms; notifications themselves
finished in microseconds. Address runs' maximum batch times are 2.589/4.433 ms.
Normal DaiOuJou r5 fixed replay returns naturally: 1,800 continuous frames,
max 17.325 ms, p99 histogram upper bound 17.2 ms. It still has a worker taking
3.805 ms after wake returns, now without shared-gate reacquisition. Therefore
remaining host wake/scheduling and ~4 ms draw/sync processing need investigation;
performance gates remain open. Identity/data are in ANALYSIS.md. Do not lose
the full v0.3 scope, but do not resume modes or release steps before this work.

The user has authorized stepwise v0.3 implementation with minimal interaction.
Follow the v0.3 section in docs/IMPLEMENTATION_PLAN.md: playable DaiOuJou plus
its native online feature adaptation, localization/bilingual titles, the open
SDOJ audio/input-latency and Ketsui replay-hitch investigations, then verified
release packages, OSS upload and the two named QQ notifications. The earlier
request to only record the SDOJ latency issue is superseded by this implementation
goal. Do not equate launcher work or input inspection with v0.3 completion.
The DaiOuJou ZIP has now completed; all 61 members passed CRC/SHA-256 inspection.
The complete local input is .local/inputs/daioujou-20261020-r2; r1 remains a
failed/incomplete extraction. games/daioujou/data.json inventories 60 runtime
files, excluding the packaging Project.gp4. Six ELF programs are prepared by the
shared runtime. The direct native entry has actual ARCADE/Type-A/Shotia first-stage,
shooting, death/respawn and pause evidence, plus settings save/cold readback.
Normal collection launch and this title's community adaptation now have a
representative ce006b1 acceptance: GUI selection, ARCADE/Type-A/Shotia player
entry, a real 1040-point round, formal HTTPS score/replay upload and in-game board
read/download/save, then networking-disabled cold playback to natural return.
The formal service is 171d8a5; do not repeat this chain as a new prerequisite.
Other modes/devices and final five-game packages remain separate scope.
The user now requires DaiOuJou and Ketsui frame-pacing/performance fixes in v0.3,
not a later release. Investigate the remaining ~19 ms intervals and tens-of-ms
replay hitches after the accepted DaiOuJou integration. Separate active CPU/GPU
work from pacing/presentation waits and scheduling; report tail distribution,
missed display opportunities and performance headroom using fixed replays.
Do not accept average 60 FPS or a short zero->20-ms sample as resolution, nor
promise every Windows frame is exactly 16.667 ms. Retain correct game speed,
native slowdown and synchronization. No v0.3 packaging/upload acceptance until
the causes, fixes and bounded representative validation are recorded; real
lower-spec coverage and any controlled load proxy must be distinguished.
The user has stopped the uncapped-FPS experiment: do not resume it. All three
measured stages need optimization investigation in v0.3: ~3.6 ms command
processing (including conservative GPU drains), the slow frame's ~3.89 ms final
output, and ~1.58 ms between submissions (guest logic, host services, scheduling
and previous diagnostics). Do not dismiss the last interval as miscellaneous.
Measure costs, implement safe high-value reductions and replay-check each path;
do not claim these wall-time stages are pure CPU/GPU work. Normal ~10 ms pacing
wait is intentional, not evidence that the three paths need no improvement.
Keep failed attempts as incomplete evidence. Preserve frozen v0.1/v0.2 archives.

The user now requests a small offline benchmark before further iterative
optimization. Use scripts/ps4/run_offline_bench.py to screen changes without
game entry or windows, then group promising software changes into short actual
replay regressions. Keep the six authored cases, warmup, repeated exact
percentiles, pixel/task checks and paired build/source identities. Continuous
and 60 Hz-cadenced handoff results are separate; neither is game FPS or scanout.
The physical-cores-first hard affinity experiment regressed actual Ketsui
playback and was withdrawn; the production mapping remains unchanged. The
software-only replay improved output pacing, but DaiOuJou still has two ~33 ms
frames dominated by 13–15 ms task completion waits. This is not a closed gate.
Investigate software paths first, batch analysis/changes per costly game test,
and do not sacrifice synchronization, FIFO/timeout/delete semantics or
notification lifetime merely to lower benchmark numbers.

The latest semaphore software batch caches immutable thread priority in TLS,
uses a two-entry weak handle cache and notifies outside the semaphore gate with
an owning notification lease. Unique-owner waiter reuse avoids reusing a CV
still owned by an earlier notifier. All 24 semaphore and 18 thread subcases,
73 native checks and 372 source tests pass. Two offline repetitions improve
common handoff costs but still contain 3–7.5 ms tails. Actual DaiOuJou r4 plays
the same replay to natural return: 1,800 continuous frames in 30 seconds,
maximum 17.311 ms, p99 histogram upper bound 17.2 ms, outside submissions mean
1.570 ms/max 2.828 ms. This is bounded improvement evidence, not proof that the
old r3 13–15 ms waits cannot recur. Full performance gates remain open; next
cover the remaining main modes (including CUSTOM) and SDOJ investigations.

The final batch additionally pairs a notifier release with acquire before TLS
node reuse; use_count alone is not a publication barrier. Source 372/native 73
and all six offline cases pass again. Offline semaphore-software-r3 has 17 us
burst medians but cadenced p95 168/305 us, maxima 8.837/9.191 ms: tail gains did
not persist. Do not call this a long-frame fix. The actual r4 replay below is
the pre-publication-hardening candidate, not fresh gameplay of the final binary;
combine its pending gameplay check with main-mode coverage instead of opening
the game for every tiny change. Final host/GPU identities are in ANALYSIS.md.

Read README.md and docs/IMPLEMENTATION_PLAN.md before changes. The user has
approved implementation: P0 snapshots, P1 G2 playable baseline, then collection
integration, Futari, Ketsui and release. Do not substitute launcher work for the
unfinished G2 baseline. All stages and verification gates remain in scope.
P1 is now accepted for migration: the human completed the same-process repeated
entry/return test and accepts mode-setting coverage without additional testing.
P2's native common library, fixed catalogue, read-only inspection, pinned tools
and path/configuration tests now pass. P3 has integrated G2 and P4 has established
the shared 360 platform; do not repeat accepted P1 checks or ask for more scores.
Read FOR_AGENT.md for current commands.
The G2 native community score/replay loop has passed against both local and
deployed HTTPS services, including download and cold-restart playback. Physical
input and extended coverage remain later work; do not repeat the online baseline.
Stable G2 frame pacing remains a playable-baseline gate; high host FPS is not
proof of smooth gameplay. Keep guest cadence separate from host presentation.
The user asks for proportionate optimization, not indefinite tuning of isolated
millisecond peaks. G2 community/Live's native board and score/replay baseline
has passed; do not restart it as a prerequisite to migration. Continue P3's
independent runtime build, shared configuration and launcher. G2 writes scores
before replays, unlike Futari. Persistent
offline recovery is a PC enhancement to add after the native basic flow, not a
confirmed original G2 feature or a prerequisite for basic Live.

G2 baseline work was completed in the original ESPGaluda2-PC checkout. Further
collection work belongs here; preserve the original G2, Futari and Ketsui
checkouts, game data and profiles. Use collection test profiles.
The current development profiles are test data. Ending, restarting and resetting
these test sessions and accepting their in-game warnings are user-authorized;
do not interrupt the user for each such operation.

Keep ROMs, generated game program bodies, prepared images, installed tools,
logs, dumps and credentials out of Git. SDK fixes belong in maintained source,
not generated C++. Record source origins and preserve licenses. Build with two
jobs at low priority by default; builds must not automatically launch a game.

The first completed collection release is v0.1, after integration and release
validation, not the current unfinished G2 baseline. Provide two explicit package
flows from the same verified build: Internal includes launcher, runtimes, all
integrated games' complete local data (including supported DLC/patches), symbols
and a self-contained source/.git with build and agent documentation; Public
includes runnable programs, default INI, player documentation and licenses, but
no ROM/data, source, Git history or local diagnostic artifacts. Public packaging
uses an allowlist and rejects game data/source even when present on the build
machine. Both archives exclude credentials and existing player/test profiles.
The v0.1 playable set is G2, Futari and Ketsui; Coming Soon games do not block it.
Do not publish or upload an internal archive as part of building either flavor.
After v0.1 packaging and acceptance, the user explicitly requests a separate
delivery step: upload the verified Internal full ZIP to the QQ group files of
the exact group 人均233.3, then send a group announcement. The user authorizes
composing and sending that announcement without message-content review. Verify
the uploaded archive's identity and completed upload before announcing it;
record delivery evidence separately from package acceptance. Explain included
games, extraction/launcher/config use and log feedback, with accurate known
limitations. Do not substitute an OSS/public upload or a different group.
The announcement may include a small set of screenshots selected by the agent:
prefer the final launcher and one or two representative gameplay views. Capture
the actual release, not mockups, and exclude unrelated windows or account data.
Keep delivery pending until both upload and announcement are verified; follow
the applicable computer-use and tool requirements when executing it.

The collection has one config.ini with shared defaults and optional per-game
overrides. Default nickname is Player and online is enabled by default. The
deployed community API base is https://xiaolu.life/cave/api/v1. All three clients
are integrated; formal score/replay flows have actual gameplay evidence.

Before launcher implementation, research launcher UI and prepare a visual
mockup. Self-review, iterate and finalize it without waiting for human review
or confirmation. Use a clean vertical banner list in this display order: Futari,
ESPGaluda II, Ketsui, DaiOuJou, ESP Ra.De. The final two are Coming Soon with
distinct visual treatment. Source suitable title-bearing banner art online and
record its origins; cropping, retouching and tonal treatment are allowed.
The configuration button opens the shared root config.ini in a text editor,
not a new settings GUI. Display order does not change migration order.
Apply collection-wide UX conventions during each migration, including icons,
operation labels, fullscreen shortcuts, focus behavior and launch/exit errors.
Launcher title: CAVE PC Collection. Game title: CAVE PC Collection — <display
name>, using the same manifest display name as the launcher. Keep backend/SDK
build details in diagnostics rather than the normal player-facing title.

Use source tests and real gameplay for their respective claims. Update the
verification matrix with actual supported modes, save readback, input and
audio evidence. Never mark a whole stage passed from a narrower smoke test.
The user now requests a small, representative v0.1 regression set, not a fresh
full acceptance campaign. Reuse reliable existing results. Prioritize real
collection launch, one first-stage entry per game (including a DLC selection),
save/restart readback, the shortest online score/board/replay flow, and package
extraction/use. Do not expand cache/path extremes, fault injection, all-mode
combinations or long endurance tests; run targeted checks only for a real issue.
The prior 60-minute mixed-game campaign is deferred, not a v0.1 gate. Keep
unrepeated coverage explicitly reused or deferred, never relabel it as retested.
The user has deferred further repeated-entry gameplay tests. Keep the existing
Black Label result, mark the other repetitions deferred (not passed), and do not
make them a prerequisite for the current migration work.
P5's native PS4/GPU builds, shared INI, native preparation/cache, runtime staging
and shared launch contract now work; Ketsui is integrated=true. Actual launcher
selection, two child starts, mode menus, fullscreen shortcuts and return to the
launcher passed. Logs include a readable live stdout/stderr, config snapshot,
binary hashes and preparation receipt. Normal cache reuse passed; shorter cache
generation names address the diagnostic-root path-length issue. Direct-backend
ARCADE player entry and formal score/replay upload are existing evidence, not
a new launcher first-stage result. The latest CU millisecond coin taps reached
scePadRead but did not start the arcade core; do not claim player entry from
the demonstration. The later explicit 120 ms diagnostic bridge has now allowed
actual GUI-launcher ARCADE/Tiger player entry, a 380-point round and formal
two-board score/replay upload. A new normal-input process records diagnostic 0,
reads BGM 69% back, displays Player/380 online, downloads its replay and fully
plays the previous process's new local replay to natural return. This does not
claim physical-device or all-mode revalidation. Package acceptance is now complete
at the frozen release commit below. Exit 27 remains incomplete guest cleanup;
the launcher recognizes only the matching process's explicit close receipt.
P4's formal HTTPS score/replay upload has now passed in an actual Black Label
round. Audio response and physical-input extension checks remain open; they
are not implicitly passed by the network result.

The user has now asked to continue the post-v0.1 work. Start compiler unification
and the Futari/Ketsui frame-pacing investigation/optimization, preserving the
released v0.1 archives and original projects. Use one pinned LLVM distribution
with the backends' required distinct driver styles and ABI flags. Rebuild the
SDK, hosts, GPU and game modules, verify symbols and mechanisms, then run a small
representative gameplay comparison before accepting the new toolchain. Do not
replace runtime validation with compiler-version equality. Keep diagnostics and
proportionate tuning. G2's observed ~19.1 ms maximum frame interval is a
scene-specific comparison baseline, not measured input latency or a universal
pass threshold. Keep optimization proportionate when that future work starts.

Post-v0.1 launcher changes now provide a single selected-game launch action and
shared fullscreen/nickname controls. Keep the text-config entry, unknown text,
per-game overrides and on-launch full validation. These newer requirements do
not change the frozen v0.1 packages. UI-only tests are not new gameplay passes.

The user requires playable SDOJ integration for v0.2 completion. Compiler,
pacing and launcher work alone do not complete v0.2. Integrate sdoj (not
daioujou) into the shared build/config/launcher and release flows, verify actual
player entry, mode transitions and save/replay behavior, and perform a small
representative regression of the existing games. Input identification, a
Coming Soon banner or an upstream standalone executable is not integration.
Do not tag or announce v0.2 complete before this gate passes. Preserve v0.1.
SDOJ community/Live is also required for v0.2: adapt the title's native score,
leaderboard and replay flow to its own service namespace at the shared endpoint.
Keep its capability disabled until implemented and verified, rather than borrowing
another game's view IDs. Investigate disc-resident shop/unlockable content and
distinguish it from external DLC; the music patch remains explicitly deferred.
For content unlocked for free through gameplay, preserve the native progression:
do not enable everything by default, grant currency or edit unlock flags. Verify
that the native shop/unlock/save path works instead.

P6 has completed v0.1 package acceptance at f652df0, tagged and pushed as v0.1.
Both final archives were actually extracted and verified; Internal contains all
three games and independent source Git, and Public contains no ROM or source.
The empty-gitlink-directory RC1 defect is fixed and verified in the later ZIPs;
preserve RC1 as failed historical evidence, not a repaired pass. A fresh clone
restored its own pinned tools and 19 PS4 dependencies, built Ketsui and the
launcher from source, staged the runtime and passed common/launch CTests.
There is also a separate no-ROM v0.1 source attachment with all 25 SDK and 19 PS4
dependencies for recipients without private-repository access, supplied on request.
The verified Internal ZIP was uploaded to the requested group files and its
version notice and actual launcher screenshot were sent and observed there.
Delivery evidence remains local. Follow-up documentation commits do not alter
the frozen f652df0 ZIPs or tag. Do not repack v0.1 merely to include these records.
For a new version, commit source first and rebuild all three games at that commit.
Keep real package extraction, recipient source access and gameplay acceptance
separate from fixture tests. Do not upload an unaccepted candidate.

v0.2 is now accepted and frozen at 562e019 (tag v0.2). Its four-game Internal
and Public ZIPs were actually extracted and verified (2738/128 files), and the
Public runtime started with only SDOJ data. The no-ROM source attachment contains
the matching independent Git and all 25 SDK/19 PS4 dependencies; 37084 ZIP files
were hash-verified. Normal SDOJ launch, four main-mode entries, formal personal
score/replay service, offline cold replay, fullscreen and naturally earned shop
purchase/save/cold readback have evidence. Leave Shop and wait for native save
before closing. Reuse accepted old-three short replays; the user explicitly
asked to stop elaborate testing. Team scoring, the music patch, extended coverage,
Ketsui exit cleanup and occasional A/V stutter remain outside this acceptance.
v0.2's verified Internal ZIP and checksum were later uploaded to the existing
OSS bucket, and links, release notes and the real launcher image were delivered
to both 人均233.3 and 🦜鹦鹉牌🍓供应@DJS. Delivery receipts are local; do not resend
v0.2. Preserve frozen ZIP identities;
follow-up documentation does not require repacking or more gameplay tests.
