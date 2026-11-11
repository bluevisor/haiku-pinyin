# 海酷拼音 Haiku Pinyin

A modern Chinese input method for Haiku, Linux and macOS, in the spirit of
搜狗拼音 and the macOS pinyin input: whole-sentence conversion, abbreviations
(`zgr` → 中国人, `bjdx` → 北京大学), half-typed syllables, learning from what
you pick, and inline composition. One engine, three native front ends.

![海酷拼音 typing in a text view, and in Temino](docs/screenshot.png)

| | Get it | Runs as | Only here |
|---|---|---|---|
| **macOS** 13+ | [`.pkg` or `.dmg`](https://github.com/bluevisor/haiku-pinyin/releases/latest), signed and notarized | InputMethodKit input method | on-device AI picks and gray completion (Apple silicon), self-update, settings window |
| **Linux** | [`.deb`](https://github.com/bluevisor/haiku-pinyin/releases/latest) for arm64 / amd64 (Ubuntu 22.04+, Debian 12+), then signed updates with `apt`; or from source | IBus engine (GNOME and anywhere IBus runs) | settings in the top-bar input menu |
| **Haiku** (64-bit) | [`.hpkg`](https://github.com/bluevisor/haiku-pinyin/releases/latest) for arm64 / x86_64, with signed checksums | input_server add-on | Deskbar menu, inline in BTextView and Terminal |

The features below work on all three, except where marked.

- **Full pinyin** and **shuangpin**: 拼音加加, 小鹤, 自然码, 微软, 智能ABC
- **简体 / 繁體** output
- **Sentences scored with word pairs** (a bigram model from libpinyin), with the
  last word you typed as context: `wozaixuexizhongwen` → 我在学习中文
- **AI** (macOS, Apple silicon): a small language model on the Mac reads the
  text before the cursor, moves the candidate that reads best first (✦), and
  after a commit shows what likely comes next in gray after the cursor
  (中华人民 → 共和国), taken with Tab; any other key types on past it
- **v-mode**: `v123` → 一百二十三 / 壹佰贰拾叁 / 壹佰贰拾叁元整, `v1+2*3` → 7,
  `v2026.11.7` → 2026年11月7日 / 二〇二六年十一月七日, `vrq` `vsj` `vxq` → today's
  date, the time, the weekday, and symbols by code (`vjt` arrows, `vsx` maths,
  …); Shift+V in shuangpin
- **模糊音**: z/zh c/ch s/sh, an/ang en/eng in/ing, n/l f/h r/l, each optional
- **中英混输**: `hello` also offers *hello*; nonsense pinyin offers English first
- Optional **cloud candidates** (Google Input Tools), off until you turn on 云候选, marked ☁
- Emoji among the candidates: 太阳 → 🌞, 猫 → 🐱 (monochrome, via Noto Emoji)
- Full-width punctuation (`,` → `，`, `"` → `“”` or `「」`), left alone after digits
- Shift tap switches 中/英; Caps Lock types English
- Hover, the scroll wheel and the page arrows work in the candidate window;
  Ctrl+Delete forgets a word it learned
- Space or 1–9 pick, `-` `=` page, `[` `]` take the first/last character of a
  word (以词定字), Enter commits the letters, Backspace reopens the last pick
- Types inline in input-method-aware views (BTextView, Terminal) and plain
  text into everything else

## Install on Haiku

Download the package for your machine from the
[releases](https://github.com/bluevisor/haiku-pinyin/releases) (`arm64` or
`x86_64`; 32-bit Haiku's gcc2 input_server can't load it), copy it into
`~/config/packages`, then load it:

```sh
haiku_pinyin_restart      # restarts the input_server; keep Deskbar menus closed
```

(Logging out and in works too.) The candidate window needs a CJK font such
as Noto Sans CJK; for emoji, add Noto Emoji. App_server falls back to both
on its own. An `.hpkg` can't carry a signature, so check it against the
signed checksums (see [Checking a download](#checking-a-download)).

To build it yourself instead, see Build below.

## Install on Linux

On Linux it's an [IBus](https://github.com/ibus/ibus) engine (GNOME,
and anywhere else IBus runs). On Ubuntu 22.04+ or Debian 12+, download
`haiku-pinyin_<version>_<arch>.deb` (`arm64` or `amd64`) from the
[releases](https://github.com/bluevisor/haiku-pinyin/releases), then:

```sh
sudo apt install ./haiku-pinyin_*.deb
haiku-pinyin-enable        # as yourself: restarts IBus, adds it to GNOME
```

The package also adds the project's apt repository and its release key
(`/etc/apt/sources.list.d/haiku-pinyin.sources`,
`/usr/share/keyrings/haiku-pinyin-archive-keyring.gpg`), so `sudo apt update
&& sudo apt upgrade` brings each new release, and apt refuses one that isn't
signed with that key. The repository is the latest GitHub release itself.

Or build it with the IBus headers:

```sh
sudo apt install build-essential libibus-1.0-dev curl python3   # Debian/Ubuntu
tools/fetch_sources.sh && make data ibus
sudo make install-ibus && linux/enable.sh  # into /usr, where IBus looks
# or, without root (GNOME, or another systemd user session):
linux/install-user.sh                      # into ~/.local; --remove undoes it
```

`haiku-pinyin-enable` (`linux/enable.sh` in a build) restarts IBus and adds **海酷拼音 Haiku Pinyin** to
GNOME's input sources (or add it under Settings → Keyboard → Input Sources →
Chinese); switch with Super+Space. On GNOME, don't use `ibus restart`: the
daemon runs as a systemd user unit and stays stopped. Restart it with
`systemctl --user restart org.freedesktop.IBus.session.GNOME.service`. Apps
started outside the desktop session need `GTK_IM_MODULE=ibus` to reach it. Everything in Using it
below applies, except that the settings live in the input source menu in the
top bar, and are saved in `~/.config/haiku-pinyin/` with `user.dict`.

## Install on macOS

On a Mac it's an InputMethodKit input method (macOS 13 or later, Apple
silicon or Intel). Download `HaikuPinyin-<version>.pkg` from the
[releases](https://github.com/bluevisor/haiku-pinyin/releases) and open it:
the installer puts it in `~/Library/Input Methods` for you alone (no admin
password), adds it to the input menu and switches to it. Or download
`HaikuPinyin-<version>.dmg`, open it and open **HaikuPinyin**: it offers to
install itself into
`~/Library/Input Methods`, adds itself to the input menu and switches to it.
When macOS wants that done by hand, it says so and opens the place: System
Settings → Keyboard → Input Sources → Edit… → **+** → Chinese, Simplified →
**Haiku Pinyin**. Switch to it with Control+Space or 🌐. Both are signed
(the package with a Developer ID Installer certificate) and notarized, and it
keeps itself up to date: once a day it looks for a newer
release, checks the download's signature, and puts it in place while the
Mac sits idle (**自动更新** in the settings turns that off; **检查更新…**
looks now).

A Shift tap switches 中/英, with a 中 or 英 by the text to say which;
**设置…** in the input menu opens the settings window. Control-Delete forgets
a learned word. What it learns is in
`~/Library/Application Support/HaikuPinyin/user.dict`.

**AI** needs Apple silicon and a model: **下载模型** in the settings window
fetches Qwen3-1.7B-Base and Qwen3.5-0.8B-Base, converted to 4-bit MLX (about
1.4 GB), from Hugging Face (or its mirror) into
`~/Library/Application Support/HaikuPinyin/models`, and checks every file. A
stopped download carries on where it left off. A helper process holds the
model (about 1 GB of memory while AI is on) and answers on the side, like the
cloud: typing never waits for it. The settings window turns AI picks and
completions on or off and chooses the model: 自动 uses the 1.7B and falls
back to the 0.8B on a Mac too slow for it; any MLX model in that folder can be
chosen. Nothing you type leaves the Mac.

To build it yourself, with Xcode's command line tools:

```sh
tools/fetch_sources.sh && make data install-mac   # into ~/Library/Input Methods
```

`macos/install.sh --remove` uninstalls it. A build of your own is signed ad
hoc and never updates itself. `macos/ai/fetch-models.sh` converts the models
itself (or links copies you have: `fetch-models.sh <directory>`);
`NO_AI=1 macos/build.sh` builds without the helper. `make mac-release`
(`macos/release.sh`) makes the download: signed with a Developer ID,
notarized and stapled, as an installer package (`macos/pkg.sh`), a disk
image, and the zip and `latest.json` the updater reads, all in `dist/` for a
GitHub release; `macos/ai/upload-models.sh` publishes the models.

## Checking a download

Each release has `SHA256SUMS`, signed with the project's release key in
`SHA256SUMS.asc`. The key is `tools/release-key.asc` here and
`haiku-pinyin-release.asc` in each release, fingerprint
`1016 60C1 F375 2B00 10E7  25F6 E34D C770 E462 7F58`. In the folder with the
downloads:

```sh
gpg --import haiku-pinyin-release.asc
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum --ignore-missing -c SHA256SUMS   # shasum -a 256 -c on a Mac; sha256sum on Haiku
```

The Mac downloads are also signed and notarized by Apple, and apt checks the
Linux repository on its own. `tools/sign-release.sh` makes the checksums and
the apt repository for a release, after the Mac, Linux and Haiku packages are
in `dist/`.

## Using it

Switch to it like any input method: on Haiku from the keyboard menu in
Deskbar or with **Alt+Space** (with [Kigo](https://github.com/bluevisor/kigo),
which takes Alt+Space for itself, **Win+Space**); on Linux with Super+Space;
on a Mac with Control+Space or 🌐.

| Key | |
|---|---|
| letters | pinyin (or shuangpin codes); the sentence is candidate 1 |
| Space, 1–9 | take the highlighted / numbered candidate |
| ← → ↑ ↓, `-` `=`, Page Up/Down | move the highlight, page |
| `[` `]` | just the first / last character of the candidate (以词定字) |
| Enter | type the letters as they are |
| Backspace | delete a letter; past a pick, reopen it |
| Escape | cancel |
| Ctrl+Delete | forget the highlighted word it learned from you |
| Tab | (Mac, AI) take the gray completion; Escape or any other key drops it |
| `v` | v-mode (Shift+V in shuangpin): see below |
| Shift (tap) | 中 / 英 |
| Caps Lock | English while it's on |

Clicks, the scroll wheel and the page arrows work in the candidate window.

**v-mode.** `v` (Shift+V in shuangpin, where v is a key) then:

| Typed | Offers |
|---|---|
| a number: `v123`, `v12.05` | 一百二十三, 壹佰贰拾叁, 壹佰贰拾叁元整, 一二三, ⑫, XII; 十二点零五, 壹拾贰元零伍分 |
| a sum: `v1+2*3`, `v(1+2)/4`, `v20%` | 7 and 1+2*3=7 |
| a date: `v2026.11.7` (or `-` `/`) | 2026年11月7日, 二〇二六年十一月七日, 2026-11-07, with the weekday |
| `rq` `sj` `xq` | today's date, the time, the day of the week |
| `bd` `kh` `jt` `sx` `dw` `hb` | punctuation, brackets, arrows, maths, units, currency |
| `xl` `xh` `ts` `py` `zy` | Greek, numbering (①⑴⒈㈠Ⅰ), shapes and marks, pinyin tones, 注音 |

After a number, digits are typing, so letters pick (a b c …); after a code,
digits pick as usual. `v` alone lists one of each. Letters that are no code
(`very`) are pinyin and English as before.

The settings (Deskbar menu on Haiku, the top-bar input menu on Linux, the
settings window on a Mac) are: 全拼 or a shuangpin layout, how
shuangpin spells a, o and e (aa oo ee, oa oo oe, or one key each), 简体 /
繁體, 全角标点, emoji, 中英混输, 云候选, the three 模糊音 groups,
and candidates per page. On Haiku they're saved in
`~/config/settings/HaikuPinyin`, next to `user.dict`, the words it has
learned (plain text).

Views that are input method aware (BTextView, so StyledEdit and most apps;
Terminal; [Temino](https://github.com/bluevisor/temino) 0.2.4+) show the
pinyin inline; everywhere else the committed text arrives as typing.

**Cloud candidates** send the pinyin of each composition, but nothing you've
committed, to Google Input Tools. They're off until you turn on 云候选, so
by default nothing you type leaves the machine.

## Layout

| Path | What |
|---|---|
| `engine/` | Portable C++17 engine: lexicon, syllable graph (full pinyin, shuangpin, 模糊音), bigram beam-search decoder, user dictionary, S→T converter; the questions and answers for cloud and AI candidates |
| `haiku/` | input_server method add-on and candidate window |
| `macos/` | InputMethodKit method (`InputController.mm`), AI helper client (`AI.mm`) and the helper itself (`ai/`, Swift + MLX), settings window and 中/英 bubble (`Settings.swift`), candidate window and gray AI completion (`Candidates.swift`), self-update (`Updater.swift`), model download (`Models.swift`), `build.sh`, `install.sh`, `release.sh` (signed, notarized zip, DMG and `.pkg` (`pkg.sh`, `pkg/`) and `latest.json`; `ai/upload-models.sh` puts the models on Hugging Face); dev only: `test-session.sh` types through it in a test window (full pinyin, shuangpin, then AI when its models are installed), `snapshot-settings.swift` renders the settings window and bubble to PNG |
| `linux/` | IBus engine (`main.cpp`; its settings and top-bar menu in `Settings.cpp`, cloud candidates in `Cloud.cpp`), component file, installers (`enable.sh`, `install-user.sh`), `deb.sh` (arm64 and amd64 .deb in Docker, `make linux-deb`; each adds the apt repository, `haiku-pinyin.sources`); dev only: `sync.sh` builds on a Linux box over ssh, `test-session.sh` types through gedit with xdotool |
| `cli/hpcli.cpp` | Drive the engine from a terminal: `echo nihao \| build/hpcli --layout pyjj` |
| `tests/` | Engine tests |
| `tools/build_data.py` | Builds `build/data/` from `sources/` |
| `tools/sign-release.sh`, `tools/release-key.asc` | Signs a release in `dist/`: the apt repository (`InRelease`) and `SHA256SUMS.asc`, with the release key |
| `CHANGELOG.md`, `tools/release-notes.sh` | What changed in each release; the script prints a version's notes (or Unreleased) for the GitHub release and the Mac updater's feed |
| `tools/haiku-testkeys/` | Dev only: a virtual keyboard device + `hpkeys` to type through the input_server over ssh, `imtest`, `hpsettings` |

## Build

```sh
tools/fetch_sources.sh      # rime-ice, libpinyin, OpenCC, rime-double-pinyin
make data test              # anywhere: data, engine, hpcli, tests
echo zgr | build/hpcli      # try it
haiku/sync.sh haiku-dbg     # copy to a Haiku machine and build there
# on Haiku:
make install                # add-on + data into ~/config/non-packaged
haiku/package.sh            # or: dist/haiku_pinyin-<v>-1-<arch>.hpkg
```

`haiku/cross.sh` cross-compiles the x86_64 add-on on a Mac (Haiku's
cross-compiler image in Docker) and packages it on a Haiku machine.

The input_server doesn't reload a replaced add-on, so run
`haiku/restart-input-server.sh` after each install, with no Deskbar menu open:
the new input_server waits on Deskbar, and an open menu deadlocks it (keyboard
and mouse stop until the script runs again over ssh).

## Data and licenses

Haiku Pinyin is GPL-3.0. Word frequencies come from
[雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice) (GPL-3.0), simplified →
traditional tables from [OpenCC](https://github.com/BYVoid/OpenCC)
(Apache-2.0), the AI's models from [Qwen](https://huggingface.co/Qwen)
(Apache-2.0, downloaded separately) run with
[MLX](https://github.com/ml-explore/mlx-swift) (MIT), word pairs from [libpinyin](https://github.com/libpinyin/libpinyin)'s
open-gram model (GPL-3.0, with Apache-2.0 parts), and shuangpin layouts from
the speller rules of
[rime-double-pinyin](https://github.com/rime/rime-double-pinyin) (LGPL-3.0).
