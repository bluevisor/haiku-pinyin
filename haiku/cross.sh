#!/usr/bin/env bash
# Cross-builds the add-on for x86_64 Haiku from a Mac and packages it:
#   haiku/cross.sh            -> dist/haiku_pinyin-<v>-1-x86_64.hpkg
# Compiles in Haiku's cross-compiler image (gcc 13 + an R1/beta5 sysroot),
# then packages on a Haiku box (HAIKU_HOST, default haiku-dbg), whose
# `package` handles any architecture. DOCKER_CONTEXT picks the Docker engine
# (amd64 images; Rosetta on Apple silicon).
#
# No 32-bit build: 32-bit Haiku's input_server is built with gcc2, and an
# add-on built with gcc 13 can't load into it (the C++ ABIs differ).
set -euo pipefail
cd "$(dirname "$0")/.."
host=${HAIKU_HOST:-haiku-dbg}
cxx=x86_64-unknown-haiku-g++
out=build/x86_64

echo ">> $cxx (ghcr.io/haiku/cross-compiler:x86_64-r1beta5)"
mkdir -p "$out"
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src \
	ghcr.io/haiku/cross-compiler:x86_64-r1beta5 sh -ec "
		flags='-O2 -std=c++17 -fPIC -Wall -Wno-multichar'
		for f in engine/*.cpp haiku/*.cpp; do
			$cxx \$flags -c \$f -o $out/\$(basename \$f .cpp).o &
		done
		wait
		# BInputServerMethod resolves against the input_server at load time.
		$cxx -shared -o $out/HaikuPinyin $out/*.o -lbe
		rm -f $out/*.o"

echo ">> packaging for x86_64 on $host"
src='~/src/haiku-pinyin-cross'
COPYFILE_DISABLE=1 tar czf - --no-xattrs haiku README.md LICENSE VERSION \
	build/data "$out/HaikuPinyin" \
	| ssh "$host" "rm -rf $src && mkdir -p $src && cd $src && tar xzf -"
pkg=$(ssh "$host" "cd $src && HP_ADDON=$out/HaikuPinyin HP_ARCH=x86_64 HP_PKG_REV=${HP_PKG_REV:-1} haiku/package.sh" | tail -1)
mkdir -p dist
scp -q "$host:$src/$pkg" dist/
echo ">> dist/$(basename "$pkg")"
