# 海酷拼音 Haiku Pinyin

**English** · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) ·
[Website](https://bluevisor.github.io/haiku-pinyin/) · [Privacy](PRIVACY.md) ·
[Security](SECURITY.md) · [Contributing](CONTRIBUTING.md)

A modern Chinese input method for Haiku, Linux and macOS, in the spirit of
搜狗拼音 and the macOS pinyin input: whole-sentence conversion, abbreviations
(`zgr` → 中国人, `bjdx` → 北京大学), half-typed syllables, learning from what
you pick, and inline composition. One engine, three native front ends.

![海酷拼音 typing in a text view, and in Temino](docs/screenshot.png)

| | Get it | Runs as | Only here |
|---|---|---|---|
| **macOS** 13+ | [`.pkg` or `.dmg`](https://github.com/bluevisor/haiku-pinyin/releases/latest), signed and notarized | InputMethodKit input method | on-device AI picks and gray completion (Apple silicon), self-update, settings window |
| **Linux** | [`.deb`](https://github.com/bluevisor/haiku-pinyin/releases/latest) for arm64 / amd64 (Ubuntu 22.04+, Debian 12+), then signed updates with `apt`; Arch `PKGBUILD` and Fedora `.spec`; or from source | IBus engine (GNOME and anywhere IBus runs) or Fcitx5 addon (KDE Plasma and anywhere Fcitx5 runs) | a settings window (IBus, GNOME Settings; it works with Fcitx5 too), the top-bar input menu, or Fcitx5's configuration tool |
| **Haiku** (64-bit) | [`.hpkg`](https://github.com/bluevisor/haiku-pinyin/releases/latest) for arm64 / x86_64, with signed checksums | input_server add-on | Deskbar menu and a settings window, inline in BTextView and Terminal |

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

On Linux it's an [IBus](https://github.com/ibus/ibus) engine (GNOME, and
anywhere else IBus runs) or a [Fcitx5](https://github.com/fcitx/fcitx5) addon
(KDE Plasma, and the distributions and desktops that use Fcitx5). Both are the
same engine, and both keep their settings and the words they learn in
`~/.config/haiku-pinyin/` (`settings`, `user.dict`), so switching from one
framework to the other keeps them.

**Debian, Ubuntu** (Ubuntu 22.04+, Debian 12+): download
`haiku-pinyin-data_<version>_all.deb` and the package for your framework,
`haiku-pinyin_<version>_<arch>.deb` (IBus) or
`fcitx5-haiku-pinyin_<version>_<arch>.deb` (Fcitx5), `arm64` or `amd64`, from
the [releases](https://github.com/bluevisor/haiku-pinyin/releases), then:

```sh
# IBus
sudo apt install ./haiku-pinyin-data_*.deb ./haiku-pinyin_*.deb
haiku-pinyin-enable        # as yourself: restarts IBus, adds it to GNOME
# Fcitx5
sudo apt install ./haiku-pinyin-data_*.deb ./fcitx5-haiku-pinyin_*.deb
fcitx5 -rd                 # as yourself: restarts Fcitx5
```

`haiku-pinyin-data` also adds the project's apt repository and its release
key (`/etc/apt/sources.list.d/haiku-pinyin.sources`,
`/etc/apt/keyrings/haiku-pinyin-archive-keyring.gpg`), so `sudo apt update
&& sudo apt upgrade` brings each new release, and apt refuses one that isn't
signed with that key. The repository is the latest GitHub release itself;
once it's set up, `sudo apt install fcitx5-haiku-pinyin` (or `haiku-pinyin`)
works too. Upgrading from a release that had just the one `haiku-pinyin`
package brings in `haiku-pinyin-data` on its own. Both files are
configuration: `apt remove` leaves them, still working together, and
`apt purge haiku-pinyin-data` takes them away.

**Arch**: `make linux-arch` writes an AUR-ready `PKGBUILD` (in `dist/arch/`)
that builds `ibus-haiku-pinyin`, `fcitx5-haiku-pinyin` and
`haiku-pinyin-data` from a release's source and data tarballs; run
`makepkg -si` beside it. **Fedora**: `make linux-rpm` writes
`dist/fedora/haiku-pinyin.spec` and builds the rpms (`ibus-haiku-pinyin`,
`fcitx5-haiku-pinyin`, `haiku-pinyin-data`) in Docker; `sudo dnf install` the
ones you want. Neither is in the AUR or Copr yet.

Or build it yourself, with the headers of either framework or both:

```sh
sudo apt install build-essential curl python3 libibus-1.0-dev   # IBus
sudo apt install fcitx5-modules-dev libfcitx5core-dev \
	libfcitx5config-dev libfcitx5utils-dev                      # Fcitx5
# Fedora: gcc-c++ make ibus-devel fcitx5-devel; Arch: base-devel ibus fcitx5
tools/fetch_sources.sh && make data ibus fcitx5
sudo make install-ibus && linux/enable.sh  # into /usr, where IBus looks
sudo make install-fcitx5 && fcitx5 -rd     # into Fcitx5's addon directory
# or the IBus engine without root (GNOME, or another systemd user session):
linux/install-user.sh                      # into ~/.local; --remove undoes it
```

**IBus**: `haiku-pinyin-enable` (`linux/enable.sh` in a build) restarts IBus
and adds **海酷拼音 Haiku Pinyin** to GNOME's input sources (or add it under
Settings → Keyboard → Input Sources → Chinese); switch with Super+Space. On
GNOME, don't use `ibus restart`: the daemon runs as a systemd user unit and
stays stopped. Restart it with
`systemctl --user restart org.freedesktop.IBus.session.GNOME.service`. Apps
started outside the desktop session need `GTK_IM_MODULE=ibus` to reach it.
The settings are in the input source menu in the top bar and in a settings
window: 设置… in that menu, the gear by 海酷拼音 in Settings → Keyboard →
Input Sources, or Preferences in `ibus-setup`. A change takes effect at once,
with no restart. The window also has the 词库 actions and **Report a
problem**, which opens a new GitHub issue with the version, the system and
the settings filled in, never anything you typed. It's in Chinese, or
English when the system's language isn't.

**Fcitx5**: add **海酷拼音 Haiku Pinyin** in `fcitx5-configtool` (on KDE,
System Settings → Input Method → Add Input Method); switch
with Ctrl+Space. Its settings are on the configure button next to it there,
the same ones as IBus's menu. The settings window comes with the IBus
package (`haiku-pinyin-setup`, beside the engine); when it's installed it
works with Fcitx5 too, and Fcitx5 picks up its changes at once. A Shift tap switches 中/英 as everywhere else,
with one difference: when the keyboard layout comes before it in the input
method group, Fcitx5's own Left Shift ("temporarily switch between first and
current input method") goes to that layout instead, which types English just
the same; Right Shift and 中/英 in the tray menu switch 海酷拼音's own mode.

Everything in Using it below applies to both.

## Install on macOS

On a Mac it's an InputMethodKit input method (macOS 13 or later, Apple
silicon or Intel). Download `HaikuPinyin-<version>.pkg` from the
[releases](https://github.com/bluevisor/haiku-pinyin/releases) and open it:
the installer puts it in `~/Library/Input Methods` for you alone (no admin
password) and switches to it. Or download
`HaikuPinyin-<version>.dmg`, open it and open **HaikuPinyin**: it offers to
install itself into `~/Library/Input Methods` and switches to it.

The first time, you add it to your input sources yourself: macOS 26 lets
only you do that. Either installer opens System Settings › Keyboard, says
what to click (Input Sources: Edit… → **+** → Chinese, Simplified → **Haiku
Pinyin** → Add) and waits; the moment it's there, it switches to it and
says it's ready. Updates keep it in the list. Both installers speak the system's language: English, 简体
or 繁體 Chinese. Switch to it with Control+Space or 🌐. Both are signed
(the package with a Developer ID Installer certificate) and notarized, and it
keeps itself up to date: once a day it looks for a newer
release, checks the download's signature, and puts it in place while the
Mac sits idle (**Update automatically** in the settings turns that off;
**Check for Updates…** looks now).

A Shift tap switches 中/英, with a 中 or 英 (EN in English) by the text to
say which; **Settings…** in the input menu opens the settings window. A
setting changed anywhere else (`defaults write
com.bluevisor.inputmethod.HaikuPinyin layout pyjj`, a tool that syncs
preferences) takes effect at once too, with no restart. The
menu and the window follow the system's language: English, 简体 or 繁體
Chinese. With VoiceOver on, the highlighted candidate is read out (with its
place on the page) as it changes, and each candidate is a button VoiceOver
can press. Control-Delete forgets
a learned word. What it learns is in
`~/Library/Application Support/HaikuPinyin/user.dict`. For a bug report,
**Diagnostics → Copy** in the settings copies the version, the Mac, the
settings, the AI's and the updater's state and the end of the AI's log;
never what you type or the words it learned. **Report a Problem…** adds a
summary of any crash reports macOS kept for the method or the AI helper in
the last 30 days, shows it all for you to edit, and opens a new GitHub issue
in your browser with it filled in. Nothing is sent until you submit the
issue there. When macOS has kept a new crash report since you last opened
the settings, a notice at the top offers to report it.

**AI** needs Apple silicon and a model: **Download** in the settings window
fetches Qwen3-1.7B-Base and Qwen3.5-0.8B-Base, converted to 4-bit MLX (about
1.4 GB), from Hugging Face (or its mirror) into
`~/Library/Application Support/HaikuPinyin/models`, and checks every file. A
stopped download carries on where it left off. Without a model, the first
pause after typing a word shows a small card under the line that offers
this, once; it never takes the keyboard. A helper process holds the
model (about 1 GB of memory while AI is on) and answers on the side, like the
cloud: typing never waits for it. The settings window turns AI picks and
completions on or off and chooses the model: **Automatic** uses the 1.7B and falls
back to the 0.8B on a Mac too slow for it; any MLX model in that folder can be
chosen. Nothing you type leaves the Mac.

**Uninstall…**, at the bottom of the settings window, removes it. It switches
you to your English keyboard layout (ABC or U.S.) first, takes 海酷拼音 off
the input menu, and deletes the app from `~/Library/Input Methods` once it has
quit. Your learned words, custom phrases, AI models and settings stay, so
installing again picks up where you left off, unless you tick **Also delete
my data**: then `~/Library/Application Support/HaikuPinyin` (the models
included), the settings and the logs go too. Models linked into the models
folder from elsewhere stay where they are. It's gone from the input menu
at once; macOS forgets it entirely the next time you log in.

To build it yourself, with Xcode's command line tools:

```sh
tools/fetch_sources.sh && make data install-mac   # into ~/Library/Input Methods
```

`macos/install.sh --remove` uninstalls a build (or use **Uninstall…**). A
build of your own is signed ad hoc and never updates itself. `macos/ai/fetch-models.sh` converts the models
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
Linux repository on its own.

A release is one command on the owner's Mac. `make release` is the dry run:
it checks a release can start (a clean main, up to date; notes in
CHANGELOG.md; the Developer ID certificates, the notary profile and the
release key; Docker; the Haiku VM) and lists what it would do.
`make release PUBLISH=1` builds every package, makes the checksums and the apt
repository (`tools/sign-release.sh`), checks every asset and signature,
publishes GitHub release v<version> and turns CHANGELOG.md's Unreleased into
that version. `tools/release.sh --check` runs just the asset check on
`dist/`.

## Using it

Switch to it like any input method: on Haiku from the keyboard menu in
Deskbar or with **Alt+Space** (with [Kigo](https://github.com/bluevisor/kigo),
which takes Alt+Space for itself, **Win+Space**); on Linux with Super+Space
(GNOME, IBus) or Ctrl+Space (Fcitx5);
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

The settings (the Deskbar menu and its settings window on Haiku, the settings window, the top-bar input
menu with IBus or Fcitx5's configuration tool on Linux, the
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
by default nothing you type leaves the machine. [PRIVACY.md](PRIVACY.md)
lists everything that can reach the network.

### Custom phrases (自定义短语)

`phrases.txt`, next to `user.dict`, maps a code to text: type the code and the
text is offered at the position you chose, several per code if you like. Open
it from 自定义短语 in the settings window (Mac, Linux), the 词库 menu (Linux: in the
top bar with IBus, in the tray with Fcitx5) or the Deskbar menu and settings window (Haiku); it's made with a commented example the
first time. Changes take effect as soon as it's saved.

```
# code,position=text (Sogou's syntax); # and ; start comments
dz,1=北京市海淀区中关村大街 1 号
yx,1=me@example.com
yx,2=work@example.com
sig,1=
Several lines:
up to the next code,position= line
```

- **Codes** are letters (and `'`, or `;` where a shuangpin layout uses it),
  matched against the keys exactly as typed, in full pinyin and shuangpin
  alike: `dz` offers the address in 拼音加加 too, where it also spells 段.
  Phrases replace the same text elsewhere among the candidates, are typed as
  written (never converted to 繁體) and teach nothing.
- **Rime's `custom_phrase.txt`** lines work as they are: `text<Tab>code<Tab>weight`
  (the weight optional) go first, the higher weight earlier; spaces in a code
  are ignored, and `# no comment` makes `#` ordinary text from there on.
- **Sogou's `PhraseEdit.txt`** works as it is too, UTF-8 or UTF-16; its date
  functions (`#$year…`) are skipped.

### Importing and exporting words

导出 writes what it learned as text, `词<Tab>pin yin<Tab>count` (the columns
of Rime's export too). 导入 reads a file back and merges it with what's
there, the higher count winning, so importing the same file twice changes
nothing. It also reads, telling them apart by their contents:

| From | File | Becomes |
|---|---|---|
| 海酷拼音, or Rime's `rime_dict_manager --export` | `词<Tab>pin yin<Tab>count` | words |
| Rime's user dictionary snapshot | `*.userdb.txt` (`pin yin <Tab>词<Tab>c=…`; deleted and never-typed entries skipped) | words |
| Sogou's exported words | `'pin'yin 词`, UTF-8 or UTF-16 | words |
| Rime's `custom_phrase.txt`, Sogou's `PhraseEdit.txt` | as above | phrases |
| The Mac's text replacements | the `.plist` you get by dragging them out of System Settings → Keyboard → Text Replacements | phrases (shortcuts that aren't letters are skipped) |

Words whose pinyin is missing or unusable are spelled the way the lexicon
spells their pieces, and skipped when it can't. Imported phrases go to the
end of `phrases.txt`. GBK text (older Sogou exports) and binary plists are
refused with how to convert them (`iconv -f GBK -t UTF-8`,
`plutil -convert xml1`). Sogou's `.bin` and `.scel` files aren't read.

On the Mac, 导入… / 导出… are in the settings window; on Linux in the
settings window and the 词库 menu (with zenity's or kdialog's file dialogs); on Haiku in the Deskbar menu and the settings window (Tracker's
file panels). Anywhere, from a build: `build/hpcli export words.txt`,
`build/hpcli import file` (on this system's `user.dict`, or `--user FILE`).
A running input method takes up the change as the next composition starts.

### Syncing between machines (同步)

Learned words and custom phrases can follow you between your Macs, Linux PCs
and Haiku machines through a folder **your own sync tool** already keeps
alike: iCloud Drive, Dropbox, Syncthing, Nextcloud, a network share. There's
no server. Pick the folder under 同步 in the settings window (Mac, Linux,
Haiku); none, the default, is off. The setting is the file `sync-folder`
beside `user.dict`.

- **One file per machine.** The folder gets a `haiku-pinyin/` folder holding
  `<id>.words` and `<id>.phrases` for each machine. A machine writes only its
  own and reads the others', so the sync tool never sees two machines change
  one file and makes no conflict copies. The id is random, kept in `sync-id`
  beside `user.dict`; the machine's name is in each file's first lines.
- **Words** are text, `词<Tab>pin yin<Tab>count<Tab>time<Tab>cost`, every word
  that machine knows. They merge by the higher count, never the sum, so the
  same word learned on two machines isn't counted again each time they sync.
- **Forgetting** a word (Ctrl+Delete) forgets it everywhere: the files list
  forgotten words too (`-<Tab>词<Tab>pin yin<Tab>time`), and a copy learned
  before the forget doesn't bring it back. Using the word again afterward, on
  any machine, does.
- **Custom phrases** go as a whole file: the most recently edited
  `phrases.txt` replaces the others', which keep the one it replaced as
  `phrases.txt.bak`. An edit made after a machine took the others' phrases
  counts as newer than them, even if that machine's clock runs behind. Edit
  them on one machine at a time, or merge by hand from the `.bak`. To remove
  every phrase, empty the file; a deleted one comes back from the others. A
  new machine's commented example never replaces anyone's phrases.
- **When:** it writes its files as it saves what it learned (every few
  words), and reads the others' when they change, on a thread of its own that
  typing never waits on: within seconds while you type, within a minute
  otherwise. Files are written to a temporary name and renamed into place,
  and a file the sync tool has only half delivered is ignored until it's
  whole.
- `build/hpcli sync FOLDER` does one round by hand (`--user FILE` for
  another `user.dict`; without `FOLDER`, the one in the settings).

The folder holds the words you learned, in plain text: anyone who can read
it can read them. 海酷拼音 sends nothing anywhere itself; your sync tool
does. See [PRIVACY.md](PRIVACY.md#the-sync-folder).

## Layout

| Path | What |
|---|---|
| `engine/` | Portable C++17 engine: lexicon, syllable graph (full pinyin, shuangpin, 模糊音), bigram beam-search decoder, user dictionary, custom phrases (`Phrases`), word import and export (`Words`), sync through a folder (`Sync`), S→T converter, the prefilled bug report link (`Report`); the questions and answers for cloud and AI candidates |
| `haiku/` | input_server method add-on (`PinyinMethod.cpp`: the engine, the Deskbar menu), candidate window, settings window (`SettingsWindow.cpp`) |
| `macos/` | InputMethodKit method (`InputController.mm`), AI helper client (`AI.mm`) and the helper itself (`ai/`, Swift + MLX), settings window and 中/英 bubble (`Settings.swift`), candidate window and gray AI completion (`Candidates.swift`), self-update (`Updater.swift`), model download (`Models.swift`), custom phrases, import and export, the sync folder (`Words.swift`, `Words.mm`), `build.sh`, `install.sh`, `release.sh` (signed, notarized zip, DMG and `.pkg` (`pkg.sh`, `pkg/`) and `latest.json`; `ai/upload-models.sh` puts the models on Hugging Face); dev only: `test-session.sh` types through it in a test window (full pinyin, shuangpin, then AI when its models are installed), `snapshot-settings.swift` renders the settings window and bubble to PNG |
| `linux/` | IBus engine (`main.cpp`; its settings and top-bar menu in `Settings.cpp`), the settings window (`setup/Setup.cpp`, GTK 3, and `setup/ibus-setup-haiku-pinyin.desktop.in` for GNOME Settings), component file, installers (`enable.sh`, `install-user.sh`); what it shares with the Fcitx5 addon: settings, data, user dictionary and custom phrases (`Common.cpp`), cloud candidates (`Cloud.cpp`), the 词库 menu (`Words.cpp`); packages, built in Docker: `deb.sh` (`make linux-deb`: IBus and Fcitx5 .debs for arm64 and amd64, and `haiku-pinyin-data`, which adds the apt repository, `haiku-pinyin.sources`), `arch/PKGBUILD.in` and `arch.sh` (`make linux-arch`), `fedora/haiku-pinyin.spec.in` and `rpm.sh` (`make linux-rpm`), `sources.sh` (the data tarball those two build from, `make linux-sources`), `check-install.sh` (are the files where IBus and Fcitx5 look); dev only: `sync.sh` builds on a Linux box over ssh, `test-session.sh` types through gedit with xdotool |
| `linux/fcitx5/` | Fcitx5 addon (`HaikuPinyin.cpp`: the engine, candidate window, 中/英, the 词库 menu in the tray, settings for Fcitx5's configuration tool), addon and input method `.conf` files; dev only: `docker-test.sh` types through it under Xvfb in Docker (`test-session.sh`, `textview.py`, and `panel.py`, which picks tray menu items as KDE's panel does) |
| `cli/hpcli.cpp` | Drive the engine from a terminal: `echo nihao \| build/hpcli --layout pyjj`; `hpcli import`, `hpcli export`, `hpcli sync` |
| `tests/` | Engine tests |
| `tools/fetch_sources.sh`, `tools/sources.sha256` | Fetches `sources/`, each upstream at a pinned commit, checked against the checksums |
| `tools/build_data.py` | Builds `build/data/` from `sources/` |
| `tools/release.sh` | The whole release (`make release`): pre-flight, every build, the asset and signature check, the GitHub release |
| `tools/sign-release.sh`, `tools/release-key.asc` | Signs a release in `dist/`: the apt repository (`InRelease`) and `SHA256SUMS.asc`, with the release key |
| `CHANGELOG.md`, `tools/release-notes.sh` | What changed in each release; the script prints a version's notes (or Unreleased) for the GitHub release and the Mac updater's feed |
| `tools/haiku-testkeys/` | Dev only: a virtual keyboard device + `hpkeys` to type through the input_server over ssh, `imtest`, `hpsettings` |

## Build

```sh
tools/fetch_sources.sh      # rime-ice, libpinyin, OpenCC, rime-double-pinyin, pinned
make data test              # anywhere: data, engine, hpcli, tests
echo zgr | build/hpcli      # try it
haiku/sync.sh haiku-dbg     # copy to a Haiku machine and build there
# on Haiku:
make install                # add-on + data into ~/config/non-packaged
haiku/package.sh            # or: dist/haiku_pinyin-<v>-1-<arch>.hpkg
```

`make bench` measures conversion: about 300 everyday sentences in full
pinyin, 50 abbreviations and some shuangpin (`tests/bench/`), typed with no
user dictionary, and held-out sets (`heldout*.tsv`) written apart from them:
a scoring change should help both, not just the sentences it was tuned on.
It prints how often the first candidate is the whole
sentence, how many characters are right, how often the sentence is among
the first five, and the worst misses, and fails when the first drops more
than a point below `tests/bench/baseline.txt`. After a change that improves
the scores, `make bench-update` records them; commit the baseline with it.

The upstream data comes at pinned commits, every file checked against
`tools/sources.sha256`, so the data and the bench scores only move when a pin
does. To bump one: set it in `tools/fetch_sources.sh`, run
`tools/fetch_sources.sh --update` (it rewrites the checksums), then
`make data test bench`, and commit the script, checksums and baseline together.

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
