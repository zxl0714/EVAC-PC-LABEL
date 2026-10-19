# Development entry

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

The collection has one config.ini with shared defaults and optional per-game
overrides. Default nickname is Player and online is enabled by default. The
deployed community API base is https://xiaolu.life/cave/api/v1. Futari and Ketsui
have compatible proxy aliases; their clients still require P4/P5 integration.

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
The user has deferred further repeated-entry gameplay tests. Keep the existing
Black Label result, mark the other repetitions deferred (not passed), and do not
make them a prerequisite for the current migration work.
P5's pinned Ketsui source, isolated dependencies and collection-local native
PS4/GPU builds now pass. Continue with shared INI, data preparation and launch contract;
keep integrated=false until actual launch/gameplay verification. P4's remaining
formal-network, audio and physical-input checks remain open, not implicitly
passed because PS4 build preparation has started.
