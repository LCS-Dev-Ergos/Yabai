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

- `yabai_tests` runs the upstream unit tests against the unity build, plus
  navigation argument and signal dispatch regressions. The signal tests launch
  harmless local shell actions and check socket lifetime, isolated event
  variables and retained standard output/error. They do not run the daemon.
- `navigation_tests` checks the fork's [space navigation](navigation.md)
  implementation with simulated OS calls, including rapid repeats and failure
  restoration; numeric arguments are also covered by the unity tests.
- `navigation_queue_tests` checks how relative requests are recognized and
  merged while one waits: repeats, separate presses, directions and ordering.
- `focus_tests` exercises production focus-event handling with simulated OS
  calls: reuse of pending observations, stale activations, invalid/hidden or
  minimized windows, and the normal AX fallback, including no focused window.
- `fade_tests` checks the production [opacity engine](effects.md) with simulated
  SkyLight calls, including timing, cancellation and concurrent requests.
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
  space handlers return early; threaded opacity fades are excluded.

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
The sanitizer CI job also runs `fade_tests` separately under ThreadSanitizer.

## Live release checks

Installing a build or running `sudo yabai --load-sa` can restart Dock. Obtain
the user's approval before changing the active installation or loading the
payload. After the user switches to the signed release, check space creation,
destruction, focus and moves (`space --move`, `--swap` and `--display`), plus
window opacity, layer, shadow and sticky state. Verify the load result and
`build/debug/tools/sa_client handshake`: macOS 27 expects attribute bits `0x5D`.
