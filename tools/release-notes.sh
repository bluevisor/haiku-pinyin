#!/bin/sh
# Prints the release notes for a version from CHANGELOG.md: its own section,
# or Unreleased when it has none yet (the usual case just before a release).
#   tools/release-notes.sh [--plain] [<version>]     (default: VERSION)
# --plain drops the Markdown, for the Mac updater's feed (macos/release.sh).
# The GitHub release takes the Markdown, with the downloads line after it:
#   gh release create v$V --notes-file <(tools/release-notes.sh $V) …
set -e
cd "$(dirname "$0")/.."
plain=
if [ "$1" = --plain ]; then
	plain=1
	shift
fi
version=${1:-$(cat VERSION)}

# The lines under "## <heading>", up to the next "## ".
section() {
	awk -v want="$1" '
		/^## / { inside = (substr($0, 4) == want \
			|| index(substr($0, 4), want " ") == 1); next }
		inside' CHANGELOG.md \
		| sed -e '/./,$!d' | awk 'NF { for (; blank > 0; blank--) print ""; print; next } { blank++ }'
}

notes=$(section "$version")
[ -n "$notes" ] || notes=$(section Unreleased)
if [ -z "$notes" ]; then
	echo "CHANGELOG.md has nothing for $version and nothing under Unreleased" >&2
	exit 1
fi
if [ -n "$plain" ]; then
	# Bold, code and the line wrapping go; each bullet becomes a line.
	printf '%s\n' "$notes" | sed -e 's/\*\*//g' -e 's/`//g' \
		| awk '/^- / { if (line != "") print line; line = $0; next }
			/^$/ { if (line != "") print line; line = ""; next }
			{ sub(/^ +/, ""); line = (line == "") ? $0 : line " " $0 }
			END { if (line != "") print line }'
else
	printf '%s\n' "$notes"
fi
