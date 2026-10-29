# Development entry

Active workspace: E:/Projects/EVAC-PC-LABEL. Develop/build here. The former
E:/Projects/CAVE-PC-Collection is a recovery/evidence copy.

## Current objective

ACTIVE October30: implement v0.8.2. Full NEO/PS4 Pro support, four-title
ordinary/NEO benefit comparison, Dangun Feveron latency improvement, bilingual
docs and LOCAL program package. Stop before publication; no push/upload/tag.
User prefers NEO default on, with per-game neo=false opt-out. No subagents.
Published v0.8.1 ZIP/tag stay frozen.

Implementation and actual comparison are complete, local build/package pending.
Production config defaults neo=true for all4 PS4 titles. Full 1080p profile,
NEO350 init/indexed UCONFIG, ALT16pipe and D16S8 GPU conversion to D24/D32
are wired. GPU validation-r5 passes18 image checks/1703936bytes/zeroVUID;
common661, config22 and PM4/compatibility passed. Current version0.8.2.
New maintained upstream hashes recorded; original CRLF preserved with scoped
whitespace attributes. AGENTS is LF: apply_patch or explicit newline='\n'.

Final latency evidence in .local/input-latency-v080-r1:
v082-{ketsui,daioujou,esprade,feveron}-neo-comparison-r1.json, reproduced by
corresponding tools/latency/review_*_neo.py. Padreturn->firstresponsePresent
ordinary/NEO: Ketsui4.9112/5.4886ms, DOI5.5856/5.6678, ESP7.5844/7.935,
Fever21.538208/6.888592. Ketsui/DOI/ESP one clean same-command pair each;
Fever12 each. DOI observer identifies LAST PadRead+originalcallback/P1delivery,
12/12 each; lowlatencyflag1 live aftercrossmodequickload. Whole budget adds
8.333333ms sampling+historical3.300679ms presenter, NEO rounds17/17/20/19.
ESP ordinary alreadyahead2; do not doublecount this as new NEO benefit.
Early capture analysis/death/respawn cases excluded, not new regression evidence.
ESP neo-pixel-r3 overflowed because CPUhelper endedid93 armedpixels while
paused; EXCLUDE. AcceptedESPbase-r4/Neo-r5 are pixel-only, both observedslot01.

GPU/CPU work reports .local/v082/{ketsui,daioujou}-paired-work-r2.json,
esprade-paired-work-r1.json; public reviewer review_ps4_neo_workload.py.
GPUspanus ordinary/Neo407/494,307/362,546/677,Fever267/308. Spans include
batch dependencies, not exclusivebusy. p95intervalall~17.03-17.09ms.
Matched timingCPU Ketsui.293/.223,DOI.285/.281,ESP.410/.426cores; no causal%
claim from4s windows. READMEcleanNEOmeans .25/.26/.38/.27cores; source identities
and r2 vs r3(D32fallbackonly) distinction in docs/TEST_MATRIX. Keepcostscopesapart.
Fever original01000/01010 audit v082-feveron-lookahead-audit-r3/report.json:
ordinaryeffective1update,Neo2/3. Save/predict/restore preserved, not V counts.

Actual NEO KetsuiTypeAStage1 save/load01, DOIShotiaStage1 save/load01,
ESP YusukeStage1 oldbase1350slot01+Neo5620slot02 save/load, all passed.
Fever olddownloadedFeveronCheck7pointreplay from isolatedv080profile seeded
into v082-replay-game-r1 completed1:23.56 naturally and returnedlist7points.
PID95772 closednormally. No games running. Old empty measurementprofile-r14
list was emptydata, not gamefailure; validprofile-r15 used. No input after
replaystartid54 except finalclose, trace/launch receipt binds testfixture.

BilingualREADME/playerguide/changelog updated. Need finish final targetedchecks,
commit source locally, build/certify all8runtimes+launcher atsamecommit, one local
Public programZIP+oneextraction acceptance. No publication/push. GoalACTIVE;
do notmarkcompleteuntilpackageactuallydone. Earlier test invocation errors in
source-tests-r1/r2 were wrong standalone testloaderROOT/PYTHONPATH; use supported
scripts/ps4/run_native_checks.py after certification, not directoldtestmodule.

COMPLETE October29 22:46 CST: v0.8.1 formally published as GitHub Latest.
Release https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.8.1,
ID427992601. main and annotated tag pushed; tag/source/build/package commit
b176ac52867fe17ffed8ec87705401860a2fb03f. All8runtimes+launcher certified at
same commit. Final ZIP releases/v0.8.1-final-b176ac5/EVAC-PC-LABEL-v0.8.1-win-x64.zip,
110896400bytes, SHA85414cb0844ba7674877a7dab633594c4403c81e2f5ebf92da7163a7156723d7.
ONE actual extraction161files/9preservation categories accepted under
.local/release-acceptance-v081/85414cb0844ba767/accepted.json. Packaged bilingual
README includes PS4 table; presets fullscreen/720p/900p/1080p. Final common,
launch, preview, PS4 compatibility and hidden launcher UI checks pass. Initial
hidden UI failure was stale preset index3; fixture now finds1280x720 by value.
No production change for that correction;60421b4 candidate never accepted.
Published notes are3player-facing bullets+table per language. Public API
confirms Latest/non-draft/non-prerelease,2asset sizes/digests and notes (CRLF
normalized);100-byte downloaded checksum matches. No ZIP redownload/re-extract.
Delivery .local/v081-github-delivery-r1.json, screenshot
.local/v081-release-published.jpg. No OSS/internal-ROM/group announcement.
This closing checkpoint is docs-only: do not rebuild/repack or move release tag.
User follow-up answered: Ketsui1.00/1.02 and Daioujou1.00/1.04 use shared host
latency improvements; vsync=false for low-latency presentation, original modes
may differ. Older checkpoints below are historical, superseded by this release.

LATEST October29 launcher preset follow-up: user considers sub-720p choices
unnecessary. Source kWindowSelections now only1280x720/1600x900/1920x1080 plus
the launcher fullscreen choice. Keep shared/custom INI dimensions and automatic
small-screen fitting. Build only common tests+launcher; no runtime campaign or
package rewrite. Frozen75ea7b8 ZIP predates this change. User also asked about
PS4 data in README: repo bilingual tables are current, packaged README.txt still
has360 table only; that omission was disclosed and needs syncing at next repack.

COMPLETE October29 latest v0.8.1 local package, STOP BEFORE PUBLICATION.
Source/build/package commit75ea7b813ac64bac4642249a3c585fcf273d42bf includes
ordinary-PS4 ESP Ra.De. latency fix. All8runtimes +launcher certified at same
commit; PS4 and360 incremental builds pass. Source audit2615files/22submodules
passes. Latest Public ZIP releases/v0.8.1-final-75ea7b8/EVAC-PC-LABEL-v0.8.1-win-x64.zip,
110896019bytes, SHA7c1c24098dcff0c6a2f15903dad11bd479677b1978c06dc64fdb96ea34458dcc.
ONE actual extraction161files/9preservationchecks passes, accepted.json under
.local/release-acceptance-v081/7c1c24098dcff0c6/. Updated bilingual notes and
unchanged-launcher-hash verified preview adjacent. Notes omit launcher display
selection bullet per user. Delivery .local/v081-low-package-delivery-75ea7b8.json.
No push/upload/tag/release, no games started. Earlier ZIPs retained. This source
checkpoint is documentation-only; don't recertify/repack the frozen ZIP for it.

COMPLETE October29: user changed plan to ordinary PS4 mode with lower ESP Ra.De.
latency. Production keeps Neo=false/720p. All full-Pro profile/GNM/GPU changes
were restored (ours/initially clean); no Pro/1080p support is being shipped.
loader/esprade_latency.h pins4 rawSHA +11-byte original selector; ONLY prepared
copy sbb eax,0 becomes3NOP. Original machine query and low-latency OFF remain;
ON uses original2-frame save/predict/restore. Original game-data hashes unchanged.
C++/Python full prepared bytes match all4modules, recipe predictionFrames2.
Build r2 successful; first const-plain failure retained. Compatibility passes,
native88/88, analyzer5+ROI3 pass. Private low-probe-build-r1 compiled restored
base objects +new loader; old directory name pro-probe-r2 does not imply Pro.
Default-path latency NO experimental ahead env: v081-esprade-low-latency-r2,
4 actual Stage1A/Yusuke responses7.7792/7.3476/7.4504/7.6678ms,mean7.56125.
All current callback,1837ahead2,1836/1836 clockrestore;96197trace/74pixels complete.
README estimate8.333+7.56125+3.300679=19.195ms ->19ms, same scope as older tables.
CPU same EXE, detailed core observer OFF/NO ROI, actual Stage1 shooting2x2seconds:
1437.5CPUms/4.001053wall=.359280cores ->.36. Earlier.25 is different activity,
not controlled before/after CPU cost. Shad reference46ms/.62cores retained.
Functional91780: Arcade/SuperEasy Stage1 shooting/movement/pause; quick save/load;
Replay Theater menu (0entries, NO playback claimed); Osarai menu; room firstshot
tutorial cleared(reward10money/3tickets). Cold34604 loaded Arcade slot01 with
score1350/life2/power2. Both closed viaAltF4; complete386953/147534traces, no ROI
or core observations,720p. No games left running. No all-mode/full-replay claim.
Final verified evidence .local/input-latency-v080-r1/v081-esprade-low-final-r1.json,
recheck generator review_esprade_low_final.py. Bilingual README/CHANGELOG, local
release-notes draft, ANALYSIS/TEST_MATRIX updated. Source changes uncommitted.
NO release/push/upload/repack. Frozen cea174f ZIP unchanged SHA b445d966...e7b3;
it does not include this fix. Earlier checkpoints below are historical.

Abandoned full-Pro evidence retained privately: PM4 index1 VGT_PRIMITIVE_TYPE,
then D16S8->D24S8 unsafe copy validation VUID00171(18.28MB>16MB). Test128024
reachedtitle/closednormally, others exited. Do not revive these unrelated GPU
changes for the ordinary-mode request. No user help is needed to finish task.

LATEST October29 Daioujou follow-up STATIC ANALYSIS COMPLETE. All4 original
cores01000/10/20/30 setter69c0 via200058 writes enabled?1:0, no core Neo import.
Original save/predict/restore exists; NOT ESP Ra.De.'s base1/Neo2 selector.
Eboot has6 direct Neo callers, including display/capability paths; don't claim
the whole title has no Neo queries. console.overdrive is separate outer branch.
Earlier01030 responsePadRead->Present4.487ms precedes nextcallback17.131ms;
doesn't measure live option value. Audit tool audit_daioujou_lookahead.py and
.local/input-latency-v080-r1/daioujou-lookahead-audit-r4/report.json bind all6
originals and earlier response. No game launch, production changes, rebuild,
repack or publication. No need more measurement to answer this code question.

LATEST October29 ESP Ra.De. latency ROOT CAUSE CONFIRMED, analysis request done.
Original module01030 callback10e0 calls sceKernelIsNeoMode (WslcK1FQcGI), selects
lookahead1 for current base profile/0, 2 for Neo/1, variable3403f60. Private
ARCADE/Yusuke 1->2->1 controls, four pixel-verified movements each, read->first
responsePresent24.190->7.655->23.787ms. Last2 use IDENTICAL EXE/DLL/config,
causal saving16.133ms. Input delivery~.28ms, original callback~.98/1.17ms;
not20ms CPU work. Original save/run-ahead/restore, next normal scheduler clocks
match previous normal end in1503/1251/1248 adjacent pairs. console.overdrive0
is a SEPARATE branch. Audit esprade-core-audit-r2/report.json; final evidence
.local/input-latency-v080-r1/v081-esprade-lookahead-cause-r2.json, public reviewer
tools/latency/review_esprade_lookahead.py. Details docs/ANALYSIS.md. All game PIDs
closed normally. Added private measurement tools only; NO production fix,
build/repack/push/release. Frozen cea174f ZIP remains unchanged. README40ms is
current production; experimental~19ms estimate is NOT shipping. If implementing
later, prefer title-specific original lookahead2; do not globally enable Neo.
Eight relevant analyzer/ROI tests pass. No user help required.

LATEST October29: ESP Ra.De./Feveron CPU+latency supplement COMPLETE. Bilingual
README/CHANGELOG and local final-cea174f release-notes updated: ESP40/46ms,
CPU.25/.62cores; Feveron31/46ms,CPU.25/.66. Four/six inspected responses per
side, normalized60Hz phase, reused same-PC presentation budgets3.301/4.506ms.
Evidence .local/input-latency-v080-r1/v081-ps4-extra-comparison-r1.json, checked
by review_extra_final.py. All6 accepted pixel captures complete/zerooverflow.
All test processes closed normally. DOCS ONLY: frozen ZIP/build unchanged;
do not rebuild/repackage automatically. No push/tag/upload/publication.

LATEST completion October29: v0.8.1 REPACK COMPLETE, stop before publication.
Frozen code/build/package commit cea174febb906363debb49bddb798a0188d42d81.
NEW releases/v0.8.1-final-cea174f/EVAC-PC-LABEL-v0.8.1-win-x64.zip,
110892363 bytes, SHA b445d966caa2614c88dca09c59d988abae1ce6d3440d9759516ffd901de8e7b3.
ONE extraction accepted 161 files/9 preservation checks, .local/release-acceptance-v081/b445d966caa2614c/accepted.json.
All 3 build stages succeeded at cea174f, old d33 frozen ZIP preserved. Notes and
unchanged launcher preview beside new ZIP. Delivery .local/v081-repack-delivery-cea174f.json.
CPU .29/.23 cores in bilingual README/CHANGELOG; latency retained17/16ms.
Anonymous360 practice comment: 3 representative actual boss entries passed:
G2 Xbox360 Stage3 MID BOSS/Ageha, Futari1.5 Xbox360 Original Stage1Boss/RecoNormal,
SDOJ Xbox360 Stage1BOSS/TypeD SayaShot. Natural death/return observed, no crash
reports/fatal logs. .local/v081-practice-checks-r1.json. Still unlocated, NOT fixed;
no all-boss/character matrix. Test PIDs95536/53380/39964 closed normally viaAltF4;
earlier G2 104864 also closed. Direction keys for CUA remapped T/G/F/H only in
.local/v081-futari-display-r1/user-data/config.ini; player defaults unchanged.
DO NOT rebuild/repackage due this documentation-only checkpoint after cea174f.
No push/tag/upload/release. This supersedes older running/next checkpoints below.

LATEST user request: rebuild the local v0.8.1 candidate including draw preparation
optimization; investigate an anonymous "360 practice boss crash" comment. User
confirmed NO game/mode/stage/log details exist. Do not infer a specific game or
claim a fix without reproduction. No publication/push/upload is authorized.
Updated CPU samples verified in actual Stage1: Ketsui93924 .288941cores,
DOI46888 .226463cores. Both closed viaAltF4. Original core observers enabled,
draw timing OFF, ROI afterCPU. The first sample in each process was menu/dialog
and REJECTED; valid files end -stage-cpu.json. Same-process later ROI snapshots
hit capacity after earlier menu shots; retained end scenes verified, not accepted
as complete latency captures. Report v081-ps4-cpu-comparison-r2.json binds values.
Bilingual README/CHANGELOG CPU now.29/.23 vs shad.62/.34; latency stays17/16.
Next commit source locally, refresh all3 build receipts at same commit, PublicZIP
in a NEW releases directory, ONE extraction acceptance, preserve frozen oldZIP.

LATEST October29 user-requested draw preparation optimization COMPLETE locally.
This supersedes the frozen-package stop below for source/build status. No push,
tag, upload or publication. Current source changes are not committed. Build r4
and staging completed; old releases/v0.8.1-final-d33c445 ZIP remains unchanged
and DOES NOT include this second processing round. Do not present it as updated.
Pipeline/fetch decoding reuses only exact live bytes; specialization shares
current-draw vertex descriptors with binding. Single-view backing reads and
metadata queries avoid redundant allocation/checks; empty/small SRT is cheaper.
GPU targets7 passed (initial fake-metadata fixture failure fixed and rerun),
native checks88/88 passed including4 live vertex pixel cases, Python15 passed.
Interleaved before/after/after/before benchmark:
.local/offline-bench/v081-preparation-abba-r1/report.json, fetch170 mean663->515us,
fetch250 mean919->694us, p95709->554/998->730. Final standalone timing r2 had
host drift; retained, not the source of these interleaved numbers.
Actual DOI private test118624 closed viaAltF4. Stage1A/TypeA/Shotia moved/fired,
score34000; raw22frames/75850trace records,0overflow. Game pipeline.931->.724ms,
vertices.436->.073,resources.253->.226 in similar227..244draw batches (196new).
Representative savings~.60ms, NOT frame-matched or whole4.006ms latency A/B.
.local/input-latency-v080-r1/v081-draw-preparation-comparison-r1.json binds all.
Earlier test82652 title and116916 BGM menu samples REJECTED. No game running.
Full details in docs/ANALYSIS.md under second draw preparation round.

FINAL v0.8.1 COMPLETE locally. Stop work; no push/tag/upload/publication.
Code/build/package commit d33c4455ab62cdff5bb8c72ab7798c3ddb0976d9. Subsequent
commit is documentation-only completion checkpoint; DO NOT rebuild/repackage
just because HEAD now has that documentation commit. All3release build receipts
and packageSOURCE point to the exact tested code commit d33c445. All8runtimes
and launcher built, accepted program ZIP110878509bytes, SHA09b7d35a85129caa223c
5bfc669160ed234a35b2ef9024a8f2875d2e8247b1d7. Path releases/v0.8.1-final-d33c445/
EVAC-PC-LABEL-v0.8.1-win-x64.zip, adjacent .zip.sha256, launcher-v0.8.1.png and
release-notes.md. ONE actual extraction161files/9preservationchecks accepted:
.local/release-acceptance-v081/09b7d35a85129caa/accepted.json. Actual extracted
launcher PID124808 opened/selected960x540/closed; PID100792 reopened with AX
960x540 confirmed, closed. No games/launcher left running. No publication.
Implementation, CPU/latency methods and Feveron fix details immediately below
remain valid; all older RUNNING/NEXT notes are historical. Final docs record
the one transient source HTTP10053 failure and isolated passing rerun, not an
invented all-green first run. Next user action may authorize publication; don't
relaunch games, collect more latency, reextractZIP or publish automatically.

LATEST end-of-implementation checkpoint (overrides every live-process note below):
NO game running now. Both present-wait tests closed normally. DOI97228 also
passed Stage1/Shotia pause;8980IDs all completed/0drops,240gameplay bounds mean
3.3448/p953.977ms,20valid rawframes/nooverflow. Ketsui bound3.2566ms as below.
Both bilingual READMEs and v081CHANGELOG now include PS4 totals17/16ms versus
shad55/55ms and CPU.26/.37 versus.62/.34; method is EXPLICIT stage-budget
estimate with per-runtime sampling periods and completion-return upper bound,
not physical panel or exact OS timestamps. Plan implementation items checked.

Version now0.8.1 (CMake/product.py/Release.ps1/playerguide). All8 production
hosts+launcher built successfully before local commit; native PS4 88checks pass.
Final858source tests:856pass,1skip,1Windows10053 loopback connection-budget
failure; ONLY failed test rerun passes, original logs retained. No serverchange.
Actual360Futari960x540 PID73216 reachedtitle, closed; matches150%DPI640x360CUA,
same as earlierPS4window. Real hidden HWND geometry checks alreadypassed.

Feveron friend archive LAST now resolved: SHAe33af98b..., final
unimplemented_import HUbZmOnT-Dg#Q#Q libc/libc fromdfeveron_01010+9f74 is
double atan2, verified by ownNIDhash and fixedshad source. Added SysV double
wrapper sharing typedhelper, preservederrno; floatentry unchanged. Native3new
mathcases and authoredSSE/import+wrongnamespace fixtures inrun_native_checks
pass88/88. Actual production fixture .local/v081-feveron-startup-r1: PID37932
closed afterattract; PID116772 restartwithONLYCAVE_DIAGNOSTIC_INPUT_TAP_MS100
to make CU0.6-1.5ms taps reliable, enteredARCADE/TypeA/lockon/defaultspeed first
stage, moved/fired(score5), eventuallyGO due nohumanmovement, closed normally.
Same01010core asfriend; exit.ini window_close_requested27, presentation cleanup
complete, no unimplemented_import. No defaultinput change. Do NOT askhumanhelp.

NEXT: final source audit/diff/commit locally (NOpush), rebuild same localcommit
for release receipts (mostlyNinja no-op), local Public ZIP+SHA+screenshot, ONE
actualextract/preservation acceptance, stop before publication. Existing
.local/accept-v080-program.py is template for v081 acceptance; copy/adapt rather
than reextractoldZIP. scripts/release_package.py requires cleangit and all3
receipts sourceCommit==HEAD; currentprecommit receipts still08f0186. Final
buildlogs .local/v081-final-{common,xbox360,ps4}-build-r1.log. Finalsource log
.local/v081-final-source-checks-r1.log, isolatedHTTP rerun
.local/v081-http-budget-rerun-r1.log. No v081 package/publication yet.

LATEST override (Oct29, this checkpoint supersedes ALL older live-process notes):
Only live game is private DOI PID97228/window24646948, CUA waitD1. CPU-scene
helper exec70486 running; poll it, then inspect actual Stage1/pause and normal
AltF4 flush. Label v081-daioujou-present-wait-r1 under input-latency-v080-r1.
It uses current optimized GPU plus PRIVATE asynchronous present-wait observer;
CPU here is diagnostic, not public CPU baseline. DOI build7steps succeeded.

Actual progress: KetsuiCPU frame-r2 PID101380 closed normally. .257722cores over
4.001s is accepted representative Stage1/Type-A; actual raw GPU frames0/15
verified71.616ms after final CPU read. It retained30valid frames, then dropped
late frames at capacity2531; don't feed it to strict latency read_capture or
pretend complete timing ROI. Earlier frame-r1 produced NO pixels because width
1280 exceeded observer1024limit, not an established WGC/HIF staleness bug.
New launch_ps4_probe.checked_pixel_region rejects this BEFORE launch; shared
with shad launcher,3tests pass. run_ps4_cpu_scene now turns ROI OFF after150ms
and uses Back/Touchpad to pause M2 games: Options is arcade Start, NOT pause.

ShadKetsui PID112948 closed: CPU-r1(.629) was PSN dialog, REJECTED. After real
observed clearing signout/credit dialog, insert coin/Start, select Type-A and
3s wait, CPU-r2 is .616953cores/4.002s. Actual immediate end shows Stage1 pause,
2spares,0score, intro river. Not identical exact frames to native laterStage1.
DOI earlier native.369014/shad.341682 both valid representative scenes as below.
Aggregate .local/input-latency-v080-r1/v081-ps4-cpu-comparison-r1.json records
hashes, scene limitations and includes input timestamp observer in BOTH builds;
do not label these uninstrumented CPU or attribute entire difference to one fix.

Postpresent: NEW tools/latency/vulkan_present_wait.h, async bounded request
worker, NO render-thread wait; sourceID joins exact trace Presentbegin. New
scripts/vulkan_present_wait.py +5tests pass (timeout/drop/reversed clock/source
reuse rejected). Standalone r9 bench all3eight-second controls pass. Wait-mode
Immediate421steady bounds mean3.942/median3.727/p957.673ms; FIFO7.295/7.435/12.965.
This is KHR completion-notification upper bound, NOT precise OS event or photons.
Private Ketsui actual r1 PID84940 closed,8822IDs all completed/0drops/0errors,
trace74575complete/0overflow; same CPU window240real game presents gives bound
mean3.25657/median3.37245/p954.1964/max5.3351ms.20raw GPU frames valid/0overflow,
first+67.443ms; actual Stage1 pause confirmed. Summary -present-wait-r1-summary.
Production source/binaries do NOT enable present-wait. Private source generator
prepare_ps4_present_wait_probe.py copies presenter and vk_instance; rebuild tool
compiles private vk_instance then resolves it before original static library,
checks original inputs unchanged. NativeK buildr2 7steps passed; failed prep1/2
and build1 remain (source match/const pNext typing), corrected in generator.
KHR wait is supported on this GPU but no new production dependency introduced.

Current remaining: DOI present-wait end/analysis; actual360 resolution launch;
final honest PS4 total latency/CPU tables +bilingual docs; Feveron friend ZIP
LAST; version0.8.1 and same-snapshot builds/localZIP/SHA/screenshot/one acceptance.
NO v081 package, push, tag, upload or release yet. User sleeping; no help/UAC.

RUNNING NOW override: private frame-r1 KetsuiPID8956/window22353188, CUA variable
cpuFrameK, raised. CPU scene runner launched in exec session74750; poll THAT
session, do not restart on observation timeout. It replays accepted nativeR4
navigation to~66.9s, samples4s, then arms ROI90 and requests Options91. Output
v081-ketsui-cpu-frame-r1-result.json plus -navigation.json under input-latency-
v080-r1. Inspect actual end/rawGPU scene after normalAltF4 flush; ROI30full
1280x720 frames created ONLY AFTER CPU. This is not yet accepted gameplay CPU.
The latest helper metadata names pause_REQUESTED (ACK isn't proof of pause).
Ketsui r5 ended234.30ms/s but CUA signout/credit3/AMI, rejected; no traceoverflow
(291467complete). No newWGC-stale or product-regression claim without rawframes.
All other games/builds are closed. No release artifacts created or published.

LATEST October29 ~03:40 override to checkpoints below: KetsuiCPU r5 also failed
visual acceptance (post-sample CUA was signout, then credit3-unlock, then AMI
with credit0). It is234.30ms/s but DO NOT use it as gameplay CPU. Its full trace
was flushed after normalAltF4,291467records/zerooverflow; test-pad capacity
was NOT the cause. After clearing dialogs AMI appeared static in repeatedCUA;
could be timing/navigation or stale HIF window capture, not established as a
product regression. All prior KetsuiCPU r1..r5 remain unaccepted. NO LIVE old
game29032 now. CUA cpuKetsuiR5 window961023178 is stale.

Better next action ALREADY prepared: rebuilt a complete private native+GPU ROI
observer against CURRENT production objects (including shader coalescing),
rebuild_ps4_probe.py using unchanged verified v081-ketsui-bootstrap-probe-r1;
new v081-ketsui-cpu-pixels-build-r1,6compile/linksteps succeeded, no originals
modified. Fresh staged v081-ketsui-cpu-frame-r1 has fullscreentrue/vsyncfalse,
seed from old private accepted latency fixture, --pixel-region0,0,1280,720,30.
New launchlabel v081-ketsui-cpu-frame-r1 JUST STARTED; read launchJSON forPID.
Not raised/runner-started at this exact checkpoint. This test captures RAW GPU
frames after CPU to resolve possible stale CUA/HIF captures and actual scene.

run_ps4_cpu_scene.py now EXCLUDES original nav command90 (doesn't change released
controls) and uses activeCPU pulse IDs1001+, so existing vulkan_pixel_probe.h
gate90<=command<1000 makes NO ROI allocations/copies during CPU. AFTER sampler
last CPU read it sends90/zero, waits100ms to capture actual unpaused scene, then
91/Options to freeze and capture following UI. Thus do not call65000 to pause:
that disables ROI. This design is test-only, no production flags changed. Run
helper using accepted v081-ketsui-latency-r4-trace.csv as reference, newlaunchJSON,
fresh output name -result.json to avoid launchlabel metadata collision. Duration
~71s; stop/flush normally afterwards and inspect raw pixels, not just CUA. If
raw scene notStage1, reject, fix navigation from actual scene instead of blindly
repeating r1..r5. Existing Shad ROI observer uses same gate and supports full
1280x720 source; accepted old header confirms width1280,height720. For its CPU
test use --pixel-region0,0,1280,720,30 and helper exact nav ref
shad-ketsui-core-pixels-r3-trace.csv; capture ONLY AFTER CPU for scene proof.

PlayerREADME CN/EN display paragraph updated to dropdown/remembers selection;
version still0.8.0. No newCPU table or VulkanPresentWait observer implemented.
No game processes other than JUST launched new frame-r1 known live; no builds.

October29 03:32 checkpoint: user handed over computer and is sleeping; continue
v081 autonomously, no help requests, STOP before release as originally agreed.
Concrete progress this turn: shader metadata/code coalescing implemented in
third_party/ps4/gpu_core/host/gpu_program.{h,cpp}, with8 meaningful new mutation/
layout/fallback checks in program_reader_tests.cpp. Nearby metadata (gap<=64)
can be reread with the full current code in ONE RPM rather than two; no stale
code or metadata cache. Disjoint/shrunk mapping falls back and demotes the hint
so no repeated speculative faults. API/pacing/fences unchanged. Production GPU
DLL now6b91733ace7fae16771b632877befb55a761805930bc8d8ad3787d004fe9af5c.
GPU7 tests pass (first cache fixture failed due nested CacheMemoryScope in new
test, fixed to reuse current backend; failed target rerun passes, other6 pass).
Complete Python source tests850 pass/1 skipped, .local/v081-source-checks-r1.log.
Real hidden HWND/DPI/workarea/aspect tests added common_test.cpp and passed;
common/launch and launcher359 previously passed, all8 production hosts built.

Controlled authored GPU microbench128samples/16warmup/two repeats:
.local/offline-bench/v081-processing-baseline-r1/report.json versus
v081-processing-coalesced-r1/report.json.170draw mean731.25->562.07us(-23.14%);
250draw934.46->768.19us(-17.79%); RGB/draw/fence checks pass. No other renderer
ran during these benchmarks. Actual DOI timing profile R1 PID129716/R2 PID103608
both closed,425batches/~7.08s each: pipeline1144->931us, nested shader read
530->311us; vertices/resources roughly unchanged. Not pixel/frame matched,
use as corroboration not exact whole-frame gain or subtract from4.01ms.
Reports v081-daioujou-processing-breakdown-r1/r2.json under input-latency-v080-r1.
Docs ANALYSIS/TEST_MATRIX updated, plan now explicitly requires processing opt.

CPU work: DOI native PID129616 closed, eight-second ARCADE/TypeA/Shotia fixed
sequence yields369.014ms/s=.369cores, v081-daioujou-cpu-r1.json. Native endpoint
CU shows still playing Stage1/midboss, with last life, valid representative
gameplay. Shad DOI first CPU r1=.422cores is NOT accepted (slow old navigation,
end GameOver/credit-unlock modal). Fast same native navigation Shad r2 PID117480
closed,341.682ms/s=.342cores, v081-shad-daioujou-cpu-r2.json; CU during/end
window shows Stage1A/Shotia/TypeA with2spare lives and actual shots. Both low
overhead input timestamp probes, NO ROI/timing trace. Trace overhead is included;
don't label these ordinary uninstrumented production figures. Same monitor/
fullscreen but nativeImmediate/shaddefaultFIFO, profile/stage identities recorded.
No table yet, don't hide DOI CPU reference being slightly lower than native.

KetsuiCPU attempts r1(.172) stayed signout menu; r2(.287) endedGO; r3(.117)
returned signout; r4(.242) used correct full recorded navigation but no end pause
so not accepted by visual scope yet. DO NOT silently use these in public table.
Ketsui has LATE PSN/signout dialogs after ~30s: generic fast DOI sequence fails.
Accepted old nativeR4 source pre101: Start0/1.98s; B2.706..10.989 seven times;
then B41.609/42.989; Y45.598; Start46.296; X57.971; zero66.575; sample66.901.
Use new tools/latency/run_ps4_cpu_scene.py to replay that actual timing and
sample4seconds with small100ms left/right+fire pulses. It NOW automatically
sends Options/Start AFTER last CPU read to freeze endpoint for CU inspection,
metadata scene_verified=false until actual CU proof. Avoid repeated blind
navigation. Current new nativeKetsui label v081-ketsui-cpu-r5 launched in
.local/input-latency-v080-r1/v081-ketsui-cpu-r1; CHECK launch JSON/PID live.
Not yet raised or runner started at this checkpoint. No other game known live.
Old CUA cpuKetsuiR4 window371854160 is stale. Current Ketsui game EXE56610451...
and latest production GPU6b9173...; source fixture binaries documented under
v081-ketsui-cpu-binaries-r1 and probe-stage.json. Native DOICPU EXEaf5dee7...
is old pinned private observer with same original input semantics; trace on,
ROI off. After nativeKetsui, shadKetsui uses existing shad-ps4-r3 fixture,
launch_shad_probe.py default audit, replay accepted shad-ketsui-core-pixels-r3
trace; sample same4s and freeze; actual screenshot required before accepting.

Remaining v081: finish CPU/table with honest endpoints and sample scopes;
actual360 window dimension launch; final post-Present alignment evidence;
Feveron friend archive LAST; bilingual playerREADME/changelog/version0.8.1;
same-snapshot builds and local ZIP+SHA+screenshot, one acceptance, NO publish.
Version STILL0.8.0, no local v081 package. All changes uncommitted; no push/tag.
Postpresent critical limitation still holds: real GPU bootstrap changes games
toHIF and same-session DXGI->OS~.9ms versus360~.98ms, but that's INTERNAL DXGI,
not same-image application Vulkan->OS. Authored no-feedback bench causal tail
~.494ms median, actual-game endpoint still needs proof/bounds. Researched
VK_KHR_present_wait: supported (and wait2/id/id2) per original GPU extension log.
An asynchronous waiter keyed by owned image/presentId could give a completion
upper bound without UAC; don't block render or remove pacing to measure it.
Official vkWaitForPresentKHR has approximate first-pixel semantics, NOT physical
photons; mailbox replacement/retired swapchains and observer backlogs matter.
No waiter implemented yet. Keep diagnostic extension use separate, because
previous VK_EXT_present_timing itself changed driver presentation path.
No new UAC can be confirmed by sleeping human; no security bypass/Taskmgr.

October29 latest human steering: computer is available, human is sleeping and
cannot assist. Complete GUI/tests autonomously; do not wait for manual input.
They explicitly require PS4 processing optimization in v0.8.1, beyond the
presentation/queue fixes already made. DOI accepted single response4.006ms is
wall time: original frame callback remainder.929ms; then2.734ms to prepare;
snapshot/ready->CPU Present.343ms. Same-process submission3154 matches response
prepare within5.1us and is unique: execute2.291ms,241draws, draw1.821ms,
ACQUIRE_MEM.241ms,99prefetches.055ms,flush.031ms,other.128ms. Saved evidence
.local/input-latency-v080-r1/v081-daioujou-response-cost-r1.json. Inspect existing
EVAC_PS4_TIMING_TRACE draw breakdown and authored offline bench before changing
GPU code. WithNativeVideoGpu ALREADY wraps the complete command parser in one
resident->Run; nested Sync is reentrant, do not propose batching as a new fix.
Futari prior core update.115ms/draw.432ms are wall intervals from a different
game/scene; don't call all4ms CPU or compare17ms pacing as processing.
Main table always includes sampling wait plus game/draw plus output tail.

All eight production hosts now rebuilt successfully with fresh vsync=false
and requested display sizes. Logs .local/v081-display-default-{common,ps4,
xbox360}-build-r1.log. common/launch tests pass; launcher hidden359 and PS4
compatibility tests pass. New private .local/v081-window-cpu-r1 contains
production Futari/Ketsui/DOI and launcher,960x540 window, offline/no gamepad.
Ketsui108984 window test reached ARCADE and has been closed normally by AltF4.
Earlier85456 failed startup because its PS launch passed one concatenated
argument; receipt v081-window-ketsui-launch-r1.json is NOT valid runtime test.
Correct launch r2 used Python argument list. No game processes known live now;
recheck before another renderer. CUA handle winCpu1452084108 is stale.
CPU/shad table, processing optimization, actual 360 window validation, friend
Feveron crash LAST, final same-snapshot build/package remain. No publication.

October29 latest checkpoint: Ketsui40732 has now exited normally; exit.ini in
.local/v081-ketsui-user-play-r1/logs/ketsui/run-20261028T181446238Z-40732
records window_close_requested, code27, presentation_cleanup_complete=true.
Recheck live processes before further interactive tests. Fresh shared vsync
default is now false in Settings/default.ini; explicit existing true remains
true. New default assertions and both README display-setting paragraphs changed;
these newest edits still need final build/tests. No v081 package/publication.
The user questions whether4ms skips periodic sampling: original DOI frame
callbacks in R3 are129 samples, median16.667ms. First response PadRead+4.487ms,
original delivery+.480ms, callback return+1.410ms; next callback+17.131ms.
Evidence v081-daioujou-input-phase-r1.json under input-latency-v080-r1.
Do not call delivery return an actual emulated CPU port read or4ms a full
input latency. VSync FIFO adds refresh/queue wait after GPU readiness, separate
from the original60Hz game clock. Analysis now explains this and conditional
144Hz/60Hz refresh waits. Still finish all v081 requirements and LAST friend
Feveron archive; no source changes/builds beyond the above newest defaults yet.

October29 02:15 historical handoff: the human was trying the new presentation path.
Production Ketsui PID40732 was launched in
.local/v081-ketsui-user-play-r1, fullscreen=true / vsync=false, normal keyboard
and gamepad enabled, no latency observer or synthetic input. Receipt
.local/v081-ketsui-user-play-r1-launch.json. Do not control/close this window,
start another renderer, or run heavy builds/benchmarks while they are playing.
Continue lightweight source/docs/offline analysis; resume interactive tests
only after they finish or explicitly authorize them. Earlier Feveron17764
test window was already absent when switching. Its independent logs remain.
The human asked what disabling vsync means: game still uses original60Hz
guest pacing; host presentation needn't wait for screen refresh and can tear.

The approved one-hour collector69872 has NOW EXITED normally. Raw
.local/v081-runtime-display-r1.csv has120553 rows, SHA
e321ab82652003ebf82b3a17d7a99366c701abc066b95aa7661f80ce503ecd87.
Summary .local/v081-runtime-display-r1-summary.json SHA2ef1031e...cf813,
generated by tools/latency/report_runtime_display.py. Twelve processes,
no repeated frame identities; two out-of-order startup ETW rows are explicitly
counted, not used as Vulkan image joins. Native four games use Hardware
Independent Flip after bootstrap, driver-DXGI->OS tail roughly.9..1.3ms;
same-session production Futari134264 about.98ms. Shad96568/93224 hardware
composed independent flip about4.5ms. Mixed menus/attract/gameplay, not same-image
Vulkan->OS or photons. All reference/probe processes are closed.

Accepted new first-response evidence: Ketsui R4 PID124092,146ROI/50763complete,
core-delivery->first-response CPU Present4.9432ms; report
.local/input-latency-v080-r1/v081-ketsui-first-response-r1.json, reviewer
tools/latency/review_ketsui_v081_pixels.py. DOI R3 PID100224,148ROI/57026complete,
4.0063ms; report v081-daioujou-first-response-r1.json, reviewer
review_daioujou_v081_pixels.py. Two disjoint body features per response, zero
trace overflow. Each accepts ONE response, not a mean/P95. Original DOI73.4343
uses SAME core delivery->CPU Present endpoint (15.2343+16.7976+41.4024), but
old config WINDOWED/FIFO and candidate FULLSCREEN/Immediate differ. Human asked
this exact comparison; explain both common endpoint and changed conditions,
not a controlled mean69ms saving or physical input-to-light. Actual emulated
CPU port-load and physical frontend still unmeasured; delivery is the original
integration input function returning, not a physical switch timestamp.

New production policy/default vsync still unresolved (source/default.ini true).
CPU/shad comparisons, actual window-size handoff, final complete build/packages
and LAST friend Feveron crash remain. No publication and no scope reduction.
New microbench r8 adds proper DPI and HWND lifetime; FIFO and no-feedback
bootstrap Immediate121frames each pass. r7 measured sources archived under
.local/v081-vulkan-present-bench-r7/sources before editing. Sampler
sample_process_cpu.py only API-smoked against a shell, NOT game CPU evidence.

Latest steering: record friend Feveron start-of-game crash archive
C:/Users/Xiaolu/Downloads/feveron.zip. Add to v081 and investigate/fix LAST,
after current latency work/regressions, before final local packages. Do not
interrupt the current measurement sequence or treat our successful Stage1
as proof that this other-machine report is solved. Source archive untouched.

October29 01:29 checkpoint: approved PresentMon2.5.1 collector69872 is live
until02:02:52 Asia/Shanghai, .local/v081-runtime-display-r1.csv (one-hour bounded
capture; do not restart it because the CSV is write-locked). Microbench r7 and
controlled ETW show NVIDIA Immediate-only startup takes Composed Copy/GPU GDI;
creating then replacing an unused FIFO swapchain selects Independent Flip
without timing extensions. OwnDC alone does not help. Evidence in
.local/v081-wsi-controls-r1/comparison.json and per-case raw captures. Production
bootstrap helper and lifecycle tests pass252 native compatibility checks;
all four PS4 production binaries and fresh private probe built successfully.
The timing extension itself changes the driver path, so feedback-enabled
microbench results cannot establish production behavior. Game capture still
needs final ETW verification after collector exit. Existing fallback is
Immediate -> Mailbox -> required FIFO when vsync=false, FIFO when true.
Default vsync remains true; low-latency default policy is still unresolved.
Current candidate Ketsui private R3 entered ARCADE/Type-A/Stage1 successfully
with180ms Y then Start (earlier500ms holds were unreliable, not proven a runtime
regression). Trace complete171487 records/zero overflow; owned image
prepare->Present median.381ms, ready->Present.056ms in the requested interval.
Its149 ROI frames show CONTINUE and cannot establish first-response latency.
Fresh R4 PID124092 is now navigating the same fixture with earlier bounded
sampling planned. Do not accept R3 as input-to-image evidence. Baseline
control80856 also entered Type-A and closed normally. Launcher dropdown actual
UI/persistence passed960x540 close/reopen in private fixture; game size launches
and Xbox builds remain. Goal v081 active; no publication; all requirements stay.

October29 admin-display checkpoint: the human asked to prioritize any UAC-assisted
display capture before sleeping. The approved PresentMon2.5.1 session75904 ran
900s and auto-exited00:10:04 Asia/Shanghai. Raw .local/v081-display-admin-r1.csv
has43980 rows, SHA d1dd08731026ad6f69b404cf47098832643b818bf9f9bd0cd99961a49e5fdef3;
summary .local/v081-display-admin-r1-summary.json preserves per-PID/mode results.
Captured candidate Ketsui130588, accepted v080 Futari20660, shad110828. All three
test games and collector have now closed normally. GPU/display feedback exists;
do NOT say the capture failed because the CSV was locked while collecting.
CRITICAL: NVIDIA Vulkan's tracked DXGI events are asynchronous to actual
vkQueuePresent calls: only3 contained intervals per Vulkan trace, NOT accepted
causal image/display joins. Do not label internal DXGI->display distributions
as Vulkan call->display or input->display measurements. No accepted input
response was obtained in these menu/title/attract scenes, no representative CPU
claim. Independent Flip and Hardware Composed Independent Flip differ, retain
separate groups. Both private traces flushed complete with zero overflow.
Current GPU supports VK_EXT_present_timing rev3, surface stages0x5 (queue end
and FIRST_PIXEL_OUT, NOT FIRST_PIXEL_VISIBLE), relative timing only. Driver
present-ID feedback is a useful next non-admin route; it
still needs implementation/verification, not a measured result. Baseline alone
does not accept future backend changes. The v081 goal remains active, all four
requirements and local packages remain, NO publication. No need to repeat this
admin baseline or restart games just to inspect the same unchanged CSV.

LATEST human request (October28): IMPLEMENT v0.8.1 through accepted local
packages, then STOP before formal release. All four requirements stay in scope;
no scope reduction to only UI or research. No push/upload/tag/Release/QQ until
separately requested. Human allows computer use and has finished playing; use
independent test directories, preserve C:/evac-pc-label and frozen v0.8.0.
Authoritative list at
docs/IMPLEMENTATION_PLAN.md: PS4 latency target aligned with360 1V; replace
launcher fullscreen checkbox with a polished fullscreen/window-resolution
dropdown, persisted in user-data/config.ini; after optimization publish PS4
latency and equivalent-core CPU comparisons against shadPS4 in both READMEs;
research lower-version Vulkan options and report feasibility/cost. Cover all
four PS4 games, first baseline Ketsui/DOJ; preserve timing/replay/audio behavior.
ADDED human requirement: PS4 post-Present GPU/WSI/desktop-composition/display
adoption latency must ALSO align with360 1V. Do not stop at vkQueuePresent or
reuse D3D12 historical tail as measured Vulkan data. Common endpoints/display
conditions; monitor scanout/panel separate. This is part of the completion gate.
Do not equate1V with a fixed physical input-to-light latency. Frozen v0.8.0
release remains unchanged. Baseline worktree has only the two requirement-note
edits in AGENTS.md and docs/IMPLEMENTATION_PLAN.md; these are ours, preserve them.

v0.8.0 publication COMPLETE (October28 22:59 Asia/Shanghai). GitHub release
426810838 is stable/Latest at /releases/tag/v0.8.0; main and annotated tag pushed.
Tag/runtime source3ae60a6, release-note documentation40f7eea. ZIP and checksum
are the only uploaded assets; public API sizes/digests, exact bilingual notes,
Latest identity and downloaded100-byte checksum all pass. No ZIP redownload or
repeat extraction/build. Delivery .local/v080-github-delivery-r1.json; screenshot
.local/v080-release-published.jpg. Frozen ZIP SHAbe6ebba0...c0f21b unchanged.
No OSS upload, QQ announcement or further gameplay performed. This delivery-only
record does not invalidate the accepted frozen build or require repackaging.

Latest completed human request (October28): formally publish v0.8.0 to GitHub. Push main,
create the v0.8.0 tag and upload the accepted ROM-free ZIP plus checksum, latest
stable release. Remove the old-driver error-handling bullet from bilingual
release notes; requirements/troubleshooting stay in README. Frozen package
source3ae60a6, SHAbe6ebba09e12fdf25e817d8641f40ad0e19ceeaedacd076d0921b55260c0f21b,
161 files/9 preservation checks accepted. Documentation-only publication edits
do not rebuild/re-extract it; record the documentation commit separately.
Human finished playing and allows computer use; do not repeat gameplay tests.
No separate OSS upload or QQ announcement requested in this release turn.

Previous human request (October28): investigate Downloads/logs.rar PS4 startup
failures in v0.8.0 without computer use while the human plays. Three logs fail
before guest entry on RX580 2048SP device Vulkan1.2.133 / AMD2.0.137, despite
loader1.4.363. The resident GPU requires1.3; do not remove requirements or
advertise a1.2 renderer. Replace the version assertion with an actionable
recoverable startup error before1.3 format queries; add GPU-independent tests.
Friend must update their GPU driver; AMD22.2.1 documents RX500 Vulkan1.3 support.
Do not touch live C:/evac-pc-label, open windows, stop games or run GPU tests.
Rebuild with bounded jobs/BelowNormal, refresh the local program ZIP and STOP
before push/upload/publication. Preserve older frozen ZIPs and G2 default1V.

Previous human request (October28): make G2 default 1V and produce a new v0.8.0
program ZIP, then STOP before push/upload/publication. Built-in settings and new
INI template now use1; missing keys use1; existing explicit1/2 remain unchanged.
Keep native2V fallback and 1V synchronization intact. Reuse the already accepted
complete 1V replay/performance check; only configuration/template/migration,
launcher cold readback and affected shared-component regression are needed.
Keep frozen older ZIPs; final outputs and acceptance are recorded under releases/
v0.8.0-final-<sourceCommit>/ and .local/release-acceptance-v080/<ZIP SHA prefix>/.

NEW human CPU-comparison request (October28), separate from completed latency goal:
three accepted Xenia CPU replay-opening windows, Guwange still UNMEASURED.
Do not reopen the old latency goal. Reused existing optimized EVAC CPU evidence.
Current reader entry docs/ANALYSIS.md section 补充：优化版与 Xenia 的 CPU 对照.
Private .local/xenia-cpu-v080-r1/comparison-r1.json SHA
508082e5b2db5c4ec8e4f8853f7da22892f4aa1c95e3dca54e0d4e81d5b262ab.
Futari HD No5 Xenia1258.97 vs77.37/84.21 CPUms/s; G2 BL Seseri No2 Xenia1373.41
vs368.54/394.00; SDOJ ArcadeHD No1 Xenia3462.46 vs412.65/456.03. Request windows
+9.5..14.5,+50..65,+15..35, effective4.567/14.915/19.919s. NOT frame-lockstep:
G2 post-view CONTINUE, SDOJ pre10s entrance/post35s Stage1; SDOJ native1V vsXenia3V.
CPU helper .local/Measure-XeniaCpu.ps1, recheck .local/summarize_xenia_cpu.py.
Excluded Futari r1 truncated1.39s and SDOJ r1 load-complete dialog. G2 r1 1455.56
is supplementary, not primary. All accepted raw CPU differences recomputed.
Xenia private input-only relink (not official EXE)0256194c...4e412, base291f0c9;
main disables ALL Record before game init; private test_pad Publish fallback
sequence preserves packet changes without recording. No guest sites/pixels/ETW/
GPUtimestamps. Build scripts .local/build_xenia_cpu.py and Build-XeniaCpu.ps1;
build-r1 zero packet navigation failed, r2 fixed. Original libs/source unmodified.
G2 native header XUID B13 resolves to B13 despite E030 enumeration: copied native
TEST B13 path into private content, replay payload unchanged. Same for SDOJ.
Guwange loaded Blue but Xenia text missing; three bounded none/full/fresh-profile
attempts did NOT confirm same Blue Data1 playback. Do NOT report its menu CPU or
declare all-four comparison complete, or claim Xenia cannot run the title (prior
ordinary-mode latency gameplay passed). Latest fixture guwange-r4;111200 CLOSED
normally. All games/compilers/collectors closed, node cpuWindow stale. No production
code/runtime/game-data/config or frozen ZIP changed; no publication/push authorized.
If user continues this comparison, only Guwange remains; reuse the other three.

FINAL six-game latency analysis assembled/audited (October28). Prior goal turn
was concrete progress; this turn finalized the requested report, no new gameplay.
Authoritative reader entry: docs/ANALYSIS.md section 本轮输入延迟结论 at top;
seven rows = six titles plus OPTIONAL G2 1V, with verified-response-only medians
or explicitly single examples,60Hz duration units (NOT V), pair differences,
same-sample source->core and DIRECT source->first-response totals, physical
frontend/code analysis and144Hz/screenCENTER/THEORY light column. Physical H,
post-Present R and panel D are unknown, not zero; full total H+B+R+S+D remains
symbolic. No hardware-to-photon ranking or fixed advantage from tiny samples.
ESP Ra.De./Feveron remain HELD by human, not incomplete requirements this round.
Final tool tools/latency/finalize_input_latency.py rehashes retained reports,
trace/image sources, frontend code and staged runtime files. Current receipt
.local/input-latency-v080-r1/six-game-final-analysis-r2.json SHA
0252e2e36e3dbe6a27b5182e36ad2d6061d3b9f7a4d60a3263c06e45b4c4bf02;
82reports/258files checked. Source HEAD Xenia291f0c9be398a3e0a0e29f378cbd564fa28c55a9,
shad529ceb32f5546b820588f194b903cc39d08015fe confirmed. Measurement EXEs are
instrumented private builds, distinct from historical stock official binaries.
188latency-pattern tests pass incl6final-summary tests; diffcheckclean. Plan
latency item now marks SIX-title ANALYSIS complete, not unknown hardware
measurements. TEST_MATRIX has requirement-by-requirement completion audit.
No games/builds/collectors live; frozen ZIP hash unchanged8a93f2ac...4408b3d.
NO MORE gameplay, variants, sample enlargement, ETW/UAC, packaging, publication,
push, OSS or QQ for this analysis. Remaining turn action: show/deliver final
table and close the active analysis goal only after final document verification.

LATEST DOJ+PS4 closure (October28), supersedes currently-linking note below:
shad SESSION52006 FINISHED0; source shad-daioujou-probe-r1/build-r1 SUCCESS,
EXEcf858b179e7f1b203ed95427944303145f930ade61c331e0817dddefa25931ab.
Fixture shad-daioujou-r1. NO active game/compiler/collector. Original data,
production source/runtime and frozen ZIP unchanged. No publication authorized.
Shad R1/PID129892 CLOSED181817complete0overflow: menu-only, confirmed B input
delivered but Circle confirm didn't match isolated shad Cross-enter setting.
Read original systemservice code/config, ONLY private user/config changed
General.console_language1->0 and Input.is_circle_enter false->true to match
native profile. R2/PID131768 CLOSED135279complete80pixels0overflow,12corepairs
but 6.5s after doll was STILL ENTRANCE; no accepted response. Preserve both.
R3/PID64688 CLOSED157788complete80pixels0overflow,12/12delivery pairs, actual
ARCADE/TypeA/Shotia/Stage1A before/after sequence. Wait20s after actual doll
confirmation, CU actualStage1, then IMMEDIATELY90zero/250ms/101..112right+Square
vsleft+Square60ms gap97ms/300ms/1000zero, CU+AltF4 in sameexec after completed
script. No LLM analysis delay before final capture/close. Native confirms Circle;
TypeA and Shotia EACH need Square (600ms separation), not auto60s selection.
Shad manual101: frames6..11stationary,12right4,13right10,14..16retain10;
body516,239,12,14 andtail514,254,16,8 disjoint, template11,NCC>=.8/margin.025.
First12 IS submitted Present153229. source->delivery22.9161ms;
delivery->Prepare2.060, Prepare->submitReady20.2725, submitReady->Present10.5872;
core->firstResponse32.9197/API33.5735/source55.8358. ONE response,noP95/fixedV.
Receipt shad-daioujou-manual-first-response-r1.json SHA3529bde1...c476e.
Shad same-build OFF R4/PID128592 CLOSED142908complete0overflow,12/12delivery;
before/after actualStage1, one ship remains. ON/OFF58/59images Prepare->Present
median29.43065/29.5252,P95 31.1145/31.1929ms. No OFF response pixels.
Control daioujou-shad-pixel-control-r1.json SHAba7bd6e6...1a25d.
Aggregate daioujou-comparison-r1.json SHA19b7efe348e0a4772bca50747658716f8441dca0c6af3be207411ceb96c23d1b
rehashes original game + response/control evidence. Native/shad core73.4343/
32.9197,API74.3497/33.5735,source77.9687/55.8358. Differences40.5146/40.7762/
22.1329 are TWO separately phased cases/different progression, NOT fixed gains.
Callback cadence native16.6685ms/shad33.3894; Present intervals30.4896/33.2237.
Do not call shad's core callback60Hz simply because its config vblank60, or
subtract Ready phases with different definitions. No timing fix in production.
182latency tests pass (14new DOJ), diffcheckclean. Tools shad_daioujou_core_latency.py,
review_shad_daioujou_pixels.py, compare_daioujou.py. Current goal SIX rows:
FOUR360 +Ketsui+DOJ. Human HOLDS ESP/Feveron. Representative gameplay sampling
is DONE; do NOT launch more games, enlarge samples or retry ETW/UAC. Next ONLY
consolidate six-title final table, common physical/frontend/code-theory,
Present-vs-display and144Hz/center photon budget; audit evidence and deliver.
Physical device/panel unknowns remain explicit, theory allowed by user. Goal
still active, NOT complete until final analysis/table delivered and audited.

LATEST scope/progress (October28): human explicitly HOLDS ESP Ra.De./Feveron
latency work. Current goal is FOUR360 plus PS4 Ketsui/DOJ, six active rows;
do NOT pursue the other two or call them complete. Prior table-acknowledgement
turn was no-progress; current work makes concrete DOJ/controls progress.
Ketsui shad same-build OFF now DONE, PID125324 CLOSED319673complete0overflow,
live Stage1/TypeA after sequence,12/12delivery pairs. ON/OFF Prepare->Present
median30.743/30.812ms,P95 31.619/31.677,57/58images. Control receipt
shad-ketsui-pixel-control-r1.json SHA1b212a25...422c4; old next-OFF note superseded.
DOJ actual ARCADE loads daioujou_01030.prx (not prior01000). Original audit
daioujou-core-flow-r3/report.json SHA439aeffb...b6e3a3 confirms400088->6700,
GOT2006A0->integration01700+61250,helper68C0->2FA50, tail parent return1B55E
plus alternate direct27C04. Do not reuse Ketsui29629 or assume raw masks.
Native private source evac-daioujou-probe-r2/build-r2 SUCCESS; EXE9141221c...2d478f,
GPUa6dcd549...9c123e, stage evac-daioujou-r2/collection. Original sources/data intact.
R1/PID124036 CLOSED28660complete: module discovery only. R2/PID115648 CLOSED
80424complete146images: early gameplay but laterCONTINUE, no accepted response.
R3/PID99368 CLOSED44922complete147images0overflow,12/12 input deliveries,
ARCADE/TypeA/Shotia/Stage1A alive2ships at immediate final CU. Manual command101
frames15..19stationary;20right4px,21right10,22right14. Two disjoint features
(576,239,12,14) and (574,254,16,8), template19, all NCC>=.8/margin.025.
20ready NOT presented,21first responsePresent42664. Core->firsthandoff15.2343ms,
next handoff16.7976, handoff->Present41.4024; total core73.4343/API74.3497/
source77.9687. ONE response, no P95/physical photons. Receipt
native-daioujou-manual-first-response-r1.json SHA41da4b19...9768d.
Native same-build OFF R4/PID131164 CLOSED51785complete,12/12delivery, liveStage1
at final CU. ON/OFFready->Presentmed38.33635/38.1703, Prepare->Present
49.8841/49.6678ms,58/57images. Control daioujou-native-pixel-control-r1.json
SHA992be555...7d68b. No extra whole output cycle established by ROI, not zero cost.
Tools daioujou_core_latency.py, review_daioujou_pixels.py, compare_daioujou_control.py;
11new mechanism tests pass. All native processes closed. Latest shad DOI source
shad-daioujou-probe-r1 prepared; build shad-daioujou-build-r1 currently linking
under exec SESSION52006, not finished yet: poll SAME handle before game sampling.
Never run gameplay measurements while compiling. New shad launch --audit chooses
actual per-game original identity; defaults retain old Ketsui route. Next complete
DOJ shad same-scene response+bounded OFF, then consolidate SIX rows.144Hz/center
photon THEORY retained; no ETW/UAC retry, no publication, no user gameplay request.

LATEST shad Ketsui first-response progress (October28): previous goal turn IS
progress, not blocked. Private source shad-ketsui-probe-r5, build
shad-ketsui-build-r5 SUCCESS, EXE8285d53c9ae6fb3b8ece3592d0addd639770ba4bf452c9354fbc4910a3f22a4e.
Baseline shad-source529ceb and original objects/libs unchanged. Only private
linker/presenter/swapchain objects compiled sequential BelowNormal. Win206 first
link and CMake-expands-@rsp second attempt preserved. Direct original lld-link
@private-link-direct.rsp /MANIFEST:EMBED,ID=1 succeeded via SAME compiled objects;
build-result and resume-link-direct-plan pin inputs. Do not rebuild baseline.
Tools prepare_shad_ketsui_probe.py, rebuild_shad_probe.py, Build-ShadProbe.ps1.
Fixture shad-ps4-r3; first launch seeded only stopped shad-ps4-r2/user HOME and
users.json, not cache/config/credentials. launch_shad_probe.py verifies original
eboot/modules SHA against ketsui-core-flow-r2 audit, staged EXE and fresh outputs.
New shad core observer wraps original50088->6610 and only input GOT313936 ->
loaded integration+5C0A0, actual loaded bases, no instruction rewrite. Site98
(kind10,PC29629,c=mainbase) now allowed by inspect_ketsui_core_probe.py.
shad_ketsui_core_latency.py resolves source by controllerID/timestamp/buttons
(kind27 -> original PadRead report), then shared LAST-report/same-thread/first
callback pairing. Not nearest matching and not replacing original history.
shad pixel adapter derives native Vulkan capture but waits original draw
timeline ONLY during teardown/quick_exit. Ready is AFTER GPU SUBMIT, NOT GPU
completion (different from native completed/paced handoff). Copies same original
guest image before host scaling; exact prepare/ready/Present retained. Stop1000.
R1/PID31260 CLOSED520494complete/80pixels0overflow; R2/PID124364 CLOSED256050/
79pixels0overflow. Both12 core pairs BUT PIXELS ARE CHARACTER-SELECTION/
entrance, NOT accepted gameplay response. Just increasing after-X wait to16.5s
did NOT fix R2: X had arrived before actual character-selection was ready.
R3/PID52592 CLOSED303319complete/80pixels0overflow,12/12 pairs; actual live
ARCADE/Type-A/Stage1. Navigation correction: VERIFY actual coin screen; Y12,
500ms->Start13,1500ms->CU actual TYPE-A CHARACTER PORTRAIT with30count. Then
Square/X14 hold600ms; wait16.5s ->90zero1ms ->250ms ->101..112 right+Square /
left+Square60ms/gap97ms ->300ms ->1000zero. Entire final sequence executed with
exec_command yield30000 AND immediately sky screenshot+AltF4 in SAMEfunctions.exec
after completion, NO LLM boundary; this avoids minutes of unintended gameplay
between input script end and CU observation. Do not repeat blind first8.5s.
Mode pre-nav fast batch B3/4,B5,B6/7 left SECOND signout notice still open;
observed it then B10/11 before actual coin12. Avoid guessing notice timing.
R3 manual command101: image4..11 stationary,12 right4px,13 right8px,14/15retain8.
Two features body205,238,28,18 and upper206,225,28,28, templates6even/7odd,
search180..280, NCC>=.8 and margin.025 all pass. First image12 IS first submitted
response, Present298787; source298707/input-delivery end298748.
source->delivery26.6243ms; delivery->Prepare1.6556; Prepare->submitReady23.6611;
submitReady->Present7.6633. Core-delivery->firstResponsePresent32.9800ms;
APIreturn33.5911/source59.6043. ONE response, no P95 or fixed V.
Receipt shad-ketsui-manual-first-response-r1.json SHA
c0fd20a876027dbe53611a97bdb63b52dc06c51ba2f5822e987a119c4a7227ba.
Tool review_shad_ketsui_pixels.py. shad-ketsui-core-input-r3.json all12pairs.
Rawtrace SHA7bc91a4199d1d6adae7eb151692598db0788bf91d80fd82b8d30e9fc2427bc98.
Vulkan reader strengthened: metadata ready0/Present0 must agree with full trace
NO actual matching ready/gamePresent. Cannot infer replacement from zero fields
alone. 168latency tests pass. Native old pixel evidence revalidated with stronger
reader: current manual receipt native-ketsui-manual-first-response-r3.json SHA
3e0adda9345001acfbdead5f3c5e7c789e900649addb9b749075fd1292468ee1.
Use r3, not old r2 whose source-reader hash is now historical. Values unchanged.
Aggregate ketsui-comparison-r1.json SHA
72ba5882cd1c7116ab0e7d56ed86afbc1565c2c66aa551c04e3cf52cbf9ed375;
compare_ketsui.py validates source trees. Native/shad core-delivery63.5003/32.98,
API63.978/33.5911, synthetic67.1224/59.6043. Differences30.5203/30.3869/7.5181
are TWO SEPARATELY PHASED EXAMPLES, NOT fixed population advantages. Stage
positions differ, no matched-live OFF response control. Do not subtract their
Ready timings because definitions differ. No production timing fix implemented.
System WMI reads RTX4080,3840x2160,144Hz,driver32.0.16.1088; not actual scanout.
Next: one bounded shad SAME-BUILD OFF control using now-confirmed navigation;
then remaining DOJ/ESP/Feveron actual responses+shad comparisons. No more native
Ketsui sample enlargement. Physical frontend/display/photons remain explicit
gaps/theory; all8-game goal active. NO game/compiler/collector live. Node
shadWindow is stale closed52592; reselect fresh returned windows. No publication.

Latest bounded native Ketsui OFF control: SAMEr9 fixture/EXE/config/stage/contract,
label evac-ketsui-pixels-off-r3, PID32448 CLOSED normally121486complete0overflow.
Source101..112 intervals each57Present events. ON/OFFready->Present med
37.9678/37.9307ms; Present intervals30.5717/30.5416ms. Still substantial waiting
with ROI disabled. OFF later CU screenshot GAME OVER, not independently known
alive throughout source interval: PRESENTATION-TIMING control only, NOT OFF
first-response/zero observer cost/live-input-equivalence pass. Don't rerun to
enlarge native samples. Next shadPS4 same-scene actual response. Original
receipt native-ketsui-manual-first-response-r2.json remains immutable; new
ketsui-present-pixel-control-r1.json SHA
4a0d0ced2941d4d5fc8cd5bf0247b2286ccc3d8717cbc9b4ad1d638ffdb6613a
pins both traces/launch receipts, same binary/config/contract and ready IDs.
Tool compare_ps4_present_control.py. OFF raw SHA
00851e608c1206775ef6d3dc17a9f2ec345672f5b1a0cd12e182053a94a2d277;
discovery ketsui-pixels-off-discovery-r1.json. 5066complete callbacks, all12
sources within core interval, actual core direction+Square states recorded.
OFF profile logging run-20261028T092552663Z-32448. All games/compilers closed.
154latency tests+10existing pixel tests pass, diffcheckclean. NumPy tests require
bundled Python, not system py3.12 (system missing numpy failure retained).
Node ps4Window is now stale CLOSED32448; reselect fresh returned windows.

Latest Ketsui response-image progress (October28): private native build r9 from
sourceprobe-r10 SUCCESS. EXE28d3e77ac943281e59b6becdcdacc34a1137e1122fc2fbdbca87023eeaa383ff,
GPU7e4519f60a34addc9632a32749503f80b9aa650f1885187ccfedfb3209f6c0c6.
Fixture .local/input-latency-v080-r1/evac-ps4-r9/collection. All processes CLOSED.
New vulkan_pixel_probe.h copies SAME original guest image on RecordSnapshot's
command buffer, same prepare ID; exact ready/Present IDs recorded, same copied
fence proves readback. No extra submit/CPU wait/per-frame IO. Flush only after
original presenter drain and worker join. Production source/data untouched.
launch_ps4_probe.py --pixel-region x,y,w,h,capacity opts in; command90 starts,
command1000 stops. Native host exports CurrentTestCommand. ROI320,360,640,320,300.
R1/PID129260 CLOSED98212complete,146pixels0overflow; overlaps entrance animation,
NOT accepted first response. R2/PID122820 CLOSED97240complete,145pixels0overflow;
actual ARCADE/Type-A/Stage1,12/12 source->PadRead->input deliveries paired.
Only command101 response manually accepted. Raw images12..17 stationary;18first
right4px;19/20/21 continue8/12/16px. Invincibility blink phases use separate
stationary templates14/15. Primary body feature205,238,28,18 passes all12..21;
secondary213,225,14,16 passes even12/14/16/18/20. Do not treat uncertain odd
cockpit tracks as passes. Snapshot18 READY but NOT SUBMITTED;19 is first
submitted response, PresentBEGIN77627. Source77567/input delivery end77576.
Same one response breakdown: source->delivery3.6221ms; delivery->first response
handoff16.0957; next-response handoff16.576; handoff->Present30.8286. Total source
67.1224ms/APIreturn63.978/core-input-delivery63.5003. Handoff is AFTER original
pacing+copy fence, NOT earliest GPU completion. No actual CPU port load/physical
frontend/OS display/photons, no P95/4V claim. Theory remains144Hz/screenCENTER.
Receipt native-ketsui-manual-first-response-r2.json SHA
0a6140e994ad8c3d6cf254e4e798d3b03bf637b5ade9675b6dbe0aa82c0562be;
review_ketsui_pixels.py revalidates hashes, input pairs, two features and exact
image/event ownership. ketsui-pixels-core-input-r2.json holds all12 pairs.
New16 tests pass;154latency-pattern tests total. Rawtrace SHA
d4fa8c68f553c500780d3e27882da76e7e422ff56b0587c16a0f463fbe4a0f5c.
During R2 source interval ready->Present med37.9678ms, Present gap30.5717ms;
older pixel-OFF buildr8 med38.3082/30.5208, suggesting not only new ROI copy,
BUT different binary does not replace SAME-BUILD ON/OFF. Next do one bounded
OFF control on SAMEr9, then shadPS4 same scene first response; don't redo native
to enlarge image counts or move to other PS4 prematurely. All8 scope active.
Navigation correction: R1wait5.5s after Type-A was still entrance! R2wait8.5s
then command90 zero ->250ms ->12direction+Square60ms/gap97 ->300ms ->1000zero.
CU verifies after sampling. First noticesB3/B4; B5Arcade; B6/B7notices; then
Y8 coin->400ms->Start9->700ms->X10 Type-A. No pause before sampling.
Vulkan reader originally wrongly required next-ready AFTER previous Present;
valid pipeline can have next-ready BEFORE prior Present. Correct rule retains
each exact ready->Present order and Present order. Test covers this; no image
identity gates relaxed. 80/146 R1 images actually submitted; rest ready replaced.

Latest Ketsui core-input progress (October28, after the four360 representative
rows): ORIGINAL callback table50038 slot50088 ->module6610, main eboot caller
29626/[rax+50], return29629 with ignored return value. Original module6610
delivers P1/P2 through stub2F8 ->integration01400+5C0A0 BEFORE core29610.
Digital type3 buttons+8/+9, right100/left80/Square1. This is INPUT DELIVERY,
NOT yet the emulated arcade CPU port load or first response. Audit
ketsui-core-flow-r2/report.json SHA656c9c1e...2dc7b0 pins original files,
relocations, NID/library/module and ordered instructions/caller ABI.
Private native R8 EXE381e673675ff99e52bb4b8f006004ab280a49968e78a66e71c3d2de557772d4f,
GPUa1abfc8b46b5095ac5f3c7dfbdc4ebe0f8cfafb362db9f4daf5eb6b19f30e1f4;
build .local/input-latency-v080-r1/evac-ps4-build-r8, preparationprobe-r9.
Stage .local/input-latency-v080-r1/evac-ps4-r8/collection; profile is a verified
copy of stopped private evac-ps4-r6/default. Launch helper launch_ps4_probe.py
uses full FIVE-argument collection contract, capacity1m and receipt; no online.
R2/PID72980 CLOSED normally,122290complete0overflow,4515callback pairs,
9030input deliveries. CU actual ARCADE/Type-A/Stage1, same pulse masks4008/4004;
35/35 source IDs pair via LAST returned PadRead report, same thread's FIRST
following original callback, exact actual digital bits. No global nearest-time
pairing, no replacing a bad first callback with a later complete one.
Publisher->PadRead med/p958.5868/16.3195ms; APIreturn->delivery.6439/.9869ms;
paired publisher->delivery9.0682/16.8107ms. Callback walltime1.0844/1.7243ms.
Not physical frontend/CPU-only cost/response image or a V result. Report
ketsui-core-input-pairs-r2.json SHA
567d9402bfd7ddfa3d8a60c609b95df9fcc3c7cd83d37ed7ac08f7e6ebe09852.
11mechanism tests pass. scripts/ketsui_core_latency.py also checks actual
eboot image base/hash from stdout.jsonl against original audit, caller29629.
R1/PID131776 CLOSED normally112395complete;4087callbacks stopped BEFORE all35
sample inputs (paused/menu), all excluded; ketsui-core-discovery-r1.json retained.
First native import observer only handled late RebindImports, but this module
was bound immediately during Load. prepare_ketsui_core_probe.py now wraps BOTH
initial and late GuestExport paths, plus only the pinned callback data slot;
no original code instructions overwritten. Never redo R1 as a gameplay result.
Navigation: Start skips opening/title; Circle/B acknowledges offline notices;
after actual Arcade ready, Y=coin ->400ms->Start ->700ms->X=Type-A ->5500ms->
35 direction+X pulses,60ms hold/97ms gaps. DO NOT inject another Start/pause
before sampling: it was delayed relative to CU screenshot and caused R1 failure.
Core end f=100000000 records raw descriptor words+8/+12; their semantics are
UNASSIGNED, NOT image width/height! No accepted CPU bitmap/pixel capture yet.
Next: keep focus Ketsui; follow actual core port use and output image ownership,
then shadPS4 same scene; only then other three PS4. Reuse current evidence,
do not repeat failed ETW/UAC approaches, do not ask away human for gameplay.
No live game/compiler/collector. Full eight-game goal remains active.

Latest Guwange representative comparison (October28): actual original P1 port
D00011 load882CE5E0/after882CE5E4 in arcade CPU88020000, original caller821CCA78
(LR821CCA7C), now linked through exact type0/type1 queue slots and physical
VdSwap packet to owned Present. Do NOT call the ~17ms API->core-owned output
the first responsive image! Native R3/PID128112 ON20/20, R4/PID48100 OFF35/35;
Xenia R6/PID106228 ON35/35, R7/PID127068 OFF35/35. All CLOSED normally, complete
zero-overflow; raw397029/388554/688774/461756. Same EXE/modules/config within
each ON/OFF pair. API->owned Present ON/OFF med/p95 native17.012/17.292 and
17.047/17.457ms, Xenia16.811/16.840 and16.811/16.961ms. Controls only establish
no added whole output cycle in this internal path, not zero observer cost.
Actual pixels212/353 complete0overflow. Explicit manual command101: native
owned17, body unchanged16..18, FIRST right-facing step19 and continuation20..21;
Xenia owned16, unchanged15..17, FIRST18 and continuation19..20. BOTH offset+2!
Port->first-response Present46.2518/46.7818ms; GetState return->first-response
50.2032/50.1417ms, about3x60Hz cycles in THESE examples. Each one manual response,
no first-response P95/all-mode conclusion. Original NCC gates ALL reject; retain
them. Two templates only corroborate stationary pre-response body, NOT the
low-confidence positions after walking pose changes. review_guwange_pixels.py
and compare_guwange.py revalidate current source hashes, identity and clocks.
Aggregate guwange-comparison-r1.json SHA
94206671aa9b49a48d0217c4436789bdbcaff8752e0e9ee59b53a9ef2e41bcf4.
Native private EXEbc8fc7e0...743d47/moduleec7eda40...b63be1, declaration/hooks
codegen only; production source/data and frozen ZIP unchanged. R1/PID106152
failed startup: private IJKL mappings conflict with Guwange right-stick defaults;
stage now uses original arrow directions for this game. Preserve failed fixture.
R2/PID103712 native35 internal chains are valid but player obscured; no pixel
acceptance. Xenia R5 wrong ROI and R4 paused pulses remain excluded. No more
Guwange rounds to enlarge subsets. Next is Ketsui/remaining four PS4 actual
core consumption and first-responsive-image comparisons; all eight scope stays.
Device/frontend, current OS display and panel photons remain separate gaps;
144Hz/center/theory display column retained. No game/compiler/collector active.

Earlier Guwange discovery, superseded by the representative comparison above:
Latest Guwange discovery (not a latency row): prepare_guwange_xenia.py pins28
original register sites to census-r3 sections/opcodes and current XBLA0.0.0.4.
Generic Xenia22aa6a81 reused; no build. R1/PID126480 CLOSED but OVERFLOW:
523444658reserved/2000000committed; site16 was wrongly inside Sleep(0) loop.
Never use its timings. Moved16 to821F74A8 before flag store, not821F74AC.
R2/PID127568 CLOSED normally;217144complete0overflow,4/4 sources reachAPI.
Dynamic main-vcall821E9664 ->821EBA10 is only li3,1/blr, NOT core update!
Queue producers821F7620/821F7720 and draw-binding821F6628 confirmed. No module
88020000 entry yet, no player stage, no latency accepted. Receipt
guwange-control-targets-r1.json SHA98a36e7b...165a2e.
R3/PID54132 same EXE/config with --output-only (omits GUEST_SITES env) CLOSED
normally;69318complete0overflow,3/3 sources reachAPI, final CU screenshot DID
reach main menu. Do NOT call Xenia incompatible/frozen from earlier splash
screens. guwange-output-only-r1.json SHA42a68236...7a50cc. Both runs only basic
navigation; no image response/probe-cost claim. New launch flag recorded per
receipt. Two Guwange-site mechanism tests added (loop exclusion/unique sites).
Next: reuse isolated xenia-guwange-r2 (check saved profile/content before assuming
it was created) for bounded core-entry/caller discovery in Arcade/Shishin/stage1; do not
keep enlarging graphics-only sample counts. Module export4=88020000 is arcade
CPU execution loop; site30/31 can identify its real caller after module loads.
Then native Guwange measurements; four PS4 still required. No active game.

Latest SDOJ current comparison: native1V ON/OFF35/35 each, core->PresentBEGIN
median/p9516.792/17.169 and16.784/17.103ms. Host-only private EXE659318bd...c9288a;
original game DLLs unchanged, identical EXE/config both runs. R1/PID129280 and
R2/PID109524 CLOSED normally;29090/44818complete0overflow; native observers
109386/79246continuous, no budget rejection. Driver publication packet is NOT
the original InputSystem change counter. evac_sdoj_latency.py joins exact
original pass/thread, synthetic sample ID and buttons before first core copy.
No replacing an unpaired first core with a later complete core. Native352ROI
complete0overflow; explicit visual command101 frames13..15stationary,16first
right,17..18sustained, two body features agree<=1px. Manual17.207ms,offset0,noP95.
Automatic NCC gate unchanged/ALL35rejected. review_sdoj_native_pixels.py records
only that one visual response; receipt SHA43511c44...68a1c2. Native direction+x
maps24/20 versus Xenia72/68 (extra-button mapping differs); direction verified,
not identical shooting workload or subms comparison. Native7mechanism tests.
Xenia original3V remains OFF60/ON35,49.89785/49.989 and49.9049/49.9981ms;
manual command101 frame18=49.8626ms,offset0,noP95. All Xenia processes CLOSED.
Aggregate compare_sdoj.py --current-native => sdoj-comparison-r2.json SHA
27d76b9d550c6339edd204626d196b31a0910c1103bd353f1ba9f286ae9f446c.
Revalidates both native earliest-core reports, old pilot, history and Xenia;
keep historical builds separate.113latency-pattern tests+3pixel tests pass.
Next: Guwange then four PS4 internal/core-first-response comparisons; do not
redo SDOJ/Futari/G2 to enlarge image subsets. Device/frontend and current display
remain separately marked gaps. No game/compiler live; frozen ZIP unchanged.
Use launch_xenia_probe.py: correct TEST_PAD and5fieldROI, freshpaths, pinned
binary/sites, per-run receipt. SDOJ old wrong TEST_PIPE R1 and malformed4field
ROI R2 failures retained. No publication/push/upload authorized.

Latest G2 quantitative progress (October28): native default2V and experimental1V
now paired through earliest matching original core input -> synchronous update /
draw -> actual VdSwap packet -> owned Present. Private build r3 SUCCESS, EXE
eb246244...25b9c09/dat12 DLL61381351...69f169b; same binaries both modes.
R2/PID103696 and R3/PID24968 CLOSED normally, raw339356/174841 complete0overflow,
60/60 each. Core->Present begin median/p95:2V32.0215/32.9835ms,
1V16.96675/17.3231ms. Pixel ROI562/562 complete0overflow; first sprite responses
32/35, larger-template31/34, ALL offset0. Native pixel-OFF R5/PID78576 and
R4/PID102352 CLOSED;52/50 gameplay sources, core med/p9530.962/32.3735 and
16.9172/17.4309. Remaining8/10 after game-over rejected. Different stage start
offsets, so only no-extra-whole-cycle control, NOT zero observer cost or exact1ms
causation. Synthetic input is not physical/Windows frontend; no photon claim.
Artifact .local/input-latency-v080-r1/g2-native-comparison-r1.json SHA
278b5064b669adb709212e1b3f593b731efd75db020611762649d2c07848d6a7.
New tools prepare/rebuild/stage_g2_probe,ppc_functions,compare_g2_native and
scripts/g2_latency.py;6report+4build tests pass. Build reused objects,2BelowNormal
workers; production sources/ROM/frozen ZIP unchanged; no processes remain.
Original G2 VdSwap call is82170F84 ->8237BBEC, verified census import; initial
R1 wrongly used82170EF0 (different video import), no GuestSwap links, REJECTED.
Do NOT redo it. R1 pixel data valid but not accepted latency. G2 site3 at
820CFDA8 is not on these actual cycles; reporter follows confirmed core callback
82091690 and checks returned directional bits, not that optional callsite.
G2 Xenia same-scene comparison now captured with pinned generic observer EXE
22aa6a81...b4db9, original Canary291f0c9 and NO1Vpatch. R3/PID115240 ON and
R4/PID115132 OFF CLOSED normally;320240/446469 complete0overflow,60/60 internal
chains each, actual flags0x0C000000=2V. Core->Present begin med/p95:
31.4077/32.4114 and31.6789/32.486ms. ON pixels561complete0overflow; body7 and
larger2 first responses ALL offset0, noP95smalln. R2 excluded: dead before
sampling; R1Hidden startup had no usable window/flushed capture. No more reruns.
Aggregate g2-comparison-r1.json SHA b00fdd790dc75be2343a78e432f591285ba9c08e56c08a92fa687ecd3782513e;
compare_g2_xenia.py revalidates source identities and earliest core. Native2V
and Xenia both ~31-32ms, native1V~17ms; subms difference not a fixed win.
Main comparison table now explicitly includes144Hz/center/theory pixel-light
column, unknown panel D, origin image-ready NOT CPU Present. Device/frontend,
same-response OSdisplay and physical photons remain unmeasured.
Next: SDOJ/Guwange/four PS4 internal response links and emulator comparisons;
do not redo Futari FD/BL or G2 measurements to expand small image subsets.
All eight games remain in scope; Futari FD/BL extra measurements still cancelled.

Latest offline progress: all four PS4 cache resets are linked to engine phase80
BEFORE the state update(slot28), including task completion. Original per-title
virtual-table relocation, constructor registration, node adapter, reset bytes,
normal/deferred dispatch, drain->update ordering checked. The drain's original
PLT/GOT imports are SignalSema4czppHBiriw and WaitSemaZxa0VhQVTsk in all four;
native_semaphore.cpp implements actual waiting. Artifact is .local/input-latency-
v080-r1/ps4-static-pad-r1/reset-phase-r2.json (retain r1). Loop/reset/update:
Ketsui19bc20/1ea7d0/19bd30; DOJ192b30/1dc1f0/192c42;
ESP1caae0/216f20/1cabf0; Feveron192c40/1dc710/192d79.
This rules out a fixed old-input frame in this OUTER cache phase; it does NOT
establish emulated core consumption, first response, total V or photon time.
No game run, compiler, production change or publication. Next missing PS4 link
is state/module input consumption -> responsive draw batch. G2 default/1V still
need quantitative pairing; do not replace it with replay-pacing evidence.

Offline PS4 follow-up now has original per-title input boundaries, no new game
run: .local/input-latency-v080-r1/ps4-static-pad-r1/{report,cache-check}.json.
LLVM23 decodes original ELF/locally reconstructed plaintext SELF; import GOT/
PLT and actual call displacement checked. PadRead wrapper/call addresses:
Ketsui1ea660/1ea6d1, DOJ1dc0c0/1dc105, ESP216dc0/216e31, Feveron1dc5a0/1dc611.
All four request63 reports and use+0x1d cache-valid guard, with cached return
bypassing PadRead;16 key encodings validated. Reset cadence/core consumption
and total V remain unproven. Do NOT infer63frames or one new USB sample per
button getter. Original data and frozen release untouched; worker99566 ended0.

Latest HUMAN addition: final comparison MUST include the pixel-light endpoint,
explicitly labeled 144 Hz and the reference screen position (center for theory).
Separate measured OS-display timestamps from scan position and unmeasured panel
processing/response D. Ideal ready-image models are 3.47ms+D immediate versus
6.94ms+D empty-FIFO at fixed144Hz/center/uniform phase. These start AFTER the
image is ready/adoptable, not at CPU Present. Do not add them wholesale to old
ETW display tails or treat unknown D as zero. See ANALYSIS display budget.

Latest HUMAN scope clarification, October 28: no more Futari Frame Delay
variants or Black Label measurements. Hand off Ver1.5/Xbox360/Training/Stage1/
Original/RecoNormal actual1V row, with frontend/display gaps clearly separated,
then continue the remaining seven. Theory using valid old evidence remains
authorized; do not wait for new display capture to do offline analysis.
Guwange read-only audit is now .local/input-latency-v080-r1/
guwange-static-policy-r1.json: original input and section identities checked,
17 functions/17 ordered sequences/234 direct branches. Confirms input refresh
before main update and renderer VBlank callback gate before swap; does NOT
establish a fixed total V or milliseconds. PS4 shared input/ready/WSI and
display-refresh conditional budgets are now documented in ANALYSIS. No new
game run, production change, capture, build, release or publication this turn.

### Input-latency comparison reopened — October 28

Latest HUMAN direction supersedes the analysis-wide wait below: proceed with
theoretical analysis using prior valid measurements. No new game/capture/UAC
needed for this step. Keep current measured, historical measured and conditional
models separate; do not discard sound old evidence solely because of its version.
Futari presentation source blobs are identical at v0.7.5 and c427257/current:
d3d12_presenter.cpp2822c244...3d53, presenter.cppda3e39d1...fe1. GPU command/
resolve/waits changed, and OS presentation mode is environment-dependent, so
unchanged policy is not proof of identical current milliseconds.
ANALYSIS now has a conditional mean-budget model starting at game-readable state:
uniform60Hz phase8.333ms + current core->Present mean17.045852/16.834278ms +
historical display-tail mean0.918538/11.178276ms => roughly26/36ms under old
Independent/Composed Flip paths. NOT measured totals, NOT summed medians, no
model P95, excludes device/USB/pre-readability Windows/SDL and panel photons.
All eight-title deliverables remain in scope. Empirical display gate below
remains unresolved, but it does not prohibit the newly requested theory work.

Current external gate after three consecutive goal turns: same-response OS
display remains unavailable on the current token. All ordinary full/display-only,
PM2.5.1 and before-window initialization alternatives failed; no collector,
PresentMon service or reusable task is live/configured. Read-only provider ACL
query etw-access-check-r1.json reports not elevated and error5 for all four
providers. This is NOT proof of runtime event access denial; do not assert that
elevation is a guaranteed fix. DXGI submission collection remains proven.
User explicitly required finishing Futari before moving to other titles. Current
safe continuation requires a working display-feedback capture environment,
probably a human-confirmed bounded elevated session as used historically. Do not
automate UAC, change permissions/security, reboot, repeat the failed combinations,
or fill current missing display data from old medians. Preserve all eight-title
scope and unmeasured hardware boundaries. No active game/compiler/collector.
Goal is to be marked blocked pending this external condition, NOT complete.

Latest window frontend follow-up: Xenia keyboard private build R1 SUCCESS,
EXE1909642d...113668b. Only presenter+input-system objs/libs privately relinked,
all original objects unchanged. Test command>=900 now passes to original drivers;
navigation<900 unchanged. Generated copy, not pinned-source or keyboard logic edit.
window R2/PID67680 CLOSED normally; CSV194367 complete/no overflow,
SHA18c4271c...93655, ROI297 complete/no overflow. Real arrows bind DPAD22546/22547;
12 presses held0.3803..1.5879ms; 1 observed by original guest API,11 confirmed
missed after release and subsequent reads. Sole time0.0659ms is OVERLAP-CONDITIONED,
NOT typical latency or comparable to native retained-tap median7.625ms. Character
is dying at that press (ROI218..232), so no first-response/Present acceptance.
Report xenia-futari-window-events-r3.json; original12/12 native taps remain valid.
Window R1 had right-thumb bindings+209 ROI overflow; rejected, retained.
No more rounds of these sub2ms CU taps to chase samples; a held-input mechanism
or different measurement source would be needed and must retain its proper origin.
Normal-user display collection BEFORE game/window creation also tried: native
R13/PID103900 CLOSED normally,28971 complete records; timed30s PM127724 ended,
still no display CSV. Do not repeat that startup-order combination. All owned
games/compilers/collectors gone. No release/source-data/security settings changed.
Original Xenia config IJKL preserved (temporary arrows were CLI overrides only).

Present->display follow-up: native R11/PID127664 and R12/PID38064 CLOSED
normally. Current non-admin PresentMon2.6 full/display-only and official2.5.1
display-only produced no CSV; no-display2.6 DOES capture DXGI calls. Account
already belongs to Performance Log Users. Do not infer the exact cause from
the generic elevation warning or keep repeating unchanged failed captures.
R12 ROI586 complete/zero overflow; all586 ETW entry+return pairs fall inside
their own instrumented Present call intervals (not nearest-frame matching).
futari-etw-same-image-submit-r2.json; etw control chain60/60 actual1V. No new
first-response pixel acceptance, no display feedback in this run. Tools and
13 focused tests in scripts/present_frame_join.py/tests/test_present_frame_join.py.
Historical valid v0.7.5 vs officialXenia291f0c9 first-stage replay ETW reanalysis:
539/598 displayed rows; Present->OS display median0.3779/11.31465ms,
p952.57066/14.14793ms, different Independent/Composed Flip paths. Reports
futari-historical-present-display-r1.json and xenia-futari-historical-present-
display-r1.json. DO NOT add these historical distributions to current input
totals. Current Futari same-response OS display remains open; all eight-title
scope remains active. No security settings, production runtime or frozen ZIP
changed. Official PM2.5.1 download SHA9bec3083...c80a191, Intel signature valid.

Latest Futari result, October 28 (supersedes the pixel-pending notes):
native R6/PID118976 and Xenia R9/PID114092 are CLOSED normally. Same Ver1.5/
Xbox360/Training/Stage1/Original/RecoNormal, all60 actual1V,60/60 first-core
chains. Native ROI569/Xenia570 complete/zero overflow. Body tracking validates
19/6 first directional responses; all on the owned first output (offset0).
Large-template independent check12/4 also offset0. Reports final-r1 and pixel
response reports are in .local/input-latency-v080-r1; full conclusions now in
docs/ANALYSIS.md#虫姬二输入延迟结论. Do not call all60 pixel-verified.
Pixel-OFF controls native/Xenia R10 CLOSED,60/60 each; core->Present median
16.9839/16.8252ms,p9517.680/16.8912. ON16.95505/16.8292ms. Same EXEs within
each ON/OFF pair. This supports the1V conclusion, not zero observer cost or
a ranking at0.1ms. Native list processing before FrameDelay branch0.4316ms;
branch->PPC VdSwap call15.9807ms (wait plus resolve/swap preparation).
Native real-window frontend R9/PID96160 CLOSED, trace69841 complete/zero
overflow; Arcade Training Stage1 Original RecoNormal1V. Twelve window presses
all pair to exact next original PollFrame count/buttons; median7.625ms,
range0.615..13.144. Eight first directional pixels pair to same-image Present,
median28.85945ms,range22.73870..30.22140. No P95. Not merged with HD stages.
CU Right/Left actually emit VK_NUMPAD6/4; private fixture binds those. Earlier
IME/unbound presses and Xenia R8 (missing EVAC_LATENCY_GUEST_SITES) are rejected.
Frontend probe host now passes through real configured input for command>=900;
901 enables original keyboard with pixel gate,1000 disables pixels. Production
host/source not changed. Do not mistake this helper change for player input fix.
Xenia pixels EXE22aa6a81...b4db9 reuses all original inputs unchanged, private
presenter only; quick_exit flush verified. All owned games/compilers are gone.
Frozen release unchanged. Physical USB/SDL-device timing, Xenia real-device
frontend and scanout/photons remain unavailable; Present is the measured CPU
endpoint. Other seven comparisons remain required. Keep Futari as the focus
until its report is handed off; do not restart the failed pilot loops.

Latest user direction: complete Futari end-to-end analysis FIRST. Do not launch
or expand another game's measurement until Futari's input/logic/response image/
presentation chain, Xenia comparison and measurement controls are resolved.
The other seven comparisons remain required afterwards, not cancelled. Current
action: bounded asynchronous D3D12 pixel evidence in private builds, using the
existing Futari core/list/swap identity rather than nearest-time correlation.

Latest input diagnostic: trace-OFF/test-pad-OFF native PID129468, run
20261028T022050161Z-129468, with existing 120ms keyboard diagnostic taps, DID
insert a coin with V, select Type-A with Return/C and reach player Stage1/CONTINUE.
Normal Alt+F4/exit27; closed. This was functional only while shad compiled.
Real successful edges returned two PadRead reports; synthetic held inputs had
mostly one. Private r7 fixture adds a synthetic pending latch to the original
ReadReports edge/ack flow; its ten mechanism checks pass, build r6 succeeded.
No production code/data changed. The later R5 below validates basic gameplay
navigation with the latch, not input-to-first-response latency.
shadPS4 r3 build session36182 is terminal SUCCESS, EXE SHA
189ea3c05a6a385385c37c4929c49a3f1920f08ad1fedb7facad391246cb5523;
fresh offline stage shad-ps4-r1 exists. The historical live-build notes below
are superseded. Native r5 capture is currently in evac-ps4-r6/collection,
PID123216/session62310 is now CLOSED normally (terminal exit1/code27).
Trace evac-ketsui-probe-r5.csv:65481 complete/zero overflow,14/14 API input pairs,
SHA7395036f7e746fb61bd0bc71542b66393aecd525b3554b8e000094ab049edc45.
The pending latch DID allow coin/start/Type-A/stage1/shooting; command101 was in
player gameplay. Output figures remain mixed navigation, not latency acceptance;
window foreground was not explicitly fixed. Use sky.activate_window next time.
shad build r4/session44199 is SUCCESS, EXEbb97b9aa...f11b8f0, stage shad-ps4-r2.
Synthetic input now follows original controller queue and timestamps (kind27),
not a fabricated single PadRead report. Only handle1/user1000 opened in R1.
Shad R1/PID128008/session14442 entered ARCADE/Type-A/stage1 and shot/moved with
commands101..160; normal exit0. Trace OVERFLOW109137: invalid timing capture,
retain it. R2/session34533/PID105212 is CLOSED normally (exit0). Capacity1048576;
shad-ketsui-probe-r2.csv has429640 complete records,zero overflow/incomplete,
SHA8beddb5f577fd6e049b4da68e93268fa4b2ec491147a6252092bfb322da364d8.
Foreground explicitly activated. ARCADE/Type-A/Stage1 was shown before101 and
after160; commands101..160 (80ms left+x/right+x,43ms gap) all reach PadRead.
Report shad-ketsui-gameplay-r2.json:60/60 sources,240 owned outputs in that
window. Source->API median15.4809/p9533.7239ms; prepare->Present30.5613/64.3346ms.
These are SEPARATE chains, not input->response totals. No first-response pixels,
game consumption, display or probe-OFF evidence. No owned game/compiler running.
Analyzer82 tests pass; shad patch reverse-check passes.

Latest PS4 checkpoint (supersedes older PS4-not-built statements): native private
build r5 succeeded, using generated fixture evac-ps4-probe-r6. No production
objects, source bodies, frozen package or original data changed. Four source
objects are compiled and linked privately; expanded Ninja linker commands are
checked for redirected outputs and actual replacement-object participation.
Ketsui R4/PID72648 closed normally in exec session12644 (terminal exit1 from
original code27), stage evac-ps4-r5/collection, trace evac-ketsui-probe-r4.csv.
Navigation IDs1..19 only. No actual player gameplay: a one-second Triangle/CREDIT
hold and Options still did not start; applying another preset then default and
saving did not fix it. UI verifies Triangle/CREDIT. Do not repeat blind coin taps.
118184 records complete/zero overflow,19451 PadRead calls,10767 owned images,
19/19 synthetic inputs reach API; SHA1ee5a0428a6337c07f961ed96de34dd12fccd8978adc4d8bf7d8a5cda8f09ea8.
Report evac-ketsui-menu-r4.json remains menu/demo ONLY, no first-response claim.
No owned game is running. Close future captures normally, never terminate them.
Flush is now before original normal-close TerminateProcess27; R2's zero-byte
trace is invalid and retained. R3 menu/demo CSV45812 complete/zero overflow,
7461 PadRead calls,4178 owned output frames, SHA6b9d3e9b...24957. It is NOT
gameplay latency. Caller0x101ea6d6 requests63 reports; direct synthetic API
override navigated menus but did not coin in arcade. Current r6 instead samples
synthetic buttons into the original history cadence, with timestamp/button/source
sidecars, preserving returned report counts. This fix is not gameplay-validated.
The real keyboard frontend R1 trace45794 complete also remains menu/demo only.
scripts/ps4_latency.py keeps API input and image ownership as SEPARATE chains;
neither is proof of the first image responding to the input. 80 latency tests pass.
shadPS4 r2 build stopped2064/2363: missing default WIN32/_WINDOWS defines in
measurement CXX flags selected POSIX code. Build-ShadProbe.ps1 now restores
them; r3 is LIVE in exec session36182, shad-probe-build-r3.log, last334/1300.
Poll SAME handle, do not duplicate. Two jobs BelowNormal, no gameplay measurement
while it runs. HAVE_ENDIAN_H=0 and /EHsc are also required. Pinned source
529ceb32f5546b820588f194b903cc39d08015fe; refreshed7-file patch reverse-check
passes. Its direct synthetic API path still needs matching-history review.
All evidence under .local/input-latency-v080-r1. Actual input->response-image,
frontend/display, probe-OFF controls and all-title comparisons remain open.
LLVM23 llvm-objdump directly disassembles the original ELF PT_LOAD segments,
even with a no-section-overlaps warning; no capstone install needed. Ketsui
eboot SHAa32b4b79...ac073: scePadRead caller0x1ea6d6 is inside0x1ea660, reads63
reports into this+0x28, caches count at+0x18/valid-at+0x1d; connected check+0x74,
connected_count+0x90, information at+0x1db0. Runtime base0x10000000. No runtime
observation of downstream core consumption yet; original code unmodified.

Latest checkpoint (supersedes the historical r7/test-pad-not-wired notes below):
Xenia r9 build is terminal SUCCESS; EXE SHA309d1bb1...31cf. R6/PID42064 and
R7/PID129404 both played Futari Ver1.5 Xbox360/Training/Stage1/Original/Reco
Normal and closed normally. R6 saved native 1V; R7 cold-read it, all60 selected
presses actual1V. R7 CSV184254 complete/no overflow/no invalid sites, SHA
9b8edbc6b1d94f13a60dcdadbfef2e230892de868c7b07432149a7e637303de1.
`xenia-futari-onev-chain-r2.json`:60/60 first core samples paired through original
completion owner + draw-list pointer/count/owner/lock + swap/mailbox to Present.
Game input->Present begin median16.8242/p9516.9077ms; synthetic publish->Present
24.67105/31.8589ms. NOT Windows/SDL/device/display/pixel proof. All4932 DXGI
stats queries unpopulated. Probe OFF/official baseline controls remain open.
The refreshed Xenia patch reverse-check passed. Shared probe21 + Futari7 tests
pass. Native-log adapter7 tests pass, including teardown after trace closure.

EVAC private build session62319 is terminal SUCCESS (208/208); no compiler or
owned game is running. Preparation r2 generated only default/reco_abnormal with
17 read-only hooks and original compatibility declarations, no translated-body
edits. Native clock-check-r1.json passed1000 QPC/FrameTiming brackets, zero
failures, max1us. Stage is evac-futari-r1/collection under the private root.
IMPORTANT: do not copy Xenia container headers as a native profile. R1 could not
read those settings; it used different button mapping and ended in GAME OVER.
R1 is structurally complete but REJECTED as a same-gameplay comparison.
For R2, after R1 exited, retained the game-created native header and copied only
TEST savegame.bin from the stopped Xenia fixture (SHA c078a82b...bc88c39).
Stage script still needs this workflow correction before another fresh fixture.

EVAC R2/PID78660 closed normally and is gone. Run20261028T013714965Z-78660;
CSV evac-futari-probe-r2.csv SHA e7855977206dd735c15f90ca7bb39dda367a98bb33488c547132f40c89ab99db.
206480 native records complete, zero overflow/incomplete; adapter joins28860
production output records, excludes18 AFTER the probe interval at teardown.
evac-futari-onev-chain-r2.json pairs60/60 first core samples, all1V, actual core
masks65553/131105 match Xenia. Same Xbox360/Training/Stage1/Original/RecoNormal;
before/after screenshots show live gameplay, but deaths/first response pixels
are not independently classified per press. Core->Present begin median17.00825,
p9517.562ms; synthetic publish->Present25.83525/32.996ms. Not Windows/device or
display latency, and not a final performance ranking (probe OFF controls open).
Native VdSwap probe is entry, Xenia return: compare common total endpoints,
not the individual VdSwap-adjacent subdivisions as if identical boundaries.
Next: scene-selection receipt, stage profile fix, probe-OFF/ON controls, actual
input frontend and corresponding first response/display evidence. All eight
complete comparisons remain open; preserve the frozen release and raw R1 failure.

The active goal is to complete the promised eight-title input-latency analysis.
The frozen v0.8.0 package below is not evidence that this requirement is done.
The existing ANALYSIS table contains policy/source audits and partial historical
measurements; the all-title paired software-input/output and emulator comparison
is INCOMPLETE. Keep the frozen ZIP and its accepted extraction unchanged.
Measure current EVAC versus pinned Xenia/shadPS4 in isolated fixtures, with
default/optional modes separate. Correlate each input edge, actual game
consumption, its resulting image and that image's presentation; report paired
totals, median/p95, drops and coverage. Physical switch/USB and panel photons
need hardware evidence and must not be fabricated. Software events, emulator
policy, replay duration and frame pacing are not substitutes for the missing
paired measurements. No publication/push/upload is authorized by this goal.

Latest measurement work is in `.local/input-latency-v080-r1`. Frozen SDOJ host
3009f01e / GPU287f7a4f / runtime8f318616, run20261028T001153607Z-113580,
normal Alt+F4 closed. Arcade HD/Training/Stage1/TypeA/Shot/1V: three live-player
direction presses (9309881494138, 9309885841627, 9309890163511 us); fourth
9309895856544 is CONTINUE and excluded. Window event -> identified Present begin
median29.042 ms, range25.693..31.081; return29.214 ms. Pilot only: no P95,
first changed pixel or display. Source log rotation lost prefix; accepted local
chains are intact. Reports `sdoj-onev-{probe,probe-selection,paired}-r1.json`.
Use `--log_max_file_size_mb=100` next capture (maximum100), not default5.
`scripts/input_latency_report.py` has seven passing focused tests.

Pinned emulator source/dependency restores finished successfully. Xenia source
291f0c9be398a3e0a0e29f378cbd564fa28c55a9 and shadPS4 source
529ceb32f5546b820588f194b903cc39d08015fe are under the same private directory.
Xenia has a measurement-only patch archived in
`tools/latency/xenia-291f0c9be.patch` (reverse-check passes). Shared Windows
`latency_probe.h` logs opt-in `EVAC_LATENCY_TRACE` to a new path only, bounded
memory/QPC records, exit flush. 104001-record native check complete; overflow
check correctly flags37857 dropped; existing path unchanged. Reporter
`scripts/latency_probe.py` has14 tests, including real press ID -> binding sample
-> API return, missed/censored edges, and swap/mailbox/Present ownership.
No input-to-first-pixel claim. shadPS4 is not instrumented/built yet.

Xenia measurement tooling is being verified, NOT a completed emulator comparison.
October 28 follow-up: r4 built, but pilot R1 produced a zero-byte CSV because
Xenia exits via std::quick_exit. The measurement entry now registers Flush with
at_quick_exit. R2 (PID111216, binary f3f2b726...50796) captured43308 complete
records, zero overflow/incomplete,1881 linked output frames, zero packet-link
errors. Scene was M2/title only, NOT gameplay. Three Return taps lasted
0.8325..1.4972ms and all missed actual guest reads. This is not focus failure.
CSV xenia-futari-probe-r2.csv SHA a3ca8287...c76d9; latest report suffix
-summary-r2.json. Non-admin PresentMon exited normally without a CSV; no display
timestamp evidence came from it.

The new opt-in JIT observer (guest_sites.h/xenia_guest_probe.h) matches module,
address and original opcode, reads four PPC registers via context barriers,
and never writes guest memory. Fourteen parser checks pass; actual JIT/gameplay
acceptance remains open. R3 crashed before the window with c0000374: Ninja had
zero header dependencies, and Processor retained the old sizeof(PPCFrontend).
Correct UTF-8 MSVC include prefix and a checked processor dependency are now
in Build-XeniaProbe.ps1/msvc_includes.cmake. Full repair build r7 is LIVE in
exec session67978 (last observed855/939). Poll SAME handle; don't duplicate.
Old session56466 is terminal. R3 is invalid tool-build evidence, not a game bug.

Standalone Vulkan capability query on an actual Win32 surface reports timing
stages0x5 (queue completion and first pixel OUT), NOT pixel VISIBLE. Device
extension availability alone was insufficient. Source vulkan_surface_caps.cpp,
result vulkan-surface-capabilities-r1.txt. No production backend was modified.
Synthetic guest-only test controller test_pad.h/client.py is prepared but NOT
yet wired into Xenia. Its native acknowledgement/down/release/source-ID check
passes. It is explicitly NOT Windows/SDL/USB input evidence. Probe reporter has
18 passing tests with synthetic sources separate. No games currently running.

MSVC19.44 local versus official workflow VS2026 must be disclosed; compare trace
OFF/ON and official baseline before claiming equal behavior/performance.
Build r1 lacked /EHsc in supplied CXX flags, r2 lacked generated version.h;
both fixed in Build-XeniaProbe.ps1. r3 stopped at missing SPIR-V tools after138
of382 steps. GLSLang16.6.0 official ZIP SHA82bf434e...94a8d9b downloaded/verified,
extracted privately, glslang.exe copied to legacy glslangValidator.exe alias.
SPIRV-Tools uses Xenia's pinned33e02568 source and exact DEPS headers2a611a97.
`Build-SpirvTools.ps1` builds two utilities, then `Build-XeniaProbe.ps1` resumes
incrementally: historical session56466, shader log `spirv-tools-build-r2.log`,
then `xenia-probe-build-r4.log`. Both are complete. Two jobs/BelowNormal.
No edits to sources/headers being compiled during the active r7 build.
Shader tools finished209/209; opt SHA74e63199...4333a6a, dis5198f90c...6c39c8c.
The same live session has moved to Xenia r4 (last observed23/245, kernel sources).
49 latency-named +14 probe reporter tests pass. SDOJ paired-r2 regeneration also
reverified report/source hashes and still selects3/excludes1. Frozen ZIP hash
was rechecked and unchanged. No emulator gameplay capture yet.
Prepared xenia-futari fixture contains eight copied TEST profile/save files and
config:1280x720,vsync,keyboardIJKL,Z confirm/X back,Return start,C/P face buttons,
protect_zero=false. Source original profile and official EXE untouched. No game
is currently running. All eight complete paired comparisons remain outstanding.

### v0.8.0 local delivery complete — October 28, 06:43 local

Final program ZIP is frozen at source c427257bdf45e0e5b57087108739e16342eb894c:
releases/v0.8.0-local-r1/EVAC-PC-LABEL-v0.8.0-win-x64.zip, 110826007 bytes,
SHA256 8a93f2ac042e95f6dcaee7ec30c356d6281dccf59b165ea25ee5ee8644408b3d.
Sibling checksum and actual packaged-launcher screenshot launcher-v0.8.0.jpg
are ready. All SDK/hosts/modules/PS4/launcher receipts name that source. Final
SDK trace benchmark is OFF, native RGBA8 resolve ON, overwrite-upload OFF.
642 source tests (one existing privilege skip), 47 native CTests, 86 PS4 native
checks, compatibility, 32 GPU pixel cases and 275 GPU timing submissions pass.

ONE actual extraction into .local/v080-final-accept-r1/collection verified all
161 payload hashes. No ROM/source/PDB/user-data payload; protected paths have
no archive collisions. Do not re-extract or rebuild this ZIP for status records.
Raw Guwange ZIP -> packaged Extract-XBLA GUI -> Open game files -> copy contents
to game-data/guwange -> launcher -> create new system save -> Arcade/1P/Shishin
stage1, bomb and pause -> normal Alt+F4 all passed. No imported cache/profile.
29 real extracted files match the manifest; source ZIP/container hashes unchanged.
Run20261027T224001339Z-128788 closed22:42:05Z; launcher records exit0, resets
running state. Three native wait summaries have zero timeouts; no error/dump.
Test-only config is offline/windowed/volume0.25/TGFH directions; screenshot
switches language to zh. Original installed files and player config untouched.
All owned game/launcher/extractor windows closed. No upload/push/tag/Release/QQ.
The requested local v0.8.0 objective is complete; stop before publication.
This checkpoint supersedes the older pending-build/onboarding notes below.

### Earlier v0.8.0 integration checkpoint — October 28, 06:26 local

All four Xbox 360 CPU optimization actual comparisons are recorded below and
in TEST_MATRIX. G2 final combination 1V run221857938Z-75992 used EXE8dde3aad,
same Black Label/Seseri/No2; request22:21:02.024Z, crop50..65: CPU380.54ms/s,
GPU3637.97us/frame, present mean16.6669/max17.618ms, no>20/audio0/queue42.7.
Replay returned naturally; normal Alt+F4 and process gone confirmed. No current
game remains. Reports g2-bl-native-onev-r1.json. 2V stays default.

Native XBLA GUI implemented under tools/xbla-extract, static CRT, new-folder
output with shared verified STFS reader. Build xbla-gui-build-r2 and native
XBLA/DLC tests pass. Build/receipt/package and bilingual instructions updated.
Actual raw container -> GUI extraction -> clean final package -> launcher ->
stage1 -> exit acceptance remains; combine with ONE final ZIP extraction.
Version is now 0.8.0 in product/CMake/release wrapper; final same-commit SDK,
hosts/PS4/launcher builds remain. Full source tests running v080-source-final-r1.
No local final package exists. Do not publish/upload/push/tag/message.

### Guwange performance follow-up — October 28

Two original completion waits are implemented and actually compared in
`.local/v080-peers-perf-r1/collection`: callback word 82C0D578 and pending
count 82C0D938, retaining predicates and original native instructions. Only
the verified pending vtable reader 821F7868 is parked. After original writes,
notify; bounded 50 ms fallback re-arms only on that producer. Host C5CD8C01,
GPU a2aa3cda/runtime 8f318616; Blue Data 1, Shishin 22280, Stage1.
Old shared/native baseline CPU 2054.54 ms/s. Candidate 525.17 ms/s, repeat
via original Restart 522.17 (slightly different restart transition, not exact
logical-frame alignment). Same-binary native-wait OFF reverses to 1871.27.
Host-present maxima 18.287 / 17.372 / 17.794 / 17.451 ms; all zero >20 ms
and audio silence. GPU ~5.13..5.29 ms, no confirmed improvement. Candidate
17247 callback/pending waits each, zero fallback. Normal exits confirmed.
Reports: `guwange-blue-shared-control-r1`, `guwange-blue-native-candidate-r{1,2}`,
`guwange-blue-native-control-r2`. Candidate run 215919561Z-126408; reverse
220410523Z-129004. Both are closed; no current game remains at this checkpoint.

Follow-up is now ACCEPTED: queue-wait host A3BAD0CF, same Blue Data1 crop,
ON 236.2637 CPU ms/s / GPU5170.02 us / max17.717ms, same-binary queueOFF
509.8627 / GPU4444.74 / max17.563. Both zero >20ms/audio silence. No GPU gain.
ON run221127352Z-128816 request22:12:49.850Z; OFF221426088Z-131060
request22:16:57.985Z. Reports guwange-blue-queue-{candidate,control}-r1.json.
10,527 queue waits /21,040 notifications/zero timeouts; both completion waits
also zero timeouts. Pause->previousmenu->replay list and normal close passed.
Both are closed (22:14:24/22:18:18 UTC). 500 shared +256 predicate checks pass.
The raw/codegen hooks are verified and 9 Python wiring tests pass. The following
paragraph records the implementation reasoning, not an outstanding candidate.

Further read-only attribution found the renderer loop 821F6AC8 spends ~317
CPU ms/s checking empty queues and native timing, after the two waits. Queue
wait was generated/built in
`.local/v080-peers-perf-r1/guwange-queue-{generation,build}-r1.log`.
It parks only after all native queue locks are released at 821F6E78, when both
ring queues are empty, the cleanup list is empty, no flush is pending and the
verified thread-state reader 8220D948 says running. Notifications follow
original resource/draw/frame enqueues, flush and cleanup readiness. Existing
SignalWait (formerly SDOJ TickWait) was moved unchanged to the shared backend;
SDOJ now aliases it, as with WordWait. Shared mechanism and predicate checks,
generated hooks and actual A/B passed. 28 raw instruction checks also pass.

Native RGBA8 resolve default is now TRUE in source after Futari actual A/B,
GPU pixel validation and G2/SDOJ/Guwange representative checks. Final SDK receipt
must be rebuilt; the current installed SDK still defaults FALSE, tested games
explicitly opted in. Overwritten-upload shortcut remains FALSE.

SDOJ Arrange smoke is complete: run 215139781Z-79440 entered Xbox360/SHOT
stage1, paused, returned to 1P START and closed normally at 21:55:27 UTC.
No frame-delay timeout. This overlapped generation, so not a performance run.

### Expanded Xbox 360 optimization scope

The user now requires optimization of all four Xbox 360 titles in this v0.8.0
round. Finish Futari first, then ESPGaluda II, SDOJ and Guwange. Reuse measured
shared-backend improvements, but inspect each game's actual wait producers,
predicates and module lifetime before changing game-specific polling. Keep
separate before/after evidence for each title, including frame pacing and a
representative gameplay/replay check. Existing timing defaults stay unchanged.
Do not mark the four-title work complete from Futari's results alone. The final
local-package / stop-before-publication boundary remains unchanged.

The latest user emphasizes that Futari's remaining ~198..217 CPU ms/s after
polling removal is the MAIN optimization target, requiring repeated measured
CPU rounds rather than stopping at the easy polling win or small GPU gains.
Existing Arcade split is user157.97..173.99/kernel40.33..43.50 ms/s; this is not
all proven useful game work. Attribute threads/functions, remaining busy waits,
translation/dispatch, copies/state updates, drivers and synchronization first.
Use the new HD No.5 replay for a separately frozen baseline; do not compare HD
against Arcade. Keep benchmark-only/diagnostic cost separate, retain measurable
changes, verify pacing/input/A-V/slowdown, then repeat before moving to peers.

### Futari native-counter performance checkpoint

The default module's native 8216B148 Present wrapper spins on 821F8560 at
8216B244 (1V) / 8216B2E4 (0V); callback 8216AD44 increments it. Default-only
hooks park with WaitOnAddress and wake after the original store. Original
predicate, counter, GPU drain, frame-delay branches and guest registers remain.
50 ms failure falls back to original polling; futari_host_vblank_wait=false
also retains the control path. Thirteen raw instruction checks and generated
hook inspection passed. C++ concurrent/predicate tests and three source tests
pass. Microbench uses the production wait: original ~1016 CPU ms per 60 waits,
address wait below GetThreadTimes resolution, median wake 7.2..7.6 us.
Independent .local/v080-futari-perf-r1 used the same No.3 Ver1.5 Arcade Original
Reco replay. Baseline 118632 and candidate 105616 each completed twice and
closed normally. Request-relative early-stage crops show 1235 CPU ms/s before,
217/198 after (82.4/83.9 percent less). Mean host-present interval 16.663 vs
16.669/16.664 ms; max 17.528 vs 17.725/17.892 ms, no >20 ms or audio silence.
This is short representative evidence, not exact logical-frame or physical
latency proof. Candidate SHA 1a5d89e53147837cb442985d5ad8f569b18301be7071b9f66ede8101bc0fbc32.
Report counter-comparison-r1.json and build-r1.log are under that fixture.
GPU optimization remains open. Optional --d3d12_profile_submissions=true now
records within-command-list GPU timestamp pairs, reclaimed only after the
existing submission fence; it adds no waits/submissions and is disabled by default.
Its explicit hardware test passed 275 submits, ring saturation/drop/reuse,
incomplete/removal-fence guards and D3D12 debug validation with no errors.
Build-Sdk receipt is gpu-timing-sdk-build-r1.log, three pairs verified. GPU DLL
13548517e3015574692c9a02904988d2a6076bd53e1c57269bb3dd7cbc01bd8f was copied ONLY
to the independent fixture; original remains baseline-host. Actual PID 24060
played No.3 through Stage 1 and returned, then closed normally. Request was
18:55:33.612 UTC; +9.5..14.5 sec crop has 300 frames, GPU mean 417.410 us / p95
482.304 / max 518.144; CPU deferred-command encoding mean 113.745 us, median
104.25. Mean 2.2 submissions, 282.57 draws, 3 guest resolves per frame. This
excludes queue gaps, presenter/compositor/scanout and non-encoding CPU work.
Report gpu-baseline-r1.json; target records rotated into game.1.log. Initial
analysis of only game.log correctly failed for lack of samples; no new run.
An opt-in Build-Sdk -GpuTraceBench now builds rexgpu_trace_bench and a one-shot
trace request-file hook. Offline replay uses the real D3D12 backend and offscreen
presenter; original capture-reference.ppm must match before accepting timings.
Captures-r1/r2 were rejected: stable repeated output was still missing original
textures. Trace fixes include advancing multi-range download offsets, honoring
primary-buffer completion and invalidating all cached texture watches at capture
start. Build r3 includes these fixes; HD pixel validation now passes. All
evidence is private under .local/v080-futari-perf-r1; never package traces or ROM
pixels. The SDK currently has developer tracing enabled: rebuild with the default
OFF before final certification. No GPU optimization gain claimed yet.
The user requested Xbox 360 HD mode for GPU capture. Use Ver1.5 Xbox 360 for the
new GPU baseline; preserve earlier Arcade CPU evidence and do not compare costs
across modes. PID 120084 entered Ver1.5 Xbox 360 / Normal Play / Original / Reco
Normal stage 1, captured frame 37743, then closed normally. This fixture has no
HD replays. Menu confirm is Z, but game mode/character/shot confirm is C in this
copied profile; do not misdiagnose Z's lack of gameplay confirmation as input lag.
captures-r3/435A07D7_37743.xtr is 93,962,619 bytes, SHA256
82b7407447545024b3f3f37fb18108185aaddd53f24adb6ac6763a18b230b097.
Original reference SHA256 af520eb54163766a692562e51eb81fb0ce3b450fab0d7e8f0ea082f0ffa1865e.
Offline 3+5 then 20+120 replays all exactly matched the original 1280x720 RGB
output (hash 5AAC8EE6FF984412); trace-baseline-r3 and trace-baseline-hd-r1.
This is the first valid offline capture benchmark, NOT a GPU optimization gain.
Replay wall time includes snapshot restoration/decompression; separate those
costs before using it as a steady-frame CPU/GPU benchmark.

Native GPU prototype: d3d12_native_rgba8_resolve remains default FALSE. It copies
a fully owned, unscaled, single-sample RGBA8 host RT directly to guest tiled
memory, preserving ownership, partial-page uploads, UAV ordering, invalidation
and original clears; unsupported cases fall back. Scalar prototype had no stable
gain. Current four-pixel Store4 shader passes 32 real GPU gradient/endian/swap/
offset/partial-width cases with D3D12 validation, and all captured RGB pixels.
Warm offline replay reuses only equal CPU bytes without GPU-written pages;
snapshot restoration is still separate and this setup improvement is not a game
optimization. GPU-free report tests pass 7 cases. A new production format/span
policy test passes 35 checks; default/developer exported CommandProcessor layouts
are now identical. SDK rebuild native-policy-sdk-build-r1.log passed three
PE/PDB pairs; latest GPU SHA a462bba83e500a15bff91070d19c1aaac68da396db31a5fdb8206a2c7e57546f.
native-policy-pixels-r1 passed 3+8 original-image comparisons with old/new
alternating; 32 GPU cases still pass validation. This later SDK is installed
but NOT copied into the actual-game fixture described below.
128x actual-copy amplification, same-process old/new/new/old, 24 warm + 400
measured, yields 22.46/23.25 percent GPU reduction in two runs (99 complete groups
each). Normal 1x copies give only 2.17 percent median paired-group improvement;
mean 8.69 percent is outlier-sensitive, NOT an in-game performance claim.
Reports vector-interleaved-r1/r2.json and vector-normal-interleaved-r1.json.

Actual prototype PID 111672, GPU DLL SHA 2fe6337fbda015f49af000e596423b854fd1aa2e70b6afce0e6f2a89d93a6bd8,
explicitly enabled native resolve and timing in the independent fixture. After
an initial accidental Arcade entry, returned to TOP and visibly selected Xbox360;
Normal Play / Original / Reco Normal entered Stage1, shot/explosion/background
looked correct, ended without continuing, and saved a NEW No.5 HD replay. Existing
No.1..4 retained. replaydata.bin is 19,428 bytes, SHA256
24964d7566abf172bd294bbfe4e4df7f6cab0a8d100fc654d0c27b5d0f244701.
03:40:08..20 local contained 603 host-present intervals, mean16.6664/max18.600 ms,
no >20 ms; audio silence0/queue42.7 ms. This is a smoke, not a matched A/B.
native-hd-smoke-r1.json and run-20261027T193609875Z-111672 retain evidence. Normal
Alt+F4 closed at03:42:19. New HD No.5 enables actual same-replay A/B next.

CPU round 2 identified TimerQueue: HD sampler PID85696 measured ~120 CPU ms/s
in that thread versus GPU worker31; IP wall samples are NOT on-CPU shares.
The production 1ms KeTimeStampBundle timer still spun/yielded before sleeping.
New Windows DeadlineWait uses a high-resolution one-shot plus retained auto-reset
notification, checks original published sequence/deadline, preserves recurring
due+=interval and Disarm. Unsupported/failing hosts or init option
timer_queue_blocking_wait=false keep the original strategy. 79 production-queue
checks pass in both modes (including 1000 callbacks, concurrent publication,
earlier deadline, cancellation, self-disarm and shutdown); timing reports are
timer-bench-{original,blocking}-r1.txt. SDK timer-sdk-build-r1.log verified3 pairs.
Independent fixture runtime SHA8f318616a9b00dd1da5d111197344cd22dd58f615b7229f5e24f4220462a1a11;
GPU stayed2fe6337f. Previous pair is timer-baseline-host (runtime4dbe6344).
Actual No.5 HD baseline PID85696 request19:49:57.030UTC yielded296.62 CPUms/s.
Candidate PID87692 requests19:54:53.633 and19:55:59.601 yielded131.63/114.33
CPUms/s in the same +9.5..14.5sec crop (55.62/61.46% less). Mean present16.667;
max17.384 baseline vs20.489/17.270. First candidate has ONE >20ms interval,
retained, not declared pacing-clean. Guest VBlank max17.160 vs17.784/17.156;
audio silence0/queue42.7. Both replay completions natural; normal close03:57:13.
Thread-only non-suspending follow-up measured TimerQueue3.09ms/s, but that later
window is result/menu, not the same Stage1 crop. Raw reports futari-hd-timer-*.json,
collector results under .local/perf-compare-v075, .local/futari_timer_report.py.

CPU round3 candidate now parks the GPU command worker directly rather than
500 SwitchToThread calls before waiting. CallbackQueue adds synchronized enqueue/
dequeue and notifies the same retained event; ring publication and shutdown
already notify. Original option gpu_blocking_command_wait=false remains.
command-wait-sdk-build-r1 verified3 pairs; 3+8 offline frames still match original
RGB5AAC8EE6FF984412 (command-wait-pixels-r1). Production policy microbench60
handoffs: worker cycles10.85m/9.70m original vs1.11m/1.48m parked; CPU timer
resolution reports0 for both. Wake medians6.3/7.3us vs8.9/8.6us; concurrent/
reentrant/callback-only/ring/shutdown checks pass. This is NOT actual-game gain.
New SDK GPU/runtime must be used as a matching pair; full final host rebuild
still required. Actual command candidate PID124508 used runtime8f318616 and
GPU1af929f90d879439571352a358b07f17a69e91d079e585c6ddb97f433b3df7b8,
both native-resolve and GPU profiling OFF. No.5 requests20:00:56.140 and
20:02:03.431UTC gave101.875/111.228 CPUms/s at the same crop; mean present
16.669/16.6655, max17.310/17.587, no>20 or audio silence, queue42.7. Both complete
naturally. CTest4/4 passed (shared-wait-ctest-r1.log), including overlapping
producers/consumer. Reports futari-hd-command-candidate-r{1,2}.json.
Third No.5 request20:03:08.772UTC was solely non-suspending thread attribution,
starting~5.5sec later for10.05sec: GPUworker24.876 CPUms/s, mainThr18.657,
gameMain15.548, XThread15DB0 12.438, SDLAudio12.438, AudioWorker6.219,
XThread1C3C0 6.219, two unnamed9.329/3.110, vsync1.555; TimerQueue below this
GetThreadTimes resolution. This is a different window, not a sumable phase
decomposition. Evidence command-candidate-gameplay-thread-cpu-r1.txt; replay
SHA24964d75 remains unchanged. PID124508 returned to list for the third time and
closed normally04:04:20.416. No game, collectors or builds running.
Next: attribute useful CPU work (register state, memory protection/uploads,
driver calls and guest/module processing) with low-overhead developer-only cycle
scopes or representative offline benchmarks; do NOT stop at these wait wins.
No final package exists; GPU and remaining three titles stay open.

### Futari useful-work candidate checkpoint

Developer CPU attribution now separates IssueDraw phases using thread cycles
and wall time, plus nested upload/request subsets. It is compiled only with
REX_GPU_TRACE_BENCH and disabled by default. Offline attribution exposed 6.35MB
uploads/frame, but snapshot restoration can create these reloads. Never call
this the live game's steady upload volume. Same-value shader constant dedup
was tested in 400-frame ABBA: paired median -0.109%, no benefit. Removed that
candidate; private constant-dedup-abba-r1 retains evidence.

d3d12_resolve_skip_overwritten_uploads remains FALSE. Candidate avoids uploading
old CPU contents for fully overwritten RGBA8 32x32 tile-row/page prefixes; tail
still uploads normally, original RangeWrittenByGpu/watches/barriers preserved.
108 policy checks pass. overwrite-upload-sdk-r2.log verified three PE/PDB pairs,
GPU cffe670206b84b85e3c518847913bcfa98c2b108b769c6eef8f747a6e29289a9.
Normal 400-frame offline ABBA (99 complete groups) saves21.81% median packet
wall and38.95% GPU work, every output matches originalRGB5AAC8EE6FF984412.
These are offline restoration/reload workloads, NOT in-game speedups.

Same-DLL actual No.5 ON PID124440 requests20:19:18.203/20:20:18.965UTC:
CPU127.85/144.38ms/s, present max19.745/17.389ms. OFF PID114952 requests
20:24:19.636/20:25:25.838UTC:104.90/134.73ms/s, max17.342/17.499ms.
All four crops +9.5..14.5s, no >20ms/audio silence, queue42.7. All replays
complete naturally. OFF normal close04:26:34, unchanged replay SHA24964d75.
No repeatable actual benefit: do NOT enable from offline numbers. Reports
futari-hd-overwrite-{candidate,control}-r{1,2}.json remain private.

Follow-up build actual-work-profile-sdk-r1 verified three pairs. GPU
a2cf0580f3ded29fcae2ed7ec959046da56d116da5d2a86a0caf5ab7657cafdc copied only
to independent fixture. PID85224 completed No.5 with cpu-work profiling ON and
both GPU prototypes OFF; request20:29:12.592UTC, normal close04:30:43.
Actual 240-frame crop uploads53,265bytes/frame vs6.35MB in offline replay.
Draws278.55/frame, shared requests236.58, texture-prepare161.01; upload subset
wall27.19us/frame vs offline350.72us. Thus overwritten-upload optimization
targets replay restoration cost, not an actual dominant game workload.
Report futari-hd-work-profile-r1.json. CPU_WORK aggregates
60 swaps (frames field); offline bench uses1. Phase wall includes instrumentation,
nested subsets overlap and are not additive; profile run is NOT a total-CPU A/B.
SDK trace build must still be rebuilt OFF before final certification.

Next useful-work change borrows the sole memory range instead of heap-allocating
and sorting a one-element vector. Multi-range validation/sorting/merging, page
validity, allocations and uploads unchanged. RangeRequests production helper
passes18,201 boundary/random/reuse/input-preservation checks. Two microbench
ABBA rounds: single normalization26.1ns->2.45ns, multi-range~33ns unchanged.
This is a small mechanism gain, NOT a large total-CPU claim.
range-sdk-r1.log verifies3 pairs, GPU d22ad2d17322d3d79faeba8bc1f7a076da5b40f15ba27fdc631f72fff5b70999.
Separate baseline/candidate8+120 captured frames match originalRGB exactly;
range-pixels-comparison-r1.json retains wall/GPU variability, no cross-process
offline speedup claim. range-micro-r2.txt retained. Current installedSDK traceON
includes helper; actual fixture still a2cf0580 profiling build, no game running.
Four-title shared-wait rollout next; verify this small change in those actual
short checks instead of another broad Futari campaign. Normal SDK rebuild
shared-release-sdk-r1.log NOW completed with tracing OFF and3 pairs verified:
GPU a8f2035729362962bcaa7d5e379a1d47c9e37fe6843a4e1396f1403b22699875,
runtime8f318616 unchanged. Developer binaries preserved range-bench-candidate;
do NOT run stale out/sdk-build/bin trace EXE against normal GPU. Six CTests
(counter wait, both timers, command wait, range requests, resolve policy) pass.
Fresh G2 perf fixture .local/v080-g2-perf-r1/collection copied ONLY runtime and
user-data from g2-latency fixture, communityOFF,2V,IJKL,original data read-only.
No.2 SHA54798537fe1a55e96e5f21c2c396537d8118014fabcef4669cb3090c13580862.
Four-host normal build shared-hosts-r1.log completed successfully (70 steps,
existing generated-code timestamps unchanged); all receipts/staging passed.
Futari EXE e0b1156b and G2 EXE760d6fb0 now use GPUa8f20357/runtime8f318616.
Independent G2 perf fixture has those fresh runtime/module files, not old ones.
Next matched same-binary shared wait OFF/ON No.2 G2 samples; CPU20s crops
preferred over5s. Final same-commit build/package is still pending.

### Futari actual single-pass-copy comparison

Normal trace-OFF SDK, same HD No.5, both shared waits ON, overwritten-upload
experiment OFF. GPU timestamps ON on both sides. No other builds during runs.
OFF PID83852 (closed) requests20:44:51.405/20:45:51.338UTC: CPU151.44/131.29ms/s,
GPU791.02/769.05us per frame, host-present max20.278(one>20)/17.245ms.
ON PID111876 (closed) requests20:48:36.788/20:49:51.813UTC: CPU77.37/84.21ms/s,
GPU626.33/622.47us, present max17.802/17.834ms, no>20. Reverse OFF PID124636
request20:54:16.444UTC: CPU91.06ms/s, GPU804.06us, present max19.595ms, no>20.
All same +9.5..14.5sec crop, 299/300 valid GPU frames each, silence0/queue42.7.
Reports futari-hd-native-{control-r1,control-r2,candidate-r1,candidate-r2,
control-r3}.json under .local/v080-futari-perf-r1. Reverse control confirms
GPU reduction (~19..23%), but CPU varies substantially even with path OFF;
do NOT claim the first two runs' ~40% CPU difference as a stable causal gain.
Original images and independent GPU correctness checks already pass. Native
copy remains default FALSE pending the other titles' short shared-backend
checks; overwritten-upload stays FALSE because actual benefit was not seen.
Ordinary screenshots of gameplay are not pixel-exact validation. Neither
GPU command-list timestamps nor host present intervals are display scanout.

### G2 shared-performance rollout and native fence candidate

Futari reverse-control PID124636 completed No.5 and closed normally04:55:22.
G2 independent perf fixture now has actual same-No.2 baseline/candidate:
OFF PID71148 request20:56:38.398UTC, both shared waits/native-copyOFF;
ON PID32968 request20:59:31.135UTC, both waits/native-copyON; GPU timestampsON
on both, frame_delay2 unchanged. No.2 contains ~30sec of character selection;
use +50..65sec actual Stage1, NOT the initially proposed +15..35 crop.
CPU1296.45->1185.59ms/s; GPU4003.25->3825.28us, maxhost19.296->18.624ms,
no>20/audio silence, queue42.7. Only one aggregate A/B, not isolated native-copy
proof. Both full recordings returned to the list, normal close04:58:15/05:01:22.
Reports g2-bl-gameplay-{control,candidate}-r1.json in .local/v080-g2-perf-r1.
Initial g2-bl-shared-control-r1 collector ended too early, preserve but do not
use its whole-window CPU as Stage1 evidence. No game/collector remains.

Separate candidate MENU sampler menu-hotspots-r1.txt found MainXThread~995
CPUms/s and native sub_82173AE8/sub_8216FF48. IP samples perturb scheduling,
not timing acceptance. This loop waits for the device's completed GPU serial
through device+10896, with submitted+10908 and original modulo predicate.
New G2-only hook at82173B78 parks the canonical physical alias; EVENT_WRITE_SHD
notifies after its unchanged original store. Original watchdog/helper,
comparisons, stores and 2V/1V presentation policy remain. Failure50ms disables
host parking and keeps native polling; lifecycle stop wakes last parked word.
Source tests3 pass, 9 original instruction checks pass (fence-raw-r1.json).
Raw default hash7991cb83. New production helper test covers alias mapping,
wraparound, publish-before-wait, concurrent notification and CPU microbench.
Normal SDK fence-sdk-r2 and four-host fence-hosts-r3 builds now pass. First SDK
link needed synchronization.lib; first host compile used unavailable generated
load macros. Retain both failed logs. First actual candidate PID121172 completed
No.2 but CPU1187.96ms/s was unchanged: its raw base+address read omitted the
E0000000 physical alias's +1000 host offset. WaitOnAddress returned immediately,
4.50 billion calls; NOT an accepted optimization. Process closed normally.
ReadGuestWord now uses Memory::TranslateVirtual, volatile loads and byte swap.
Two extra mapping regression checks plus existing tests pass (2523 checks).
SDK GPU a2aa3cda / runtime8f318616, corrected EXE8dde3aad. PID108352 completed
the same No.2 twice at2V, requests21:13:47.348/21:15:30.906UTC. Same +50..65sec
crop: CPU394.00/368.54ms/s vs prior shared-only1185.59; GPU3856.84/3916.48us;
host mean16.666/16.668ms, max19.215/18.882, no>20 or audio silence, queue42.7.
Both first stages visually confirmed, both natural list returns. No fallback;
29921 waits by normal close05:17:04.595. Reports g2-bl-fence-candidate-r2/r3.
Separate sampler at21:16:43.127 (+72sec, AFTER measured crop) found main147,
GPU134CPUms/s; this late replay/result window is NOT a phase breakdown of crop.
Same-binary reverse OFF PID110704, request21:18:12.791UTC, completed No.2:
CPU1199.04ms/s, GPU3720.90us, hostmax18.715/no>20/audio0, queue42.7.
Report g2-bl-fence-control-r2 confirms ~67..69% CPU reduction from this wait.
Closed normally after natural list return. Next peers' shared-performance A/B.
Native-copy still defaultFALSE until peer checks. Do not edit during receipt builds.

### SDOJ native-wait optimization checkpoint

Read-only raw/default and both-module instruction audits located three busy
waits: callback flag8279C770 (8219D9B4/DA84, producer8219CD08), pending frame
count8279C774 (8215298C, audited vtable+68 target8219DE60, producer8219DB18),
and module native tick886B41EC+Offset (880332D0, producer88030DF0). Native stores,
comparisons, GPU drain, frame-delay policy and guest registers remain unchanged.
Tick wait also wakes for the original teardown/reset stores88030A8C/880316A4.
Host uses init-only sdoj_host_callback_wait; module sdoj_host_tick_wait. Both
default true; sdoj_poll_callers is bounded16sites/thread and default false.

Both original predicates can legitimately wait >50ms while loading/compiling.
Failed r2 host disabled all waits forever; r3 fixed host waits but the module
still latched off during load. Preserve those results. Current r4 WordWait and
TickWait fall back to original polling until their own original producer is
observed again; notifications cannot undo lifetime Stop. 500 C++ checks and92
SDOJ Python tests pass, original frame-delay ownership1371 checks also pass.
Build native-wait-build-r4.log is incremental, NOT final receipt certification.
Host SHAe5b8ba336e2a930b2dc8083c661d307f8c17a31eb9ede5678d21ba755748163c,
normal module34a28dcd5cc8d8f1af4df43b89dd99d82ac7666b165a927e1770f1d597cb47d6,
Arrange00e0dcaca2453b8ff69dd66599acc7bdd3bfb0ce02a4dba5a19c739503a80fe1.

Independent fixture .local/v080-peers-perf-r1/collection, communityOFF,1V,IJKL.
Arcade HD No1 TypeA Shot 1458133783 Stage4, request-relative15..35s Stage1 crop.
Control PID112328 request21:27:02.607UTC:3316.10CPUms/s, GPU6732.71us/frame,
host mean16.6668/max17.896ms,0>20/audio0. Old module, shared waits/native copyOFF.
r3 PID123800 request21:35:36.085UTC:1340.71CPUms/s,GPU6559.00us,max17.495,
0>20/audio0; tickwait fell back on load, later non-suspending thread check still
finds one~994CPUms/s native module thread. Both hosts now park successfully.
Normal close21:36:38.240UTC. r3 is partial progress, not the final candidate.
All use GPU timestampsON and log_max_file_size_mb=64 solely for capture; otherwise
the many submissions rotate away the sample. No production log limit change.
Final r4 PID81824 requests21:38:26.216/21:40:20.083UTC give412.646/456.030CPUms/s,
GPU6639.24/6631.59us, host max17.868/17.841ms,0>20/audio0/queue42.7. Original
pause/return-to-menu then re-enter succeeds, normal close21:41:10.841UTC.
Same EXE/modules reverse control PID116176 disables ONLY the two SDOJ waits,
shared waits/native copy stayON. Request21:42:42.057UTC:3294.734CPUms/s,
GPU5737.67us,host max17.562ms,0>20/audio0; normal close21:43:30.188UTC.
Thus native waits alone reduce CPU86.2..87.5%; GPU fluctuates with no proven
SDOJ gain. No clock-locking or power-setting changes. Logs/report filenames
sdoj-arcade-native-{candidate-r4,candidate-r5,control-r3}. Only Stage1 short crops,
not full four-stage replay. Post-crop perturbing sampler finds GPUworker181,
native-render122, another native worker59CPUms/s; tickthread3.1, no saturated
native waiters. IP residency is not on-CPU attribution. All processes closed.
Arrange module shares audited tick hook addresses but still needs its short
actual entry/exit smoke before final certification. Evidence and failed raw
runs are retained under that fixture.

### G2 low-latency and Futari replay-author checkpoint

G2 configurable frame_delay=1/2 is implemented; default and missing values are 2.
Native post-VBlank notification plus a bounded host gate replaces only Present's
queued sync in mode 1. Existing GPU drain, resolve, swap and retirement remain.
1013 gate checks, 561 common checks, three source contracts and the existing
37 raw-instruction/63-instruction comparison passed. Independent fixture
.local/v080-g2-latency-r1/collection ran the same Black Label/Seseri/No.2 replay
fully with no playback input at 1V (PID 74232) and then 2V (PID 105632).
Both reached stage 1 and returned to the list, then closed normally. Module
lifetimes were 77.769 and 77.804 seconds; this is a coarse duration check, not
deterministic slowdown or physical-latency proof. Replay bytes stayed unchanged.
The initial 1V shutdown warning meant Stop cancellation; the rebuilt candidate
logs that normal path at INFO. Latest G2 EXE SHA256 is
9120760df9083b9c602b08d988a01fbe3b12826060c191df77c525278ed7b9c7.
Causal timing, stressed slowdown equivalence and final A/V scope remain open.

Futari's replay author defect is traced to the native local-list refresh at
821AA000: metadata +32 equal to the local XUID replaces +12 with the local name.
An actual HTTPS Black Label replay has that shared local B13EBABEBABEBABE XUID.
The candidate binds verified downloads to the cached score author's display tag
and a stable SHA256-derived C-prefix XUID, touching only +12..27 and +32..39.
The server's original attachment and local recording path are unchanged.
Native loopback tests (including cold session, changed nickname/token order,
offline byte readback, separate authors and compressed-stream preservation) pass.
Build/tests: .local/v080-futari-author-{build,tests}-r1.log. Actual download and
save passed with ReplayViewer (PID 122028); cold offline PID 119536 showed No.5
as xiaolu while prior same-local-ID records showed User. Config was renamed to
OfflineViewer, but offline provider uses User, not that config nickname.
Both processes closed normally. Saved file SHA stayed 672e6ad7; original and
downloaded copies of the same full recording differ only at local +40..47 XUID.
Evidence: .local/v080-futari-author-cold-r1.json, TEST_MATRIX. The Futari bug is
complete; inspect G2/SDOJ/Guwange author handling separately, as the user asked.
Do not apply Futari metadata offsets to those formats. Keep original data intact.

G2 also has the author bug: 8209BBD8 compares record +64 XUID to local identity,
renames +80, and resolves foreign nonzero IDs through legacy Live lookup. Its
zero-owner branch preserves the embedded author. The new optional NativeSchema
prepare_download callback runs only after attachment/hash/run verification;
G2 clears owner, stores the rank author and updates the native save checksum.
No SDOJ/Guwange callback is enabled. Eleven raw checks and seven targeted
community CTests pass; .local/v080-g2-author-{raw-r1.json,tests-r1.log}.
Build-r2 succeeds with EXE SHA d78b8b28f0ab7ecd6d9e5340b25ca8a09480c7de937e073d6b931b86b6cccc0d.
Actual PID 73772 downloaded CollectionCheck/10 via Black Label/Seseri view 39,
saved empty No.3; offline cold PID 103736 showed CollectionCheck while old No.2
showed User. Both closed normally. Independent fixture v080-g2-author-r1.
Old/new copies differ only in owner +64..71 and checksum bytes +2/+3;
input stream and tag identical. Saved/cold reports are in .local.
The first-download issue is now fixed. Diagnostic PID 122228 proved cold
view 73 capacity=0 / required=88 before HTTP. G2's sole remote-path caller at
820AB67C..820AB6A8 has a 2048-byte array at object+5876, then length at +7924.
G2's adapter initializes that exact verified/writable layout on every download;
the shared API retains its insufficient-buffer behavior. 17 raw checks and
seven community CTests pass; .local/v080-g2-path-{build-r3,tests-r1}.log.
Candidate EXE SHA b88e6dead5aeab96acc44ed716b0b0ee803006308148c4f8f9227768dc1f7623.
Cold PID 124048 succeeded on its FIRST view 73 download, saved empty No.4, and
closed normally. No.4 is byte-identical to previously accepted No.3 (33afeee0).
Failed diagnostic run and two intermediate build errors are retained.
SDOJ local-list loader 8212E878 sets renderer mode +7772 to 3; 821479B8 prints
the row-name field only for mode 1. Offline PID 126540 confirmed Arcade HD's
local list shows date/score/character/style/stage, not an author. Guwange's
offline PID 115200 confirmed Arrange Xbox 360 Data 2 (previously downloaded
Shishin/5336) shows date/stage/characters/scores, with no author field either.
Downloaded GWN_RP_XB01.bin remains SHA256 faf6fd90028a8739b90eade2aa23c8401537cb0fe27695a6bdba24e3cd588fdc.
Both peer processes closed normally; independent fixture is
.local/v080-replay-author-peers-r1/collection. Neither title needs the Futari/G2
author rewrite; their existing download-byte-preservation loopbacks pass.
This completes the four-title author audit, not a new networking campaign.

### Active Feveron implementation checkpoint

The v0.8.0 branch now has a 46-file Feveron base-US data manifest, native
preparation policy/build routing and focused catalogue/compatibility checks.
SFO APP_VER is 01.00; VERSION 01.01 is not the application patch version.
Original data remains at .local/v080-feveron-base-r1/uroot. Native prepared
paths are in .local/v080-feveron-prepare-r1.json, warm cache checked.
First execution stalled because native_fios rejected flags=1. The supplied
FIOS initializer accepts 0..3; corrected mask with 29 passing production-provider
fixtures, including real asynchronous reads, callbacks and original file hashes.
Build receipt: .local/v080-feveron-fios-flags-build-r1.log (7 PE/PDB pairs).
Actual r4 offline test entered ARCADE Type-A / Lock-On / speed 2 first stage.
The collection route, isolated settings/profile/logs and native community identity
are now implemented. Catalogue is integrated and communitySupported is true
after local and production acceptance. Stage emits four PS4 hosts and manifests.
Local live test .local/v080-feveron-online-r1/play-r2.jsonl entered the same mode,
scored 7 with shot/bomb, ended without continuing, uploaded score/replay to boards
0 and 1, displayed FeveronCheck/7 on the ranking screen and downloaded to native
slot 0017-0000. Two scores and two replays are in the independent play.sqlite3.
Local service was stopped; offline-r1 with community disabled and nickname
OfflineViewer displayed original author FeveronCheck/7 and played the entire
~1:23.56 downloaded recording through GAME OVER to the list without input.
No community request/unimplemented import was recorded. That process is closed.
All UI tests use the existing --diagnostic-tap-ms 120, not physical-input proof.
Prior game closes returned the existing exit-27 presentation-cleanup receipt.
Feveron local replay fix removes only two network gates in the verified original
title_replay_view script, via immutable read overlay. 123 resource checks and
cold/warm preparation pass; CLI and cache overlays have the same SHA256. Latest
build .local/v080-feveron-cli-build-r1.log certifies seven PE/PDB pairs. Native
profile 86 checks and four independent HTTP cases pass. See TEST_MATRIX.
SUPER EASY / Type-B, FEVER / Type-A and CUSTOM / Type-A each entered actual
stage 1 and opened Pause Menu in modes-r1. A separate settings-cold-r1 process
restored CUSTOM on its Pause Menu, then closed with presentation cleanup 27.
US mode reset uses held Cross: --diagnostic-cross-hold-ms 1500 enables H only
for these explicit developer runs; ordinary input is unchanged. Built in
.local/v080-feveron-cross-hold-build-r1.log; 42 pad checks and 27 content/CLI
checks pass, including disabled defaults, bounds and Circle/Cross exclusivity.
Player placement/readmes and the v0.8 eight-game package profile are added;
49 package tests pass (one existing link-privilege skip). No final ZIP exists.
The service-only local commit is eba1845. Production installation passed 18 tests,
eight HTTPS health checks, empty-board read and a consistent first backup.
Seven peer PIDs/links and previous nginx route bytes are unchanged. Evidence:
.local/v080-feveron-deploy-r1/installed.json. No release or Git push.
production-r2 (configured provider, real HTTPS) completed CUSTOM / Type-A /
Lock-On / speed 2 with score 5, no continue. Boards 60/61 accepted both score
and 16,622-byte native replay; ranking displayed FeveronCheck/5; download passed
the native envelope check and saved. This uses actual gameplay, not fixtures.
production-r1 lacked the diagnostic CLI's explicit provider flag and stayed
offline; retained as setup evidence, not a service failure. Normal collection
launch already supplies --community-provider configured.
The enabled build is .local/v080-feveron-enabled-build-r1.log (7 PE/PDB pairs).
18 config, 547 common, 153 launch, 300 hidden UI checks and three-language preview
pass. Nickname coverage now uses Feveron's 799 glyph codepoints, with its 46
additions and eight exclusions, without changing peers. No physical-input/full
campaign acceptance is claimed. Feveron integration is complete for the agreed
representative scope; final same-commit package build remains with v0.8 delivery.
The eight-title latency evidence table is now in docs/ANALYSIS.md. New
scripts/present_latency.py has 13 passing tests and reuses the accepted Futari
PresentMon 2.6.0 scene crops. Present entry to OS display median is 0.3779 ms
for EVAC v0.7.5 (539 samples, Independent Flip), 11.31465 ms for Xenia 291f0c9
(598, Composed Flip). These are different display configurations, not physical
input latency or a controlled architectural advantage. Reports and input hashes:
.local/analysis/v080-latency-r1/{evac,xenia}-futari-present.json. Both SyncInterval
values are zero. Guest FrameDelay was not read back in those historical runs.
Current input audit: both EVAC backends use SDL3; PS4 sampler is ~16.67 ms with
pending-edge sampling at reads; shadPS4 v0.20.0 has event updates plus 4 ms timer.
Xenia selected build uses SDL2 for SDL and also has XInput; old capture used keyboard.
Do not infer per-device latency from timer periods or SDL library major versions.
Full software causal chains and physical input/photon measurements remain open;
do not block implementation on unavailable hardware measurements. The user is now
asleep/unavailable and requests autonomous operations with no manual keypress help.
Continue the comparison and then G2 experimental 1V; no release publication.

### v0.8.0 — implement locally, stop before publication

The active user goal is to implement v0.8.0 and finish a verified local package,
then stop before a formal Release. Scope is Feveron integration (play,
saves/replays and community networking), Futari Black Label downloaded-replay
authorship, Guwange's first-time XBLA workflow, and Xbox 360 runtime CPU/GPU
performance optimization of all four Xbox 360 titles, completing Futari first
as the representative workload before ESPGaluda II, SDOJ and Guwange. The G2
input-to-output comparison is complete. The user subsequently requested an
experimental configurable G2 1V mode for v0.8.0, to be implemented AFTER Feveron.
The latest user request adds an eight-title input-latency comparison table:
finish Feveron first, then compare current ports with Xenia (360) or shadPS4
(PS4), then implement G2 1V and add its result. Include the default settings and
optional latency modes separately, emulator versions, matched scenes/settings,
frames/ms, differences and evidence. Distinguish game-side V policy, software
input-to-corresponding-presentation timing and physical input-to-photon latency;
never turn static policy or FPS into measured latency or assume an emulator adds
a fixed frame. Reuse reliable evidence and representative short checks, not a
full-mode campaign; mark unavailable measurements explicitly. See the plan.
The comparison now also includes physical device-to-game and submission-to-display
latency, not only the middle game pipeline. Audit the actual SDL/XInput/HID input
routes and sampling of EVAC, Xenia and shadPS4; both current EVAC input backends
use SDL3. Separate earliest observable OS/SDL event, guest API return and actual
logic consumption. For Futari 1V, distinguish guest immediate presentation from
host Present(0): GPU/queue, compositor/flip, scanout and panel response remain.
Record actual refresh/VRR/presentation mode and measurement endpoints; software
events/Present return are not physical button/photons. Do not invent absent
hardware measurements, sum overlapping phases or add separate medians as totals.
Keep the current 2V strategy as default (including a missing setting), preserve
explicit switching back to 2V, and validate timing/slowdown, replays and A/V.
Do not add the optional
keyboard, audio, build-parallelism or compression backlog to this version.
G2 analysis is complete: its native standard D3D Present wrapper and device
flags correspond most closely to Futari 2V, not default Futari 1V. Xbox360 and
Black Label core entries are synchronous before draw/present. This is a static
presentation-policy comparison, not measured physical or fixed 33.3 ms latency.
37 raw-section checks and a 63-instruction Present comparison are recorded in
.local/analysis/g2-latency-r1/comparison.json; see docs/ANALYSIS.md. No G2 timing
or configuration was changed during the analysis. The later user request now
authorizes the separate experimental implementation, after Feveron integration.
Use the current worktree, keep older projects/data/releases intact, and record
focused evidence in TEST_MATRIX. Local source commits for build/package identity
are normal steps; do not publish, upload packages, push release tags or announce.
The external program ZIP is the required deliverable; internal data packaging is
not requested in this goal. Complete actual targeted acceptance, not just source
tests, before handing over the local candidate and concise bilingual notes.

Xbox 360 performance is now a required v0.8.0 item, not optional backlog.
Try replacing costly polling with correctly synchronized host waits while
preserving frame pacing, input response, audio and original slowdown. Profile
CPU and GPU separately; production-backed microbenchmarks should guide repeated
optimization, including GPU backend changes and verified title-specific native
fast paths. Freeze a same-scene baseline, repeat measurements, validate output,
then report CPU execution cost, actual GPU work and frame-time tails before/after.
Do not count a lower utilization percentage or synthetic-only speedup as the
requested meaningful in-game gain. This authorizes focused iterative performance
work, not removal of necessary synchronization or changing game speed.

Futari Black Label
shows the current player's own nickname as author after downloading someone
else's replay to the local list. Fix in v0.8.0; cause and reproduction remain
unconfirmed. Preserve the original author through download, save and local list
loading. Cover different author/downloader nicknames, cold restart, a local
nickname change and the player's own recordings. See docs/IMPLEMENTATION_PLAN.md.

Perform a Guwange first-time-player workflow audit for v0.8.0. Start with
only a clean external program package and the original XBLA container; verify
the bundled extractor, discoverable steps/dependencies, output placement under
game-data/guwange, launcher detection and actual first-stage entry/exit. Do not
rely on a developer checkout, prepared data or existing caches. Assess the flow
for players unfamiliar with shell commands and fix concrete friction with minimal
changes, keeping bilingual placement guidance aligned. Initial implementation
has not yet passed new gameplay or onboarding acceptance.

### v0.7.6 — published October 27

Published at 23:18 Asia/Shanghai:
https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.7.6 (425627949).
Main and annotated tag are pushed; keep v0.7.6 fixed at accepted binary source
9393c10. Final program ZIP reuses those binaries and updates README only,
with documentationCommit=c83b376 and all 156 other payloads unchanged:
releases/v0.7.6-docs-c83b376/EVAC-PC-LABEL-v0.7.6-win-x64.zip, 106931835 bytes,
SHA256 6f450f85b53d0db18b252b72ff6671dc536a8b545e0c1391e469e8312fb00f4b.
One extraction passes 157 files and 9 preservation checks. Anonymous API verifies
Latest, non-draft/non-prerelease, and exactly two uploaded assets with matching
sizes/digests. Evidence: .local/v076-github-delivery-r1.json and
.local/v076-release-published.jpg. No OSS upload or QQ announcement. The owned
offline replay process is closed with presentation cleanup complete (exit 27,
the existing finalizer behavior). Do not repeat builds/extraction/gameplay for
delivery-only records. Next-version Feveron implementation is not started.

The user reviewed the local package/image and simplified bilingual release notes,
then requested normal publication. The replay check, DaiOuJou deployment,
repository push and program ZIP/SHA256 publication are complete. No ROM/source/
PDB package, OSS upload or group announcement was requested. The README refresh
preserves the original binary source identity.
Player-facing release notes list user-visible changes only. Keep internal fixes,
development/deployment status and routine installation instructions out of release
notes; retain pending acceptance and deployment work in the development plan.

Extractor scope is complete locally. Added current v0.7.6 work: adapt DaiOuJou
01.00 community networking using the base PKG under E:/Game-Share/roms/
JP0092-CUSA32997_00-DAIOUJOU00000000-A0100-V0100.pkg. It extracts and fully
verifies as base-self (45 files/46 hashes) in .local/v076-daioujou-base-r1/.
Native contracts compared: base replay container 0x1000e/core 0x10000,
ARCADE_01000 only. Base rank/replay view scripts match 01.04. Forced-offline gate
removed, seed lookup uses verified SELF at RIP-relative load 0xde. Client/server
accept the base record without rewriting bytes; original future-version guard
remains. Real base ARCADE Type-A/Shotia score 28 (display 280), upload/read/download
and save passed against local loopback. Evidence: play-r2.log, play.sqlite3 and
prepared-final.json under .local/v076-daioujou-base-r1/. Production deployment
passed 23 tests on Python 3.10.12; the consistent backup and existing 3 players,
4 scores and 4 replays were verified. Seven HTTPS healths and three DaiOuJou
boards pass; peer processes and nginx are unchanged. Receipt:
.local/v076-deploy-r1/installed.json. Cold offline replay acceptance is below.
Cold run 126768 disables community networking, reads native downloaded slot
0020-0000, then plays Base100Check/280 for the full 57 seconds and returns to
DOWNLOAD REPLAY without input during playback. Evidence: cold-offline-r3.log
and cold-offline-{playback,result}.png in the same fixture. Navigation is
Backspace -> MATERIALS ROOM -> REPLAY THEATER -> DOWNLOAD REPLAY, not the
separate RANKING VIEWER. Diagnostic keyboard aliases use I/J/K/L, X confirm.
Dangun Feveron is now Coming Soon only; actual support is next version.
Its supplied PKG is E:/Game-Share/roms/
UP2789-CUSA06735_00-DANGUNFEVERONUSA-A0100-V0101.pkg (PS4 CUSA06735).
Catalogue has eight cards but only seven integrated titles. Official artwork is
in launcher/assets/feveron.jpg with source/hash in artwork.json. CN/EN/JP actual
preview passes; .local/v076-feveron-card-final.png shows selected disabled card.
Preview also exposed a pre-existing missing 13pt font cache entry (invalid map key).
Fonts now cache on demand, and launcher_preview CTest covers render/failure paths.
PS4 local build is .local/v076-final-build-r1.log. Latest launcher build is
.local/v076-launcher-height-r1.log: initial outer height is capped at 90 percent
of monitor work area; extra cards scroll, header/footer stay fixed. 1080p
100/125/150-percent geometry checks and common/launch/preview CTests pass.
Root launcher stays unchanged; current candidate is out/common/bin/EVAC PC LABEL.exe.
Publication is now authorized; preserve all older release tags and artifacts.
The standalone player entry is out/ps4-extract-player/tools/Extract-PS4.exe with
its full sibling dependency directory. Native Win32 GUI selects one base or patch
PKG and a new output folder, extracts resources plus sce_sys metadata, supports
cancel and opens uroot. PkgTool is pinned/bundled; no player shell is required.
The user requests English for the extractor UI, independent of Windows language.
Keep manual base-copy/patch-overlay, existing game-data layout, detection rules,
launcher and Xbox 360 paths unchanged. Do not build an installation manager.
Treat reported detection failures as extraction/merge issues first: actual ESP
Ra.De. 01.00 + 01.04 extraction/overlay passes the unchanged inspector (48 files,
49 hashes, update-104). Evidence: .local/v076-data-validation.json and
.local/v076-extract-r1/. GUI patch extraction/Open game files passed; the user
then pressed Escape to stop computer use. Do not resume desktop control just
to repeat this check. Final source-only cleanup is built, not visually rechecked.
28 native extraction checks, seven focused Python checks and 48 packaging cases
(one existing skip) pass. Bilingual setup guides and package dependency rules
are updated. The final launcher/inspector/extractor receipt has three binary/PDB
pairs; root launcher was deliberately not staged. The full seven-game build at
9393c10 certifies 73 Xbox, 7 PS4 and 3 launcher/inspector/extractor PE/PDB pairs.
The local program ZIP passed one extraction (157 files, 9 preservation checks).
Actual launcher smoke showed eight cards with Feveron disabled; screenshot is
releases/v0.7.6-9393c10/launcher-v0.7.6.jpg. Final publication refreshes README
only and records documentationCommit separately; all other payloads stay fixed.
See docs/TEST_MATRIX.md for verification boundaries.

### v0.7.5 — published October 27

The user authorized normal GitHub release delivery after local acceptance, then
confirmed publication. Published at 00:07 Asia/Shanghai on October 27:
https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.7.5 (release 424467884).
Main and annotated tag are pushed; tag v0.7.5 stays fixed at the accepted source
6d62d6b. Anonymous API verifies Latest, non-draft/non-prerelease, and exactly two
uploaded assets with sizes/SHA256 matching the accepted ZIP and checksum file.
Bilingual release notes cover the two fixes and earlier GPU diagnostic logging.
Evidence: .local/v075-delivery-r1.json and .local/v075-release-published.jpg.
No OSS upload or group announcement was requested. Published v0.7.4 is unchanged.

Completed at source 6d62d6b23d68a162c01bd2aba8d22cc898f9f1eb. Seven-game incremental
build certifies 71 Xbox / 7 PS4 / 2 launcher binary/PDB pairs at that commit.
Final GPU CTests 7/7, native compatibility/profile 2/2, score 38 cases, and
common/launch/update 3/3 pass. Both compatibility queue variants pass explicit
synchronization validation with zero errors after the final cleanup correction.
Logs: .local/v075-final-*.log. Program ZIP is
releases/v0.7.5-6d62d6b/EVAC-PC-LABEL-v0.7.5-win-x64.zip, 106353621 bytes,
SHA256 66bc214a8c5dac13911d61dbd269da734e19d625adb2964d460eeb9f595f24e1.
One actual extraction verifies 150 files and nine update-preservation sentinels;
no ROM/source/PDB/user config. Receipt:
.local/release-acceptance-v075/66bc214a8c5dac13/accepted.json.
The packaged launcher is 0.7.5.0 and byte-identical to the development entry.
The accepted package was reused without rebuilding or extracting again for
GitHub delivery. Keep its source tag and ZIP fixed; delivery-only documentation
does not require repeating builds, acceptance or uploads.

The user confirmed that selecting View Player Profile in DaiOuJou rankings
caused run-20261026T142646136Z-112540 to exit at 22:27:36 Asia/Shanghai.
The final event is unimplemented_import for libSceNpProfileDialog,
Lg+NCE6pTwQ#BD#t, eboot.bin+0x1da93a, exit 20; preceding leaderboard/download
requests returned HTTP 200. Logs remain in C:/evac-pc-label/logs/daioujou.
The local profile card is implemented. Real DaiOuJou run 105528 opened it twice,
closed with Enter/Esc, and returned to the ranking list. The shared hidden-window
ABI/lifecycle test and validated-ranking nickname cache checks pass.

The user also added PS4 Vulkan compatibility to v0.7.5. Downloads/logs 0.7.4.rar
contains Ketsui run 19128 and DaiOuJou run 12552 from final source 46deed5;
both exit 2 before device selection because neither EXT nor KHR surface
maintenance1 is advertised. GPU model/driver are absent, so this is not evidence
of insufficient performance. The candidate now selects per-image acquisition
when maintenance1 is unavailable, defers old chains through oldSwapchain, and
uses queue-idle only at explicit drain/teardown or bounded resize pressure.
Keep existing fast-path fences, guest timing and snapshot handoff. Candidate
component tests pass on both independent/shared queues with synchronization
validation; seven GPU CTests pass. Three-game menu smoke uses an independent
.local/v075-smoke-r1/collection and EVAC_PS4_PRESENT_COMPAT=1; all three reached
mode menus. User gameplay run 39740 confirmed no noticeable performance gap.
It closed with presentation cleanup complete, as did all three menu checks.
Final presenter cleanup also clears replacement_old after retiring old chains;
the final GPU tests cover that build. Reporter's actual older-driver device
verification remains external feedback, not a claim of local hardware coverage.

### v0.7.4 — delivered October 26

Published https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.7.4 at
22:00 Asia/Shanghai. Final source/tag is 46deed5dcde4c8e1fda2b315a893c2ce40e87a31.
The earlier unpublished tag was corrected with an exact remote lease; do not
move the published tag. Program ZIP: releases/v0.7.4-46deed5/EVAC-PC-LABEL-v0.7.4-win-x64.zip,
106325853 bytes, SHA256 69f19cdb08abdec79065464529a9f151c8c9877c3ea391175f8e2a6f5f6edfee.
One extraction passes (150 files, nine preserved sentinels); receipt
.local/release-acceptance-v074/69f19cdb08abdec7/accepted.json.
GitHub API verifies ZIP/checksum size, SHA256, uploaded state, Latest and
non-draft/non-prerelease. Issue #1 was thanked and linked to v0.7.4, then closed
as completed; comment 6417890785. No ROM, source bundle or personal credits.

Actual offline desktop smoke used an independent installation: Ketsui 01.02
run 113576 reached mode menu/ARCADE transition; DaiOuJou 01.04 run 116532 reached
mode menu and arcade title. ESP initial run 6484 exposed the missing variant,
fixed in 46deed5. Final run 115044 reached title, mode menu and ARCADE loading.
Its EXE/GPU/launcher hashes match the final package. All three close through
the existing window-close exit 27 with presentation cleanup and no dump; all
test processes are gone. Ketsui/DOJ accepted smoke is reused after the ESP-only
manifest correction; their policies and the GPU binary are unchanged.
143 ROM/GPU-free native checks now include every actual shipped PS4 manifest;
19 catalogue and three wiring tests plus common/launch/update CTests pass.
Seven-game incremental certification verifies 71/7/2 Xbox/PS4/launcher pairs.

The old 66ce21f candidate remains in a private, untagged GitHub draft titled
"Superseded v0.7.4 candidate - do not publish" (title uses an em dash online).
Deletion was not confirmed, so it and the local failed ZIP were retained;
it is not the public release. Do not publish/re-upload that candidate.
Do not repeat builds, gameplay, extraction, publication or issue replies for
delivery-only documentation. Final evidence: .local/v074-delivery-r2.json.

### v0.7.4 — issue #1 compatibility work

Publication smoke caught an ESP Ra.De. regression in candidate 66ce21f:
its single-version manifest did not emit a DataVariant, so the new preparation
policy received empty version/layout strings. Do not publish the 56dc9b87 ZIP.
The manifest now names update-104; native compatibility tests consume every
actual generated PS4 variant and verify all policy modules against its manifest.
The Python ESP check failed before this fix and passes afterward. Ketsui 01.02
and DaiOuJou 01.04 reached actual menus/arcade startup and closed as expected;
ESP candidate run 6484 failed before guest execution. Evidence remains under
.local/release-acceptance-v074/56dc9b872fe91aed/extracted/logs.
The v0.7.4 GitHub release is still a draft. Source/tag 66ce21f was pushed before
this smoke failure; update the unpublished tag to the corrected certified source,
replace the two draft assets, and verify ESP actual launch before publication.

Completed locally at source 66ce21f62b6a87e16e8d278b2247805c66c2dba1.
110 ROM/GPU-free compatibility checks, 68 PS4 source tests, 19 catalogue,
12 tool, 7 toolchain, 4 product and 47 package tests pass (one existing skip).
Five GPU CTests/presentation smoke and existing Ketsui 01.00 / DaiOuJou 01.04
native preparation pass. Final common/launch/update CTests and PS4 native
round/client/config checks pass. Reporter's hardware/data remain his coverage.
SDK and seven-game incremental build verify 3/71/7/2 SDK/Xbox/PS4/launcher
binary/PDB pairs. No gameplay campaign or production scores were generated.
Program ZIP: releases/v0.7.4-66ce21f/EVAC-PC-LABEL-v0.7.4-win-x64.zip,
106323584 bytes, SHA256 56dc9b872fe91aedc92dd28ab4b15359f8a89bd0585195cc71a9a9c40a519f22.
One extraction passes: 150 files, nine preserved sentinels, no ROM/source/PDB/config.
Receipt: .local/release-acceptance-v074/56dc9b872fe91aed/accepted.json.
No push, tag, upload, issue reply or public release. All owned builds/tests finished.
Reuse this accepted package; delivery records do not require rebuilding it.

Implement PS4 KHR maintenance1 negotiation, Ketsui 01.00 plaintext SELF,
DaiOuJou 01.00 offline base support and the official Ninja executable pin.
User requests small durable regression tests independent of the reporter's GPU
and data. Shared production policy/decoder tests must cover both new and old
paths; do not substitute a full gameplay campaign. No extra personal credits
or acknowledgements; reference issue #1 for provenance. Preserve old releases.
Prepare a new local program package after builds/checks; publication is separate.

### v0.7.3 GitHub Release — delivered October 26

The user accepted the refreshed package and authorized push/publication.
Main through 74e34fc and annotated tag v0.7.3 are pushed; the tag stays fixed.
Published at https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.7.3
at 02:47 Asia/Shanghai, marked Latest, with bilingual release notes.
Assets are the accepted 106289507-byte program ZIP (SHA256 2a402a850285b827b856ca133e00731dcc27ca168cf9ff7aed1b57bb0b669c82)
and its .sha256 file only. Anonymous GitHub API verifies both sizes, hashes,
uploaded states and the non-draft/non-prerelease status. No ROM/internal asset.
Evidence: .local/v073-github-delivery-r1.json and .local/v073-github-release.jpg.
No rebuild, repeat extraction, OSS upload or group message. Delivery records
do not change the release tag/package; reuse the accepted package and tests.

### v0.7.3 placement-guide refresh — packaged locally

All seven placement guides now use directory trees and short bilingual setup
steps, committed as c86ae2c. The refreshed program package reuses the accepted
0fac99f binaries; all 143 non-guide payloads are byte-identical. Its manifest
keeps sourceCommit=0fac99f and records documentationCommit=c86ae2c separately.
ZIP: releases/v0.7.3-guides-c86ae2c/EVAC-PC-LABEL-v0.7.3-win-x64.zip,
106289507 bytes, SHA256 2a402a850285b827b856ca133e00731dcc27ca168cf9ff7aed1b57bb0b669c82.
One actual extraction passes: 150 files, seven updated guides, nine preservation
sentinels. Receipt: .local/release-acceptance-v073/2a402a850285b827/accepted.json.
No recompilation, upload, push, tag or announcement. Older ZIPs remain intact.
Use this refreshed package for subsequent delivery; do not repeat acceptance.

### v0.7.3 follow-up — automatic raw DLC import

The user rejected a manual extraction step: players place original DLC files
in the existing locations and start the game. Implement native automatic
extraction to prepared/cache, preserve originals and all v0.7.2 layouts/config.
The frozen 906bf98 program ZIP below predates this requirement. Keep it intact;
do not describe it as supporting automatic raw-container import. Current work
adds bounded native STFS inspection/preparation, bilingual player instructions
and focused raw-input/cache/legacy equivalence checks. No new UI or shell prompt.

Completed at source 0fac99fbd2d5658f11b569c604b2e6a4572708c5: both real original
containers verify and extract through the host preparation function; native
enumeration, warm reuse, unchanged source hashes and v0.7.2 cache equivalence
pass (.local/v073-raw-dlc-accept-r1.log). Four authored native STFS groups, 527
common, 146 launch, 12 CLI, four docs and 46 package cases pass (one existing
symlink skip). Final five focused CTests pass. Seven-game incremental build
certifies 71 Xbox, six PS4 and two launcher binary/PDB pairs. Root launcher is
staged and matches the new program ZIP, SHA af8e57d4c6e25ea9fdb0b9fe4d5925f797ecdaaed287aadbf6496b63d4832d3b.
New package: releases/v0.7.3-0fac99f/EVAC-PC-LABEL-v0.7.3-win-x64.zip,
106291280 bytes, SHA e765ed3c6b0888acbc79cca6938ce66936adfd203e9c154bff7a99b8ef9f91b4.
One extraction passes: 150 files and nine preservation sentinels; no ROM/source/
PDB/config. Receipt .local/release-acceptance-v073/e765ed3c6b0888ac/accepted.json.
No push/upload/tag/announcement; all owned sessions finished. Reuse the accepted
gameplay/byte-equivalence gates and frozen ZIP; do not rebuild for this record.

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
