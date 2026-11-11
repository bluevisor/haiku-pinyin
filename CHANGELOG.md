# Changelog

What changed for people using 海酷拼音, newest first. Add to **Unreleased**
as changes merge; at release, `tools/release-notes.sh` turns that section
into the GitHub release notes and the Mac updater's feed, and it's renamed
to the version.

## Unreleased

- **云候选 is off until you turn it on**, on macOS, Linux and Haiku. Cloud
  candidates send the pinyin you're composing to Google Input Tools, so a new
  install no longer does that before you agree. (On the Mac, 0.1.17's
  settings showed it on while the input method had it off.)
- **macOS**: when the AI can't run (no model downloaded, the chosen model
  missing, or a model that fails to load), AI 选词 and AI 补全 show off, and
  the settings say why. A broken model is no longer reloaded every 30 seconds
  while you type.

## 0.1.17 — 2026-11-10

- **云候选 on by default** (cloud candidates from Google Input Tools, marked
  ☁), on macOS, Linux and Haiku. Only the pinyin you're composing is sent,
  never text you've committed. Settings you already saved keep their value.

## 0.1.16 — 2026-11-10

Signed installers on every platform.

- **macOS**: a `.pkg`, signed with a Developer ID Installer certificate and
  notarized. It installs for you alone into `~/Library/Input Methods` (no
  admin password), switches to it, and takes over from an older copy.
- **Linux**: the `.deb` adds the project's apt repository and release key, so
  `sudo apt upgrade` brings new versions, and apt refuses any that aren't
  signed with the key.
- **All**: `SHA256SUMS`, signed with the release key in `SHA256SUMS.asc`.

## 0.1.13 — 2026-11-09

- **macOS**: 简体 / 繁體 right in the input menu; the input menu follows the
  system language (English, 简体 or 繁體); the settings subtitle mentions the
  on-device AI.

## 0.1.10 — 2026-11-08

- **macOS**: the settings window has a solid title bar; scrolled settings no
  longer show through behind it.

## 0.1.9 — 2026-11-08

First release for all three platforms: macOS (signed, notarized disk image),
Linux (IBus `.deb` for arm64 and amd64) and Haiku.

- **v-mode** everywhere: `v123` → 壹佰贰拾叁, sums, dates, symbols.
- **macOS**: on-device AI picks and gray completion (Tab), and in-app model
  download.

## 0.1.8 — 2026-11-08

- **macOS**: gray AI completion after the cursor (Tab to accept), automatic
  updates, in-app model download.
- v-mode for numbers in 大写, dates and symbols.

## 0.1.1 — 2026-10-26

- **联想 completes phrases**: after 祝你生日 the first suggestion is 快乐, then
  礼物, 蛋糕.
- **Cleaner 联想**: no more particle-bound fragments (的人, 他的, 你啊) or rare
  oddities.
- **Candidate window**: its left border no longer goes missing after it
  resizes.

## 0.1.0 — 2026-10-26

The first release, for Haiku.

- Full pinyin and shuangpin: 拼音加加, 小鹤, 自然码, 微软, 智能ABC
- Whole sentences, scored with word pairs and the word typed before
- Abbreviations and half-typed syllables: `zgr` → 中国人, `bjdx` → 北京大学
- 联想, 模糊音, 中英混输, emoji, 简体 / 繁體, full-width punctuation
- Learns the words you pick; Ctrl+Delete forgets one
- Optional cloud candidates (Google Input Tools), off by default
