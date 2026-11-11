# Contributing

Thanks for helping with 海酷拼音. Bug reports, conversions that came out
wrong, and pull requests are all welcome. Issues and pull requests can be
in English or Chinese.

## Getting it building

```sh
tools/fetch_sources.sh      # rime-ice, libpinyin, OpenCC, rime-double-pinyin
make data test              # anywhere: data, engine, hpcli, tests
echo zgr | build/hpcli      # try it; --layout pyjj and friends for shuangpin
```

Then the front end you're working on: `make install-mac` (macOS),
`make ibus` / `make fcitx5` (Linux), `haiku/sync.sh <host>` and `make install`
on Haiku. [README.md](README.md) has the details for each.

## Tests

- **`make test`** runs the engine tests (`tests/engine_test.cpp`). Add a
  case for the bug you fix or the behavior you add.
- **`make bench`** types about 300 everyday sentences, 50 abbreviations and
  some shuangpin (`tests/bench/`) with no user dictionary, and fails when
  the first candidate's hit rate drops more than a point below
  `tests/bench/baseline.txt`. Run it after any change to the decoder, the
  lexicon or the data. When a change improves the scores, run
  `make bench-update` and commit the new baseline with it.
- **CI** runs both on Linux, builds the Mac app (without the AI helper) and
  cross-compiles the Haiku add-on, on every pull request. It has to pass.

### Live typing tests

`macos/test-session.sh`, `linux/test-session.sh`,
`linux/fcitx5/docker-test.sh` and `tools/haiku-testkeys/` type real keys
through a running input method. Two rules for them, and for any new one:

- **Set the layout explicitly before typing**, and every other setting the
  test depends on. Never go by the defaults or whatever is in the settings
  file: a shuangpin layout left behind turns `nihao` into different
  syllables, and the test passes or fails for the wrong reason.
- **Leave the tester's own settings and words alone.** The existing scripts
  save the settings and `user.dict`, type with neither, and put them back;
  what a test types would otherwise teach their dictionary. Keys go to
  whatever has focus, so stop when focus moves.

## Code style

Follow the file you're in. In general:

- **C++17**, in the Haiku style: tabs, the return type on its own line in a
  definition, two blank lines between functions, `fMember`, `kConstant`,
  `sStatic`, `_PrivateMethod()`, `#pragma once`.
- **`engine/` stays portable**: no platform headers. Front ends talk to it
  through `Engine`.
- **Comments** are plain sentences about what happens and why, not what
  the next line does. Each file starts with one saying what it is
  (`// 海酷拼音 Haiku Pinyin for Linux: cloud candidates.`).
- **Shell** is POSIX `sh` with `set -e`. Dev-only scripts say so in their
  first comment.
- **Mac strings**: the window's words go through `L("…")`, with entries in
  `macos/Resources/zh-Hans.lproj` and `zh-Hant.lproj` (Taiwan usage).
  `macos/check-strings.py` checks the tables against the code.
- **English text** uses American spelling, and stays short and plain.

## Pull requests

- **One change per pull request**, and a commit for each step of it.
  Commit subjects name the area and say what it does now:
  `Mac: VoiceOver reads the candidate window`,
  `Linux: 云候选 keeps up with fast typing`.
- **Say how you tested it**: which platform, which layout, what you typed
  and what came out.
- **Something people will notice** gets a line under **Unreleased** in
  [CHANGELOG.md](CHANGELOG.md), written for someone typing, not for a
  programmer. Update the README when behavior it describes changes.
- **Anything new that reaches the network** is off until the user turns it
  on, and updates [PRIVACY.md](PRIVACY.md) in the same pull request.
- **Leave `VERSION` alone**; it moves as changes merge.
- **New data** needs a license compatible with GPL-3.0, named in the
  README's Data and licenses.

By contributing you agree your work is released under the project's
[GPL-3.0](LICENSE) license.

Security problems go privately through [SECURITY.md](SECURITY.md), not in
an issue.
