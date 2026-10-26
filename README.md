# 海酷拼音 Haiku Pinyin

A modern Chinese input method for Haiku, in the spirit of 搜狗拼音 and the
macOS pinyin input: whole-sentence conversion, abbreviations (`zgr` → 中国人,
`bjdx` → 北京大学), half-typed syllables, learning from what you pick, and
inline composition.

- **Full pinyin** and **shuangpin**: 拼音加加, 小鹤, 自然码, 微软, 智能ABC
- **简体 / 繁體 / 繁體（台灣）/ 繁體（香港）** output
- Full-width punctuation (`,` → `，`, `"` → `“”` or `「」`), left alone after digits
- Shift tap switches 中/英; Caps Lock types English
- Space or 1–9 pick, `-` `=` page, `[` `]` take the first/last character of a
  word (以词定字), Enter commits the letters, Backspace reopens the last pick
- Types inline in input-method-aware views (BTextView, Terminal) and plain
  text into everything else

## Layout

| Path | What |
|---|---|
| `engine/` | Portable C++17 engine: lexicon, spelling graph, Viterbi decoder, user dictionary, S→T converter |
| `haiku/` | input_server method add-on and candidate window |
| `cli/hpcli.cpp` | Drive the engine from a terminal |
| `tests/` | Engine tests |
| `tools/build_data.py` | Builds `build/data/` from `sources/` |
| `tools/haiku-testkeys/` | Dev only: a virtual keyboard device + `hpkeys` to type through the input_server over ssh, `imtest`, `hpsettings` |

## Build

```sh
tools/fetch_sources.sh      # rime-ice, OpenCC, rime-double-pinyin into sources/
make data test              # anywhere: data, engine, hpcli, tests
echo zgr | build/hpcli      # try it
haiku/sync.sh haiku-dbg     # copy to a Haiku machine and build there
# on Haiku:
make install                # add-on + data into ~/config/non-packaged
```

The input_server doesn't reload a replaced add-on; quit it after installing
(`kill` its team) and app_server starts a fresh one.

## Data and licenses

Haiku Pinyin is GPL-3.0. Word frequencies come from
[雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice) (GPL-3.0), simplified →
traditional tables from [OpenCC](https://github.com/BYVoid/OpenCC)
(Apache-2.0), and shuangpin layouts from the speller rules of
[rime-double-pinyin](https://github.com/rime/rime-double-pinyin) (LGPL-3.0).
