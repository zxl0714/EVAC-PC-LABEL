# Connect your recomp to EVAC-PC-LABEL

The community server hosts rankings and replays for nine games. Choose your game to look up its board numbers, score fields and replay format. To integrate a game, read its leaderboard, submit the game's eligible scores, and use the returned score ID to attach each run's original recording.

```text
https://xiaolu.life/cave/api/v1/games/{game-id}
```

This request reads the first five entries on SaiDaiOuJou's Xbox 360 / Shot board.

```sh
curl -fsS 'https://xiaolu.life/cave/api/v1/games/sdoj/leaderboard?view=23&start=1&count=5'
```

## Choose your game

| Game | `game-id` | Example board parameter | `rules` to send | Replay `format` |
| --- | --- | --- | --- | --- |
| [Mushihimesama Futari](#mushihimesama-futari) | `futari` | `view=100` | `standard` for ordinary play. Also `training`, `modified`, `unknown` | `futari-xstorage-v1` for network recordings, `futari-replaydata-v1` for a shared local slot |
| [ESPGaluda II](#espgaluda-ii) | `espgaluda2` | `view=39` | `standard` | `espgaluda2-native-v2` |
| [DoDonPachi SaiDaiOuJou](#dodonpachi-saidaioujou) | `sdoj` | `view=23` | `standard` | `sdoj-disc-native-v1` |
| [Guwange](#guwange) | `guwange` | `view=2` | `standard` | `guwange-base-native-v1` |
| [Ketsui Deathtiny](#ps4-board-numbers) | `ketsui` | `board=51` | `standard`, including Custom mode | `ketsui-npscore-1.02` |
| [DoDonPachi DaiOuJou](#ps4-board-numbers) | `daioujou` | `board=46` | `standard` | `daioujou-npscore-1.04` |
| [ESP Ra.De. Psi](#ps4-board-numbers) | `esprade` | `board=23` | `standard` | `esprade-npscore-1.04` |
| [Dangun Feveron](#ps4-board-numbers) | `feveron` | `board=33` | `standard` | `feveron-npscore-1.00` |
| [Battle Garegga Rev.2016](#ps4-board-numbers) | `garegga` | `board=7` | `standard` | `garegga-npscore-1.00` |

`rules` separates scores earned under different play conditions on the **same** board. Use `standard` when the original scoring, eligibility and slowdown rules apply. In Futari, use `training` for practice, `modified` for altered rules and `unknown` when the run's rules are unknown. Each has its own ranking. Ketsui installations reading an existing `custom-<64 lowercase hex digits>` partition can use its exact saved key on boards 60–62. The game's ordinary Custom menu uses `standard` on those boards. All score submissions and PS4 leaderboard reads require `rules`. Xbox leaderboard reads and estimates default to `standard`. Replay uploads inherit the ruleset from the score ID.

Another port of a listed game can use that game's route and board IDs directly. The PS2 ports of ESPGaluda, Ibara and DaiOuJou, and the standalone DonPachi Recomp integration, have no hosted board. `daioujou` selects the PS4 game. A new title needs its own server route and board definitions before it can receive scores.

## HTTP reference

All paths are relative to the selected game's URL. Game clients connect over HTTPS without registration or an API key. Let players choose their nickname. Two people using the same normalized nickname share one personal best on a board. The URL identifies the game for every operation. Some responses include `title_id` for older clients. Omit it from requests.

Send POST bodies as UTF-8 JSON with `Content-Type: application/json` and `Content-Length`. Use standard padded base64 for binary fields, lowercase hexadecimal for SHA-256, and decimal **strings** for 64-bit scores.

| What you want to do | Xbox 360 games | PS4 games |
| --- | --- | --- |
| Read available boards | [GET /boards](#get-boards) | [GET /boards](#get-boards) |
| Read rankings | [GET /leaderboard](#get-leaderboard-xbox-360) | [GET /leaderboards](#post-identity-and-get-leaderboards-ps4) |
| Estimate a rank before writing | [GET /estimate](#get-estimate-xbox-360) | Rank is available in the [score receipt](#post-scores-ps4) after submission. |
| Submit a score, then an optional replay | [POST /scores](#post-scores-xbox-360), [POST /replays](#post-replays) | [POST /scores](#post-scores-ps4), [POST /replays](#post-replays) |
| Download a replay | [GET /replays/{replay_id}](#download-a-replay) | [GET /entries/{entry_id}/replay](#download-a-replay) |
| Look up a player's identifier | Use the nickname directly | [POST /identity](#post-identity-and-get-leaderboards-ps4) |

### GET /health

Optional availability check. No parameters. Read `status`. Xbox returns `{"status":"ok"}`. PS4 also supplies handshake metadata to released clients. You can go straight to the endpoint you need.

### GET /boards

No parameters. Xbox lists **populated** board/rules pairs. For example, `{"boards":[{"view":23,"rules":"standard","players":2}]}` means two nicknames have scores there. Use the game tables below to build a menu that includes empty boards.

PS4 returns **every defined board**, including empty ones. Each item has `id`, `mode`, `ship` and `upload_limit`. DaiOuJou also supplies `doll`. A Ketsui item looks like `{"id":51,"mode":"ikd_2007","ship":"type_a","upload_limit":10}`. The limit is the last serial rank eligible for replay upload. Zero means score or progress only, or a read-only board as identified below.

### GET /leaderboard (Xbox 360)

```http
GET /leaderboard?view=23&rules=standard&start=1&count=5
```

| Query | Value |
| --- | --- |
| `view` | Required integer from the game's board table. |
| `rules` | String from the game list. Defaults to `standard`. |
| `start`, `count` | First rank, 1–1,000,000, default 1. Page size, 1–100, default 20. |
| `around` | Optional URL-encoded nickname. Center the page on this player if ranked. |
| `name` | Optional nickname filter. Repeat for up to 100 players. Returns matches with their board ranks. |

Here is a populated Futari response to `GET /leaderboard?view=100`. An empty board has `total:0` and `rows:[]`.

```json
{
  "view":100,"rules":"standard","total":1,"start":1,
  "rows":[{
    "id":"22222222222222222222222222222222","nickname":"Player","score":"12345",
    "columns":[{"id":1,"type":2,"value":"12345"}],"build":"example-1.0",
    "created":1760000000,"rank":1,"replay_id":null,"replay_size":null,"replay_format":null
  }]
}
```

`total` counts distinct players. Each row contains `id` (score ID), `nickname`, `score` (decimal string), `columns` (the game's score fields), `build`, `created` (Unix seconds), `rank` (1-based), and `replay_id` (null until attached). An attached row includes `replay_size` and `replay_format`. ESPGaluda II, SaiDaiOuJou and Guwange also provide `replay_sha256`. The board shows the highest score per nickname, with the earliest submission first on ties.

### GET /estimate (Xbox 360)

```http
GET /estimate?view=23&rules=standard&score=12345&name=Player
```

| Input | Type and meaning |
| --- | --- |
| `view` | Required board number. |
| `rules` | Optional ruleset, default `standard`. |
| `score` | Required unsigned decimal text within the game's score range. |
| `name` | Required URL-encoded nickname for the prospective run. |

With one other player already at 12,345 or more, the server returns HTTP 200 and a prospective rank of 2.

```json
{"view":23,"rules":"standard","rank":2,"protocol":1,"title_id":"435A07E7"}
```

`rank` is one plus the number of **other nicknames** whose best on that board and ruleset is greater than or equal to `score`. An empty board returns `1`. The caller's own previous best is excluded. This request writes nothing. Futari returns the three fields `view`, `rules` and `rank`.

### POST /scores (Xbox 360)

Generate a new 32-character lowercase hex `submission` for each eligible run **on each board** (for example, `uuid.uuid4().hex`). Save it before sending. Retry the same submission with the same payload if the response is lost. This example submits a SaiDaiOuJou Xbox 360 / Shot score.

```json
{
  "protocol": 1,
  "submission": "11111111111111111111111111111111",
  "nickname": "Player",
  "view": 23,
  "rules": "standard",
  "score": "12345",
  "build": "example-1.0",
  "columns": [
    {"id":536870916,"type":2,"value":"12345"},
    {"id":536870914,"type":2,"value":"-5384438183559864396"},
    {"id":268435463,"type":1,"value":48},
    {"id":268435462,"type":1,"value":0},
    {"id":268435461,"type":1,"value":0},
    {"id":268435457,"type":1,"value":0}
  ]
}
```

All eight top-level fields shown are required. `protocol` is the JSON integer `1`. `build` identifies the client build (1–96 characters). Nicknames are normalized to Unicode NFKC and trimmed. Use 1–32 characters within 48 UTF-8 bytes and, for ESPGaluda II, SaiDaiOuJou and Guwange, 32 UTF-16 code units.

```json
{"id":"22222222222222222222222222222222","duplicate":false,"protocol":1,"title_id":"435A07E7"}
```

Save `id` as the **score ID**. An identical retry returns the same ID with `duplicate:true`. Futari returns just `id` and `duplicate`. For any of the other Xbox games, send the same eight fields to its URL and substitute its `view`, `score` and `columns` from the [game tables](#xbox-board-numbers-and-score-columns).

### POST /identity and GET /leaderboards (PS4)

To look up one player, send the nickname to `POST /identity`. The following request and response are for Ketsui.

```json
{"nickname":"Player"}
```

```json
{"nickname":"Player","account_id":"7782643393547210698"}
```

Treat `account_id` as a decimal string scoped to the game. For page reads, pass `board` and `rules`. Omit both paging parameters for ranks 1–20, or supply both `start` (positive 32-bit integer) and `count` (1–100). To read just one player's row, supply `account_id` in their place.

```http
GET /leaderboards?board=51&rules=standard&start=1&count=10
GET /leaderboards?board=51&rules=standard&account_id=7782643393547210698
```

```json
{
  "board":51,"rules":"standard","total":1,"start":1,"sorted_us":"1760000000000000",
  "entries":[{
    "entry_id":"22222222222222222222222222222222","nickname":"Player","account_id":"7782643393547210698",
    "score":"12345","serial_rank":1,"rank":1,"created_us":"1760000000000000",
    "comment_base64":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
    "game_info_base64":"","build":"example-1.0","replay":null
  }]
}
```

`sorted_us` is the last accepted score's Unix time in microseconds. An empty board returns `"sorted_us":"0"` and `"entries":[]`. Equal scores share `rank`. `serial_rank` gives each entry a unique 1-based position, earliest first on ties. When the recording is available, `replay` contains `sha256`, `size` in bytes and `format`.

### POST /scores (PS4)

All five PS4 games use the following JSON body. This example submits Ketsui IKD 2007 / Type-A. For another game, substitute its URL, board number and score unit from the PS4 table.

```json
{
  "submission_id":"11111111111111111111111111111111",
  "nickname":"Player",
  "board":51,
  "rules":"standard",
  "score":"12345",
  "comment_base64":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64":"",
  "build":"example-1.0"
}
```

Send exactly these fields. `submission_id` is a new 32-character lowercase hex ID per board and score. The nickname accepts 1–32 characters within 48 UTF-8 bytes after normalization. `comment_base64` encodes the original 64 comment bytes (64 zero bytes for an empty comment). `game_info_base64` encodes 0–189 bytes of the game's score data (empty string for none). Decode both fields back to bytes when populating the original ranking screen. `build` is 1–80 letters, digits or characters from `._+-`. Send the native score as a nonnegative decimal string without leading zeros, except `"0"`.

```json
{"entry_id":"22222222222222222222222222222222","accepted":true,"serial_rank":1,"upload_eligible":true,"account_id":"7782643393547210698"}
```

`accepted` means this score established a new personal best on the board. An equal or lower score returns HTTP 200 with `accepted:false`. `upload_eligible` tells you whether this exact entry may receive a replay. Save its `entry_id` **and your original `submission_id`** for the upload. Retrying the same request returns its original receipt.

### POST /replays

Use the game's **native network recording** from the same run. If the game hands you its recording before it submits its score, keep it locally until the score receipt arrives. Futari also accepts a single shared local-save slot under its separate format. The general decoded size limit is 8 MiB. Tighter native limits appear below. Xbox score requests are limited to 64 KiB of JSON. For a replay, use compact JSON with unwrapped base64 so the request fits the larger upload limit.

Compute both the hash and base64 from the same untouched bytes.

```python
from base64 import b64encode
from hashlib import sha256

digest = sha256(recording).hexdigest()
encoded = b64encode(recording).decode("ascii")
```

The following are complete request objects in Python. Use only the row for the game you selected. `player` is the nickname on the score. `fmt` is the exact format from the game list. `receipt` is the parsed score response.

```python
# ESPGaluda II, SaiDaiOuJou, Guwange
body = {"protocol": 1, "score_id": receipt["id"], "nickname": player,
        "format": fmt, "sha256": digest, "data": encoded}

# Futari. Reuse the score's build string and board number.
body = {"protocol": 1, "score_id": receipt["id"], "nickname": player,
        "format": fmt, "sha256": digest, "payload": encoded,
        "build": build, "mode": f"view-{view}"}

# PS4. Send only if receipt["upload_eligible"] is true.
body = {"entry_id": receipt["entry_id"], "submission_id": submission_id,
        "nickname": player, "format": fmt, "sha256": digest, "payload_base64": encoded}
```

| Game | HTTP 200 response fields | How to fetch the recording |
| --- | --- | --- |
| ESPGaluda II, SaiDaiOuJou, Guwange | `id` (replay ID), `score_id`, `sha256`, `bytes` (integer), `duplicate` (boolean), `protocol`, `title_id` | `GET /replays/{id}` |
| Futari | `id` (replay ID), `duplicate` (boolean) | `GET /replays/{id}` |
| All five PS4 games | `entry_id`, `sha256`, `size` (integer), `format` | `GET /entries/{entry_id}/replay` |

This example receipt attaches a 136-byte ESPGaluda II recording to a score.

```json
{
  "id":"33333333333333333333333333333333","score_id":"22222222222222222222222222222222",
  "sha256":"424ed5226c9c5e1786e39a43a2fc7b0b637743562cd6d8ab4eb71c1fda4d1ce9",
  "bytes":136,"duplicate":false,"protocol":1,"title_id":"435A07D8"
}
```

An identical upload can be retried. Once attached, the recording also appears on its leaderboard entry. A conflicting second attachment is rejected.

### Download a replay

| Game | HTTP 200 response | Bytes to pass to the game's player |
| --- | --- | --- |
| ESPGaluda II, SaiDaiOuJou, Guwange | JSON with `id`, `format`, `sha256`, `bytes`, `data`, `protocol` and `title_id` | Base64-decode `data`. Check decoded length against `bytes` and the SHA-256 against `sha256`. |
| Futari | JSON with `id`, `score_id`, `format`, `sha256`, `size`, `payload`, `build`, `mode`, `protocol` and `title_id` | Base64-decode `payload`. Check `size`, `sha256` and `format` before choosing the network or local-slot reader. |
| All five PS4 games | `application/octet-stream`. Headers `X-Replay-SHA256`, `X-Replay-Format` and `Content-Length` | Use the response body directly. Check its format and digest against the headers and the leaderboard's `replay` object. |

### Errors and retries

All successful operations return HTTP 200. Check the status before reading a success body. Error JSON has a machine-readable `error` and a diagnostic `request` ID, for example `{"error":"submission_conflict","request":"0123456789ab"}`.

| HTTP | What the client should do |
| --- | --- |
| 400 | Correct the field, board, ruleset or binary encoding named by the error. |
| 403 | PS4 native endpoints reject requests carrying a browser `Origin` header. Call from the game or its backend. |
| 404 | Check the game route or referenced score/replay ID. An empty valid board returns 200. |
| 409 | Check the error code. Reuse the original body for a retried submission, and match the score, player and replay for uploads. |
| 413 / 415 | Reduce the body to the applicable limit, or send UTF-8 JSON and its byte length. |
| 429 / 5xx / connection failure | Back off. Keep the original endpoint, nickname, submission ID and payload for retry. HTTP 507 means the service has reached capacity. |

## Xbox board numbers and score columns

Send one `columns` array of up to 64 `{"id":number,"type":number,"value":...}` objects with unique IDs. Hexadecimal notation in the tables is for lookup. **JSON uses decimal IDs.** Type `1` is a signed 32-bit JSON integer. Types `2` and `7` use signed 64-bit decimal strings. Futari, ESPGaluda II and SaiDaiOuJou accept integer scores from 0 to 9,223,372,036,854,775,807. Guwange's maximum is 2,147,483,647. Use the original integer points for all four games.

### Mushihimesama Futari

`view = mode base + difficulty offset + character offset`. This covers all 96 native boards.

| Mode | Base | Difficulty offset | Character offset |
| --- | ---: | --- | --- |
| Ver.1.5 Arcade / Xbox 360 | 100 / 200 | Original 0, Maniac 10, Ultra 20 | All 0, Reco Normal 1, Palm Normal 2, Reco Abnormal 3, Palm Abnormal 4 |
| Arrange | 300 | Original 0, Maniac 10, Ultra 20 | All 0 |
| Novice (Easy) | 400 | Original 0, Maniac 10, Ultra 20 | Same as Ver.1.5 |
| Black Label Arcade / Xbox 360 | 500 / 600 | Original 0, Maniac 10, God 20 | All 0, Reco 1, Palm 2 |
| Ver.1.01 Arcade / Xbox 360 | 700 / 800 | Original 0, Maniac 10, Ultra 20 | Same as Ver.1.5 |

For example, Ver.1.5 Arcade / Original / All is `100`. Black Label Xbox 360 / God / Palm is `622`.

Futari's JSON column ID is the native write property ID **masked with `0xffff`**. Send the properties that the game wrote and preserve their values. The top-level `score` comes from native `0x20000001`.

| Wire `id` | Native property | Type | Value |
| ---: | --- | ---: | --- |
| 1 | `0x20000001` | 2 | Score, same decimal string as the top-level score |
| 2 | `0x20000002` | 2 | Native run time value, forwarded as a signed decimal string |
| 3 / 4 / 5 | `0x10000003` / `0x10000004` / `0x10000005` | 1 | Player number / character / shot type |
| 6 / 7 / 8 | `0x10000006` / `0x10000007` / `0x10000008` | 1 | Stage / area / packed name entry |
| 9 | `0x10000009` | 1 | Native replay size property |
| 32785 | `0x10008011` | 1 | Storage size reported on the score write, if present |

A score-only request on `view:100` can carry `"columns":[{"id":1,"type":2,"value":"12345"}]`. Preserve the full property list when integrating the original ranking UI. A downloadable recording is identified by the returned `replay_id`. For native network playback use `futari-xstorage-v1`, a 104-byte header followed by the original compressed stream, up to `0xD30B8` bytes in total. `futari-replaydata-v1` is the game's 99-slot save container with exactly one recording in slot 0 and 98 empty slots.

### ESPGaluda II

Each cell lists four native `view` IDs in the order **All / A / B / C**, where A, B and C are the mode's native character indices 0, 1 and 2. The period is already part of the board ID. All 84 use `rules=standard`.

| Mode (native selector) | All-time | Monthly | Weekly |
| --- | --- | --- | --- |
| Xbox 360 (20) | 24 / 25 / 26 / 27 | 32 / 28 / 29 / 30 | 31 / 33 / 34 / 35 |
| Arcade (21) | 1 / 10 / 11 / 12 | 13 / 14 / 15 / 16 | 17 / 18 / 19 / 2 |
| Arrange (22) | 20 / 21 / 3 / 4 | 48 / 5 / 6 / 7 | 8 / 9 / 22 / 23 |
| Black Label (23) | 73 / 37 / 38 / 39 | 40 / 41 / 42 / 43 | 44 / 45 / 46 / 47 |
| Omake! (24, named Arrange2 in the board data) | 61 / 62 / 63 / 64 | 65 / 66 / 67 / 68 | 69 / 70 / 71 / 72 |
| Xbox 360 Novice (25) | 49 / 50 / 51 / 52 | 53 / 54 / 55 / 56 | 57 / 58 / 59 / 60 |
| Black Label Novice (26) | 36 / 74 / 75 / 76 | 77 / 78 / 79 / 80 | 81 / 82 / 83 / 84 |

For example, Xbox 360 / Ageha / All-time uses `25`, and Black Label / Seseri / All-time uses `39`. The monthly and weekly numbers follow the original board definitions. The hosted server retains personal bests on each of those boards across calendar periods.

| Wire property ID | Type | Value |
| --- | ---: | --- |
| `0x20000001` | 2 | Total score. Required, equal to top-level `score`. |
| `0x20000002` through `0x20000007` | 2 | Stage 1 through stage 6 scores, one property per stage. |
| `0x10000008` / `0x10000009` / `0x1000000A` | 1 | Character index / continue count / extra field. |
| `0x1000000B` / `0x1000000C` / `0x1000000D` | 1 | Weapon / zero-based stage / packed native name. |
| `0x1000000F` / `0x10000010` | 1 | Player number / area. |
| `0x20000011` | 2 | Unsigned-positive UTC FILETIME ticks (100 ns since 1601), sent as a decimal string. |
| `0x10008011` | 1 | Exact recording size in bytes when attaching a replay. |

For a score without a recording, the only required property is the total score. To attach a recording, also send its character, timestamp and byte count **with the score**. The following four columns match a 136-byte recording of 999 points on `view=39`.

```json
[
  {"id":536870913,"type":2,"value":"999"},
  {"id":268435464,"type":1,"value":2},
  {"id":536870929,"type":2,"value":"134368194753020000"},
  {"id":268468241,"type":1,"value":136}
]
```

Submit the title's version-2 network record, including its 104-byte header and payload. The server checks the score, timestamp, character and length against the submitted properties.

### DoDonPachi SaiDaiOuJou

Each cell lists **Shot / Laser / Expert** `view` IDs. The 39 personal boards below are available on the hosted server.

| Game menu mode | Type A | Type B | Type C | Type D / Saya |
| --- | --- | --- | --- | --- |
| Arcade HD | 2 / 7 / 11 | 3 / 9 / 12 | 4 / 10 / 13 | 85 / 86 / 87 |
| Xbox 360 | — | — | — | 23 / 24 / 25 |
| Novice | 14 / 17 / 20 | 15 / 18 / 21 | 16 / 19 / 22 | 88 / 89 / 90 |
| Ver.1.5 | 35 / 38 / 41 | 36 / 39 / 42 | 37 / 40 / 43 | 91 / 92 / 93 |

The `POST /scores` example above is a complete Xbox 360 / Saya / Shot run. The six properties below are required. The recording-size property is optional. The packed character must match the selected board.

| Wire `id` (hex / decimal) | Type | Value and encoding |
| --- | ---: | --- |
| `0x20000004` / `536870916` | 2 | Nonnegative score, same decimal string as top-level `score`. Signed 64-bit big-endian integer at replay byte 24. |
| `0x20000002` / `536870914` | 2 | Signed decimal native FILETIME with its 32-bit halves swapped. Signed 64-bit big-endian integer at replay byte 32. |
| `0x10000007` / `268435463` | 1 | `16 * character + shot`. A/B/C/D are 0/1/2/3. Shot/Laser/Expert are 0/1/2. Xbox 360 always uses character 3, giving 48/49/50. Unsigned 32-bit big-endian integer at replay byte 52. |
| `0x10000006` / `268435462` | 1 | Native stage (0–65535), unsigned 32-bit big-endian integer at replay byte 56. |
| `0x10000005` / `268435461` | 1 | Native `AREA` field (0–65535), unsigned 32-bit big-endian integer at replay byte 60. |
| `0x10000001` / `268435457` | 1 | Native `CONTINUE` field (0–65535), unsigned 32-bit big-endian integer at replay byte 76. |
| `0x10008011` / `268468241` | 1 | Optional. Zero or the actual byte count of the network recording. |

If you receive a native timestamp property, send its signed bits unchanged. The following function converts conventional positive FILETIME ticks to the wire value used by **SaiDaiOuJou and Guwange**.

```python
def packed_native_time(ticks):
    swapped = ((ticks & 0xffffffff) << 32) | (ticks >> 32)
    signed = swapped - (1 << 64) if swapped >= (1 << 63) else swapped
    return str(signed)
```

For example, `packed_native_time(0x01dd5fb4b546a200)` is `"-5384438183559864396"`. Upload the original network recording with its 304-byte header. The server checks its magic, advertised length, native checksum and run fields.

### Guwange

| Mode | All | Shishin | Kosame | Gensuke |
| --- | ---: | ---: | ---: | ---: |
| Arcade | 1 | 2 | 3 | 4 |
| Blue | 5 | 6 | 7 | 8 |
| Arrange | 9 | 10 | 11 | 12 |

The following seven fields are required. Replay size is optional. The game writes the player's actual character even on the All board.

| Wire `id` | Type | Value |
| --- | ---: | --- |
| `0x20000004` | 2 | Score, decimal string `"0"` through `"2147483647"`, equal to top-level `score`. |
| `0x20000002` | 2 | Packed signed FILETIME. Use the conversion above. |
| `0x10000008` | 1 | Character index 0 Shishin, 1 Kosame, 2 Gensuke. |
| `0x10000006` | 1 | Native ranking stage, signed 16-bit. When deriving it from a replay, read the signed halfword at byte 62 and subtract one if it is 5 or above. |
| `0x10000001` / `0x10000003` / `0x10000005` | 1 | Continue / rank / extra fields. The supported native score writer sends zero for all three. |
| `0x10008011` | 1 | Optional. Zero or the actual recording size, 183,224 bytes to 8 MiB. |

A 12,345-point Arcade / Shishin score on `view=2` uses the following `columns`.

```json
[
  {"id":536870916,"type":2,"value":"12345"},
  {"id":536870914,"type":2,"value":"-5384438183559864396"},
  {"id":268435464,"type":1,"value":0},
  {"id":268435462,"type":1,"value":0},
  {"id":268435457,"type":1,"value":0},
  {"id":268435459,"type":1,"value":0},
  {"id":268435461,"type":1,"value":0}
]
```

Pass through the original 256-byte network header and payload. The title has separate Arcade, Blue and Arrange signatures. The replay must match the submitted mode, stage, character and score. The game's score timestamp and recording timestamp can differ.

### Returning an HTTP row to the original Xbox ranking screen

If you render your own ranking screen, read `nickname`, `rank`, `score` and the game's properties directly. If you implement the original Xbox stats calls, set the native row rating from `score`. The native **read** column numbers below differ from the JSON property IDs sent on **write**.

| Game | Native read column → value |
| --- | --- |
| Futari | Copy the numeric wire columns, then set read column 1 to the replay-present flag (0 or 1) and 8 to its byte count. Both use type 1. Only a confirmed `futari-xstorage-v1` attachment enables network playback. |
| ESPGaluda II | 1–6 → stage scores 1–6. 7–14 → character, zero-based stage, area, extra, player number, continues, weapon, packed name, in that order. 15 → the original UTC FILETIME. |
| SaiDaiOuJou | 1 → packed FILETIME (type 2). 2 → packed character/shot. 4 → stage. 5 → area. 6 → continues. The last four use type 1. |
| Guwange | On `view=1`, 4 → character and 5 → stage. On views 2–12, 6 → character and 4 → stage. Both use type 1. |

For ESPGaluda II, SaiDaiOuJou and Guwange, set native read column `0xFFFA` (type 1) to the attached byte count, or zero when `replay_id` is null. The stored recording-size write property alone can be nonzero even if the player declined to share the recording.

## PS4 board numbers

Use `board = mode base + ship or character offset`. All five games use the PS4 request bodies above. The numbers below include empty boards. `GET /boards` also gives the replay upload cutoff for each ID.

| Game | Mode bases | Offsets | Special boards and score unit |
| --- | --- | --- | --- |
| Ketsui Deathtiny | 0 Arcade, 10 Arcade 2, 20 Arcade SP, 30 Super Easy, 40 Deathtiny, 50 IKD 2007, 60 Custom | 0 All, 1 Type-A, 2 Type-B | 99 Unclassified (no replay). Use the original nonnegative integer score, up to `2^63−1`. |
| DoDonPachi DaiOuJou | 0 Arcade, 10 Custom, 20 Super Easy, 30 Premium, 40 Black Label, 50 DoDonPachi III, 60 Black Label One Loop | 0 Shotia A, 1 Shotia B, 2 Leinyan A, 3 Leinyan B, 4 Exy A, 5 Exy B, **6 All** | Regular boards use the native score, `displayed_score // 10` (max 999,999,999). Boards 10–16 are read-only. Board 99 is AC-Challenge cumulative medal points, up to 3,960,000, with no replay. |
| ESP Ra.De. Psi | 0 Arcade, 10 Super Easy, 20 Arcade Plus (`premium`), 30 Custom | 0 All, 1 Yusuke, 2 J-B, 3 Irori, 4 Alice | 88 Osarai, 99 Irori's Room. Both take native progress scores only. All boards use the title's unsigned 32-bit value. |
| Dangun Feveron | 0 Arcade, 10 Arcade Time Attack, 20 Super Easy, 30 Super Easy Time Attack, 40 Fever, 50 Fever Time Attack, 60 Custom, 70 Custom Time Attack | 0 All, 1 Type-A, 2 Type-B, 3 Type-C, 4 Uotaro/Poko | Use the original unsigned 32-bit score. Each Time Attack mode has its own board group. |
| Battle Garegga Rev.2016 | 0 Arcade, 10 Super Easy, 20 Premium, 30 Custom | 0 All, 1 Silver Sword, 2 Grass Hopper, 3 Flying Baron, 4 Wild Snail, 5 Gain, 6 Chitta, 7 Miyamoto, 8 Bornnam | Use the original unsigned 32-bit score. Supported Japanese and Korean data use `garegga`. |

The example boards in the game list correspond to Ketsui IKD 2007 / Type-A (`51`), DaiOuJou Black Label / All (`46`), ESP Ra.De. Arcade Plus / Irori (`23`), Feveron Super Easy Time Attack / Type-C (`33`), and Garegga Arcade / Miyamoto (`7`).

A DaiOuJou displayed score of 123,457 sends `"12345"` on a regular board. On board 99, 12,345 medal points send `"12345"` directly.

For a native recomp, take the board, score, comment and game-info bytes from the game's NpScore write. If one run qualifies for both an overall and a ship board, submit a separate score with its own `submission_id` to each. Retain each receipt with that board.

For playback, forward the **finished network replay** the game supplies to NpScore. Use the exact per-game `format` string in the directory, including its version suffix, across the game's supported releases. For DaiOuJou, ESP Ra.De., Feveron and Garegga the payload is one zlib stream that inflates to the original 0x80000-byte record. Hash and upload the compressed bytes. The server matches the embedded score, mode and ship to the board. Ketsui stores its native network payload byte-for-byte.
