#!/usr/bin/env python3
# Conversion benchmark: types each input in tests/bench/ through hpcli, with
# no user dictionary and no text before it, and scores the candidates.
#   tests/bench.py [--hpcli build/hpcli] [--data build/data] [--update]
# Prints, for each set, how often the first candidate is the whole sentence
# (exact), how many of its characters are right (chars, by edit distance),
# and how often the sentence is among the first five (top 5); then the worst
# misses. Fails when exact drops more than TOLERANCE points below
# tests/bench/baseline.txt. --update writes the scores there instead.
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.join(HERE, 'bench')
BASELINE = os.path.join(BENCH, 'baseline.txt')
TOLERANCE = 1.0		# points of exact match
TOP = 5

# Each set's file and its tab-separated columns.
SETS = [
	('sentences', 'sentences.tsv'),	# domain, keys, sentence (full pinyin)
	('abbrev', 'abbrev.tsv'),		# keys, word (full pinyin)
	('shuangpin', 'shuangpin.tsv'),	# layout, keys, sentence
]


def read(name, file):
	items = []
	with open(os.path.join(BENCH, file), encoding='utf-8') as f:
		for number, line in enumerate(f, 1):
			line = line.rstrip('\n')
			if not line or line.startswith('#'):
				continue
			fields = line.split('\t')
			if name == 'abbrev':
				fields = ['full'] + fields
			elif name == 'sentences':
				fields = ['full'] + fields[1:]
			if len(fields) != 3:
				sys.exit(f'{file}:{number}: want 3 tab-separated fields')
			items.append(tuple(fields))
	return items


def candidates(hpcli, data, layout, keys):
	result = subprocess.run(
		[hpcli, '--data', data, '--layout', layout, '--page', str(TOP),
			'--lines', '1'],
		input='\n'.join(keys) + '\n', capture_output=True, text=True,
		encoding='utf-8')
	if result.returncode != 0:
		sys.exit(f'{hpcli} failed: {result.stderr.strip()}')
	lines = result.stdout.splitlines()
	if len(lines) != len(keys):
		sys.exit(f'{hpcli}: {len(keys)} inputs, {len(lines)} lines out')
	return [line.split('\t')[1:] for line in lines]


def distance(a, b):
	row = list(range(len(b) + 1))
	for i, x in enumerate(a, 1):
		previous, row[0] = row[0], i
		for j, y in enumerate(b, 1):
			previous, row[j] = row[j], min(row[j] + 1, row[j - 1] + 1,
				previous + (x != y))
	return row[-1]


def score(hpcli, data, items):
	results = [None] * len(items)
	layouts = {}
	for index, (layout, keys, want) in enumerate(items):
		layouts.setdefault(layout, []).append(index)
	for layout, indexes in layouts.items():
		got = candidates(hpcli, data, layout, [items[i][1] for i in indexes])
		for index, page in zip(indexes, got):
			results[index] = page
	exact = top = chars = total = 0
	misses = []
	for (layout, keys, want), page in zip(items, results):
		first = page[0] if page else ''
		right = max(0, len(want) - distance(first, want))
		chars += right
		total += len(want)
		exact += first == want
		top += want in page
		if first != want:
			rank = page.index(want) + 1 if want in page else 0
			misses.append((right / len(want), rank == 0, layout, keys, want,
				first, rank))
	n = len(items)
	scores = {'exact': 100 * exact / n, 'chars': 100 * chars / total,
		'top5': 100 * top / n}
	return {k: round(v, 1) for k, v in scores.items()}, misses


def load_baseline():
	baseline = {}
	if os.path.exists(BASELINE):
		with open(BASELINE, encoding='utf-8') as f:
			for line in f:
				if line.strip() and not line.startswith('#'):
					key, value = line.split()
					baseline[key] = float(value)
	return baseline


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--hpcli', default='build/hpcli')
	parser.add_argument('--data', default='build/data')
	parser.add_argument('--update', action='store_true',
		help='write the scores to tests/bench/baseline.txt')
	parser.add_argument('--misses', type=int, default=10,
		help='how many of the worst misses to show')
	args = parser.parse_args()

	baseline = load_baseline()
	scores = {}
	all_misses = []
	print(f'{"":10} {"n":>4} {"exact":>13} {"chars":>13} {"top 5":>13}')
	for name, file in SETS:
		items = read(name, file)
		got, misses = score(args.hpcli, args.data, items)
		all_misses += [(name,) + m for m in misses]
		columns = []
		for metric in ('exact', 'chars', 'top5'):
			key = f'{name}.{metric}'
			scores[key] = got[metric]
			delta = ''
			if key in baseline and got[metric] != baseline[key]:
				delta = f'({got[metric] - baseline[key]:+.1f})'
			columns.append(f'{got[metric]:5.1f}% {delta:>6}')
		print(f'{name:10} {len(items):4} ' + ' '.join(columns))

	# Worst first: fewest characters right, then not on the page at all.
	all_misses.sort(key=lambda m: (m[1], not m[2]))
	if all_misses and args.misses:
		print(f'\nworst misses ({len(all_misses)} in all):')
		for name, ratio, _, layout, keys, want, first, rank in \
				all_misses[:args.misses]:
			where = f'#{rank}' if rank else f'not in top {TOP}'
			shown = keys if layout == 'full' else f'{layout} {keys}'
			print(f'  {shown}\n    want {want}, got {first or "(nothing)"}'
				f' ({where})')

	if args.update:
		with open(BASELINE, 'w', encoding='utf-8') as f:
			f.write('# Scores (%) from tests/bench.py, which make bench checks.\n'
				'# After a change that improves them: make bench-update\n')
			for key, value in scores.items():
				f.write(f'{key} {value}\n')
		print(f'\nwrote {os.path.relpath(BASELINE)}')
		return 0

	worse = [k for k in scores if k.endswith('.exact') and k in baseline
		and scores[k] < baseline[k] - TOLERANCE]
	better = [k for k in scores if k in baseline and scores[k] > baseline[k]]
	if not baseline:
		print('\nno baseline yet: make bench-update writes one')
		return 1
	if worse:
		for key in worse:
			print(f'\nFAIL {key}: {scores[key]}%, baseline {baseline[key]}%'
				f' (tolerance {TOLERANCE})')
		print('If this is a trade you mean to make, make bench-update'
			' records the new scores; commit tests/bench/baseline.txt.')
		return 1
	if better:
		print('\nBetter than the baseline. Record it: make bench-update, then'
			' commit tests/bench/baseline.txt.')
	return 0


if __name__ == '__main__':
	sys.exit(main())
