#!/usr/bin/env bash
#
# Runs the clang static analyzer over the daemon, payload and loader units.
#
# The scripting-addition payload and loader run inside or next to Dock.app and
# must stay free of warnings. Compare the test manifest with its historical
# baseline and the separately compiled daemon with its own baseline. A new
# warning or an increased count fails the run.
#
# usage: tools/analyze.sh [--update-baseline]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
baseline="$root/tools/analyzer-baseline.txt"
units_baseline="$root/tools/analyzer-units-baseline.txt"
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

# Line numbers shift with every edit, so findings are keyed by normalized
# source path, message and checker, then counted per key.
daemon_sources=(src/yabai_main.m src/navigation/navigation_effects.m
    src/sa/sa_unity.m src/displays/displays.m src/applications/applications.m
    src/spaces/layout_unity.m src/events/events.m src/ipc/ipc.m)
collect_warnings() {
    for source in "$@"; do
        "$clang" "${flags[@]}" -arch arm64 -Xanalyzer -analyzer-opt-analyze-headers "$source" 2>&1
    done | python3 tools/analyzer_warnings.py | sort | uniq -c | sed -E 's/^ +//'
}

legacy=$(collect_warnings src/manifest.m)
current=$(collect_warnings "${daemon_sources[@]}")

if [[ ${1:-} == --update-baseline ]]; then
	printf '%s\n' "$legacy" >"$baseline"
	printf '%s\n' "$current" >"$units_baseline"
	echo "updated $baseline and $units_baseline"
	exit $status
fi

# Each line is "<count> <file>: <message> [<checker>]".
check_baseline() {
    local findings=$1 recorded=$2 label=$3 regressions
    regressions=$(printf '%s\n' "$findings" | awk '
        NR == FNR { count = $1; sub(/^[0-9]+ /, ""); allowed[$0] = count; next }
        { count = $1; sub(/^[0-9]+ /, ""); if (count > allowed[$0] + 0) print count " (baseline " allowed[$0] + 0 ") " $0 }
    ' "$recorded" -)
    if [[ -n $regressions ]]; then
        echo "error: new analyzer warnings in $label:" >&2
        printf '%s\n' "$regressions" >&2
        status=1
    fi
}

check_baseline "$legacy" "$baseline" "the test manifest"
check_baseline "$current" "$units_baseline" "the daemon units"

exit $status
