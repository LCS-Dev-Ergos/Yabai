# Testing

Run from the repository root on macOS with CMake 3.25 or newer, Ninja and
the Xcode command-line tools installed. Builds stay under `build/<preset>`.

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

`release` uses the same tests with an optimized build. Pass
`-DYABAI_LINK_COMPILE_COMMANDS=OFF` when configuring to avoid changing the
editor's root `compile_commands.json` symlink.

## CTest and sanitizers

- `yabai_tests` runs the core's unit tests against the unity build, plus
  navigation argument, opacity-policy protocol, signal dispatch and query
  string escaping regressions. The signal tests launch
  harmless local shell actions and check socket lifetime, isolated event
  variables and retained standard output/error. They do not run the daemon.
- `navigation_tests` checks the fork's [space navigation](navigation.md)
  implementation with simulated OS calls, including rapid repeats, failure
  restoration, the starting Desktop of relative navigation, the raise for
  applications with windows on two displays, the focus a burst's last step
  gives a Desktop it finds current, and a queued crossfade that switches when
  its capture's event comes: with or without the image, stopped by a
  cancellation, a click, Mission Control or a display animation, abandoned by
  another command, or with its window gone; numeric arguments are also
  covered by the unity tests.
- `navigation_spaces_tests` reads a constructed Desktop snapshot: mission-control
  order across displays, visibility, fullscreen type, the Desktops visible on
  other displays, the fallback to WindowServer for unknown or unreadable data and
  the window numbers the cross-display raise check reads.
- `navigation_focus_tests` checks the deferred focus between two windows of one
  application: the event sequence and its 10 ms spacing, and cancellation by a
  newer navigation, another command or a focus change; and both raise paths.
- `navigation_schedule_tests` runs the pacing queue on a simulated clock: the
  rhythm, the burst and held-key blend durations, the wait for an activation
  and its confirmation, key repeats, reversals, Desktop numbers, the ten-switch
  limit with its jumps and replacement (every press reaching the destination),
  which step settles a Desktop, refused requests, clicks, other commands,
  failed steps, early wakes, and a step that waits for its capture: nothing
  else starts meanwhile, and a late report after a cancellation changes
  nothing.
- `navigation_queue_tests` checks how relative requests are recognized and
  merged while one waits: repeats, separate presses, directions, interleaved
  queries, allocation failure and concurrent admission/consumption.
- `focus_tests` exercises production focus-event handling with simulated OS
  calls: reuse of pending observations, stale activations, invalid/hidden or
  minimized windows, and the normal AX fallback, including no focused window.
- `fade_tests` also runs the [Desktop crossfade](effects.md#desktop-crossfade)
  against a model of WindowServer's Desktops.
- `fade_tests` checks the production [opacity engine](effects.md) with simulated
  SkyLight calls, including timing, focus ownership, shared navigation epochs,
  display cadence/cancellation, failure restoration and concurrent requests.
- `osax_patterns` checks the payload's lookups against the local Dock binary
  on Apple Silicon when `YABAI_BUILD_TOOLS=ON` (the default). It inspects the
  binary without loading a payload; see [Scripting addition](osax.md).
- `sanitize` instruments yabai and its unit tests with ASan and UBSan.
- `thread-sanitize` uses TSan and UBSan instead. Run it separately from ASan.

```sh
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ctest --preset sanitize
```

The sanitizer presets do not instrument the payload or loader injected into
Dock. The standalone fade test is instrumented; run its race check with
`cmake --build --preset thread-sanitize --target fade_tests` and
`ctest --preset thread-sanitize -R fade_tests` after configuring that preset.
Passing these checks does not verify live space or window operations.

The optional display-link probe uses real AppKit callbacks with simulated
alpha writes; it creates no windows and does not touch the running daemon:

```sh
clang -fsanitize=thread -g tests/fade/display.m \
  -framework AppKit -framework QuartzCore -o build/fade-display-probe
build/fade-display-probe
```

Run it from an active desktop session on macOS 14+. It checks callback delivery,
idle link disposal and recovery with its main run loop stalled. It does not
measure rendered frames or validate execution inside Dock.

## Signposts

The daemon and the payload emit signposts in subsystem `com.lcs.yabai`:
category `navigation` for requests, steps (with how late each ran after it
could), activations and the focus that confirms them; category `events` for
every daemon event handled in more than 10 ms, which delays everything queued
behind it; category `effects` for the daemon's snapshots (capture and
preparation time, or why none was used), and the payload's crossfades and
every fade frame it writes.
Record them with Instruments' os_signpost instrument, for example by adding it
to the Animation Hitches template, to see them against WindowServer's frames.
The unified log keeps them as well, for a few minutes depending on how much
else it records, so they can be read shortly afterwards:

```sh
/usr/bin/log show --last 5m --signpost --style compact \
    --predicate 'subsystem == "com.lcs.yabai"'
```

In zsh, a bare `log` is a builtin, hence the full path.

## Presented bar frames

The opt-in probe records the display's presented frames with existing Screen
Recording permission. Build it locally:

```sh
xcrun clang -fobjc-arc -O2 tools/effects/frame_capture.m \
  -framework Foundation -framework ScreenCaptureKit -framework CoreMedia \
  -framework CoreVideo -framework CoreGraphics -framework ImageIO \
  -o build/tools/effects-frame-capture
```

Announce the test before running it. Choose two existing ordinary Desktops on
the same display; this example uses the empty 9 and 10 on the test host:

```sh
python3 tools/effects/check_bar.py 9 10 \
  --capture build/tools/effects-frame-capture \
  --output build/bar-regression --effects crossfade none
```

The tool waits for five seconds without keys or clicks, aborts on new input,
and restores and verifies the starting Desktop and focused window after a
normal run. On new input it leaves the user's new focus in place. Exit 0
means all bar checks passed, 1 means a luminance excursion exceeded 3/255,
and 2 means the run or restoration failed. It saves frame measurements and
JSON results locally. `--images` also saves PNGs for inspection and adds
capture work; do not use that mode to benchmark frame timing.

The top 40 logical points are specific to this bar layout. Its endpoint-based
luminance check catches the [lcs.21 disappearance](reports/lcs22-space-crossfade.md),
but cannot establish Finder icon visibility, focus correctness throughout a
burst, smooth motion or CPU/GPU performance. Notifications and changing bar
content can affect the score; inspect images when the result is ambiguous.

### Desktop icons and the isolated renderer

For empty Desktops with identical wallpaper and icons, add `--images
--static-desktop` (requires Pillow). The check uses a fixed mask of bright
baseline pixels below the bar and rejects a loss over 5/255, or a full-Desktop
luminance excursion over 3/255. It catches disappearance and alignment errors;
it is not a perceptual quality score and must not be applied to changing content.

To exercise the production snapshot renderer without installing a daemon:

```sh
xcrun clang -fno-objc-arc -O2 tools/effects/snapshot_probe.m \
  -framework Cocoa -framework Carbon -framework CoreGraphics \
  -framework ScreenCaptureKit -framework QuartzCore \
  -F/System/Library/PrivateFrameworks -framework SkyLight \
  -o build/tools/snapshot-crossfade
python3 tools/effects/check_bar.py 2 1 \
  --capture build/tools/effects-frame-capture \
  --snapshot-probe build/tools/snapshot-crossfade --effects snapshot \
  --output build/snapshot-check --images --static-desktop
```

Choose indexes from the current topology. `--snapshot-warm` adds three
prepare/cancel samples before the measured switch. A probe exits unsuccessfully
if it only fell back without animating. Its capture permission and process
lifecycle differ from the signed daemon, so passing this probe does not certify
installed integration, rapid queued navigation or keyboard focus.

Bar visibility alone does **not** validate a crossfade. On lcs.23 it passed
while the destination appeared, the outgoing image returned and the transition
then finished. For a static occupied-to-empty pair with distinct central content,
record without PNG encoding and use the separate progression check:

```sh
python3 tools/effects/check_bar.py 4 1 \
  --capture build/tools/effects-frame-capture --effects crossfade \
  --output build/blend-check --blend
```

Add `--snapshot-probe build/tools/snapshot-crossfade --effects snapshot` to test the
candidate helper. `--snapshot-warm` also exposes prepare/cancel flashes.
`tools/effects/check_blend.py CAPTURE_DIRECTORY` can analyze saved `frames.txt`
and `result.json`. It projects central RGB along the static source-to-target
change, rejecting backward progress or overshoot above 0.08 and fewer than four
intermediate frames. It is a bounded regression check, not an FPS benchmark or
a proof of perceptual smoothness. Changing content and nearly identical endpoints
invalidate this check; empty-to-empty icon visibility needs the separate
`--images --static-desktop` check. PNG capture can hide timing defects by slowing
the recorder, so it must not be used for progression acceptance.

`navigation_snapshot_tests` exercises the actual asynchronous renderer with
mocked capture/window dependencies and real timers. It checks endpoint release,
cancellation, window/context/auxiliary-Space failures, late capture, a single in-flight
request, a capture whose callback never comes (and then comes late), the
window's colour space before drawing, the auxiliary Spaces the daemon
recognises as its own and the watchdog; and the asynchronous capture: its
callback and deadline reports, tokens, late and denied captures, a late
request call, cancellation and retirement by a synchronous preparation. Debug, ASan/UBSan and TSan cover this target; presented
frames still require the live probe and installed-release check.

## Static analysis

```sh
tools/analyze.sh
```

The script uses Apple clang by default (`CLANG` overrides it). Payload and
loader warnings fail the check on both arm64 and x86_64. The daemon's arm64
unity build is compared with `tools/analyzer-baseline.txt`, keyed by source,
diagnostic and checker, including the count of each finding. New findings or
increased counts fail. Existing entries include upstream ownership warnings;
the baseline is an allowance, not proof that a finding is harmless.

Only run `tools/analyze.sh --update-baseline` after reviewing the diagnostics
and the resulting diff. Do not refresh the baseline just to pass CI.

## Fuzzing

The `fuzz` preset requires a clang distribution with libFuzzer, such as
Homebrew LLVM or the Nix clang toolchain; Apple clang does not ship it.
Select both C and Objective-C compilers on the first configure:

```sh
brew install llvm
CC="$(brew --prefix llvm)/bin/clang" \
OBJC="$(brew --prefix llvm)/bin/clang" \
  cmake --preset fuzz -DYABAI_FUZZ_SECONDS=60 \
    -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)"
cmake --build --preset fuzz --parallel
ctest --preset fuzz
```

The explicit SDK path also supplies framework headers for the universal
payload and loader when using Homebrew clang. CMake caches the compiler: use
a fresh build directory when switching toolchains. Both targets use
libFuzzer, ASan and UBSan:

- `fuzz_daemon_message`: socket framing, the accept thread's navigation
  request check, tokenization and value parsing; command handlers that
  manipulate the running window manager are excluded.
- `fuzz_payload_message`: request framing and reachable handler parsing with
  SkyLight calls stubbed and the payload constructor disabled. Dock-dependent
  space handlers return early. Opacity parsing, including focus/batch requests,
  runs with worker/main-queue scheduling disabled and pending fades freed after
  each input. Real threaded/display-link behavior belongs to the fade tests.

Each target runs for `YABAI_FUZZ_SECONDS` (default 30), with a 10-second limit
per input so blocked handlers produce timeout artifacts. CTest's `fuzz` preset
selects only these two tests. Checked-in seeds and regression cases live in
`tests/fuzz/corpus/`; generated inputs go to `build/fuzz/fuzz/corpus/`.
Failure inputs are written under `build/fuzz/fuzz/`. Replay one with its
matching executable, for example:

```sh
build/fuzz/fuzz/fuzz_payload_message build/fuzz/fuzz/crash-<hash>
```

Branch and pull-request CI runs each target for 60 seconds, enforces a
120-second outer limit per test, and uploads failure inputs and the CTest log
on failure. A clean run is a bounded parser check,
not validation of Dock integration, authentication or animation behavior.
The sanitizer CI job also runs fade, navigation queue, schedule and snapshot
tests separately under ThreadSanitizer.

## Live release checks

Installing a build or running `sudo yabai --load-sa` can restart Dock. Obtain
the user's approval before changing the active installation or loading the
payload. After the user switches to the signed release, check space creation,
destruction, focus and moves (`space --move`, `--swap` and `--display`), plus
window opacity, layer, shadow and sticky state. Verify the load result and
`build/debug/tools/sa_client handshake`: macOS 27 expects attribute bits `0x5D`.
