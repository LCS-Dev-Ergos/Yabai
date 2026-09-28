# Desktop crossfade: lcs.22 candidate

2026-09-28. Based on `dev` at `325a2dd`, with signed lcs.21 and payload
`2.1.31-lcs.11` still active during the investigation. The changes below use
payload `2.1.31-lcs.12`. Local checks pass; live acceptance of this candidate
has not been performed.

## User direction

In this project's continuation on 2026-09-28, the user explicitly chose
“Continuare col crossfade dell’intero Desktop” after seeing a comparison with
window-only fades. Keep the whole-Desktop effect and address its interference
with SketchyBar, Desktop content and rapid navigation. Do not substitute a
window-only fade as the everyday default.

## Reproduced on the installed version

The landscape display is ID 3, 3008 by 1692 logical points. Desktops 9 and 10
are empty; 6 has ChatGPT and 7/8 have Edge. The second display's Desktop 11
was empty in this session, so this workload does not reproduce the handoff's
Edge-on-two-displays configuration.

ScreenCaptureKit recorded presented frames, including the top 40 logical
points containing SketchyBar. The bar's luminance normally remained near
33.5/255. In repeated 9-to-10 crossfades it fell about 10 levels outside the
range of the starting and ending frames. Visual inspection of the captured
frames confirmed that the bar was almost invisible near the end of the
transition. The no-effect and window-fade controls preserved it.

| Installed lcs.21 workload | Bar luminance excursion | Result |
| --- | ---: | --- |
| 9 to 10, crossfade 250 ms, initial capture | 10.18 | FAIL |
| 9 to 10, two crossfade repeats | 9.93 / 9.58 | FAIL |
| 8 to 9, crossfade 250 ms | 11.13 | FAIL |
| 6 to 7, crossfade 250 ms | 0.01 | PASS |
| 9 to 10, tracked diagnostic, PNGs disabled | 10.63 | FAIL |
| 9 to 10, same diagnostic without effect | 0.02 | PASS |

The last pair is reproducible with the command in [testing](../testing.md#presented-bar-frames).
Its data is in ignored `build/lcs21-bar-regression/`. Earlier image captures
remain in ignored `build/visual-*`; enabling PNG output adds work and makes
those runs unsuitable for presentation-timing comparisons. An excursion over
3 is a local regression threshold, not a general measure of animation quality.

SketchyBar's visible windows have levels 2 and 3 and belong to no managed
Desktop. Finder's Desktop window belongs to all ten Desktops on the landscape
display. Those are different ownership cases. The destination wallpaper's
raised Space level is the leading explanation for the bar disappearing when
that wallpaper is visible; it does not establish why shared Finder content
disappears. The 6-to-7 control, where the covered destination hides its
wallpaper, preserved the bar.

## Candidate changes

- Keep the destination Desktop at ordinary level 0 and lower previous layers
  to -1/-2, then restore all levels at completion. This preserves the relative
  order of the crossfade without lifting a destination wallpaper above global
  windows. SkyLight's live behavior with this ordering remains unverified.
- Reserve a settlement transaction before modifying any Desktop. Completion
  and cancellation no longer depend on allocating another transaction after
  transparency has changed. Failed intermediate allocations skip a frame;
  a failed reservation prevents the effect from starting.
- Compute the union of covered window rectangles. Previously, two overlapping
  windows covering the same half of a display could qualify as full coverage
  and wrongly hide its wallpaper. The scan is bounded to 128 rectangles and
  underestimates excess coverage. Only the system WindowManager's windows
  qualify for wallpaper alpha changes; unreadable owners are left untouched.
- Preserve the requested crossfade duration throughout a burst. The next
  paced step waits for that duration after Dock's acknowledgement and for at
  least 100 ms after the preceding synchronous step completes. This prevents
  overlapping blends driven by early input and immediate catch-up after a
  slow system call. At 250 ms, the nominal maximum is about four steps per
  second; the existing bounded queue and held-key coalescing remain.
- Schedule navigation after a focus-confirmation event returns. The old path
  could start a new Desktop switch in the middle of processing the old
  window's focus, before that handler completed its remaining work.

## Verification

Regressions failed before their fixes: settlement after transaction-allocation
failure; double-counted wallpaper coverage; immediate navigation after a slow
step; navigation inside the focus handler; and shortened/overlapping burst
crossfades. The tests execute production code against simulated SkyLight or a
simulated clock. The transaction fixture retains independent command buffers,
including the reserved settlement transaction.

- Universal Debug build: daemon x86_64/arm64 and payload x86_64/arm64e passed.
- Debug CTest: 9/9 passed.
- ASan/UBSan CTest: 9/9 passed.
- TSan: fade, navigation schedule and navigation queue, 3/3 passed.
- Static analysis: payload and loader on both architectures, plus daemon
  baseline, passed without adding baseline allowances.
- Presented-frame diagnostic compiled and reproduced the installed bar defect
  with a passing no-effect control. Each run restored the starting Desktop
  and focused window; the tracked tool verifies restoration by querying them.

The CTest socket/signal cases need ordinary temporary-file and local-socket
access. Their first sandboxed run failed for those restrictions; the complete
runs above passed outside that sandbox. The injected payload itself is not
sanitizer-instrumented; the standalone fade tests are.

## Remaining acceptance

The candidate has not replaced the installed daemon or payload. Before calling
the visual problem fixed, repeat the captured crossfade/no-effect comparison
on the signed candidate and inspect Finder icons as well as SketchyBar. Check
empty-to-empty, occupied-to-empty, empty-to-occupied, rapid reversals, separate
presses and held keys; verify final Desktop and keyboard focus after each.

The 80% wallpaper threshold remains a heuristic. Window transparency, the
uncovered margin, shared Finder windows and wallpaper writes outside Space
transactions are still fragile boundaries. Transaction commits provide no
usable success status, and a timing guard is not a presentation fence. The
reserved transaction fixes allocation failure, not an unobservable failed
WindowServer commit or a lost Dock connection.

No controlled CPU/GPU comparison was performed, and no improvement is claimed.
Synchronous AX work, missing presented frames and Edge's late cross-display
activation remain open. No macOS defaults were changed. Native transition
entry points returned success in an exploratory check but did not establish
intermediate presented frames, so they were not adopted.

Activation follows the existing handoff boundary: ask before replacing the
installed yabai, reloading the scripting addition or restarting Dock. The user
runs `darwin-rebuild switch`; changing the payload version restarts Dock during
that activation. Release/package preparation is separate from live acceptance.
