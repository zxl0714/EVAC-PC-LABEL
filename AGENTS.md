# Development entry

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
