# Crossfade continuity after lcs.23 activation

On 2026-09-28 the user described the activated lcs.23 dissolve as trembling
and stuttering. The active Nix binary was verified as lcs.23; only the internal
MacBook display was connected. The full Desktop crossfade remains the chosen
effect. Blur is not used to conceal the defect.

## Reproduction and correction

ScreenCaptureKit recordings without PNG encoding reproduced a destination frame
appearing before the outgoing snapshot returned. The installed 4->1 crossfade
failed `check_blend.py`: backward progress 0.474, only two intermediate frames.
The independent bar check passed this broken transition; it was insufficient
for motion acceptance. The no-effect control switched once without returning.

An isolated helper initialized with `NSApplicationLoad`, as in the daemon,
reproduced the problem. Replacing the remote CA surface with a synchronous
bitmap alone still failed (backtrack 0.811). Moving the overlay to its own
auxiliary Space eliminated the reversal with either drawing backend. The
sticky overlay was disappearing during the ordinary source-Space hide and
returning later. The auxiliary Space keeps it independent of both user Spaces.

Rapid prepare/cancel samples exposed a second defect: the remote layer showed
white frames, including with the independent Space (excursion 5.211).
Drawing and flushing the captured image into the window backing before showing
it eliminated those frames. This is a deliberate preparation-time tradeoff:
the warm prototypes took roughly 150-185 ms with backing versus 98-133 ms with
the remote layer. These are short samples, not a CPU/GPU benchmark.

The candidate owns and destroys the auxiliary Space with the snapshot. It
checks assigned windows with `SLSCopyWindowsWithOptionsAndTags`, including
unordered windows. `SLSCopySpacesForWindows` does not enumerate this auxiliary
Space. The Space mutators' return registers are not treated as CGError values.
The existing watchdog, cancellation, capture bounds and one-in-flight limit
remain active. No payload changes or application-window opacity writes are needed.

## Bounded results

The final source was compiled into `tools/effects/snapshot_probe.m` and exercised
with the installed daemon's ordinary switch. Each completed test restored
Desktop 4 and window 714. Input-interrupted runs were excluded.

| Probe | Result |
| --- | --- |
| 4->1, no PNGs, three prepare/cancel samples | Backtrack 0; excursion 0; 12 intermediate frames |
| 10->1, no PNGs | Backtrack 0.009; excursion 0.009; 13 intermediate frames |
| 2->1, PNGs, matching empty Desktops | Icon luminance loss 0; whole-frame excursion 0.05/255 |
| 10->1, PNGs | Inspected source, blend and destination frames; no black Desktop frame |
| Bar visibility | Passed all four final-source probes |
| Debug / ASan+UBSan / targeted TSan | 10/10, 10/10, 4/4 passed |
| Clang static analysis | Passed |

Local raw evidence is under `build/lcs23-installed-timing`,
`build/lcs24-production-timing` and `build/lcs24-production-visual`.
The red and green progression checks use the same analyzer. PNG recordings
serve visibility inspection only; their encoding overhead changes timing.
No presentation FPS, total WindowServer load reduction or multi-display
performance claim follows from these checks.

## Remaining activation gate

These are candidate-renderer results, not acceptance of the next signed daemon.
After user activation, repeat the no-PNG progression check in that daemon,
empty-Desktop icon/black checks, rapid reversals, individual and held keys,
and final keyboard focus. The dedicated GPT-6 Luna publication chat handles
signed release and the Nix package; the user performs the final Darwin switch.
External-display behavior remains unverified while those displays are disconnected.
