#!/bin/sh
# Downloads the upstream data Haiku Pinyin is built from into sources/
# (SOURCES=<dir> for another), each at a pinned version and checked against
# tools/sources.sha256, so the data and `make bench` only change when a pin
# does. A file already there and matching is kept, so a second run only
# checks.
#   rime-ice (雾凇拼音)        word list + frequencies,   GPL-3.0
#                              word -> emoji
#                              English words
#   libpinyin model20          word bigrams               GPL-3.0 / Apache-2.0
#   OpenCC                     simplified -> traditional  Apache-2.0
#   rime-double-pinyin         shuangpin speller rules    LGPL-3.0
#
# Bumping a pin: set the commit below, run `tools/fetch_sources.sh --update`
# (fetches everything again and rewrites tools/sources.sha256), then
# `make data test bench`; if the scores moved, `make bench-update`. Commit
# the script, the checksums and the baseline together. CI's cache of
# sources/ is keyed on this script and the checksums, so it refetches.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
sums=$root/tools/sources.sha256
RIME_ICE=da1fbe602e38f26db846fa10120ee64c2b0324c0	# 2026-10-05
OPENCC=a806b775d038f9fa7c061f50d270115e7ad7da5b	# 2026-10-25
DOUBLE_PINYIN=01a13287cbd27819be1c34fa1ddc1b3643d5001b	# 2025-09-24
# SourceForge's model20 hasn't changed since 2019; its checksum pins it.
MODEL=https://downloads.sourceforge.net/libpinyin/models/model20.text.tar.gz

update=
[ "$1" = --update ] && update=1
dest=${SOURCES:-$root/sources}
mkdir -p "$dest"
cd "$dest"

sha256() {
	if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi \
		| cut -d' ' -f1
}
# matches <file>: there, and the version pinned.
matches() {
	[ -f "$1" ] && [ "$(sha256 "$1")" = "$(awk -v f="$1" '$2 == f { print $1 }' "$sums")" ]
}
# ok <file>: keep it (--update fetches it again).
ok() { [ -z "$update" ] && matches "$1"; }
check() {
	[ -n "$update" ] || matches "$1" || {
		echo "fetch_sources: $1 ($2) doesn't match tools/sources.sha256" >&2
		exit 1
	}
}
files=
# get <file> <url>
get() {
	files="$files $1"
	ok "$1" && return
	mkdir -p "$(dirname "$1")"
	curl -fsSL -o "$1.part" "$2"
	mv "$1.part" "$1"
	check "$1" "$2"
}

ice=https://raw.githubusercontent.com/iDvel/rime-ice/$RIME_ICE
for f in 8105 41448 base ext; do get "$f.dict.yaml" "$ice/cn_dicts/$f.dict.yaml"; done
get emoji.txt "$ice/opencc/emoji.txt"
get en.dict.yaml "$ice/en_dicts/en.dict.yaml"
cc=https://raw.githubusercontent.com/BYVoid/OpenCC/$OPENCC/data/dictionary
for f in STCharacters STPhrases TWVariants TWPhrases HKVariants; do
	get "$f.txt" "$cc/$f.txt"
done
dp=https://raw.githubusercontent.com/rime/rime-double-pinyin/$DOUBLE_PINYIN
for s in double_pinyin double_pinyin_pyjj double_pinyin_flypy \
		double_pinyin_mspy double_pinyin_abc; do
	get "$s.schema.yaml" "$dp/$s.schema.yaml"
done
# libpinyin's open-gram model: word bigrams for sentences. Kept by
# --update too when it matches: it doesn't move, and SourceForge can be slow.
files="$files libpinyin/interpolation2.text"
if ! matches libpinyin/interpolation2.text; then
	mkdir -p libpinyin
	curl -fsSL "$MODEL" | tar xzf - -C libpinyin interpolation2.text
	matches libpinyin/interpolation2.text || {
		echo "fetch_sources: libpinyin/interpolation2.text ($MODEL) doesn't match tools/sources.sha256" >&2
		exit 1
	}
fi

if [ -n "$update" ]; then
	for f in $files; do echo "$(sha256 "$f")  $f"; done > "$sums"
	echo "fetch_sources: wrote tools/sources.sha256"
fi
