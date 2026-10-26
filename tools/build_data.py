#!/usr/bin/env python3
"""Compiles sources/ (see fetch_sources.sh) into Haiku Pinyin's runtime data.

build/data/pinyin.dict   lexicon: syllable keys -> words with costs (binary)
build/data/shuangpin.txt  "<scheme>\t<code>\t<syllable>" for every layout
build/data/s2t.txt        simplified -> traditional phrases and characters
build/data/tw.txt         traditional -> Taiwan variants and phrases
build/data/hk.txt         traditional -> Hong Kong variants
build/data/emoji.txt      word -> emoji, "<word>\t<emoji>"
build/data/english.txt    English words, one per line
build/data/bigram.bin     word pairs: "HPBIGR01", u32 count, then (u32 hash of
                          the first word, u32 hash of the second, f32
                          ln P(second | first)) sorted by the hashes
build/data/predict.bin    likeliest next words (联想): "HPPRED01", u32 count,
                          u32 word bytes, then (u32 hash of the first word,
                          u32 offset, u32 length, f32 ln P) sorted by hash
                          and likelihood, then the words (UTF-8)

Hashes are 32-bit FNV-1a of the UTF-8 word.

The lexicon format (little endian):
  "HPDICT01"
  u32 syllables, u32 entries, u32 key units, u32 word bytes
  syllables x (u8 length, bytes), zero padded to 4
  entries x (u32 key offset, u32 word offset, f32 cost, u8 key length,
             u8 word length, u16 reserved), sorted by key
  u16 key pool (syllable ids; ids follow alphabetical order, so sorting by
                id sorts by spelling)
  word pool (UTF-8)
"""

import math
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SOURCES = os.path.join(ROOT, "sources")
OUT = os.path.join(ROOT, "build", "data")

DICTS = ["8105", "41448", "base", "ext"]
SCHEMES = {
	"pyjj": "double_pinyin_pyjj",
	"xiaohe": "double_pinyin_flypy",
	"ziranma": "double_pinyin",
	"mspy": "double_pinyin_mspy",
	"abc": "double_pinyin_abc",
}
MAX_KEY = 16


def read_rime_dict(path):
	body = False
	with open(path, encoding="utf-8") as f:
		for line in f:
			line = line.rstrip("\n")
			if not body:
				body = line == "..."
				continue
			if not line or line.startswith("#"):
				continue
			parts = line.split("\t")
			if len(parts) < 2:
				continue
			weight = int(parts[2]) if len(parts) > 2 and parts[2] else 0
			yield parts[0], parts[1].split(" "), weight


def build_lexicon():
	words = {}
	for name in DICTS:
		for word, pinyin, weight in read_rime_dict(
				os.path.join(SOURCES, name + ".dict.yaml")):
			if "xx" in pinyin or len(pinyin) > MAX_KEY:
				continue
			key = tuple(pinyin)
			old = words.get((key, word))
			if old is None or weight > old:
				words[(key, word)] = weight

	syllables = sorted({s for key, _ in words for s in key})
	ids = {s: i for i, s in enumerate(syllables)}
	total = sum(w + 1 for w in words.values())

	entries = []
	for (key, word), weight in words.items():
		cost = -math.log((weight + 1) / total)
		entries.append((tuple(ids[s] for s in key), cost, word))
	entries.sort(key=lambda e: (e[0], e[1]))

	header = struct.pack("<8sIIII", b"HPDICT01", len(syllables), len(entries),
		sum(len(e[0]) for e in entries),
		sum(len(e[2].encode()) for e in entries))
	table = b"".join(bytes([len(s)]) + s.encode() for s in syllables)
	table += b"\0" * (-len(table) % 4)

	records, keys, pool = [], [], bytearray()
	for key, cost, word in entries:
		data = word.encode()
		if len(data) > 255:
			sys.exit(f"word too long: {word}")
		records.append(struct.pack("<IIfBBH", len(keys), len(pool), cost,
			len(key), len(data), 0))
		keys.extend(key)
		pool += data

	with open(os.path.join(OUT, "pinyin.dict"), "wb") as f:
		f.write(header)
		f.write(table)
		f.write(b"".join(records))
		f.write(struct.pack(f"<{len(keys)}H", *keys))
		f.write(pool)
	print(f"pinyin.dict: {len(syllables)} syllables, {len(entries)} words")
	weights = {}
	for (key, word), weight in words.items():
		weights[word] = max(weights.get(word, 0), weight)
	return syllables, weights


def speller_rules(path):
	rules, inside = [], False
	with open(path, encoding="utf-8") as f:
		for line in f:
			stripped = line.strip()
			if stripped.startswith("algebra:"):
				inside = True
				continue
			if not inside:
				continue
			if not stripped.startswith("- "):
				if stripped and not stripped.startswith("#"):
					break
				continue
			rule = stripped[2:].split("#")[0].strip()
			rules.append(rule)
			if rule.startswith("xlit/"):
				break  # later rules only turn codes back into pinyin
	return rules


def spell(syllable, rules):
	spellings = {syllable}
	for rule in rules:
		op, *args = rule.split("/")
		if op == "erase":
			pattern = re.compile(args[0])
			spellings = {s for s in spellings if not pattern.search(s)}
		elif op in ("derive", "xform"):
			pattern = re.compile(args[0])
			repl = re.sub(r"\$(\d)", r"\\\1", args[1])
			changed = {pattern.sub(repl, s) for s in spellings
				if pattern.search(s)}
			if op == "xform":
				spellings = {s for s in spellings if not pattern.search(s)}
			spellings |= changed
		elif op == "xlit":
			table = str.maketrans(args[0], args[1])
			spellings = {s.translate(table) for s in spellings}
	return spellings


def build_shuangpin(syllables):
	lines = []
	for scheme, source in SCHEMES.items():
		rules = speller_rules(os.path.join(SOURCES, source + ".schema.yaml"))
		for syllable in syllables:
			for code in sorted(spell(syllable, rules)):
				# A derive can leave a one-key spelling behind (pyjj "o").
				if not re.fullmatch(r"[a-z;]{2}", code):
					continue
				lines.append(f"{scheme}\t{code}\t{syllable}\n")
	with open(os.path.join(OUT, "shuangpin.txt"), "w", encoding="utf-8") as f:
		f.writelines(lines)
	print(f"shuangpin.txt: {len(lines)} codes in {len(SCHEMES)} layouts")


def build_table(name, sources):
	pairs = {}
	for source in sources:
		with open(os.path.join(SOURCES, source + ".txt"), encoding="utf-8") as f:
			for line in f:
				parts = line.rstrip("\n").split("\t")
				if len(parts) == 2 and parts[0] not in pairs:
					pairs[parts[0]] = parts[1].split(" ")[0]
	with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
		for key in sorted(pairs):
			f.write(f"{key}\t{pairs[key]}\n")
	print(f"{name}: {len(pairs)} entries")


def build_emoji():
	# Haiku draws one glyph per code point (no shaping), so keep emoji that
	# are a single code point once the emoji-style selector is dropped: no
	# flags, skin tones or ZWJ sequences.
	lines, kept = [], 0
	with open(os.path.join(SOURCES, "emoji.txt"), encoding="utf-8") as f:
		for line in f:
			parts = line.rstrip("\n").split("\t")
			if len(parts) != 2:
				continue
			word, values = parts
			emoji = []
			for value in values.split(" "):
				value = value.replace("\ufe0f", "")
				if value != word and len(value) == 1 and ord(value) >= 0x2000 \
						and value not in emoji:
					emoji.append(value)
			if emoji:
				# One per word is enough. Prefer the pictographs (🌞) over the
				# older symbol forms (☀).
				best = next((e for e in emoji if ord(e) >= 0x1f000), emoji[0])
				lines.append(f"{word}\t{best}\n")
				kept += 1
	with open(os.path.join(OUT, "emoji.txt"), "w", encoding="utf-8") as f:
		f.writelines(lines)
	print(f"emoji.txt: {kept} emoji for {len(lines)} words")


BIGRAM_MIN_COUNT = 5		# rarer pairs say little and cost room
PREDICTIONS = 9				# 联想 kept per word
PREDICTION_MIN_WEIGHT = 20000	# rime-ice weight a 联想 word needs: drops
							# libpinyin's odd tokens (中的, 你了)
PARTICLES = "的了啊吗呢吧"		# nor a word hanging off one: 他的, 你啊, 的人


def fnv1a(text):
	h = 0x811c9dc5
	for b in text.encode():
		h = ((h ^ b) * 0x01000193) & 0xffffffff
	return h


def build_bigram(weights):
	totals, pairs = {}, []
	section = None
	with open(os.path.join(SOURCES, "libpinyin", "interpolation2.text"),
			encoding="utf-8") as f:
		for line in f:
			if line.startswith("\\2-gram"):
				section = 2
				continue
			if section != 2 or not line.startswith("\\item"):
				continue
			parts = line.split()
			# \item <token> <word> <token> <word> count <n>
			if len(parts) != 7:
				continue
			first, second, count = parts[2], parts[4], int(parts[6])
			totals[first] = totals.get(first, 0) + count
			if count >= BIGRAM_MIN_COUNT:
				pairs.append((first, second, count))

	records = {}
	successors = {}
	for first, second, count in pairs:
		logp = math.log(count / totals[first])
		key = (fnv1a(first), fnv1a(second))
		records[key] = max(records.get(key, -1e9), logp)
		# Predictions are words of two characters or more: 的 and 了 after
		# everything would crowd them out.
		if len(second) >= 2 and all("\u4e00" <= c <= "\u9fff" for c in second) \
				and weights.get(second, 0) >= PREDICTION_MIN_WEIGHT \
				and second[0] != "的" and second[-1] not in PARTICLES:
			successors.setdefault(first, []).append((logp, second))

	with open(os.path.join(OUT, "bigram.bin"), "wb") as f:
		f.write(struct.pack("<8sI", b"HPBIGR01", len(records)))
		for (h1, h2) in sorted(records):
			f.write(struct.pack("<IIf", h1, h2, records[(h1, h2)]))

	entries, pool = [], bytearray()
	for first, following in successors.items():
		following.sort(reverse=True)
		for logp, second in following[:PREDICTIONS]:
			data = second.encode()
			entries.append((fnv1a(first), -logp, len(pool), len(data), logp))
			pool += data
	entries.sort()
	with open(os.path.join(OUT, "predict.bin"), "wb") as f:
		f.write(struct.pack("<8sII", b"HPPRED01", len(entries), len(pool)))
		for h, _, offset, length, logp in entries:
			f.write(struct.pack("<IIIf", h, offset, length, logp))
		f.write(pool)
	print(f"bigram.bin: {len(records)} pairs; predict.bin: {len(entries)} "
		f"for {len(successors)} words")


def build_english():
	words = set()
	for word, _, _ in read_rime_dict(os.path.join(SOURCES, "en.dict.yaml")):
		if re.fullmatch(r"[A-Za-z]{2,}", word):
			words.add(word)
	with open(os.path.join(OUT, "english.txt"), "w", encoding="utf-8") as f:
		f.writelines(w + "\n" for w in sorted(words, key=str.lower))
	print(f"english.txt: {len(words)} words")


def main():
	os.makedirs(OUT, exist_ok=True)
	syllables, weights = build_lexicon()
	build_shuangpin(syllables)
	build_table("s2t.txt", ["STPhrases", "STCharacters"])
	build_table("tw.txt", ["TWPhrases", "TWVariants"])
	build_table("hk.txt", ["HKVariants"])
	build_emoji()
	build_english()
	build_bigram(weights)


if __name__ == "__main__":
	main()
