#!/usr/bin/env bash
#
# Runs the clang static analyzer over the three translation units.
#
# The scripting-addition payload and loader run inside or next to Dock.app and
# must stay free of warnings. The yabai unity build inherits upstream findings
# that are mostly ownership-naming false positives, so its warnings are
# compared with tools/analyzer-baseline.txt instead: a warning that is not in
# the baseline, or appears more often than recorded, fails the run.
#
# usage: tools/analyze.sh [--update-baseline]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
baseline="$root/tools/analyzer-baseline.txt"
clang=${CLANG:-/usr/bin/clang}
flags=(--analyze -std=c11 -fno-objc-arc -F/System/Library/PrivateFrameworks -o /dev/null)

cd "$root"

status=0
for arch in arm64 x86_64; do
	for source in src/osax/payload.m src/osax/loader.m; do
		if ! "$clang" "${flags[@]}" -arch "$arch" -Xanalyzer -analyzer-werror "$source"; then
			echo "error: $source ($arch) has analyzer warnings" >&2
			status=1
		fi
	done
done

# Line numbers shift with every edit, so findings are keyed by file, message
# and checker, and counted per key.
current=$("$clang" "${flags[@]}" -arch arm64 -Xanalyzer -analyzer-opt-analyze-headers src/manifest.m 2>&1 |
	sed -nE 's/^([^:]+):[0-9]+:[0-9]+: warning: (.*)$/\1: \2/p' | sort | uniq -c | sed -E 's/^ +//')

if [[ ${1:-} == --update-baseline ]]; then
	printf '%s\n' "$current" >"$baseline"
	echo "updated $baseline"
	exit $status
fi

# Each line is "<count> <file>: <message> [<checker>]".
regressions=$(printf '%s\n' "$current" | awk '
    NR == FNR { count = $1; sub(/^[0-9]+ /, ""); allowed[$0] = count; next }
    { count = $1; sub(/^[0-9]+ /, ""); if (count > allowed[$0] + 0) print count " (baseline " allowed[$0] + 0 ") " $0 }
' "$baseline" -)

if [[ -n $regressions ]]; then
	echo "error: new analyzer warnings in the yabai unity build:" >&2
	printf '%s\n' "$regressions" >&2
	status=1
fi

exit $status
