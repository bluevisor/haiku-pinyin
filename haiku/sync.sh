#!/bin/sh
# Copies the tree (with built data, without sources/) to a Haiku machine and
# builds there: haiku/sync.sh [host] [make target]
host=${1:-haiku-dbg}
cd "$(dirname "$0")/.."
# Not the Mac's own builds (the AI helper's are gigabytes).
COPYFILE_DISABLE=1 tar czf - --no-xattrs --exclude sources --exclude build/obj \
	--exclude build/ai --exclude build/ai-derived --exclude build/macos \
	--exclude '*.app' --exclude 'build/*.log' --exclude macos/ai/.swiftpm \
	--exclude '*.dSYM' --exclude build/hpcli --exclude build/engine_test \
	--exclude build/libhaikupinyin.a --exclude .git . \
	| ssh "$host" 'mkdir -p ~/src/haiku-pinyin && cd ~/src/haiku-pinyin && tar xzf - && make -j4 '"$2"
