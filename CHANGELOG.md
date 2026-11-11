# Changelog

What changed for people using 海酷拼音, newest first. Add to **Unreleased**
as changes merge; at release, `tools/release-notes.sh` turns that section
into the GitHub release notes and the Mac updater's feed, and it's renamed
to the version.

## Unreleased

- **Better sentences.** Whole sentences come out right more often (88% of
  the conversion benchmark, from 85%; 85% of a second set written apart
  from it, from 77%): word pairs now reach into the dictionary's longer
  phrases (去看电影, 我想), 吧 and 吗 end sentences where 把 and 码 don't
  (看电影吧, 便宜一点吗), and a finished syllable at the end is read as typed
  (休息, not 修行). When the sentence isn't right, the next best one or two
  follow it on the page (收银台在那里, then 收银台在哪里).
- **Abbreviations**: z, c or s before an h that's an initial too reads as two
  initials, so `zhrmghg` gives 中华人民共和国 and `shzy` 社会主义.
- **Linux: Fcitx5.** 海酷拼音 also comes as a Fcitx5 addon now (KDE Plasma,
  and wherever Fcitx5 runs), doing all the IBus engine does: inline
  composition, the candidate window, Shift for 中/英, 云候选, the words it
  learns, and every setting, in Fcitx5's configuration tool. Both keep their
  settings and learned words in `~/.config/haiku-pinyin/`, so switching
  frameworks keeps them.
- **Linux packages**: three .debs now, `haiku-pinyin` (IBus),
  `fcitx5-haiku-pinyin` (Fcitx5) and `haiku-pinyin-data`, which both need and
  which now adds the apt repository; `apt upgrade` brings it in. For Arch, a
  PKGBUILD ready for the AUR, and for Fedora a spec, build
  `ibus-haiku-pinyin`, `fcitx5-haiku-pinyin` and `haiku-pinyin-data`.
- **Linux**: 云候选 keeps up with fast typing. An answer that came back after
  you'd typed on was dropped, and what you'd typed since was never asked.
- **Custom phrases (自定义短语)**, on macOS, Linux (IBus and Fcitx5) and
  Haiku: `phrases.txt` next to the learned words maps a code to text
  (`dz,1=` your address), at the place among the candidates you choose,
  several per code, in full pinyin and shuangpin alike. Sogou's `PhraseEdit.txt` and Rime's
  `custom_phrase.txt` work as they are. It's read again when you save it.
- **Import and export words**: 导出 writes what it learned as text; 导入
  reads that back, Rime's user dictionaries, Sogou's exported words and
  custom phrases, and the Mac's text replacements, merging with what's
  there. In the Mac's settings, the 词库 menu on Linux (the top bar with
  IBus, the tray with Fcitx5), the Deskbar menu on Haiku, and
  `hpcli import` / `hpcli export`.
- **Haiku**: the shuangpin a o e choices in the Deskbar menu now take effect.
- **macOS**: the settings window, the 中/英 bubble, model downloads and
  update messages follow the system's language, as the input menu does:
  English, 简体 or 繁體.
- **macOS**: on an Apple silicon Mac with no AI model, a small card offers
  the on-device AI once, after you've typed a word and paused. It never takes
  the keyboard, goes at the next key, and Set Up… opens the settings at the
  download.
- **macOS**: **Diagnostics → Copy** in the settings copies what a bug report
  needs (version, Mac, settings, AI and update state, the AI's log), never
  what you type or the words it learned.
- **macOS**: VoiceOver reads the highlighted candidate and its place on the
  page as it changes, and can pick any candidate.
- **macOS**: a chosen AI model that's been deleted now shows the AI off, with
  a note saying so; it showed on while nothing ran.
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
