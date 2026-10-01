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

- `yabai_tests` runs the core's unit tests against the unity build. Synthetic
  views cover BSP insertion and removal, geometry, rotation, mirroring,
  balancing, equalizing, traversal and directional lookup. Layout tests run
  the actual Space commands with fixed display geometry and visibility;
  window tests cover rule matching and effects and managed-window relations.
  NUL-separated requests check dispatch and replies in all seven command
  domains, plus state changes in the mutating domains. A single case runs with
  `build/debug/tests/yabai_tests <test-name>`; the regular CTest run selects
  every case. These tests also cover navigation arguments, opacity-policy
  protocol, signal dispatch and storage bounds, scripting-addition request
  size and query string escaping. The signal tests launch
  harmless local shell actions and check socket lifetime, isolated event
  variables, retained standard output/error and that an action answers for
  its own privacy permissions. They do not run the daemon.
  The daemon-socket tests bound reading a request and writing its reply against
  a client that sends nothing, trickles its request, hangs up or never reads,
  check the reply that tells a client why its request was refused, including
  the one past 128 waiting connections, and run requests through the message
  handler. The client tests drive the code shared by `yabai -m` and `yabai-msg`
  against a fake daemon: the packed request, output and failure lines, a 1 MB
  reply, and a refusal that arrives while a long request is still being sent.
  The pattern tests check the cost estimate of rule and signal patterns
  against accepted and refused shapes, without compiling the refused ones, and
  the refusal through `rule` and `signal`. The event-loop test checks that the
  loop's semaphore is private and that a merged mouse move adds no wake-up. The value tests check that
  decimal settings and `window --opacity` take whole numbers, that negative or
  non-finite durations, ratios, positions and sizes and grids without rows or
  columns are refused before anything changes, that labels and scratchpad
  names are escaped in JSON, that a repeated rule or signal key releases what
  it replaces (memory is measured only in a build without sanitizers), and that
  the configuration file runs from a path with spaces and shell characters.
- `navigation_tests` checks the fork's [space navigation](NAVIGATION.md)
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
- `event_queue_tests` checks the event loop's queue: order when events wrap
  around the ring's end, growth, merged mouse moves, allocation failure and
  concurrent producers.
- `socket_identity_tests` runs the daemon's peer check against real
  processes: `nc` connected to a socket of the test passes `anchor apple` in
  full once, and a second `nc` passes from the cache of trusted code; another
  user, another requirement and the test itself, which fails the requirement,
  are refused and never cached. It prints the time of a full and a cached
  check.
- `yabai_msg_frameworks` checks that `yabai-msg` links no framework.
- `focus_tests` exercises production focus-event handling with simulated OS
  calls: reuse of pending observations, stale activations, invalid/hidden or
  minimized windows, and the normal AX fallback, including no focused window.
- `fade_tests` checks the production [opacity engine](EFFECTS.md) with simulated
  SkyLight calls, including timing, focus ownership, shared navigation epochs,
  display cadence/cancellation, failure restoration and concurrent requests.
- `osax_patterns` checks the payload's lookups against the local Dock binary
  on Apple Silicon when `YABAI_BUILD_TOOLS=ON` (the default). It inspects the
  binary without loading a payload; see [Scripting addition](OSAX.md).
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
preparation time, or why none was used), and every fade frame the payload
writes.
Record them with Instruments' os_signpost instrument, for example by adding it
to the Animation Hitches template, to see them against WindowServer's frames.
The unified log keeps them as well, for a few minutes depending on how much
else it records, so they can be read shortly afterwards:

```sh
/usr/bin/log show --last 5m --signpost --style compact \
    --predicate 'subsystem == "com.lcs.yabai"'
```

In zsh, a bare `log` is a builtin, hence the full path.

## Live navigation tests

`tools/live` drives the installed daemon through its socket, as skhd does, so
it changes the Desktop and focus on screen: announce a run first. Each tool
waits for five seconds without keys or clicks, stops at the next one, and
restores the Desktop and window it started from. Its helpers `space_poll` and
`ax_focused` are built with the other tools into `build/<preset>/tools`
(`YABAI_TOOLS` overrides the directory).
The helper selects the candidate's private socket when it exists and the
installed daemon's legacy `/tmp` socket during the transition.
For a signed daemon that enforces its designated requirement, set
`YABAI_LIVE_CLIENT=binary` so the harness sends requests through the signed
`/opt/yabai/bin/yabai -m` client. `YABAI_LIVE_BINARY` overrides that path.
Launching a process per request adds client overhead to timing measurements.

An unsigned local daemon/payload pair can be compiled explicitly with
`cmake --preset debug -DYABAI_ALLOW_UNSIGNED_LOCAL=ON` or
`make UNSIGNED_LOCAL=1`. The default build rejects unsigned socket peers.
Never use the local option for a release; it applies to both socket servers.
When `--load-sa` installs a different payload version, it restarts Dock.
Run `--load-sa` again after Dock returns to inject and validate that payload.
Restore the installed signed payload and daemon after a local smoke.

```sh
python3 tools/live/burst.py 2 0.25 taps 5 100 -- held 1500 -- reverse 3 100 -- back 4 2 100
python3 tools/live/focus_reverse.py 10 100 2
```

`burst.py` prints one JSON line per scenario: when each request was sent,
every change of the active Space, where navigation ended against where it
should have, and whether the focused window belongs there. `next` and `prev`
cycle through the Desktops of every display. A scenario whose start Desktop
never became active reports `setup_ok: false`: an application that makes its
window on another display key, as Chromium does, can keep that display active.
`back` overshoots and takes steps back while they are still queued, which
leaves the Desktop reached to take focus once the presses stop. Presses under
400 ms apart make a quick burst, which switches without effects; `taps 5 450`
measures crossfade steps.
`focus_reverse.py` repeats three `next` and three `prev` and checks the Desktop
and focus they end on.

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
luminance check catches the [lcs.21 disappearance](reports/Space-Crossfade_7.1.25-lcs22.md),
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
The same target covers the veil, which needs no capture and so also runs on
macOS before 26: the window at resolution 1.0 drawn solid black, its opacity
set before it is ordered in, no capture call, the fade's alphas within the
veil's opacity and never rising, release at the endpoint, on a failed switch,
on cancellation and when the display leaves the target, no window or Space left
after a failure to create the window, its context or the auxiliary Space, to
attach the window or to order it, a new veil retiring the one showing, and a
veil retiring a pending capture request, whose late callback then does nothing.
The blurred veil (`navigation_veil_blur`) is covered the same way, with the
blur call and the Reduce Transparency query mocked: radius 0 keeps the plain
veil exactly (no blur call, opacity 0.4 before the order, an opaque fill);
a radius above 0 asks for that radius, style 1, before the window is ordered
in, fills at a tint of 0.25, orders the window at alpha 0, then writes
non-decreasing alphas ending at exactly 1.0 over at least 100 ms plus the two
refreshes, and the fade after the switch stays within [0, 1] and releases;
Reduce Transparency gives the plain veil; a refused blur, a failed alpha write
and a cancellation during the fade-in each return false and leave no window or
Space. The unity tests check `config navigation_veil_blur`: its default, get
and set of 0 to 100, and refusal of 101, negatives, fractions, hexadecimal and
words.
Both fade curves (`navigation_fade_curve`, `smooth` and `ease_out`) are checked
at run time: their endpoints, monotony and the point where 5% of the change
becomes visible, and that an overlay keeps the curve configured when it was
created. `navigation_tests`, the schedule and ingress tests and the unity tests
cover the veil step, the `veil` request and `config navigation_fade_curve`.

### Transition timing

Two tools separate where the time of a transition goes. The headless one never
changes a Desktop: it runs the production capture request and the production
draw into an overlay window that is never ordered in, at several output scales,
interleaved:

```sh
xcrun clang -fno-objc-arc -O2 tools/effects/capture_scale_bench.m \
  -framework Cocoa -framework Carbon -framework CoreGraphics \
  -framework ScreenCaptureKit -framework QuartzCore \
  -F/System/Library/PrivateFrameworks -framework SkyLight \
  -o build/tools/capture-scale-bench
build/tools/capture-scale-bench DISPLAY_ID 10 1 0.75 0.5
```

`tools/effects/transition_timing.py` is live: it switches between two Desktops
of one display whose central content differs and stays static, and measures,
from the request, the first presented frame that visibly changed (5% of the
source-to-destination colour distance) and the frame from which the
destination stays settled. Variants run interleaved and rotate each round:
the installed daemon without effect and with its crossfade, snapshot probes
compiled with the other curve or without the pre-switch wait
(`-DSNAPSHOT_PROBE_CURVE=1 -DSPACE_SNAPSHOT_PRESWITCH_WAIT=0` added to the probe
build above; the probe takes the curve from that macro, since it has no daemon
configuration), and the capture-free veil probe (`tools/effects/veil_probe.m`).
The script's docstring lists the variant syntax. It shares the five-second idle
guard and restoration of the other live tools; announce the run. Probe and
client launches add process start-up to every variant alike, so compare
variants with each other rather than with the daemon's own internal phases.

## Static analysis

```sh
tools/analyze.sh
python3 tools/check-area-headers.py
```

The script uses Apple clang by default (`CLANG` overrides it). Payload and
loader warnings fail the check on both arm64 and x86_64. The test manifest's
arm64 findings keep their historical `tools/analyzer-baseline.txt` allowance;
the production translation units use `tools/analyzer-units-baseline.txt`.
Paths are normalized before comparison, and a new finding or increased count
fails either check. The separate units can expose additional paths through
header-only helpers and narrower analyzer context. Both baselines are
allowances for known diagnostics, not proof that they are harmless.

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
  request check, tokenization and value parsing, and the `rule` and `signal`
  handlers with their patterns, which only parse and keep their entries.
  Handlers that manipulate the running window manager are excluded, as are
  rules naming a display or Space, which WindowServer resolves.
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
The sanitizer CI job also runs the event queue, fade, navigation queue,
schedule and snapshot tests separately under ThreadSanitizer.

## Live release checks

Installing a build or running `sudo yabai --load-sa` can restart Dock. The
maintainer approves any change to the active installation and any payload
load; automated tooling does not perform them. After the system is switched
to the signed release, check space creation, destruction, focus and moves
(`space --move`, `--swap` and `--display`), plus window opacity, layer, shadow
and sticky state. Verify the load result and query spaces through the signed
`yabai` binary. The unsigned `sa_client` works directly against the payload
only in an explicit unsigned local build. On macOS 27 the payload handshake
expects attribute bits `0x5D`.
