# Development entry

Read README.md and docs/IMPLEMENTATION_PLAN.md before changes. The user has
approved implementation: P0 snapshots, P1 G2 playable baseline, then collection
integration, Futari, Ketsui and release. Do not substitute launcher work for the
unfinished G2 baseline. All stages and verification gates remain in scope.

G2 baseline work belongs in the existing ESPGaluda2-PC checkout. Integrate into
this new repository only after that baseline; preserve the original Futari and
Ketsui checkouts, game data and personal profiles. Use separate test profiles.
The current development profiles are test data. Ending, restarting and resetting
these test sessions and accepting their in-game warnings are user-authorized;
do not interrupt the user for each such operation.

Keep ROMs, generated game program bodies, prepared images, installed tools,
logs, dumps and credentials out of Git. SDK fixes belong in maintained source,
not generated C++. Record source origins and preserve licenses. Build with two
jobs at low priority by default; builds must not automatically launch a game.

The collection has one config.ini with shared defaults and optional per-game
overrides. Default nickname is Player. The planned community API base is
https://xiaolu.life/cave/api/v1; it is not deployed by merely changing defaults.

Use source tests and real gameplay for their respective claims. Update the
verification matrix with actual supported modes, save readback, input and
audio evidence. Never mark a whole stage passed from a narrower smoke test.
