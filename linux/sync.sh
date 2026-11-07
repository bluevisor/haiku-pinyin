#!/bin/sh
# Copies the tree (with built data, without sources/) to a Linux machine and
# builds there: linux/sync.sh [host] [make target]
host=${1:-bluevisor@jetson-orin}
cd "$(dirname "$0")/.."
# Not the Mac's own builds (the AI helper's are gigabytes).
COPYFILE_DISABLE=1 tar czf - --no-xattrs --exclude sources --exclude build/obj \
	--exclude build/ai --exclude build/ai-derived --exclude build/macos \
	--exclude '*.app' --exclude 'build/*.log' --exclude macos/ai/.swiftpm \
	--exclude '*.dSYM' --exclude build/hpcli --exclude build/engine_test \
	--exclude build/libhaikupinyin.a --exclude build/x86_64 --exclude dist \
	--exclude .git --exclude .codegraph --exclude .remember . \
	| ssh "$host" 'mkdir -p ~/src/haiku-pinyin && cd ~/src/haiku-pinyin && tar xzf - && make -j8 '"$2"
