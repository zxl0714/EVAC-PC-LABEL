# Leaderboard nickname display

The launcher checks a changed shared nickname before saving. The check runs on
**Save**, Enter in the edit box and automatic save before launch/data selection.
Cancel keeps both the draft and the previous config; launch does not continue.
Saving anyway keeps the original UTF-8 nickname, not the display alias.
Changing only fullscreen does not repeat a previously accepted nickname warning.
Per-game nickname overrides are not affected by the shared nickname editor.

`common/nickname.cpp` supplies the same normalization, native aliases and byte
limits to the launcher and the game adapters:

| Native path | Display name |
| --- | --- |
| Futari, ESPGaluda II, SDOJ | NFKC-normalized printable ASCII, at most 15 bytes. Other names use `Player-` plus the first eight lowercase SHA-256 digits of the normalized UTF-8 name. |
| PS4 titles | At most 16 UTF-8 bytes, stopping before a partial codepoint. The leaderboard font must also contain the retained characters. |

The full community name is separate from this native display field. PS4 missing
glyphs are reported without pretending that the launcher's Windows font is the
game's font. The dialog distinguishes the name passed to the original game from
what that font can actually draw; it does not invent a replacement glyph preview.

## PS4 glyph coverage

`launcher/nickname_fonts.h` contains Unicode coverage metadata only. It contains
no font images, glyph metrics, archive contents or executable bytes. Coverage was
checked read-only against the effective base/update resources used by the native
ranking viewer:

| Title/data | Resource | Codepoints | Decoded resource SHA-256 |
| --- | --- | ---: | --- |
| Ketsui CUSA09062 01.00 | `system/font/m2shot_font.psb.m` | 793 | `b8dc3250cc215cde684b4b9623d218f27ccd674d776a240ac0e218fbb0263370` |
| Ketsui CUSA09062 01.02 | `system/font/m2shot_font.psb.m` | 809 | `d4c5b666c9bf50f3d84a209f787827f247afe65f819de8e515fc12d3b99ed7de` |
| DaiOuJou CUSA32997 01.04 | `system/font/m2shot_font.psb.m` | 806 | `ed2dd80622ba549f74be80154452d6cbae26203d993102d7bd89825a2928e4ef` |
| ESP Ra.De. CUSA15155 01.04 | `system/font/m2shot_font.psb.m` | 829 | `26e49ecf2a9ff437a1da94c7f15097641987785a725ce3dda12035c5957412ca` |

Ketsui's two supported versions differ. When its root program identifies a known
variant, the launcher uses that version. Without identifiable local data it
labels the affected versions explicitly instead of assuming the updated font.

To refresh coverage for a newly supported version: verify its input identity,
read the effective font's `code` keys after the normal read-only archive decode,
retain only single-codepoint glyph keys, sort/deduplicate, and compare the native
ranking viewer's font path. Never use the entire Japanese/Chinese Unicode block
as a coverage rule. The PSB v3/v4 name/string/entry offsets retain the v2 layout;
the header extensions are described in
[FreeMote's PsbHeader](https://github.com/UlyssesWu/FreeMote/blob/d87be1f3b8458c81822825fd6d56123d4df860b4/FreeMote/PsbHeader.cs).

Checks live in `tests/common_test.cpp` and `tests/launcher/ui_test.cpp`. They
cover ASCII, kana, Unicode aliases, emoji, duplicate characters, byte-boundary
truncation, Ketsui versions, overrides, cancel-without-write and launch cancel.
The font check is local; it makes no network requests.
