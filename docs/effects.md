# Opacity effects

Payload `2.1.31-lcs.3` replaces the per-window fade threads with one shared
worker. The worker sleeps on a condition variable when no fades are active.
All requests and frame writes use the same mutex; an immediate opacity change
cancels a pending fade before applying its value. Retargeting starts from the
window's current alpha, and only the latest target for each window is retained.

Progress is calculated from `CLOCK_MONOTONIC`, using an ease-out cubic curve:
`1 - (1 - t)^3`. The endpoint is assigned exactly once elapsed time reaches
the requested duration. Late frames skip ahead rather than replaying steps.
This changes the shape of ordinary opacity fades as well as navigation fades.

One timer serves all active windows, at up to 120 frames per second. This is
not display-link synchronization. System-call and scheduling delays can still
delay frames and completion; the engine does not promise a hard real-time
deadline. Even an overdue frame yields through a timed condition wait so a
new request is not starved for the duration of an animation.

Invalid alpha/duration values are ignored. Closed windows are removed when
their alpha cannot be read or written. If allocation or worker creation fails,
the requested opacity is applied immediately instead of leaving a stuck fade.

## Verification

`fade_tests` exercises the production engine with simulated SkyLight calls.
It checks easing and elapsed-time endpoints, skipped frames, cancellation,
invalid input, failed system calls and worker creation, concurrent updates,
and interruption when system calls are slower than a frame. ASan/UBSan and
ThreadSanitizer can instrument this standalone test even though the injected
payload itself is not instrumented by those presets.

On the development host, a 100 ms fade with a simulated 4 ms cost for each
alpha write took 184.5 ms with the old payload code and 106.2 ms with the new
engine. This is a controlled timing check, not a measurement of visual quality
or WindowServer CPU in the live Dock.

## Native compositor investigation

The reconstructed [Mousecape SkyLight declarations](https://github.com/alexzielenski/Mousecape/blob/master/Mousecape/mousecloak/CGSInternal/CGSWindow.h)
include a duration for `CGSSetWindowListAlpha`. Inspection of the installed
macOS 27.2 SkyLight confirmed that `SLSSetWindowListAlpha` forwards float alpha
and duration to a WindowServer message. A probe on its own temporary window
returned success, but querying alpha returned the target immediately.

Those observations do not establish the rendered transition, cancellation
semantics or behavior on other applications' windows from Dock. Consequently
this release uses the tested shared worker. Native interpolation, display-link
pacing, spring curves and a configurable easing selector remain future work.
