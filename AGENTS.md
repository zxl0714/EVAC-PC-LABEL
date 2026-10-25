# Development entry

Active workspace: E:/Projects/EVAC-PC-LABEL. Develop/build here. The former
E:/Projects/CAVE-PC-Collection is retained as a recovery/evidence copy, not the
active build root. Older .local evidence referenced below remains there.

## Current objective

### v0.7.3 follow-up — automatic raw DLC import

The user rejected a manual extraction step: players place original DLC files
in the existing locations and start the game. Implement native automatic
extraction to prepared/cache, preserve originals and all v0.7.2 layouts/config.
The frozen 906bf98 program ZIP below predates this requirement. Keep it intact;
do not describe it as supporting automatic raw-container import. Current work
adds bounded native STFS inspection/preparation, bilingual player instructions
and focused raw-input/cache/legacy equivalence checks. No new UI or shell prompt.

### v0.7.3 DLC import and update check — implemented and packaged locally

Release source 906bf98a2087cc607c4d03bfea30558ae020c9aa is committed locally.
All seven components are built at that source; 70 Xbox, 6 PS4 and 2 launcher
binary/PDB pairs verify. Five focused final CTests pass. Program ZIP:
releases/v0.7.3-906bf98/EVAC-PC-LABEL-v0.7.3-win-x64.zip, 106171080 bytes,
SHA256 3ad734833d830e600efaf8203d6ffddc71f750bccc099d3c6bc3166ecae42b3c.
One actual extraction passes: 150 files, 9 preservation sentinels, no ROM/source/
PDB/user config; DLC helpers and JSON license included. File version0.7.3.0.
Receipt .local/release-acceptance-v073/3ad734833d830e60/accepted.json; logs
.local/v073-{build,native,package,accept}-r1.log. Development root launcher
matches the packaged EXE; all owned test windows and build sessions are closed.
No push, tag, upload, GitHub Release or QQ announcement is performed here.
Do not repeat accepted gameplay/build/extraction or overwrite frozen releases.
Documentation below records implementation evidence; delivery notes do not
change the frozen package's source commit.

The user requires fixing the external-player DLC setup gap in v0.7.3.
Only Futari previously required generated .header sidecars as input; the other
six titles do not. The plan in docs/IMPLEMENTATION_PLAN.md covers automatic
metadata, plain extracted payloads/original-package guidance, common nested
layouts, external-root selection and bilingual placement instructions.
Keep the existing ROM/DLC directory layout; do not replace it with named mode
folders or move user inputs. Generated headers/canonical runtime copies may
live in a separate prepared/cache directory, as the user explicitly clarified.
The launcher provides a simple automatic GitHub Release check:
launcher-only background checks, 24-hour successful-result
cache, a release-page link in the notice and independent config switch;
no automatic download/install. Each new release is automatically announced only
once, persisted after the notice is shown. Keep the launcher free of persistent
version/update text and manual-check controls, per the user's UI correction.
Use Open release page / Close, without Later / Skip choices.
Public-facing documentation should describe the feature and its behavior, not
research-process notes. Preserve required third-party licenses and attribution.
Player setup guides should state required files, paths and actions; omit
implementation explanations such as generated .header files and cache mechanics.
Implementation checks pass:
539 common native checks, generated DLC cache preparation/enumeration with
synthetic payloads, 48 update model/cache/lifetime checks, hidden launcher
UI checks (including deferred and once-per-version reminders), 18 catalogue,
12 native CLI, 9 STFS extraction and 46 packaging cases (one existing symlink
skip). The native update fetch made one real read-only GitHub request and
returned v0.7.2. Real local Futari payload hashes verify with no header in the
manifest (four Black Label resources and one Ver.1.01 resource).
Visible update-notice dismissal and cold restart without a repeated notice pass.
The local v9.0 fixture is not a published version; the user rejected its footer
label, so all persistent version/update text and manual UI have been removed.
Actual Futari enumeration exposed native display-name comparisons: metadata now
uses the exact Japanese names, separate from launcher labels. Both DLC options
are enabled. Run 20261025T175705513Z-111648 passed Black Label Arcade first-stage
entry, return to top menu, and Ver.1.01 Arcade first-stage entry in one process.
It closed normally at 02:02:16 on October 26. Five original/fixture DLC payloads
retain their hashes. The fixture added I/J/K/L direction aliases for computer
use only; product bindings are unchanged. Do not repeat these gameplay gates.
The complete English game-data guide and reciprocal links are present; README
download links now use releases/latest. Product checks and local guide links pass.
The final certified build and program package supersede the earlier mechanism
builds. Keep frozen v0.7.2 intact. 300 hidden UI checks pass after footer removal;
the updated visible launcher also has no persistent update text. The isolated
fixture's fake v9.0 cache and temporary direction aliases have been cleared.
Future upload/publication requires the user's requested destination and scope.

### v0.7.2 complete package follow-up — delivered October 25

The user subsequently requested a complete v0.7.2 package. Internal is built
from the frozen f736f94 release source, reusing the certified binaries without
recompilation. Main is restored to the delivery branch. GitHub program assets,
old OSS objects and player installations remain unchanged.
ZIP: releases/v0.7.2-internal-f736f94/EVAC-PC-LABEL-v0.7.2-win-x64-internal.zip,
8230170812 bytes, SHA256 a373eb88972a556edd9e0163699666b43b386979873123ce6fa01dd696891437.
One extraction passes: 697 files, all148 program files match the accepted
v0.7.2 program package, all512 data entries match the accepted v0.7.1 complete
package, and all9 update-preservation sentinels pass. Seven titles, both Futari
DLC and symbols are included; no source/config/saves. Receipt:
.local/release-acceptance-v072/a373eb88972a556e/accepted.json.
Uploaded at the new fixed v0.7.2 OSS key, public-read/no expiry. Upload overlapped
the single extraction. At21:43 Asia/Shanghai, HEAD size/SHA metadata, public
checksum and32-byte Range206 all pass. Receipt:
.local/v072-internal-oss-verified-r1.json; summary v072-internal-delivery-r1.json.
All three owned package/extraction/upload sessions are closed successfully.
Do not repeat compilation, packaging, extraction or gameplay. Keep internal
download links out of the public README and GitHub Release; no group message.

### v0.7.2 GitHub Release — delivered October 25

The user made the repository public themselves and requested the v0.7.2
program-only ZIP on GitHub Releases. Source f736f94a9d590c3064aa67528c13b966ba90d148
and annotated tag v0.7.2 are pushed. Do not change repository visibility,
existing OSS object ACLs or old release links; no group message is requested.
All seven components built incrementally at that source; 68 Xbox, 6 PS4 and
2 launcher PE/PDB pairs verify. Two focused native tests and 47 source/package
cases pass (one existing symlink skip). Program ZIP is complete: 106033741 bytes,
148 files, SHA256 24284fec748fdcddbc3b61a9772a48b6fcdd24ab78c658f693e270fb65be7720.
One actual extraction and nine preservation checks pass; launcher version0.7.2.
Evidence: .local/v072-{build,package,native}-r1.log and
.local/release-acceptance-v072/24284fec748fdcdd/accepted.json. Reuse these results.
Published at https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.7.2
at 21:09 Asia/Shanghai after the user signed in. The ZIP and .sha256 are the
only uploaded assets; GitHub's generated source archives are automatic.
Anonymous API verifies public release/non-prerelease, both uploaded states,
sizes and SHA256 digests matching local files. Both README download links now
use GitHub. Delivery documentation is a separate commit; keep the release
tag/source fixed and do not rebuild, extract or upload again. Evidence:
.local/v072-github-delivery-r1.json and .local/v072-github-release.jpg.
That initial program delivery did not upload OSS, change old objects, produce
an internal package or announce in QQ; the later complete package is above.
Scope: version identity, bilingual player documentation, precise source/build
links and retained third-party notices. Public issues request redacted text,
not whole run folders containing config/dump/original-module snapshots.
Reuse v0.7.1 gameplay acceptance. One incremental build and one actual ZIP
extraction with update-preservation checks suffice; no new gameplay campaign.
The initial request was program-only; the later complete-package request is
recorded above. No source attachment is requested. Original game data,
C:/evac-pc-label and frozen v0.7.1 artifacts stay untouched.

### v0.7.1 player-card replacement — delivered October 25

Source 674002e10a8d1ab9e90bef0bfcecf497c59d3672 is pushed. Four Xbox titles
share PLAYER CARD / CLOSE and opt-in Enter/Escape dismissal; Guide is absent.
SDK and seven-game incremental build passed (3 SDK, 68 host, 6 PS4 and 2
launcher PE/PDB pairs). Five focused native tests and 50 source/package checks
pass, with one existing symlink skip. Actual SDOJ run 113360 opened the card
from Xbox360/ONLINE/ALL/SHOT rankings and passed Enter, mouse CLOSE and Escape,
then normal exit. No scores submitted; do not repeat gameplay campaigns.
Evidence: .local/v071-card-smoke-r1/acceptance.json. Existing auto-sizing clips
the longer title for short nicknames; CLOSE is fully visible. No redesign.

Both replacement ZIPs were assembled once and extracted once. Public has147
files,106028358bytes,SHA982675f979f7c35e; Internal has696files,8230165466bytes,
SHAf95107d09faace79. 44 shared runtime hashes and nine preservation sentinels
match; all512 game-data manifest entries are unchanged. Both fixed v0.7.1 OSS
keys/checksums were overwritten as explicitly requested, public-read/no expiry.
HEAD/SHA metadata/public checksum/32-byte Range checks pass. Internal upload
overlapped extraction; final verification at19:30 Asia/Shanghai. Complete
receipt: .local/v071-card-delivery-r1.json. Do not rebuild/re-extract/re-upload.
Earlier local archives and original v0.7.1 tag remain intact. No QQ message;
C:/evac-pc-label untouched. All owned test/build/package/upload sessions closed.

### October 25 Guide follow-up — cancelled

The user cancelled the common system-pause shortcut after the actual Futari
test kept playing behind PAUSE. SDOJ did enter its own pause menu, but that is
not a four-game pass. Remove the experimental host/config/input/notification
changes; do not resume this investigation or claim F10 support. Keep only the
shared PLAYER CARD / CLOSE labels and opt-in Enter/Escape closing behavior.
The isolated .local/guide-smoke-r1 collection and logs are experimental, not a
release. Frozen v0.7.1 packages/OSS objects and player installs are unchanged.
The owned Futari test PID 107764 closed normally. At that checkpoint runtime/ was
restored from the already-accepted v0.7.1 extraction (hash checked, no repeat
extraction); see .local/guide-rollback-r1.json. That checkpoint's experimental
SDK/hosts have since been replaced by the accepted card-only build above.


### v0.7.1 — implemented, packaged and uploaded

Final seven-game build and both local packages are complete at source
f43852eae98263ab6d5a008e5f16849d952c704b. This supersedes the pending-build
checkpoints below. PS4/host/launcher receipts share that commit; 6/68/2 PE/PDB
pairs verify. Eight focused final CTests pass; reuse accepted UI/gameplay gates.
Each final ZIP was extracted once: Public 147 files / 106028374 bytes /
SHAd4b8233737d92317; Internal 696 files / 8230162441 bytes / SHAa322d74d9488a59a.
All seven data sets, both Futari DLC and symbols are in Internal; neither package
contains source or user configuration. All package paths are ASCII; all seven
placement guides are README.txt. 44 shared runtime hashes and nine preservation
sentinels per package pass. Evidence: .local/v071-investigation/final-package-set-r1.json
and .local/release-acceptance-v071/. No repeat extraction/build is needed.
Implementation and tag v0.7.1 are pushed. The user requested delivery on October
25; both accepted ZIPs and checksum sidecars are now uploaded at fixed v0.7.1/
OSS keys, public-read without expiry. HEAD length/SHA metadata, 32-byte Range GET
and public checksum checks pass; see .local/v071-{public,internal}-oss-verified-r1.json.
No repeat build/extraction or old-object replacement was performed. No group
announcement has been sent. C:/evac-pc-label and frozen releases remain untouched.
All owned test/build/package/upload sessions are closed. Audio behavior was not changed: targeted
mode/event/F8 reproduction remains follow-up work, not a claimed audio fix.

The user approved isolated launcher/Guwange desktop testing on October 25.
Use .local/v071-smoke-r1/collection, not C:/evac-pc-label or an old player copy.
Nickname alignment/save and README.txt placement names are implemented. Scroll
and hover invalidate only the list; WS_CLIPCHILDREN protects native preferences.
282 hidden UI checks pass in launcher-checks-r5, including four DPI scales and
production invalidation requests. r4's separate startup-lock fixture failure is
retained; an unchanged rerun passes. Visible nickname editing/save, card hover
and wheel input were exercised; the tall window fits all seven cards, so no
claim of observed viewport displacement or captured transient flicker.
42 package checks (one existing symlink skip), 12 native CLI and five source-
boundary checks passed earlier. No new release, upload or player-file replacement.

Earlier development checkpoint, superseded above: Guwange host build r1 and 20 PE/PDB pairs
pass. Development runtime/ is staged; the isolated copy verifies all 16 files.
Actual run 103400 (20261024T182017406Z) returned through Game Library, logged
Execution complete -> Window closing -> hard exit within 215ms. Its process is
gone and the launcher showed exited/enabled. Relaunch 58620 reached the title
and closed normally; no dump. See .local/v071-smoke-r1/acceptance.json. Do not
repeat that accepted gate. Common, launch, community_sync and guwange_community
CTests pass. Root launcher, C:/ player files and frozen ZIPs/OSS are unchanged.

SDOJ Team Play dump 104832 reaches fatal after a false-success XHTTP startup
stub and missing titlestorage endpoint. NetDll_XHttpStartup now returns BOOL
false and guest last-error 120 rather than leaving caller=1 as success. The
actual-export/thread ABI regression passes for callers 1 and 2. SDK r2 and SDOJ
host r1 are built (19 PE/PDB pairs). Actual SDOJ 55104, run 20261024T182759514Z,
entered Xbox 360 Team Score Attack, showed its original D-team assignment,
entered Shot-style selection and stage 1, paused at 2558 and returned through
both menus. It closed normally at 02:31:20.839; process gone/no dump. The native
game continues despite XHTTP startup failure; it does NOT just show an error.
No team transport was added or success response fabricated. Do not rerun this
accepted entry gate. See .local/v071-smoke-r1/sdoj-team-acceptance.json.
Five SDOJ/SDK CTests and 59 focused source tests (one existing skip) pass.
All independent test windows/build sessions are now closed. Audio first-pass
investigation below is complete, without a confirmed audio fix. Remaining:
final version/docs/build/package was pending at that checkpoint and is now complete above.

User added suspected SDOJ sound-effect delay to the same investigation. Compare
Futari's scoped 1ms timeout/yield fix, native command/channel queueing and 1V/3V
A/V timing; don't transplant Futari addresses or alter all audio queues. Latest
user run 104832 has 1358 samples per output, queue32–42.7ms, consumption max52.6ms,
zero silence, but no guest probes/F8. Older valid slot-matched engine report has
100–122ms channel4 waits; same cause is NOT established. See ANALYSIS section
v0.7.1 and .local/v071-investigation/sdoj-user-audio-r1.json. Only read-only
audio investigation at that checkpoint; now superseded by the bounded paired
run below. User's play remains undisturbed. Historical audio evidence is in the retained CAVE-PC-Collection
.local tree; don't rerun or edit that tree.

SDOJ audio worker probes are now built/tested, default off under sdoj_audio_trace:
one summary/sec, separate cap180, thread-local stats, no guest writes/scheduling
changes. 21 native and17 generation checks pass. Build sdoj-audio-r1 closed0,
19 PE/PDB pairs; EXE07da5440 / source snapshot b31f4159. Isolated offline runs
83412 and44616 (20261024T183537635Z / T183803576Z) played the SAME Arcade HD
first replay's opening, not a whole replay, then closed normally/no processes.
The chosen replay is1458133783/Stage4, SHA42d2c8cc, not the old short zero-score
fixture. Wait target82197AD0 -> KeDelayExecutionThread -> nonalertable Sleep(1),
not Futari's post-Wait timeout yield. End-loop waits mean1.68/1.70ms,max3.43/3.50;
engine queue max18.66/30.97ms; output32-42.7ms. No seconds-long delay reproduced.
Detailed4096-event budget exhausts in both runs; worker summary continues. Only
62 command-prefix records match exactly, then contention/unpaired differences:
do not claim complete sequence/A-V equality. No audio behavior fix is justified.
Keep mode/event/F8 reproduction open; first investigation task is checked, not
"audio fixed". Reports .local/v071-investigation/audio-{low,original}-r1.json,
audio-compare-r1.json and *-output-r1.json; ANALYSIS/TEST_MATRIX have identities.
Test INI restored1V/offline; C:/player unchanged. Version bumped to0.7.1 for final
certification. 79 focused source checks pass (one existing symlink skip), including
tiny actual v0.7.1 program/internal assembly and extraction; source audit2356files,
22submodules,noROM. Log .local/v071-investigation/source-final-r1.log. This is
not final seven-game build or real package acceptance. Do not rerun accepted UI/gameplay gates.

### v0.7 Guwange — released

v0.7 adds Guwange XBLA (guwange / ぐわんげ): normal, Blue and Arrange,
saves, native community boards/replays and collection delivery. All six steps
in IMPLEMENTATION_PLAN are complete. Online co-op is out of scope.
Final seven-title build is complete at 00450c0e7bfa82e7f1f1243e5e23acf4f1c5011b;
tag v0.7 is pushed. G2, Futari and SDOJ quick checks are accepted by the user.
All 33 native CTests pass (five needed a sandbox temporary-directory permission
retry, without code changes); 557 source checks already passed, one known skip.
Both final ZIPs have one actual extraction acceptance; 44 common runtime files
match and nine preservation sentinels pass. Program ZIP 4db2428a33454630 and
internal ZIP 09b2adffe0d38eef are uploaded/verified at fixed v0.7 OSS keys.
The user-requested upload/extraction overlap passed; both receipts agree on
archive hash and size. The user approved the notice; the complete outgoing
message and both links were verified in QQ 人均233.3 at22:23 on October24.
Receipt: .local/v07-qq-delivery-r1.json. Do not rerun gameplay, rebuild,
re-extract or send the release notice again. Commit documentation separately;
these records do not change the frozen package source/tag 00450c0 / v0.7.
Evidence: .local/v07-final-smoke-r1/acceptance.json, .local/release-acceptance-v07/,
and .local/v07-*-oss-verified-r1.json. The isolated launcher remains user-owned;
do not manipulate it. The user explicitly resumed QQ computer use after Escape.
User feedback: Arrange's default keyboard layout is awkward. Improve it in
follow-up work; the user explicitly said to send this release first.
v0.6.1 is complete; the README revision and release notice were delivered to
QQ 人均233.3 at 15:50 on October 24. Do not repeat old delivery work below.

Latest user scope decision (October 24): cancel long-running gameplay tests.
Do not reinstate the >4.5-minute gate or create a soak/input harness for it.
Reuse the accepted thunk-cache mechanism tests and actual mode switching;
reuse the now-passed short input and HTTPS checks, then finish release delivery.

Production Guwange service installed October 24 20:55 from 2bf184f41fb5:
/opt/cave-community/guwange-current -> releases/guwange-2bf184f41fb5,
PID2695508, loopback18090, shared /cave/api/v1/games/guwange route. Seven
healths/two legacy routes/unknown404 and first empty DB backup passed; six
peer PIDs and pointers unchanged. Receipt: .local/v07-guwange-deploy-r1/installed.json.
Catalogue is enabled; normal launcher/HTTPS acceptance passed in r19 below.
Do not rerun the first-install script or fabricate scores. Final build,
old-title smoke, packages and upload are complete as recorded above.

Latest r19 is CLOSED: normal launcher -> run 20261024T130448514Z-90800,
EXE72f9f1bc161e57b8faa095b7ed1a1034a5c732c9415b74e7d9e6dfec250c8f3c.
Arrange Xbox360 / ScoreAttack / 1P / Shishin naturally ended at 5336. Production
HTTPS views9/10 score, estimate and replay uploads returned200; native board
showed Guwange-QA/5336 and attachment. Download183230bytes and native Arrange
Data2 (GWN_RP_XB01.bin) both match upload SHA
faf6fd90028a8739b90eade2aa23c8401537cb0fe27695a6bdba24e3cd588fdc.
Data1 preserved. Original x1 replay finished naturally and returned to rankings.
Normal close21:14:39, PID90800 gone/no dump; launcher106744 closed normally too.
Evidence: .local/guwange-analysis-r1/production-community-r19.json.
Do not repeat this accepted closure. Root runtimes/frozen releases unchanged.
Seven focused CTests and20 PE/PDB pairs passed before launch. Readme/agent/data
guides and v0.7 version defaults now prepared. Source tests r20:557 tests pass,
one existing symlink-privilege skip; audit2356files/22submodules/no game data.
Final same-commit seven-title build and old-title smoke have now passed.
Do not edit tracked source/docs during a future live build receipt.
Local two-player assessment: SDK only signs in user0; SDL device enumeration or
the native 1P/2P starting-side menu does not establish two-player support.
This is documented as not validated; don't expand v0.7 into co-op work.

Latest r17 is CLOSED: run 20261024T124715195Z-12552 used the same r16 EXE
with bounded input diagnostics. Arrange Xbox 360 / Offline / 1P reached stage 1,
death/continue and pause/return to main menu. W+I delivered RT255/RY32767;
H+W+J delivered DpadRight/RT255/RX-32767 together; W+K+L delivered
RT255/RX23170/RY-23170. Each returned to zero on the following guest frame.
This accepts actual short-key delivery, not sustained physical dual-stick play.
Normal close at 20:52:50, PID12552 gone. No game currently running.

Latest actual r16 is CLOSED: run 20261024T123153031Z-71660, EXE SHA
af0d33df621d60a2eabb328677ab73f60eb3f02a72fc6b4af8c4198896170bd5.
Blue Score Attack / Shishin / stage 1 ended at 2370. Views 5/6 score writes,
rank estimates, replay uploads and storage-size acknowledgements succeeded.
The original online board showed the player and replay attachment. Download
returned 183250 bytes with SHA e678a73525db9aa712c1ea051dc068dcd6420fb6b0d207a8375f1cc6598ffc18;
native Blue Data2 save has the same hash, and Data1 was preserved. Full x1 replay
finished naturally, displayed its completion dialog and returned to rankings.
Normal Alt+F4 closed at 20:41:17; PID71660 is gone, no crash dump. Five focused
CTests passed before launch. This accepts the actual LOOPBACK score/replay
closure, not production HTTPS. No game is currently running. Root player
runtimes are unchanged; latest catalogue development state is described above.
Server session 50339 is the previous loopback handle: revalidate before use.

Earlier actual r15: the approved shell escalation successfully launched the game
normally (no alternative UI launch). Run 20261024T122213083Z-106668 uses r14 EXE
0d5f3ada5afc792a3d30ae3036ce3997a11eb6f0118230abcee33f6787294c4c.
Arcade Score Attack / Shishin ended at 3200; both native /scores requests returned
200 and the UI confirmed success. The earlier timestamp and empty-best fixes
are now actually exercised. The independent local DB has views 1/2 for
Guwange-Local, score 3200, zero replays. Saved Data2 SHA
b1bf308673cfb87385881278c579bfe975a0f25ae8e4cfa7e1e3369735aad49d;
Data1 remains intact. r15 closed normally at 20:28:41.

Replay submission failed BEFORE transport at native EstimateRank -> XLIVEBASE
0005000D. Guwange's static Xbox API hooks were missing, despite the provider
unit tests passing. r16 adds all five reviewed base API entries: 8224FFA0 rank,
8224FD10 upload, 8224FA30 download, 82250180 local path, 82250218 remote path.
They forward the original registers into existing SDK community helpers like
G2, not the original game's score/eligibility/recorder state machines. Ten input
contracts pass; generation/build and actual upload/download subsequently passed
in r16 above. Do not repeat this accepted loopback replay without a related change.
The test server has been restarted with the new validators on the SAME loopback
port 64517, exec session 50339. No production records or service changes.

The supplied Downloads/Guwange (World) (XBLA).zip is unchanged. Its base version
0.0.0.4 is extracted only under .local/guwange-analysis-r1/extracted (29 files,
Title ID 58410A78). TU1 is absent; an optional question asks the user for it.
Do not apply the reference port's TU1 addresses to this base. Source metadata
and declarations under games/guwange are provisional BASE-version development.
All five no-execution censuses pass, including overlay unload/overlap checks.

FunctionDispatcher now caches host/procedure thunks per caller, invalidates
cross-module export slots on provider unload and preserves retired pointers as
invalid. The old-runtime reuse test fails; new procedure_thunks passes repeated
100000 lookups, parallel lookup, target rebind, same-base reload and real memory
slot clearing. SDK built in out/sdk-build and installed with verified symbols.
This is mechanism evidence, not actual Guwange long-run acceptance.

Five-module generation r5 succeeded with no unresolved branch traps. It includes
146 base-version declarations with reviewed vtable/core callback evidence and
input/entry hashes. Generated bodies are never hand patched.
The common generation script now rejects emitted unresolved-branch traps even
when codegen exits zero. Original six player runtimes and frozen releases are
unchanged; only development SDK/out outputs are rebuilt. Guwange's catalogue
integrated and communitySupported flags remain false until real acceptance.
Host r1 completed all 294 tasks; 16 PE/PDB pairs are certified. Only the isolated
.local/guwange-play-r1/collection received those runtime files. Two real boots
(106012 / 86008) initialized D3D12, audio and module registration, but the native
main returned 0 before any menu. Both windows disappeared; their remaining
windowless owned processes were stopped after path checks. They are NOT gameplay
passes. Logs are run-20261024T094047165Z-106012 and
run-20261024T094155415Z-86008 under that isolated collection. The second uses debug
logging. r6/r7 diagnostic generation is complete. Real r3 boot 108320
(run-20261024T095015489Z-108320) proves device allocation/init/reset and engine
initialization all succeed, then main returns 0. Its owned windowless process
is closed. The original cmp/beq keepalive at 821E98A4 is removed by clang -O2:
main-r3-x64.txt shows an unconditional jump to return instead of the back-edge.
Base-bound main-wait.toml now preserves the zero-register lifetime with cooperative
sleep/title-termination checks and leaves nonzero fallthrough untouched. Its
authored optimized mechanism test passes; r8 generation and r4/r5 host builds
are complete (17 PE/PDB pairs). Real r4 boot 107112 stayed alive and closed
normally, but rejected type-0 texture fetches. The existing compatibility flag
is now enabled only by Guwange's host, not globally. Real r5 boot 103748
(run-20261024T100048113Z-103748) displayed title and menus, loaded the normal
module, selected Shishin and entered stage 1. Score growth, death/respawn and
pause were visible; menus still showed the SDK's default XBLA trial edition.
Alt+F4 closed normally at 18:07:13; the process is gone. Do not claim all inputs,
mode-return, save/replay or long gameplay acceptance from this run. Up/Down CU
taps did not move the pause menu; isolated I/J/K/L aliases are prepared for a
controlled comparison. Host r6 sets this title's license_mask=1, preserving
native mode/save gates. Actual r6 run-20261024T100847454Z-48048 displayed the full
menu and created native SYSTEM.DAT (6956 bytes) after its own save prompt.
I/J/K/L menu aliases worked. Settings cold read remains unverified. Entering
Blue unloaded normal, loaded Blue, then failed at unregistered 0x882C95A0.
The process exited; crash files are preserved in that isolated run.
The original hash-verified 76-byte body is a state-writing core leaf. Review
found 3 Blue, 4 Arrange and 10 co-op leaves omitted by the conservative
readonly/same-shape inventory. All 17 now have base-bound declarations; section
hashes, pointer references, independent mtctr/bctr boundaries, internal branches
and final blr pass check_state_leaves_r7.py. The 22 remaining per-mode CRT/SEH
continuations are not independent functions and must not be declared blindly.
Nine input-contract tests pass. Generation r9 and host r7 completed; all 17
declared generated bodies and 17 PE/PDB pairs verify. Actual r7
run-20261024T104156469Z-684 switched normal -> Blue, entered Offline/1P/Shishin
stage 1, scored, died/respawned, continued once and naturally ended at 22280.
Native Data1 replay save succeeded (183290 bytes; signed payload checksum 62350),
then returned to the mode menu. Header matches mode 1 / character 0 / score 22280;
magic padding is nonzero, play kind is 0 (not a format version; corrected in
r14). See blue-replay-actual-r7.json.
Same-process Arrange entered stage 1 with automatic fire, naturally ended at
5170, saved Data1 (183249 bytes; checksum 51932), then returned to mode selection.
See arrange-replay-actual-r7.json. r7 closed normally at 18:57:24; PID684 is gone.
Native Controls confirms dpad/left stick moves the character, right stick moves
Shikigami, RT summons, B bombs. Guwange-scoped keyboard right-stick I/K/J/L is
built in host r8, with 18 PE/PDB pairs and all 9 CTests passing (535 source checks,
one existing privilege skip). Tests cover simultaneous movement/summon/right
stick, opposition, focus clear and one-frame taps. The isolated menu aliases
are now T/G/F/H; do not use I/J/K/L to navigate menus in r8.
Actual r8 run-20261024T105755908Z-107948 cold-read Blue Data1, loaded and played
it at x1 through recorded continue/game over, displayed Replay playback has
finished, then returned to the unchanged list and mode menu. See
blue-replay-playback-r8.json. r8 changed native BGM 80 -> 79, saved and closed
normally at 19:15:03. r9 run-20261024T111525691Z-105172 visibly cold-read
BGM 79 / SE 80 / VOICE 80, then closed normally. See settings-cold-read-r9.json.
No >4.5-minute gameplay or held-input acceptance yet; the r8 W+I tap alone does
not establish independent Shikigami movement.

The base-native community adapter and independent guwange server are implemented
but NOT staged in the running r8. Replay parser passes 67 authored checks and
the two actual r7 recordings. Guwange native HTTP loopback passes 281 checks
across all three modes; 34 Python G2/SDOJ/Guwange server checks pass. Shared
schema callbacks now receive view IDs, preserving G2/SDOJ through wrappers;
both older adapters' unit and loopback CTests were rebuilt and pass. Only test
targets were built in out/xbox360; root player files remain unchanged. Server
attachment validation binds the actual database view to the replay mode.
Guwange reader columns are view1 character/stage=4/5, other views=6/4; only
reviewed columns and FFFA are returned. Native score and replay timestamps
are separately generated and must not be forced equal. No production deploy,
score or replay upload yet. Catalogue remains disabled pending actual acceptance.

Host r9 completed with 20 PE/PDB pairs and all 12 CTests passing, including
542 source checks (one existing privilege skip). Actual r9 correctly kept
community disabled because PrepareRun gates it on the compiled catalogue;
an INI change alone cannot enable development networking. Host r10 adds the
same explicit loopback-only test provider pattern used by SDOJ, with cancel,
stop and profile setup. It builds/certifies in two tasks, 20 pairs, and is staged
only in .local/guwange-play-r1/collection. Its normal capability remains off.
The initial launch with --guwange_community_test_endpoint was rejected by tool
policy; no alternate launch was attempted. The user then manually started r10,
run-20261024T112117715Z-106344, and confirmed it. Actual Arcade Score Attack
ended at score 0 and read views 1/2 successfully from loopback, then crashed.
Dump/PDB identify sub_821D56C0 at original 821D5968, reading guest address 0x10:
the first-row pointer is null when a new player has no score. The process exited.
Guwange's one-row XUID enumerator is a personal-best lookup. The title adapter
now maps only this request shape to the existing explicit-user query, returning
an unranked local zero row instead of no row or another player's top score.
Public and multi-row around-user boards stay unchanged; no score is invented
on the server and original score comparisons are preserved. Authored regression
fails before the fix and passes after it: 380 checks, all three modes, empty
and populated boards, exact user identity, ranked readback and SDK row pointer.
Evidence: community-crash-r10.json and community-unranked-{red,green}-r11.log.
Host r11 built in two tasks, certified 20 PE/PDB pairs, and staged 17 files only
to the independent play root. Five focused Guwange CTests pass. Staged EXE SHA
is 1979d565ecebe381aa3ee8f64c5dd7ec42e248ad0645d89b0943f27ce996df78.
No game is currently running. This candidate still needs actual game regression;
do not claim online acceptance. Manual launch with the same loopback argument
is required; do not retry the previously denied launch through another mechanism.
The independent local server is ready on 127.0.0.1:64517, exec session 50590,
database .local/guwange-community-play-r1/community.sqlite3. No actual score
or replay has been submitted; keep authored fixtures in disposable databases.
Current isolated config requests this loopback endpoint and nickname
Guwange-Local, not production. Root config and six player runtimes are unchanged.
Do not skip original initialization/error branches.

v0.7 data/release scaffolding is now prepared independently of the gameplay
gate. The supported data contract is base 0.0.0.4 without TU1 (29 files).
scripts/Extract-Xbla.ps1 runs on Windows PowerShell 5.1 with no Python/tool
download; bounded LIVE/PIRS single-table extraction checks all paths, chains
and data hashes before publishing a fresh directory. Seven authored test groups
pass, including the level-1 boundary, corrupt/aliased blocks, unsafe paths and
preserved existing/junction destinations. Actual extraction at
.local/guwange-extract-helper-r1 verifies all 29 manifest files / 27516 blocks;
accepted.json binds helper/input/output identities. Reuse this evidence; do
not repeatedly extract the unchanged container. Original ZIP is unchanged.
Seven-game Public/Internal profiles, placement guide and bilingual v07 player
TXT are prepared. Authored package tests pass 41 checks (one existing link
privilege skip), including real tiny ZIP assembly/extraction and Guwange-only
Internal selection. Old v0.4/v0.6 profiles retain six guides. Product/default
release version and published READMEs stay v0.6.1 until final acceptance; no
real v0.7 package was built. Catalogue capability remains disabled. Manual
game relaunch is still pending; continue independent development meanwhile.

Independent r12 work is complete: Guwange deployment/backup/proxy templates and
three-mode snapshot recovery tests pass (8 title, 6 deployment, 5 backup tests).
They are NOT installed on ECS. Source suite passes 553 checks (one privilege
skip); after that, the new relative-output regression and all 8 XBLA-helper
groups pass. The C# extractor is byte-identical to the accepted r1 copy;
only PowerShell output resolution changed. Do not re-extract the real input.
Latest seven-title common/launcher build passes (17 tasks, 2 PE/PDB pairs),
with -SkipLauncherStage: root launcher and player runtimes unchanged. New
common/launch, 12 native CLI, 245 hidden UI and 26 preview checks pass. Tests
now require seven titles, rather than silently accepting the old inspector.
29 manifest files are copied to NEW game-data/guwange and fully verified by
the latest inspector; shared config/originals unchanged, capability still off.
Evidence: .local/guwange-analysis-r1/*-r12.* and guwange-launcher-visual-r12.
Ready states in previews are authored examples, not actual installation or
gameplay acceptance. No Guwange process exists at the latest observation;
the user was given the corrected manual command. No alternative launch tried.

Source review r13 checked the thunk-cache lifetime/invalidation, Guwange-only
personal-best reply, shared G2/SDOJ schema wrappers, right-stick configuration,
base-bound hooks and generation/output gates. No new definite defect was found.
The development checkpoint retains integrated=false/communitySupported=false;
it is not a v0.7 release. r11's exact staged EXE/PDB remain the next gameplay
candidate; do not rebuild/re-extract merely for the checkpoint commit.
Independent preparation is now complete. Remaining gates require actual game
entry/input/result/replay, then independent HTTPS deployment and final release.

Actual r14 used r11's certified EXE in run-20261024T120741766Z-92548.
Arcade Score Attack / 1P / Shishin reached stage 1 and naturally ended at 2780.
The previously crashing new-player personal-best lookup now passes: views 1/2
returned unranked local rows and the native upload prompt appeared. Submission
was rejected locally before HTTP because the original score writer swaps the
two FILETIME words (821BF1EC..821BF1FC). Captured signed value
-5304130541461871696 decodes to 2026-10-24T12:10:51.638Z. Preserve this raw
signed property for transport/readback/RunKey; decode only for validation.
Do not change the SDK time conversion or replay header timestamp byte order.
Native Arcade Data1 saved successfully and returned; window closed normally
at 20:13:07. No score/replay reached the actual local server in this run.

The saved 183260-byte Arcade recording (SHA 04716d48094c21dc4e4a70bed890515464a5e85356835cb3bae72062b39e7890)
also proves header offset 56 is selected play kind, not format version:
ordinary Play=0, Score Attack=1. Builder 821936A8 reads 82689BB8, set from
menu selection at 821C9688. Client/server now accept both recording kinds;
kind 2+ remains outside the supported recording contract. Corrects the early
replay-static-r2 assumption; original records are not rewritten. Focused checks
pass: 82 native replay checks plus the actual recording, 391 community checks
and 10 Guwange server tests. r14 host rebuild/staging and positive-score
upload/download playback are pending. Neither >4.5-minute actual gameplay nor
held-input acceptance has been completed. Do not count menu waiting as play.

Nickname warnings now exclude unintegrated/community-disabled titles. Native r4
passes all seven CTests including the optimized main-wait check; source r4 has
533 checks with one existing privilege skip. The initial common failure is kept.
The original six runtimes and root launcher are not staged. Static community
boundary notes are in community-boundaries-r1.json, not an accepted schema.
The approved six-game development summary was sent to 人均233.3 at 17:32;
group-experience-sent.json records UI readback. Do not repeat that message.
The approved runtime-build follow-up was sent at 17:48 and visually verified;
group-runtime-build-sent.json records it. Do not resend either group message.

### v0.6.1 player README revision — current

The user now asks to apply the group README feedback. Chinese/English READMEs,
the player TXT and GAME_DATA are updated with extraction commands, face-button
mapping, nickname identity and accurate frame-policy wording. A player helper
scripts/Extract-Ps4Pkg.ps1 uses external LibOrbisPkg v0.2 PkgTool to preserve
metadata, included as tools/Extract-Ps4Pkg.ps1 from v0.6.1 onward. Actual ESP
base/update extraction matches all 48 existing manifest files. A common other
extractor differed in two metadata files; do not recommend that uncorrected
output. Evidence is .local/v061-readme-unpack-r1. Original PKGs are unchanged.
Both new local ZIP copies now retain all original compressed runtime/game members;
135 Public / 649 Internal unchanged members match exactly. Five changed/new files
were actually extracted and checked. Final payload counts are 138 / 652.
Both r3 ZIPs and checksums replaced the fixed v0.6.1 OSS links and passed remote
HEAD/range/SHA metadata/checksum verification. Receipts are
.local/v061-{public,internal}-docs-{accepted,oss-verified}-r3.json. Original r2
ZIPs remain recoverable. No game rebuild or full re-extract; no upload remains.
The group release notice has NOT been sent. Its draft now acknowledges the
completed README work, replacing the earlier wording that it was only a TODO.

### v0.6.1 final packages uploaded — current

Both fixes are built from 21477ca06b132a2d940adc0131f876c0bc374e95;
main and annotated v0.6.1-final are pushed. Preserve the older room-only tag.
Final six-game build, both single-extraction acceptances and OSS verification
are complete. No build/upload/test process remains. Do not rebuild or re-extract
for delivery documentation. See TEST_MATRIX for package identities and receipts.
The Public/Internal fixed URLs under v0.6.1/ now contain both crash fixes.
QQ release notice is pending action-time confirmation; it has NOT been sent.

Latest QQ feedback read through 14:32 on October 24: another member confirmed
the G2 crash and suspected all modes; actual new regression covers Xbox 360 and
Arcade, while original-code/ABI checks cover all seven equivalent overlays.
The member also supplied README.md and README.txt, downloaded to Downloads.
They request ISO/PKG extraction tools and clearer player instructions. The later
user request authorizes the completed README revision recorded above; the drafts
were checked against actual code and extraction rather than copied verbatim.

### v0.6.1 expanded to G2 stage-3 crash — current

The user requests BOTH ESP room and G2 stage-3 midboss fixes in v0.6.1.
QQ 人均233.3 member reports Xbox 360 and Arcade. Downloaded
run-20261024T053850744Z-42844.7z: dat12 calls missing 0x880EEA28.
Verified original 12-byte flag-0 forwarder and six equivalent overlays against
input/section hashes. dat163 was already fixed; six new module declarations
and a seven-module generated native ABI fixture are complete. 84 ABI checks and
521 source tests pass (one existing link-privilege skip). See TEST_MATRIX.
G2 actual offline training passes Xbox 360 and Arcade Stage 3 MID BOSS: firing,
parts destruction, score 1260/2520, death and natural return. Run 101948 in
.local/v061-g2-stage3-play-r1 closed normally. Both crash fixes are accepted
within their focused scope; do not repeat campaigns. Final packaging is complete above.
Room-only candidate commit/tag v0.6.1 is 33a8ddda72433053af82107715e86352d3a04c67.
Its public ZIP was uploaded at the fixed v0.6.1 URL but NOT announced. Replace
it after the G2 fix, retaining the old local candidate and tag identity.
Internal room-only candidate finished at releases/20261024T054329Z-dd2df1c5,
SHA c6330aea3e0fc94532aa29a7df02860e95e386fb9412f934725ce6bcc58d7c25.
It is not uploaded. Reuse unchanged compressed game-data where practical.
Do not announce or label either old candidate as G2-fixed. All test games closed.

### v0.6.1 room-mode crash — fixed and gameplay checked, release pending

The user requested inspecting the room-mode report in QQ 人均233.3 and proposed
v0.6.1. Downloaded run-20261024T045137301Z-32308.zip through the QQ UI; extracted
logs only to .local/v061-room-crash-r1. ESP v0.6 exits at unimplemented import
HlTPoZ-oY7Y#i#G / libSceGnmDriver / sceGnmDrawIndex, caller eboot.bin+0x238118.
The call uses 10 dwords, 480 indices, flags=0, type=1. Import, PM4 decoding and
resident submission now support the indexed-draw path through paired GPU ABI 13.
47 PM4 checks and the real 16/32-bit GPU pixel test pass, with zero Vulkan
validation errors (.local/v061-pm4-r1.log, v061-indexed-pixels-r2.log).
Actual run logs/esprade/run-20261024T052725293Z-103840 entered the room,
selected Yusuke, ran Story Course / Stage 1A with Super Easy support to 11110,
then returned from MISSION FAILED to the mission list and room. Indexed
commands of 420/448 indices executed. Same-process ARCADE / Yusuke also
entered the real first stage. No unimplemented import appeared.
Final source checks, clean paired build, two packages, OSS and group notice
remain to finish. Use one extraction per final ZIP, not repeated full cycles.
Keep the crash fix separate from the completed launcher/package correction below.
The plan and TEST_MATRIX record focused v0.6.1 work; avoid unrelated retesting.
The approved Codex self-introduction was sent to 人均233.3 at 13:26 on
2026-10-24 and verified in the sent bubble. Do not repeat it.

### Post-v0.6 package correction — latest

New packages use format 3 and ship only EVAC PC LABEL.exe plus its matching
PDB when symbols are selected. Do not reintroduce CAVE-PC-Collection aliases
for shortcut compatibility. Historical format-1/2 verification remains.
37 package tests pass (one existing symlink-privilege skip), plus six source
package tests. The user explicitly requested replacing the same v0.6 OSS links
AND fixing the header helper row. Source ff11f84 removes the label and its
20-DIP reservation; launcher-only build and matching PDB pass. 450 common,
242 hidden UI, 14 visual and three localization checks pass. Actual desktop
inspection and normal close pass; .local/v06-header-refresh-ui-r2.json records
evidence. Root launcher is updated; game runtime remains cca37bb.
Both revised ZIPs pass unchanged compressed-member equality (135/648) and
changed-file extraction checks (4/5). Payloads have 137/651 files. Source.txt
and metadata distinguish launcher ff11f84 from game baseline cca37bb. See
.local/v06-{public,internal}-refresh-accepted-r2.json and TEST_MATRIX for SHA.
Both packages/checksums and screenshot are replaced and remotely verified.
See .local/v06-{public,internal,screenshot}-oss-refresh-verified-r2.json. Upload
session 3329 completed; do not restart it. Original fixed URLs remain in use.
After action-time user confirmation, the correction/apology and program URL
were sent to QQ 人均233.3 at 13:20 on 2026-10-24. The sent bubble and empty
composer were verified. It also states that the room fix belongs to v0.6.1,
not this UI-only revision; do not send it again.
Preserve old local ZIPs and v0.6 tag. No automatic installed-file deletion.

### v0.6 delivered with fixed OSS links — latest, supersedes pending work below

Player source is fixed at cca37bb69e87cddeb15fedbe6221eccd1354b66a and pushed.
Six-game build .local/v06-final-six-build-r1.log passes: 56 Xbox, six PS4 and
two launcher PE/PDB pairs, all three player receipts at that commit. Final
native run v06-final-native-r1.log passes 28 common/360 CTests, five GPU CTests,
73 native, 46 preparation, 115 resource and 23 cache checks. The CTest source
entry also passes the 517 source tests, with the existing one privilege skip.

Both format-2 ZIPs are FINAL and extracted exactly once. Do not rebuild or
re-extract them for documentation changes or publication approval:
- releases/v0.6-win-x64-cca37bb/EVAC-PC-LABEL-v0.6-win-x64.zip:
  81,939,010 bytes; SHA da6e1f4d05618d9fe038b9947941b01819296d13f5d3e81457f0351d72ba0129.
- releases/v0.6-win-x64-internal-cca37bb/EVAC-PC-LABEL-v0.6-win-x64-internal.zip:
  8,069,399,388 bytes; SHA ca1a09dca5ba83b2bee5d04d0d9bcb4559130adc09256c5783c6ebdd43b6d0b0.
138 Public / 653 Internal payload entries, nine preserved sentinels each and
38 identical shared runtime files pass. Internal selects all six games, both
Futari DLC and matching symbols; neither ZIP contains source or user config.
Acceptance is .local/v06-{public,internal}-accepted-r1.json, cached by ZIP SHA
under .local/release-acceptance-v06. All build/package/acceptance sessions exited
0 (14301, 62993, 25789, 16623, 51277); do not poll/restart those closed sessions.

Actual extracted Public launcher opens, shows all six banners, creates default
1V config and disables launch when data is absent. Its old-name native alias
cold-reopens with ESP selected, recognizes external ESP data and enables launch.
Both windows are closed. UI evidence: .local/v06-public-ui-r1.json; screenshot:
.local/v06-release-media/launcher-v0.6.jpg. No game was rerun for packaging.
Owned old loopback 94680 and wrapper 102464 have exited; databases/logs remain.

The user chose to keep the repository private with limited sharing, then
explicitly requested uploading first and handling the repository themselves.
Both ZIPs, their checksums and launcher-v0.6.jpg are uploaded under OSS v0.6/.
The user subsequently rejected link expiry; these five exact objects now use
public-read ACL and fixed HTTPS URLs without query strings. Anyone with the
link can download. Bucket permissions, old objects and repository visibility
are unchanged. CLI upload/CRC, size/SHA metadata, 32-byte header comparison and
checksum readback passed before this ACL-only change; all five anonymous HEADs
now return 200 with unchanged lengths and ZIP SHA metadata. No full download.
Evidence is .local/v06-oss-permanent-links-r1.json. The earlier private receipts
and signed URLs are historical; use the fixed links for delivery. Public README
retains v0.4 downloads pending the user's repository work; do not add the
ROM-containing internal link there. No historical QQ work.
The v0.6 implementation and delivery are complete; do not restart accepted work.

The focused gameplay work is complete. ESP ARCADE / ARCADE PLUS / SUPER EASY,
formal HTTPS score/replay upload, native ranking/download, full online/offline
playback and cold BGM 068% / SE 070% readback pass (TEST_MATRIX). Do not repeat
those flows without a related change. The six-game catalogue is enabled.

The ABI-12 presenter and guest-fence split pass independent/shared-queue
validation. ESP fetch-shader reads now batch only the last successful length;
all bytes are reread live, with exact fallback and mutation/growth/shortening
tests. The corrected red test is .local/v06-fetch-batch-red-test-r2.log; five
GPU CTests and the paired build pass. Restore native CRLF in vk_instance.cpp
and upstream-files.json was formatting only; manifest SHA is updated.

Final trace-OFF scenes under .local/v06-present-regression-r1 are closed:
esprade-off-fetch-batch (94988), 65.942 s, 3956 consecutive completions, max
17.512 ms; ketsui-off-fetch-batch (85888), 127.231 s, 7634 consecutive records,
max 17.808 ms, stage 1 through BULLFROG at 16403. Neither has intervals >18 ms
or increased audio underruns. All accepted log records drained, present cleanup
completed; existing guest-finalizer exit 27 remains. Detailed window timing is
OFF in these scenes: these are guest completion intervals, not scanout proof.
No game remains from these tests. first-minute-comparison.json records the ESP
draw CPU-side wall interval reduction 10.100 -> 4.908 ms on separate same-mode
play sessions. Stop millisecond tuning; preserve earlier failures and the
separate Daioujou result/save audio-lock observation.

Final source regression r4 passes 517 tests (one Windows symlink privilege
skip); audit covers 2306 source files, 22 submodules and no game data. Review
and CRLF-aware diff check pass. The 48 manifest files were copied to the NEW
game-data/esprade directory; native full inspection verifies all 48 hashes.
Original PKGs and merged preparation inputs are unchanged.

No v0.6 implementation or delivery work remains. Repository access and any
change to the public README download section are handled separately by the user.
Builds, ZIPs and v0.6 source tag are frozen. No historical QQ work.

### v0.6 PS4 asynchronous presentation — current candidate

The paired PS4 host/GPU ABI is now 12. Guest copies and original pacing remain
on the GPU worker; WSI acquire/composition/present run on an owned presentation
thread. Three GPU snapshot slots hold at most one writing, ready and active
image; only stale ready images can be replaced. Two WSI resource slots retire
at reuse/drain. Prefer a second same-family queue when available; the shared
queue fallback externally synchronizes submissions and presents. No normal
CPU pixel readback. Preserve guest flip/label semantics and copy completion.

Build-Ps4 async-r2 verified six PE/PDB pairs and staged all three PS4 profiles.
All five GPU CTests pass; explicit validation passes on both separate and shared
queues with zero errors. Evidence: .local/v06-present-async-{build-r2,ctest-r1,
validation-r1,shared-validation-r1}.log. Deliberate paused-consumer tests prove
bounded latest-ready replacement, not ordinary gameplay drop rates.

Actual isolated Ketsui run .local/v06-present-async-play-r1 (PID 85148, closed)
entered ARCADE/Type-A, fired/scored 320, died/respawned and naturally ended.
The copied profile maps CREDIT to R2 (keyboard W), not Triangle/V. Its observed
30.977 s stage slice has 1858 consecutive window events, max queue interval
17.552 ms, none over 18 ms; audio underruns did not increase. Guest trace has
24 missing records, so do not confuse adjacent logged intervals with real gaps.
Normal close drained presentation and all 437607 log records; no sink failures.
Analysis: play-summary-r1.json and analysis-r1.json under that scene. New capture
analysis separates guest publication and window queue clocks; no scanout claim.
The later trace-off Super Easy run (19964, closed) spans 144.252 s between F8
markers, reaches the first boss/20152, and has no new audio underrun. It found
one REAL 33.327 ms completion interval: command acquire took 15.681 ms. One
other missing log submission is separate from that real long frame. Source
inspection found ACQUIRE_MEM still called full Drain, rejoining WSI every frame.
ABI 12 adds FinishGuest (same guest fences/writeback/callback completion, no
presenter drain). Full Drain remains for teardown/window changes. The paused
consumer regression fails before the fix and passes after it. Build verifies
six PE/PDB pairs; 5 GPU CTests and independent/shared-queue validation pass with
zero errors. Evidence: .local/v06-guest-fence-* and
.local/v06-present-regression-r1/ketsui-off/play-summary.json. Actual new-candidate
regression is recorded below; do not label the old 33 ms result accepted or an
output gap caused solely by missing logs.
ABI 12 Ketsui 66832 is now closed. ketsui-on-guest-fence has 132.279 seconds
between F8 markers, Super Easy/Type-A through stage-1 clear at 20372 and stage 2.
Observed shot/autobomb/death/respawn. Recorded completion max 17.732 ms, p99
17.143 ms; consecutive window queue max 18.330 ms, none over 20 ms. Audio
underrun delta 0. Detailed guest/window records have gaps; distinguish those
from real individual interval values. Full summary/source identity is in that
scene. No build/test load during sampling. Trace-off Ketsui new candidate and
ESP short regression remain. Source rerun .local/v06-source-regression-r3.log
passes 517 tests (one Windows symlink privilege skip); source audit passes.
Daioujou 83372 is now closed: daioujou-off-guest-fence-r2 entered
ARCADE/Type-A/Shotia and ended at 560. The first 10 seconds after F8 have 600
consecutive records, max 17.346 ms, zero audio-underrun delta. Full 142-second
marker range includes result/save and idle credit-unlock dialog, with three
~33 ms frames and two underruns near long native NGS rack locks/save transactions.
Do not call that entire range stable gameplay or assume save I/O causality.
transition-analysis.json retains separate scopes. Presentation drained and all
77625 records were written.
Earlier 103484 normal-launch test also closed; shared config restored exactly.

ESP ARCADE/Yusuke actual score 1390 reached loopback boards 0/1, both replay
uploads, visible ranking and download slot 0018-0000 in online-r2 (95148, closed).
Full playback worked but the local viewer retained NOW ACCESSING: Sony ID mapper
failure skipped the overlay close. The ESP-only hash-bound local viewer transform
now uses stored names, removes local-cache UGC gates and preserves native loading.
Original data is unchanged. 115 native resource checks, 6 oracle tests and actual
native/Python comparison with cold/warm preparation pass; build/staging evidence:
.local/v06-esprade-replay-fix-build-r1.log and v06-esprade-replay-fix-r1/.

Cold offline replay in .local/v06-esprade-offline-replay-r1 (93980, closed) passed:
Player/1390 visible, full playback to 1390, natural return to a clean list. Normal
Alt+F4 drained presentation and all 68418 records, no drops or sink failures;
existing guest-finalizer/exit-27 limitation remains. Networking and detailed trace
were disabled. Do not replay this accepted recording without a related change.

ESP formal service is deployed from d66c11afdee8, with its independent pointer,
database, port 18089 and daily backup. Six HTTPS health identities, legacy routes,
first backup and unchanged five peer PIDs pass. Do not rerun first-install scripts
in .local/v06-esprade-deploy-r1; installed targets must be preserved.
The development catalogue enables ESP. Normal launcher isolated-root run
.local/v06-esprade-https-r1/collection (launcher 12048, game 81344; both closed)
entered ARCADE/Yusuke with a fresh profile, naturally scored 3300, uploaded both
scores/replays and displayed/downloaded the real record over formal HTTPS.
Detailed trace is off; 120 ms diagnostic taps are not physical held-input proof.
The full downloaded replay naturally returned to a clean list. A cold restart
(launcher 77368/game 32232, both closed) also played ARCADE PLUS to 3510, with
formal boards 20/21 and both 76900-byte replays verified in plus-result.json.
New launcher correctly recognized the normal close/exit-27 receipt, no crash
modal. Exact sound-setting percentage still needs a clear UI recheck: earlier
65/68 readings and arbitrary save-byte offset were not reliable. Do not repeat
accepted online replay tests. Other representative modes and final release
remain. Loopback 94680/102464 in online-r1
is still owned (revalidate), not used by this HTTPS run.
v0.6 six-game package profiles and bilingual player README are implemented;
55 packaging/product/catalogue tests pass, one file-link privilege skip. Common
close-receipt recognition now includes ESP with unchanged PID/birth/cleanup gates;
146 launch tests pass; the replacement copied launcher passed actual close.

### v0.6 presentation scope — October 24 revision

Replace the all-game automatic refresh/display plan with a focused PS4 presenter
refactor: independent image handoff, bounded outstanding work, safe resource
retirement and capability-based present modes. Defer automatic desktop refresh
switching, exclusive-fullscreen management, new shared display configuration and
360 presenter changes. Preserve existing fullscreen/vsync and guest flip/label
semantics. Optional timing extensions are tools, not mandatory scope. Keep the
Ketsui stutter evidence and a small real-play regression; wallpaper correlation
is not a proved cause. See docs/IMPLEMENTATION_PLAN.md. This is a plan revision,
not implementation or authorization to change desktop settings now.

### v0.6 SDOJ default — October 24 decision

The player compared 1V and 3V and requested default 1V for v0.6. This supersedes
the historical default-3V instructions below for current development only.
Use frame_delay=1 in Settings, the new-config template and missing-key reads;
preserve explicit 1/2/3 preferences. The local shared config is also set to 1.
Keep the existing low-policy implementation and recovery guards unchanged.
Only configuration/migration/cold-readback regression is needed for this switch;
do not rerun campaigns, overwrite running components or rebuild frozen releases.
456 common and 238 hidden launcher checks pass, including independent cold
readback. Evidence: .local/v06-sdoj-default1-checks-r1.log. Only test components
were rebuilt; player binaries will take the embedded default in the v0.6 build.

### Current player request — Ketsui timing capture (October 24)

The user requested instrumentation, a complete build and direct Ketsui launch
for one actual play session. Do not operate the controls after handing it over.
Opt-in environment EVAC_PS4_TIMING_TRACE=1 records pacing targets/notification,
GPU-worker handoffs, host waits, slow draw/resource access and nonblocking GPU
batch timestamps through the bounded asynchronous logger. The GPU ABI is now 8;
build and stage the paired host/DLL. analyze_capture.py correlates long frames
and F8 markers. WPR start failed with 0xc5585011 (performance policy permission),
and status confirms no recording; no system trace/physical scanout claim.
GPU batch spans include dependencies, not exclusive GPU busy time. Initial five
GPU CTests and display-clock self-test pass with capture enabled. The first
whole-build compiled successfully, but Xbox staging detected newly added analysis
sources; rerun the incremental build after edits settle, not manual certification.
This is diagnostic instrumentation, not a completed frame-pacing fix. Keep the
ESP online work below; its actual online acceptance remains unfinished. Both old
ESP game PIDs 99892 and 100304 are closed, superseding stale running-PID notes.

### v0.6 implementation — current, supersedes older v0.4 delivery work

The active user goal is stepwise v0.6 implementation including ESP Ra.De.
community features. Do not mark the goal complete from the launcher subset.
Do not resume historical QQ announcements; the user handles those messages.

Launcher layout, English Launch.bat and nickname warnings are implemented in
the dirty development tree. Common nickname normalization/native display rules
are shared with the three Xbox adapters and PS4 ShortOnlineId. Actual PS4 font
coverage metadata distinguishes Ketsui 01.00/01.02. Cancel leaves the draft and
INI intact and cancels automatic launch. See docs/NICKNAME_DISPLAY.md.
456 common checks, launch CTest, 238 hidden UI checks, 7 Xbox community CTests,
3 localization tests pass; PS4 community library/host link and client self-test
pass (r2; r1 expected esprade to remain unknown, corrected to a truly unknown
title while retaining cross-title rejection). Evidence is
.local/v06-nickname-*. Actual independent desktop launcher PID 97768 showed the
Chinese warning, default Return cancelled without saving, then normal Alt+F4
closed it. The shared user config and root launcher remain unchanged. The 497
source tests finished: one Windows symlink privilege skip, one audit failure
from the unstaged BAT rename. Staging the rename and rerunning the 17 catalogue/
boundary tests passes; no audit rule was loosened. Logs are
.local/v06-source-regression-r1.log and v06-source-audit-checks-r2.log.
Do not describe an old binary/PDB receipt as a new full release.

ESP Ra.De. sources are prepared read-only under .local/v06-esprade-inputs-r1;
merged-104 has 48 files/17 programs, original PKGs preserved. games/esprade/data.json
and initial loader/staging are implemented; catalogue remains Coming Soon and
communitySupported=false. Plain SELF inputs now keep original source SHA while
preparing ELF (46 native differential checks passed). Current owned actual
offline game PID 99892 is still running under .local/v06-esprade-smoke-r3 using
the r4 prepared cache. It reached title, original ARCADE attract and native
pause/options, not player first stage yet. Resume its current window if live;
do not blindly restart it. Remaining: first-stage/save acceptance, normal
collection-launch path, native ESP boards/replay and independent server, full
v0.6 release profiles/docs and final component build/package checks. Production
services are unchanged. Earlier r1/r2 game failures are retained locally.

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

- Use English ASCII names for authored source files and current player-package
  paths. Per-game placement guides are README.txt. Keep bilingual contents and
  user-supplied paths supported; never rename original game resources. Historical
  archive verification may recognize old names, but new packages must not emit them.
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
After the packager closes the ZIP and writes its checksum, start upload and
the single local extraction acceptance in parallel. Wait for BOTH before
announcing the download. This is the user's October 24 workflow preference.
Post-release docs/delivery records do not trigger frozen-package rebuilds.
Keep old tags, ZIPs and OSS objects. Latest accepted v0.3.2 CUSTOM source is
a8a75fb9d86a11003c8fa9f3e1fb38521bcb2a07 (tag v0.3.2-custom), SHA
9bb302546e3f42c9050a4180e1cbee0d3b7784a272979fa71825835173b7ca99:
https://xiaoluoss.oss-cn-beijing.aliyuncs.com/v0.3.2/CAVE-PC-Collection-v0.3.2-update.zip
Reuse its accepted overlay/delivery records; do not re-extract or overwrite it.
Service deployments and group delivery are separate from builds. Do not restart
peer services, write synthetic production scores, or repeat old QQ delivery.
