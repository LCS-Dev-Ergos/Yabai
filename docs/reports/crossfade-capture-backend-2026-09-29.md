# Crossfade capture backend: local candidate and live gate

Date: 2026-09-29. Status: local source correction; no daemon installation,
restart, release or real Desktop capture from this worktree.

## Why this backend

The [retention diagnosis](windowserver-capture-retention-2026-09-29.md)
isolated approximately one 6016 × 3384 frame of cumulative WindowServer
growth per call to `captureImageInRect` on the tested macOS 27.2 host. Twelve
calls to `captureScreenshotWithRect` did not show that slope in a bounded
client-alive trial. The internal WindowServer object is unknown. The trial
does not establish visual equivalence or long-term stability.

The production path now requests one `SCScreenshotOutput` with a display-space
rectangle and an SDR `sdrImage`. It asks for the display mode's physical pixel
width and height, local-display rendering and a visible cursor. The callback
retains the image before the output object can be destroyed and owns the
configuration through callback completion; the existing request releases its
image after preparation or cancellation. A missing image or
size mismatch omits the effect. The request still uses the original 150 ms
deadline, generation, token and single-in-flight policy. No framework error
or timeout retries through the old entry point.

The macOS 26 SDK describes rectangle coordinates in screen points across
displays, output dimensions in pixels, SDR output in the display colour space,
and a visible cursor by default. The original API had no cursor setting; its
observed cursor behavior is still unknown. The portable A/B probe explicitly
hid the cursor, so cursor appearance needs live comparison. The current
overlay copies the CGImage into a window with that image's colour space;
SDR/local-display is intended to preserve that path, but color, HDR
tonemapping, physical dimensions, rotated or scaled displays, and screen
continuity still require visual checks.

On macOS earlier than 26 the crossfade is omitted and ordinary navigation
continues. A filtered capture might later restore the effect there if its
availability, cost, and output are verified. This candidate does not perform
per-request display enumeration or start a continuous stream.

## Offline evidence

`navigation_snapshot_tests` calls the actual snapshot request and capture
code through a ScreenCaptureKit mock. Before the change it invoked the legacy
selector and failed the backend assertion. Afterward it checks the new
selector, rectangle, SDR/local-display configuration, physical dimensions,
cursor request, wrong-size rejection, and the existing timeout, late callback,
cancellation and token paths. A passing mock cannot establish WindowServer
memory behavior, TCC access, frame content, or presentation quality.

The complete Debug build and 11/11 CTest suite passed outside the restricted
filesystem sandbox (several unrelated core tests create fixed `/tmp` sockets
or files). The snapshot target passed under ASan/UBSan and TSan/UBSan; the
public ARC reproducer compiled but was not run here. These are offline checks.

## Coordinated live protocol

Run only in a quiet interval agreed with the performance investigator. Keep
the installed daemon and the ordinary checkout untouched until the candidate
is selected for a separate deployment check. Record the exact binary hash,
macOS build, WindowServer PID, display bounds/modes, scaling, color settings,
Space pair and workload before comparing.

1. Build `tools/effects/capture_memory_probe.m` using the command in the
   retention report. Run `newrect 12` as a separate client without saving
   images. At READY and after every STEP, sample WindowServer's `top` MEM and
   the client PID; at DONE sample again after a 5-second hold **while the
   client remains alive**, then after its exit. Stop on input or unexpected
   growth. This reproducer uses default output dimensions and hides the
   cursor, so it is a retention control rather than a visual acceptance test.
2. If a legacy A/B is still needed, cap `direct` at three calls and stop if
   WindowServer rises by about three frames or 300 MiB. Measure READY, every
   STEP, the hold while the client is alive and after exit. Do not interpret
   only the post-exit value: the known growth disappeared on client exit.
3. After an authorized candidate activation, use the same two-display
   workload and compare six effect-free switches against six crossfades, with
   per-step `top` MEM, a hold, and no concurrent Desktop reordering. A stable
   plateau during the daemon's lifetime is required before longer trials.
   If memory rises frame by frame, stop effects and collect the trace.
4. Capture a short visual sample in each direction on both displays. Check
   outgoing content, Finder icons, SketchyBar, black/white frames, cursor
   duplication or lag, color and HDR appearance, physical image size, focus,
   late or missing capture fallback and 150 ms deadline behavior. Repeat
   after a display sleep/reconfiguration only once the basic run passes.

The tests and small API trial support the candidate. Release and daily-use
acceptance remain open until the coordinated live protocol is complete.
