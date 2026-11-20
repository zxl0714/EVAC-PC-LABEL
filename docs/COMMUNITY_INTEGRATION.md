# Connect your recomp to EVAC-PC-LABEL

EVAC-PC-LABEL hosts community rankings and replays for the nine games below. Choose your game, read a board, and submit an eligible run. To share its recording, attach the original network replay to the score returned by the server.

```text
https://xiaolu.life/cave/api/v1/games/{game-id}
```

| Game | `game-id` | Example board | `rules` |
| --- | --- | --- | --- |
| [Mushihimesama Futari](#mushihimesama-futari) | `futari` | `view=100` · Ver.1.5 Arcade / Original / All | `standard`, `training`, `modified`, `unknown` |
| [ESPGaluda II](#espgaluda-ii) | `espgaluda2` | `view=39` · Black Label / Seseri / All-time | `standard` |
| [DoDonPachi SaiDaiOuJou](#dodonpachi-saidaioujou) | `sdoj` | `view=23` · Xbox 360 / Shot | `standard` |
| [Guwange](#guwange) | `guwange` | `view=2` · Arcade / Shishin | `standard` |
| [Ketsui Deathtiny](#ketsui-deathtiny) | `ketsui` | `board=51` · IKD 2007 / Type-A | `standard` |
| [DoDonPachi DaiOuJou](#dodonpachi-daioujou) | `daioujou` | `board=46` · Black Label / All | `standard` |
| [ESP Ra.De. Psi](#esp-rade-psi) | `esprade` | `board=23` · Arcade Plus / Irori | `standard` |
| [Dangun Feveron](#dangun-feveron) | `feveron` | `board=33` · Super Easy Time Attack / Type-C | `standard` |
| [Battle Garegga Rev.2016](#battle-garegga-rev2016) | `garegga` | `board=7` · Arcade / Miyamoto | `standard` |

For a first read, try SaiDaiOuJou's Xbox 360 / Shot board.

```sh
curl -fsS 'https://xiaolu.life/cave/api/v1/games/sdoj/leaderboard?view=23&start=1&count=5'
```

The URL selects the game. A port of a listed game uses the same game ID and boards. `daioujou` is the PS4 game. A game outside the table needs its own server route and board definitions.

## HTTP API

All endpoint paths below are relative to the game's URL. Use HTTPS from the game or its backend. Players choose their nickname and need no account registration or API key. Scores with the same normalized nickname share one personal best on each board and ruleset.

Send UTF-8 JSON in POST requests with `Content-Type: application/json` and `Content-Length`. Use JSON integers for board IDs and counts, decimal strings for scores and 64-bit identifiers, standard padded base64 for binary fields, and lowercase hexadecimal for SHA-256. Response examples show the fields to read. Clients can ignore additional keys.

Each board and `rules` pair has its own rankings. Use `standard` for the original scoring, run eligibility and slowdown. Futari also has `training` for practice, `modified` for altered rules and `unknown` when the run's rules cannot be determined. Ketsui's original Custom mode uses `standard`. An existing Ketsui Custom partition on boards 60–62 can be read using its saved `custom-<64 lowercase hex digits>` key. Replays use the ruleset of the score they attach to.

| Operation | Xbox 360 games | PS4 games |
| --- | --- | --- |
| Check availability | [GET /health](#get-health) | [GET /health](#get-health) |
| Discover boards | [GET /boards](#get-boards) | [GET /boards](#get-boards) |
| Read rankings | [GET /leaderboard](#get-leaderboard) | [GET /leaderboards](#get-leaderboards) |
| Look up a player | Filter by `name` on `/leaderboard` | [POST /identity](#post-identity) |
| Estimate a rank | [GET /estimate](#get-estimate) | Read `serial_rank` from the score receipt |
| Submit a score | [POST /scores](#post-scores-xbox-360) | [POST /scores](#post-scores-ps4) |
| Upload a replay | [POST /replays](#post-replays) | [POST /replays](#post-replays) |
| Download a replay | [GET /replays/{replay_id}](#download-a-replay) | [GET /entries/{entry_id}/replay](#download-a-replay) |

### GET /health

No parameters. If you need an availability check, read `status`. You can also call the operation you need directly.

```json
{"status":"ok"}
```

### GET /boards

No parameters. The Xbox 360 response lists board and ruleset pairs with published scores. `players` is the number of distinct nicknames. Use the game tables below if your menu also shows empty boards.

```json
{"boards":[{"view":23,"rules":"standard","players":2}]}
```

The PS4 response lists every defined board, including empty ones. Each entry includes its `id`, `mode`, `ship` and `upload_limit`. DaiOuJou also includes `doll`. `upload_limit` is the last serial rank eligible for a replay. A value of zero disables replay uploads on that board.

```json
{"boards":[{"id":46,"mode":"black_label","ship":"all","doll":"all","upload_limit":100}]}
```

### GET /leaderboard

Xbox 360 games only. A request for the first five ranks looks like this.

```http
GET /leaderboard?view=100&rules=standard&start=1&count=5
```

| Query field | Value |
| --- | --- |
| `view` | Required board number from the game's table. |
| `rules` | Defaults to `standard`. |
| `start` | First rank, 1–1,000,000. Default 1. |
| `count` | Page size, 1–100. Default 20. |
| `around` | Optional URL-encoded nickname. If ranked, the returned page starts at `max(1, rank - floor(count/2))`. |
| `name` | Optional URL-encoded nickname filter. Repeat for up to 100 players. Returns matches with their ranks on the whole board. |

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

`total` is the number of nicknames on the board before filtering. Each row is one player's highest score, with earlier submissions first on ties. `id` identifies the score and `created` is Unix time in seconds. A row with a recording supplies `replay_id`, `replay_size` in bytes and `replay_format`. ESPGaluda II, SaiDaiOuJou and Guwange also return `replay_sha256` when attached and omit the other replay metadata when `replay_id` is null. A valid empty board returns `"total":0` and `"rows":[]`.

### GET /estimate

Xbox 360 games only. Pass the numeric `view`, the prospective `score` as unsigned decimal text, and the URL-encoded player `name`. `rules` defaults to `standard`.

```http
GET /estimate?view=23&rules=standard&score=12345&name=Player
```

```json
{"view":23,"rules":"standard","rank":2}
```

`rank` is one plus the number of other nicknames with a personal best greater than or equal to this score. The caller's previous result is excluded. On an empty board it returns 1.

### POST /scores (Xbox 360)

All four Xbox 360 games send these fields. Copy the board and column values from the [game reference](#xbox-360-game-reference).

| Field | Type and value |
| --- | --- |
| `protocol` | JSON integer `1`. |
| `submission` | New 32-character lowercase hex ID per run and board, for example a UUID without hyphens. |
| `nickname` | 1–32 Unicode characters, with no control characters. The server applies NFKC normalization and trims whitespace. The result must fit in 48 UTF-8 bytes. ESPGaluda II, SaiDaiOuJou and Guwange also limit it to 32 UTF-16 code units. |
| `view` | JSON integer from the game's board table. |
| `rules` | String from the game list. |
| `score` | Nonnegative decimal string. Maximum `9223372036854775807` for Futari, ESPGaluda II and SaiDaiOuJou, or `2147483647` for Guwange. |
| `build` | Client build string, 1–96 characters, for example `"example-1.0"`. |
| `columns` | Up to 64 objects of the form `{"id":536870916,"type":2,"value":"12345"}` with unique IDs. The game's table gives the required objects. |

Column `id` is a JSON integer. The tables give decimal wire IDs and show the original hexadecimal IDs for reference. Type `1` carries a signed 32-bit JSON integer. Types `2` and `7` carry signed 64-bit decimal strings. Use type `2` for the score and timestamp properties marked as such in a game's table.

```json
{"id":"22222222222222222222222222222222","duplicate":false}
```

Keep `id` as the score ID. It identifies this submission even if another run holds the personal best. An identical retry returns the same ID and `duplicate:true`. If the same run belongs on several boards, use a separate submission ID and receipt for each board.

### POST /identity

PS4 games only. Send the player's nickname to obtain the account ID used by that game's rankings.

```json
{"nickname":"Player"}
```

```json
{"nickname":"Player","account_id":"7782643393547210698"}
```

The response shown is for Ketsui. `account_id` is a decimal string scoped to the game. Use it for a single-player lookup or reuse it from the score receipt.

### GET /leaderboards

PS4 games only. Send `board` as a board number from the game's table and `rules` as a string. The default page is ranks 1–20. To page, pass both `start` (1–4,294,967,295) and `count` (1–100). To look up one player, pass `account_id` in place of both paging parameters.

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

`total` counts nicknames before filtering. `serial_rank` is the 1-based position, earlier submissions first on ties. Equal scores share `rank`. `created_us` and `sorted_us` are decimal strings in Unix microseconds. `sorted_us` is the time of the latest accepted personal best on the board. An empty board returns `total:0`, `"sorted_us":"0"` and `entries:[]`. An unranked account returns an empty `entries` with the board's normal `total`. An attached `replay` is an object with `sha256`, `size` in bytes and `format`.

### POST /scores (PS4)

Use the eight fields below for all five PS4 games. Each [game section](#ps4-game-reference) includes a complete JSON body and the correct score units.

| Field | Type and value |
| --- | --- |
| `submission_id` | New 32-character lowercase hex ID per run and board. |
| `nickname` | 1–32 Unicode characters before and after NFKC normalization and trimming, at most 48 UTF-8 bytes after normalization, and no control characters. |
| `board` | JSON integer from the game's board table. |
| `rules` | `"standard"` for normal play. |
| `score` | Nonnegative decimal string with no leading zeroes except `"0"`. Check the game's maximum and unit. |
| `comment_base64` | Exactly 64 bytes before encoding. Use 64 zero bytes for an empty comment, as in the game examples. |
| `game_info_base64` | 0–189 bytes before encoding. Use `""` if you have no game info. |
| `build` | 1–80 ASCII letters, digits or characters from `._+-`. |

For the original NpScore UI, copy the score, the 64-byte comment and the declared game-info bytes from the game's score write. When displaying a row, base64-decode the comment and game info and give them back to the game as bytes. An independent leaderboard UI can use the empty values. The server stores these fields as opaque bytes for every PS4 game.

```json
{"entry_id":"22222222222222222222222222222222","accepted":true,"serial_rank":1,"upload_eligible":true,"account_id":"7782643393547210698"}
```

`accepted` means the run set a new personal best on this board. If the score is lower or equal, the request still returns 200, with `accepted:false` and the rank of the player's existing best. Use `upload_eligible` for this run's replay. Save its `entry_id` and the original `submission_id`. Retrying the same request returns the original receipt. If a run qualifies for separate overall and ship boards, submit it separately to each board.

### POST /replays

Share the completed network recording from the same run as the score. If the game produces it earlier, keep the bytes until the score receipt arrives. The decoded replay limit is 8 MiB, with tighter bounds in the game sections. Xbox score JSON is limited to 64 KiB. Use compact JSON and unwrapped base64 for replay uploads.

Pick the exact `format` from the game's section. Compute the digest and base64 from the same bytes.

```python
from base64 import b64encode
from hashlib import sha256

digest = sha256(recording).hexdigest()
encoded = b64encode(recording).decode("ascii")
```

Send the object for your game. `receipt` is the parsed response from `POST /scores`, `player` is the score's nickname, and `fmt` is the replay format string.

```python
# ESPGaluda II, SaiDaiOuJou and Guwange
body = {"protocol": 1, "score_id": receipt["id"], "nickname": player,
        "format": fmt, "sha256": digest, "data": encoded}

# Futari. The build must match the score. Use mode = "view-<view>" for native boards.
body = {"protocol": 1, "score_id": receipt["id"], "nickname": player,
        "format": fmt, "sha256": digest, "payload": encoded,
        "build": build, "mode": f"view-{view}"}

# PS4 games. Upload when receipt["upload_eligible"] is true.
body = {"entry_id": receipt["entry_id"], "submission_id": submission_id,
        "nickname": player, "format": fmt, "sha256": digest,
        "payload_base64": encoded}
```

| Game | HTTP 200 receipt | Use this to download the recording |
| --- | --- | --- |
| ESPGaluda II, SaiDaiOuJou, Guwange | `id` (replay ID), `score_id`, `sha256`, `bytes` (integer), `duplicate` (boolean) | `GET /replays/{id}` |
| Futari | `id` (replay ID), `duplicate` (boolean) | `GET /replays/{id}` |
| All five PS4 games | `entry_id`, `sha256`, `size` (integer), `format` | `GET /entries/{entry_id}/replay` |

An identical upload is safe to retry. A different recording for the same score conflicts. Once attached, the replay metadata appears on that score's leaderboard row when it is the player's best.

### Download a replay

| Game | HTTP 200 response | Playback bytes |
| --- | --- | --- |
| ESPGaluda II, SaiDaiOuJou, Guwange | JSON with `id`, `format`, `sha256`, `bytes` and `data` | Base64-decode `data`. |
| Futari | JSON with `id`, `score_id`, `format`, `sha256`, `size`, `payload`, `build` and `mode` | Base64-decode `payload` and select the reader for `format`. |
| All five PS4 games | `application/octet-stream` with `X-Replay-SHA256`, `X-Replay-Format` and `Content-Length` headers | Use the response body directly. |

Check the byte count, SHA-256 and format before passing the bytes to the game's own replay player. Use the ID returned for the selected row when reading a replay.

### Errors and retries

Successful operations return HTTP 200. Application errors have an `error` code and a diagnostic `request` ID.

```json
{"error":"submission_conflict","request":"0123456789ab"}
```

| Status | Meaning |
| --- | --- |
| 400 | Invalid input, board, ruleset or encoding. Check `error`. |
| 403 | PS4 endpoints received a browser `Origin` header. Call from the game or its backend. |
| 404 | Unknown route, score or replay. A valid empty board returns 200. |
| 409 | Submission ID reused for a different input, mismatched score and recording, or an ineligible replay. Check `error` before retrying. |
| 413 / 414 / 415 | Body or URL exceeds the limit, or the request needs UTF-8 JSON with a byte length. |
| 429 / 5xx / timeout | Back off and retry transient failures with the original URL and payload. HTTP 507 indicates that the service reached its storage limit. |

## Xbox 360 game reference

Each example is a complete `POST /scores` body. Replace the nickname, build, ID and example run data with your player's values. To use the original Xbox ranking UI, capture the game's board selector and stats write properties at `XSessionWriteStats`. The field tables show how to turn those values into JSON and map them back into a ranking row.

### Mushihimesama Futari

Compute `view = base + difficulty offset + character offset`. This defines 96 boards.

| Mode | Base | Difficulty offset | Character offset |
| --- | ---: | --- | --- |
| Ver.1.5 Arcade / Xbox 360 | 100 / 200 | Original 0, Maniac 10, Ultra 20 | All 0, Reco Normal 1, Palm Normal 2, Reco Abnormal 3, Palm Abnormal 4 |
| Arrange | 300 | Original 0, Maniac 10, Ultra 20 | All 0 |
| Novice (Easy) | 400 | Original 0, Maniac 10, Ultra 20 | Same as Ver.1.5 |
| Black Label Arcade / Xbox 360 | 500 / 600 | Original 0, Maniac 10, God 20 | All 0, Reco 1, Palm 2 |
| Ver.1.01 Arcade / Xbox 360 | 700 / 800 | Original 0, Maniac 10, Ultra 20 | Same as Ver.1.5 |

For example, Black Label Xbox 360 / God / Palm uses `view=622`. The character offsets identify board categories. Use the game's separate player, character and type values in the columns when supplying them.

| JSON `id` | Original property | `type` | `value` |
| ---: | --- | ---: | --- |
| 1 | `0x20000001` | 2 | Match the top-level score. |
| 2 | `0x20000002` | 2 | The game's 64-bit run time field, as a signed decimal string. |
| 3 | `0x10000003` | 1 | Player selector from the result. |
| 4 | `0x10000004` | 1 | Character selector from the result. |
| 5 | `0x10000005` | 1 | Shot-type selector from the result. |
| 6 | `0x10000006` | 1 | Stage from the result. |
| 7 | `0x10000007` | 1 | Area within the stage. |
| 8 | `0x10000008` | 1 | Packed name-entry bytes from the game, as a signed integer. |
| 9 | `0x10000009` | 1 | Replay size from the game's stats write, in bytes. |
| 32785 | `0x10008011` | 1 | Storage size from the game's stats write, in bytes, when present. |

The wire ID is the original property ID masked with `0xffff`. An independent ranking client can send only column 1. Copy the additional properties when bridging the original UI. This body submits a Ver.1.5 Arcade / Original score to the All board.

```json
{
  "protocol": 1,
  "submission": "11111111111111111111111111111111",
  "nickname": "Player",
  "view": 100,
  "rules": "standard",
  "score": "12345",
  "build": "example-1.0",
  "columns": [{"id":1,"type":2,"value":"12345"}]
}
```

- **`futari-xstorage-v1`** carries the 104-byte network replay header and the original compressed stream. The big-endian 32-bit words at offsets 0 and 4 give the total byte count and version `0x09100800`. Total size is 105–`0xD30B8` bytes.
- **`futari-replaydata-v1`** shares one recording from the local save. Write 99 big-endian `(offset:u32, size:u32)` pairs. Slot 0 is `(792, record_bytes)` and the remaining 98 pairs are `(792 + record_bytes, 0)`. Append the unchanged local record. Its outer and inner version words at offsets 0 and 12 both equal `0x09100800`. The word at offset 8 equals `record_bytes - 4`. The record is at least 160 bytes. Use the local-slot reader for this format.

For the original ranking screen, set row rating from `score`, use read column 1 for the confirmed network replay flag (0 or 1) and read column 8 for its byte count. Copy the other numeric properties by wire ID. The original network replay menu plays `futari-xstorage-v1`.

### ESPGaluda II

Each cell lists the `view` values in the order **All / character 0 / character 1 / character 2**. Use the character index from the selected mode. Ageha uses index 0 in Xbox 360 and Seseri uses index 2 in Black Label.

| Mode (native selector) | All-time | Monthly | Weekly |
| --- | --- | --- | --- |
| Xbox 360 (20) | 24 / 25 / 26 / 27 | 32 / 28 / 29 / 30 | 31 / 33 / 34 / 35 |
| Arcade (21) | 1 / 10 / 11 / 12 | 13 / 14 / 15 / 16 | 17 / 18 / 19 / 2 |
| Arrange (22) | 20 / 21 / 3 / 4 | 48 / 5 / 6 / 7 | 8 / 9 / 22 / 23 |
| Black Label (23) | 73 / 37 / 38 / 39 | 40 / 41 / 42 / 43 | 44 / 45 / 46 / 47 |
| Omake! (24, Arrange2 in the board data) | 61 / 62 / 63 / 64 | 65 / 66 / 67 / 68 | 69 / 70 / 71 / 72 |
| Xbox 360 Novice (25) | 49 / 50 / 51 / 52 | 53 / 54 / 55 / 56 | 57 / 58 / 59 / 60 |
| Black Label Novice (26) | 36 / 74 / 75 / 76 | 77 / 78 / 79 / 80 | 81 / 82 / 83 / 84 |

The period is part of the board ID. The server keeps personal bests on these 84 boards across calendar periods. Use the original game's eligible view list when submitting a run to multiple boards.

| JSON `id` | Original property | `type` | `value` |
| ---: | --- | ---: | --- |
| 536870913 | `0x20000001` | 2 | Total score. Required, equal to top-level `score`. |
| 536870914–536870919 | `0x20000002`–`0x20000007` | 2 | Individual scores for stages 1–6, in order. |
| 268435464 | `0x10000008` | 1 | Character index, 0–2. Required with a replay. |
| 268435465 | `0x10000009` | 1 | Number of continues. |
| 268435466 | `0x1000000A` | 1 | The game's `EXTRA` result field. |
| 268435467 | `0x1000000B` | 1 | The game's `WEAPON` selector. |
| 268435468 | `0x1000000C` | 1 | Zero-based stage. Stage 1 uses 0. |
| 268435469 | `0x1000000D` | 1 | The game's packed name-entry bytes as a signed integer. |
| 268435471 | `0x1000000F` | 1 | The game's `PLAYERNO` field. |
| 268435472 | `0x10000010` | 1 | The game's `AREA` field. |
| 536870929 | `0x20000011` | 2 | Positive UTC FILETIME in 100 ns ticks since 1601-01-01. Required with a replay. |
| 268468241 | `0x10008011` | 1 | Exact network replay size in bytes. Required with a replay. |

The total score alone is enough for a score without a replay. Pass through the optional result fields from the game when using its original ranking UI. To attach a replay, include the timestamp, size and character in the original score write. This body is for a 999-point Black Label / Seseri run with a 136-byte replay.

```json
{
  "protocol": 1,
  "submission": "11111111111111111111111111111111",
  "nickname": "Player",
  "view": 39,
  "rules": "standard",
  "score": "999",
  "build": "example-1.0",
  "columns": [
    {"id":536870913,"type":2,"value":"999"},
    {"id":268435464,"type":1,"value":2},
    {"id":536870929,"type":2,"value":"134368194753020000"},
    {"id":268468241,"type":1,"value":136}
  ]
}
```

For `espgaluda2-native-v2`, send the original 104-byte network header and payload. The header fields are big-endian. Version at byte 4 is `u32(2)`, time at byte 8 and score at byte 16 are `u64`, character at byte 36 is `u32`, and the payload size at byte 96 is `u32(total_bytes - 104)`. Byte 0 starts the local-save checksum and can be zero in a network replay. The server matches time, score, character and total length to the score columns.

For native reads, use type 2 for columns 1–6 (stage scores) and 15 (UTC FILETIME). Type 1 columns 7–14 are character, stage, area, extra, player number, continues, weapon and packed name. Use `score` for row rating and `0xFFFA` (type 1) for confirmed replay bytes or zero.

### DoDonPachi SaiDaiOuJou

Each cell lists **Shot / Laser / Expert** `view` IDs. An em dash marks a combination without a hosted board.

| Mode | Type A | Type B | Type C | Type D / Saya |
| --- | --- | --- | --- | --- |
| Arcade HD | 2 / 7 / 11 | 3 / 9 / 12 | 4 / 10 / 13 | 85 / 86 / 87 |
| Xbox 360 | — | — | — | 23 / 24 / 25 |
| Novice | 14 / 17 / 20 | 15 / 18 / 21 | 16 / 19 / 22 | 88 / 89 / 90 |
| Ver.1.5 | 35 / 38 / 41 | 36 / 39 / 42 | 37 / 40 / 43 | 91 / 92 / 93 |

| JSON `id` | Original property | `type` | `value` |
| ---: | --- | ---: | --- |
| 536870916 | `0x20000004` | 2 | Original points, equal to top-level `score`. Replay `i64` at byte 24. |
| 536870914 | `0x20000002` | 2 | Packed FILETIME decimal string. Replay `i64` at byte 32. |
| 268435463 | `0x10000007` | 1 | `16 × character + shot`. A/B/C/D are 0/1/2/3 and Shot/Laser/Expert are 0/1/2. Xbox 360 uses Saya, giving 48/49/50. Replay `u32` at byte 52. |
| 268435462 | `0x10000006` | 1 | Original `STAGE` index, 0 for Stage 1, in 0–65535. Replay `u32` at byte 56. |
| 268435461 | `0x10000005` | 1 | Original `AREA` field, 0–65535. Replay `u32` at byte 60. |
| 268435457 | `0x10000001` | 1 | Original `CONTINUE` field, 0–65535. Replay `u32` at byte 76. |
| 268468241 | `0x10008011` | 1 | Optional. Zero or the exact network recording size in bytes, greater than 304 and at most 8 MiB. |

The first six fields are mandatory. Match the packed character and shot to the board. When using the recording to fill the columns, read the offsets above as big-endian, starting at byte zero.

A FILETIME property from the game already has the two 32-bit words in the correct order for the wire. Send its signed bits unchanged. If starting with standard positive FILETIME ticks, use this conversion for **SaiDaiOuJou and Guwange**. Ticks count 100 ns intervals since 1601-01-01 UTC. From Unix milliseconds, compute `ticks = 116444736000000000 + unix_ms * 10000`.

```python
def packed_filetime(ticks):
    # 0 < ticks < 2**63
    swapped = ((ticks & 0xffffffff) << 32) | (ticks >> 32)
    signed = swapped - (1 << 64) if swapped >= (1 << 63) else swapped
    return str(signed)
```

For example, `packed_filetime(0x01dd5fb4b546a200)` gives `"-5384438183559864396"`. This is a complete Xbox 360 / Saya / Shot submission.

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

For `sdoj-disc-native-v1`, send the 304-byte native network header and its unchanged input data. Byte 0 starts big-endian magic `0x01304250`, byte 92 starts the unsigned 32-bit total length, and byte 8 starts the unsigned 16-bit checksum. The total length is even. The checksum is the sum of all big-endian 16-bit words multiplied by their 1-based positions, skipping word 5 (the checksum itself), modulo 65,536. The server compares the six required score properties to the replay fields.

For native reads, put `score` in the row rating. Use column 1 (type 2) for the packed time, then columns 2, 4, 5 and 6 (type 1) for packed character/shot, stage, area and continues. Use `0xFFFA` (type 1) for confirmed replay bytes or zero.

### Guwange

| Mode | All | Shishin | Kosame | Gensuke |
| --- | ---: | ---: | ---: | ---: |
| Arcade | 1 | 2 | 3 | 4 |
| Blue | 5 | 6 | 7 | 8 |
| Arrange | 9 | 10 | 11 | 12 |

| JSON `id` | Original property | `type` | `value` |
| ---: | --- | ---: | --- |
| 536870916 | `0x20000004` | 2 | Original points from 0 to 2,147,483,647, equal to top-level `score`. |
| 536870914 | `0x20000002` | 2 | Packed FILETIME decimal string. Use the [conversion above](#dodonpachi-saidaioujou) if needed. |
| 268435464 | `0x10000008` | 1 | Shishin 0, Kosame 1, Gensuke 2. Also use the actual character on All boards. |
| 268435462 | `0x10000006` | 1 | Original ranking stage, a signed 16-bit value. From replay, read big-endian `i16` at byte 62 and subtract one if the value is 5 or greater. |
| 268435457 | `0x10000001` | 1 | Continue field. Send 0. |
| 268435459 | `0x10000003` | 1 | Game rank field. Send 0. |
| 268435461 | `0x10000005` | 1 | Extra field. Send 0. |
| 268468241 | `0x10008011` | 1 | Optional. Zero or the exact recording size, 183,224 bytes to 8 MiB. |

This body submits an Arcade / Shishin score. Only the size property is optional.

```json
{
  "protocol": 1,
  "submission": "11111111111111111111111111111111",
  "nickname": "Player",
  "view": 2,
  "rules": "standard",
  "score": "12345",
  "build": "example-1.0",
  "columns": [
    {"id":536870916,"type":2,"value":"12345"},
    {"id":536870914,"type":2,"value":"-5384438183559864396"},
    {"id":268435464,"type":1,"value":0},
    {"id":268435462,"type":1,"value":0},
    {"id":268435457,"type":1,"value":0},
    {"id":268435459,"type":1,"value":0},
    {"id":268435461,"type":1,"value":0}
  ]
}
```

For `guwange-base-native-v1`, send the original 256-byte network header and its payload. The first 32 bytes hold the NUL-terminated signature `GWN_for_XB360_AC20100820`, `GWN_for_XB360_BL20100820` or `GWN_for_XB360_XB20100820` for Arcade, Blue or Arrange. At byte 36 the big-endian header contains `u32` payload length, `u64` UTC FILETIME, `u32` kind (2), `u32` mode (0/1/2) and `u32` play kind (0 for Play, 1 for Score Attack). The checksum at byte 32 is the sum of the signed payload bytes modulo 65,536, written as big-endian `u16`. The two `(character:u32, score:u32)` pairs start at bytes 76 and 104. An absent player uses `0xffffffff` as the character. One player must match the submitted score and character. The recording's mode and stage must match the board and score columns. The score and recording timestamps may differ.

For native reads, put `score` in the row rating. On view 1, use read columns 4 and 5 for character and stage. On views 2–12, use 6 and 4. Use `0xFFFA` for confirmed replay bytes or zero. These read columns use type 1.

## PS4 game reference

The five games use [the same HTTP body fields](#post-scores-ps4). Use the per-game board, integer score and original network recording below. When bridging NpScore, capture the complete payload handed to its game-data upload function. For native playback, pass the downloaded compressed bytes to the corresponding game's network replay reader.

### PS4 replay envelope

All five formats carry the original zlib stream of a `0x80000`-byte network record. Hash and upload the compressed bytes. The little-endian record header gives the metadata for matching a replay to its score.

| Byte offset | Type | Field |
| ---: | --- | --- |
| 0 / 4 | `u32` each | Record version and data version. See each game below. |
| 80 | 16 bytes | Original core name, NUL-terminated. |
| 104 / 106 | `u16` each | Record kind (0) and game mode. |
| 108 | `u32` | Score in the game's native units. |
| 112 / 114 | `u16` each | Packed ship or character selector and stage. |

Keep the snapshot and input stream from the original recording. The per-game ship and mode values explain how to match a network record to a score. PS4 replay eligibility is based on the entry's serial rank at the time the score is accepted.

### Ketsui Deathtiny

Use `board = base + ship`, with `ship` equal to 0 for All, 1 for Type-A and 2 for Type-B. Board 99 (Unclassified) accepts scores without replays. Other boards allow replays in the top 100 overall or top 10 for a ship. Score is the original integer, up to `9223372036854775807` for a score write.

| Mode | Base | Replay mode |
| --- | ---: | ---: |
| Arcade | 0 | 0 |
| Arcade 2 | 10 | 0 |
| Arcade SP | 20 | 0 |
| Super Easy | 30 | 1 |
| Deathtiny | 40 | 2 |
| IKD 2007 | 50 | 4 |
| Custom | 60 | 3 |

The following body submits IKD 2007 / Type-A.

```json
{
  "submission_id": "11111111111111111111111111111111",
  "nickname": "Player",
  "board": 51,
  "rules": "standard",
  "score": "12345",
  "comment_base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64": "",
  "build": "example-1.0"
}
```

Replay format `ketsui-npscore-1.02` uses record version `0x10001`, data version `0x10000` or `0x10001` and core `ARCADE_01000`. The record holds a 32-bit score and a character selector of 0–7. Values 0–3 are Type-A, and 4–7 are Type-B.

### DoDonPachi DaiOuJou

Use `board = base + offset`. Offsets 0–5 are Shotia A, Shotia B, Leinyan A, Leinyan B, Exy A and Exy B in order. Offset 6 is All. A regular score is the game's native integer, or `floor(displayed_score / 10)` when starting from the displayed decimal score. The maximum is 999,999,999. For example, displayed 123,457 goes on the wire as `"12345"`.

| Mode | Base | Replay mode |
| --- | ---: | --- |
| Arcade | 0 | 0, or 3 for a game-eligible Arcade run with the native Custom flag |
| Custom (read-only) | 10 | — |
| Super Easy | 20 | 1 |
| Premium | 30 | 2 |
| Black Label | 40 | 4 |
| DoDonPachi III | 50 | 5 |
| Black Label One Loop | 60 | 4 |

Board 99 is AC-Challenge's cumulative medal points. Send the total directly, up to 3,960,000, with no replay. Boards 10–16 are read-only. The regular writable boards allow replays in the top 100 overall or top 10 for a ship/doll combination.

The following body submits Black Label / All.

```json
{
  "submission_id": "11111111111111111111111111111111",
  "nickname": "Player",
  "board": 46,
  "rules": "standard",
  "score": "12345",
  "comment_base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64": "",
  "build": "example-1.0"
}
```

Replay format `daioujou-npscore-1.04` uses record version `0x1000E` or `0x10030` and data version `0x10000`. Version `0x1000E` uses `ARCADE_01000`. Version `0x10030` can use `ARCADE_01000`, `ARCADE_01010`, `ARCADE_01020` or `ARCADE_01030`. The character selector is the 0–5 ship/doll offset above, even on an All board.

### ESP Ra.De. Psi

Use `board = base + offset`. Offsets 0–4 are All, Yusuke, J-B, Irori and Alice. Score is the original unsigned 32-bit points, up to 4,294,967,295. Replay cutoff is top 100 overall or top 10 for a character.

| Mode | Base | Replay mode |
| --- | ---: | ---: |
| Arcade | 0 | 0 |
| Super Easy | 10 | 1 |
| Arcade Plus (`premium`) | 20 | 2 |
| Custom | 30 | 3 |

Board 88 (Osarai) and 99 (Irori's Room) take the game's unsigned progress value from their own score write, without replays.

The following body submits Arcade Plus / Irori.

```json
{
  "submission_id": "11111111111111111111111111111111",
  "nickname": "Player",
  "board": 23,
  "rules": "standard",
  "score": "12345",
  "comment_base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64": "",
  "build": "example-1.0"
}
```

Replay format `esprade-npscore-1.04` uses record version `0x10019`, `0x10020`, `0x10021` or `0x10022`, data version `0x10000` and core `ARCADE_01000`, `ARCADE_01010`, `ARCADE_01020` or `ARCADE_01030`. The 0–7 character selector includes the player side. The character-specific board offset is `(selector % 4) + 1`.

### Dangun Feveron

Use `board = base + offset`. Offsets 0–4 are All, Type-A, Type-B, Type-C and Uotaro/Poko. Score is the original unsigned 32-bit points, up to 4,294,967,295.

| Mode | Base | Replay mode |
| --- | ---: | ---: |
| Arcade | 0 | 0 |
| Arcade Time Attack | 10 | 0 |
| Super Easy | 20 | 1 |
| Super Easy Time Attack | 30 | 1 |
| Fever | 40 | 2 |
| Fever Time Attack | 50 | 2 |
| Custom | 60 | 3 |
| Custom Time Attack | 70 | 3 |

Replay cutoff is top 100 for All boards in the first six groups and top 10 for ship boards and both Custom groups. The following body submits Super Easy Time Attack / Type-C.

```json
{
  "submission_id": "11111111111111111111111111111111",
  "nickname": "Player",
  "board": 33,
  "rules": "standard",
  "score": "12345",
  "comment_base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64": "",
  "build": "example-1.0"
}
```

Replay format `feveron-npscore-1.00` uses record version `0x10001` or `0x10002`, data version `0x10000` and core `ARCADE_01000` or `ARCADE_01010`. The character selector is `(side_and_ship << 8) OR (weapon << 4) OR speed`, with `side_and_ship` in 0–7, `weapon` in 0–3 and `speed` in 0–4. The ship-specific board offset is `(side_and_ship & 3) + 1`. Time Attack keeps the same mode value in the replay as its regular mode.

### Battle Garegga Rev.2016

Use `board = base + offset`. Offsets 0–8 are All, Silver Sword, Grass Hopper, Flying Baron, Wild Snail, Gain, Chitta, Miyamoto and Bornnam, in order. Score is the original unsigned 32-bit points, up to 4,294,967,295. Replay cutoff is top 100 overall or top 10 for a ship. Supported Japanese and Korean data both use `garegga`.

| Mode | Base | Replay mode |
| --- | ---: | ---: |
| Arcade | 0 | 0 |
| Super Easy | 10 | 1 |
| Premium | 20 | 2 |
| Custom | 30 | 3 |

The following body submits Arcade / Miyamoto.

```json
{
  "submission_id": "11111111111111111111111111111111",
  "nickname": "Player",
  "board": 7,
  "rules": "standard",
  "score": "12345",
  "comment_base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
  "game_info_base64": "",
  "build": "example-1.0"
}
```

Replay format `garegga-npscore-1.00` uses record version `0x10012`, `0x10100`, `0x10200`, `0x10300` or `0x10400`, data version `0x10000` and core `ARCADE_01000`, `ARCADE_01010`, `ARCADE_01020`, `ARCADE_01030` or `ARCADE_01040`. The 0–127 character selector stores the ship in bits 4–6. For a ship board, the offset is `((selector >> 4) & 7) + 1`.
