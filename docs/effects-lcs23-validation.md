# Desktop snapshot crossfade: lcs.23 candidate

2026-09-28. Based on `dev` at `0535b1b`, with signed lcs.22 and payload
`2.1.31-lcs.12` installed during diagnosis. Only the MacBook display is
connected: ID 1, 2056 × 1329 logical points, 4112 × 2658 pixels. The final
probe reports a display interval of 8.33 ms. No installed daemon or payload
was replaced during these checks.

## Direction and diagnosis

The user reaffirmed the full-Desktop crossfade, reported disappearing Finder
icons and a black transient entering Desktop 1 from 2 or 10, and asked for
effects to remain the priority. Blur was considered but explicitly not as a
substitute for correcting those defects. The user requested a dedicated
GPT-6 Luna chat for final publication/package/install preparation.

The installed lcs.22 path reproduces both defects in captured frames. In a
2→1 crossfade, whole-screen luminance falls from 42.06 to 2.18/255; a fixed
mask of bright Desktop content falls from 237.10 to 5.08. The ordinary-switch
control preserves both. In 10→1, screen luminance reaches 11.7 and the icon
mask falls from 234.95 to 9.21. Other empty pairs also dim the icons.

Finder's Desktop window is shared by all ten Desktops on this display, while
wallpaper windows are separate. The defects occur while the old renderer
modifies whole-Space alpha and ordering. This establishes the problematic
rendering path, not the precise internal SkyLight rule responsible for the
particularly dark first Desktop.

SketchyBar's click failure had a separate, measured cause: native
MenuBarAgent windows at level 25 intercepted the hit-test grid over the
visible bar at levels 2/3. Runtime `topmost=on` moves the bar above them;
the same grid then reaches SketchyBar, and the user confirmed first-click
response. The matching Dotfiles change in `bar.lua` awaits packaging.
Temporary click/callback instrumentation was removed.

## Implementation

The daemon captures one composed outgoing frame, places it in its own sticky,
noninteractive window, switches Desktop normally and dissolves that window.
Only its owned window's alpha changes. It does not manipulate Space alpha,
Space levels, wallpaper heuristics or shared Finder windows. The payload is
unchanged; its legacy crossfade opcode is retained but unused by this daemon.

The image is attached to a Core Animation remote surface directly. A timer
applies smoothstep at the display mode's reported cadence. At most one capture
and one overlay exist globally; 24 million pixels is the image limit. Capture
requires macOS 15.2+ and pre-existing permission, and never prompts during
navigation. Unsupported, refused, late or failed capture falls back to the
ordinary switch. A callback arriving after timeout cannot resurrect an image.
The 150 ms capture budget is checked even when the API completes synchronously
after that deadline; it cannot interrupt a slow framework call itself.

Completion, cancellation, a failed switch and a one-second watchdog release
the overlay, surface, context and image. Pointer input during capture cancels
the pending switch. Display changes, Mission Control, Dock restart, wake and
other commands also cancel the overlay. Existing navigation pacing and focus
behavior are retained. The owned window ignores clicks and sits below the bar.

An experiment animating CALayer opacity directly was rejected: even after
clearing the WindowServer backing store, captured frames showed a roughly
7% icon-brightness dip. The selected window-alpha variant preserves the
static image. No blur or macOS defaults changes are included.

## Presented-frame checks

`snapshot_probe.m` includes the production renderer and uses the installed
daemon only for an ordinary switch. The recorder observes the resulting
composited display. The test waits for five seconds without input, stops on
new input, and restores the initial Desktop and window on normal completion.
All completed cases below restored Desktop 4 and window 714.

| Case | Icon mask loss / 255 | Whole-Desktop excursion / 255 | Bar excursion / 255 |
| --- | ---: | ---: | ---: |
| 2→1, final renderer | 0.00 | 0.01 | 0.05 |
| 8→9, final renderer | 0.02 | 0.01 | 0.00 |
| 10→1, occupied→empty | Not a static pair | No black frame; 43.99→41.99 | 0.22 |
| 2→1, final deadline/cadence check | 0.00 | 0.01 | 0.03 |
| 1→10, empty→occupied | Not a static pair | Blend inspected through measurements | 0.09 |

The occupied→empty frames show the Finder window dissolving over stable
Desktop icons. The fixed mask measures bright baseline content, not semantic
recognition of every icon. Local regression thresholds are 5/255 icon loss,
3/255 whole-Desktop excursion for matching empty Desktops, and 3/255 for the
bar. These are bounded defect checks, not a general visual-quality metric.

Evidence remains local under ignored directories:

- `build/lcs22-macbook/`: installed-version crossfade and no-effect controls.
- `build/lcs23-final-visual/`: 2→1, 10→1 and 8→9.
- `build/lcs23-final-cadence/`: final 2→1 and 1→10, using the daemon's interval helper.
- `build/lcs23-ca-animation/` and `build/lcs23-ca-clear/`: rejected layer-opacity experiments.

## Cost and limits

The first snapshot prototype spent roughly 49–57 ms copying the Retina image
through CGContext. Remote-layer setup measured about 1.6–4 ms in the comparison
probe. Final warm preparations measured 43.6–56.8 ms, with capture and the
presentation opportunity included. Cold preparations varied substantially
(115–237 ms across variants); late captures now fall back. These measurements
include PNG recording overhead and are not a controlled latency or GPU
benchmark. A single still image also freezes outgoing video during the blend.

There are no per-application or wallpaper alpha writes in the new path, but
capture and compositing still cost WindowServer work. No total CPU/GPU saving,
RAM-pressure diagnosis or improvement with two 4K monitors is established.
Display-mode cadence is a timer interval, not a display link or presentation
fence. The remote surface uses private APIs already present elsewhere in this
setup; runtime portability beyond the tested host remains unverified.

## Automated checks and release boundary

Navigation tests assert capture-before-switch, fade-before-focus, ordinary
fallback, failed-switch cleanup, pointer cancellation and avoidance of the
legacy Space opcode. Renderer tests execute real dispatch timers and mock
external capture/window services, including allocation failures, replacement,
endpoint, late callbacks and the watchdog. Debug and ASan/UBSan passed 10/10;
TSan passed snapshot, schedule, queue and fade tests, 4/4. Static analysis
passed without baseline changes. Both parser fuzz targets passed a local
20-second campaign. These checks do not run the injected payload under a
sanitizer or establish live signed-daemon integration.

After signed release/package preparation, the user performs the existing
`darwin-rebuild switch` step. Verify daemon version, payload handshake,
Screen Recording availability inside the daemon, captured 2→1 and 10→1,
rapid reversals, held keys, individual presses and final keyboard focus.
Repeat under the two-display workload when it is available. The isolated
renderer result is promising but is not final production acceptance.
