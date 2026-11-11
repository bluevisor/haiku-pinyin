# Privacy

[中文](README.zh-CN.md#隐私) · English

**By default, nothing you type leaves your computer.** 海酷拼音 has no
accounts, analytics, telemetry or automatic crash reporting. The words it
learns, your settings and the AI's model all stay on your machine, unless
you pick a [sync folder](#the-sync-folder) for your own sync tool to carry
the words and custom phrases to your other machines.

Four things can reach the network, each for a reason you can see and turn
off. This page lists every one of them; it was written by reading the code,
and the places to check it are at the end.

| What | Where it goes | When | Platforms |
|---|---|---|---|
| [Cloud candidates](#cloud-candidates-云候选) | Google Input Tools | only after you turn on 云候选 | all |
| [Update check](#the-mac-update-check) | GitHub | once a day, release copies only | macOS |
| [AI model download](#the-ai-model-download) | Hugging Face (or its mirror) | only when you click Download | macOS (Apple silicon) |
| [apt repository](#the-linux-apt-repository) | GitHub | when apt refreshes its lists | Debian, Ubuntu |

## What never leaves your computer

- **What you type**, and the text you commit.
- **The words it learns** (`user.dict`) and your settings, unless you pick
  a [sync folder](#the-sync-folder): then your own sync tool carries the
  words and custom phrases to your other machines.
- **The text before the cursor** that the Mac's AI reads (up to 64
  characters). It goes to a helper process on the same Mac, over a pipe;
  the helper loads the model from a folder on disk and never connects to
  anything.
- **Diagnostics**: the Mac's **Copy** puts a report on your clipboard and
  nothing more (see [below](#copy-diagnostics)).
- **Problem reports**: **Report a Problem…** in the settings window (Mac,
  Linux, Haiku) opens a GitHub issue in your browser for you to send, or not
  (see [below](#report-a-problem)).

## Cloud candidates (云候选)

**Off until you turn it on**, on every platform (the 云候选 setting).

When it's on and you pause while composing (about 0.12 seconds), it asks
Google Input Tools for one candidate, which shows up marked ☁:

- **Sent**: the pinyin of the part of the composition you haven't picked
  yet, spelled as full pinyin (shuangpin codes are spelled out first), for
  example `woxiangchifan`, plus fixed parameters naming the language and
  the app. It's an HTTPS request to `https://inputtools.google.com/request`.
- **Only** when that pinyin is at least two syllables and letters only; not
  in v-mode, not for English.
- **Never sent**: anything you've committed, the candidates you picked, the
  text around the cursor, the words it learned.
- As with any request, Google sees your IP address and a user agent (the
  system's on a Mac, curl's on Linux and Haiku). Google sets no cookie on
  these requests. [Google's privacy policy](https://policies.google.com/privacy)
  applies to what it receives.

A ☁ candidate you pick can be learned like any other word, into your local
`user.dict`.

## The Mac update check

**Release copies only**: the signed download, installed in
`~/Library/Input Methods`. A copy you build yourself never checks.

- **When**: once a day while it runs (it looks every minute whether a day
  has passed since the last check), and when you choose **Check for
  Updates…** in the settings or the input menu.
- **What**: one request for
  `https://github.com/bluevisor/haiku-pinyin/releases/latest/download/latest.json`,
  with the user agent `HaikuPinyin/<version>`. No identifier, no settings,
  nothing about you or your Mac beyond that version and your IP address.
- **If there's a newer release**, it downloads the zip that file names (a
  GitHub release download), checks it against its SHA-256 and the
  project's Developer ID signature, and puts it in place while the Mac is
  idle.
- **Turn it off** with **Update automatically** in the settings. GitHub's
  [privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement)
  applies to its requests.

## The AI model download

**Only when you click Download** in the settings (Apple silicon Macs). The
one-time card that offers the AI just opens the settings; it downloads
nothing.

- **From**: Hugging Face,
  `https://huggingface.co/bluevisor/Qwen3-1.7B-Base-4bit-mlx` and
  `https://huggingface.co/bluevisor/Qwen3.5-0.8B-Base-4bit-mlx` (about
  1.4 GB). If Hugging Face can't be reached, the same files from
  `https://hf-mirror.com`, a community mirror of Hugging Face that neither
  this project nor Hugging Face runs.
- **What's sent**: plain file requests (with a range, to carry on a stopped
  download). Every file is checked against a SHA-256 built into the app.
- **After that**, the AI runs on the Mac alone.

## The Linux apt repository

The `haiku-pinyin-data` .deb adds a repository,
`/etc/apt/sources.list.d/haiku-pinyin.sources`, pointing at
`https://github.com/bluevisor/haiku-pinyin/releases/latest/download/`.

- **The input method never contacts it.** apt does, whenever it refreshes
  its package lists: `sudo apt update`, and the refreshes your system
  schedules on its own (on Debian and Ubuntu, the daily apt timer and the
  software updater).
- **What's sent** is what apt sends to any repository: requests for the
  index files and packages, with apt's user agent.
- **To stop it**, delete that file. The Arch and Fedora packages add no
  repository, and Haiku has no update check.

## The sync folder

**Off until you pick a folder** (同步 in the settings window, on every
platform). Pick one only you can read.

When you've picked one, 海酷拼音 writes two files for this machine into
`<folder>/haiku-pinyin/`, and reads the other machines' from there:

- `<id>.words`: **every word it learned**, as plain text (the word, its
  pinyin, how often you picked it, when, and a ranking cost), and the words
  you forgot with Ctrl+Delete.
- `<id>.phrases`: your custom phrases (`phrases.txt`) as they stand.
- Each file names the machine (its host name) in its first lines. `<id>`
  is a random number kept in `sync-id` beside `user.dict`; it stands for
  nothing else.

**海酷拼音 itself sends nothing anywhere**: it reads and writes files in a
folder on your disk, and opens no connection. Whatever syncs that folder
(iCloud Drive, Dropbox, Syncthing, Nextcloud, a network share) carries
those files to your other machines and keeps them wherever that service
keeps things; its privacy policy applies to them. **Anyone who can read the
folder can read the words you learned.**

Turning it off stops the reading and writing; the files already in the
folder stay until you delete them.

## Copy diagnostics

**Settings → Diagnostics → Copy** (macOS) puts a report on the clipboard
for a bug report. **Nothing is sent**: you choose where to paste it.

It holds the version, the macOS version, the Mac's model, chip and memory,
your preferred languages, every setting, the AI's state (supported, models
installed, helper running), the updater's state, and the last 50 lines of
the AI helper's log, which says what loaded and what failed. Your home
folder shows as `~`. It never holds what you type or the words it learned.

## Report a Problem

**Settings → Report a Problem…** (macOS) helps you file a bug report. **It
sends nothing itself, and there's no server.**

- **It gathers, on the Mac**, the diagnostics above and a summary of each
  crash report macOS kept in the last 30 days for the method or the AI
  helper (`~/Library/Logs/DiagnosticReports/HaikuPinyin*.ips`): the version,
  the time, the macOS version, the exception, and the top lines of the
  thread that crashed. Never the whole file. Your home folder shows as `~`.
- **It shows you all of it**, to edit or cut, before anything else.
- **Then it opens** `https://github.com/bluevisor/haiku-pinyin/issues/new` in
  your browser, with the bug report form filled in from that. Nothing
  reaches GitHub until you submit the issue there, under your GitHub
  account, where it's public. When it's too long for a link, it goes on
  your clipboard to paste instead.
- **The notice**: when settings open and macOS has kept a crash report since
  they last opened, a notice at the top offers to report it. That only
  reads the folder, and remembers when settings last opened
  (`crashesCheckedAt` in the settings).

**报告问题…** in the settings window on Linux and Haiku does less: it opens
the same form in your browser with the version, the system (distribution,
desktop and architecture, or the Haiku revision) and the settings filled in.
No logs, no crash reports, nothing you typed or learned, and nothing reaches
GitHub until you submit it.

## On your machine

| | Learned words | Settings | Other |
|---|---|---|---|
| macOS | `~/Library/Application Support/HaikuPinyin/user.dict` | `com.bluevisor.inputmethod.HaikuPinyin` defaults | `models/` and `Updates/` beside `user.dict`; `~/Library/Logs/HaikuPinyinAI.log` |
| Linux | `~/.config/haiku-pinyin/user.dict` | `~/.config/haiku-pinyin/settings` | |
| Haiku | `~/config/settings/HaikuPinyin/user.dict` | `~/config/settings/HaikuPinyin/` | |

Beside `user.dict` on each: `phrases.txt` (custom phrases), and with sync on,
`sync-folder` (the folder you picked) and `sync-id` (this machine's id).

On a Mac, **Uninstall…** in the settings deletes the app. With **Also
delete my data** ticked, it deletes the Mac's row above too (learned words,
custom phrases, settings, models and updates) and the logs.

On a Mac, a debug log of every key
(`defaults write com.bluevisor.inputmethod.HaikuPinyin debugLog -bool YES`,
in `~/Library/Logs/HaikuPinyin.log`) is off unless you turn it on for
debugging. It stays on the Mac and diagnostics leave it out; delete it
when you're done.

## The website

The download page at <https://bluevisor.github.io/haiku-pinyin/> is hosted
by GitHub Pages. It asks GitHub's API for the latest release from your
browser and has no analytics, cookies, or outside fonts or scripts.

## Checking it in the code

| | Where |
|---|---|
| Cloud candidates | `macos/InputController.mm` (`askCloud`), `linux/Cloud.cpp` (IBus and Fcitx5), `haiku/PinyinMethod.cpp` (`CloudAnswer`); what's asked: `Engine::CloudQuery` in `engine/Engine.cpp` |
| Update check | `macos/Updater.swift` |
| Model download | `macos/Models.swift` |
| AI helper (local only) | `macos/AI.mm`, `macos/ai/Sources/main.swift` |
| apt repository | `linux/haiku-pinyin.sources`, `linux/deb.sh` |
| Diagnostics | `macos/Diagnostics.swift` |
| Sync folder (files only, no network) | `engine/Sync.cpp`; the setting: `macos/Words.swift`, `linux/setup/Setup.cpp`, `haiku/SettingsWindow.cpp` |
| Problem reports | `macos/Report.swift`; the link on Linux and Haiku: `engine/Report.cpp`, opened by `linux/setup/Setup.cpp` and `haiku/SettingsWindow.cpp` (the browser; no request of their own) |

Nothing else in `engine/`, `macos/`, `linux/` or `haiku/` opens a network
connection. If a change adds one, it updates this page in the same pull
request.

Questions go in an [issue](https://github.com/bluevisor/haiku-pinyin/issues);
for anything sensitive, see [SECURITY.md](SECURITY.md).
