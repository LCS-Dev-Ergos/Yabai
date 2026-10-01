# Effects lifetime hardening: offline and bounded live result

Date: 2026-09-29. Base: `2ff0671`, which already contains the focused capture
replacement `2131b94`. This work changes AX application observation and the
admission of snapshot captures. It does not change the renderer, navigation
commands, installed daemon, or signed payload.

## AX observation

The extended campaign traced one unresponsive diagnostic helper through 147
`kAXErrorCannotComplete` notification registrations, with 145.9 seconds summed
inside those calls. The old path attempted all seven registrations even when
the first failed, then retried the launch every 100 ms while the process lived.
This is an observed helper-launch failure path, not proof that a normal cold
application launch has the same cost.

Registration now requests a 250 ms messaging timeout on that application's AX
element, stops an attempt at the first `CannotComplete`, and preserves the
short timeout while removing partial registrations. It restores the default
timeout after successful registration or cleanup. If setting the timeout fails,
observation is skipped; a `CannotComplete` result remains eligible for retry.
Retry delays for a living process rise from 100 ms to 6.4 seconds and stay at
6.4 seconds while failures continue. The scheduled callback looks up the PSN
again, checks its PID and termination flag, and posts one launch retry.
Successful observation resets the retry count. Permanent notification refusal
does not trigger this AX retry path.

This bounds the *number* of notification calls on a `CannotComplete` attempt
and limits their intended per-element timeout; it is not a 250 ms bound on the
whole launch handler. Earlier successful additions can require up to six AX
removals, each subject to the same timeout, and `AXObserverCreate` is a
separate call. AX may also fail to honor a requested timeout. The bounded live
comparison below measures helper-query responsiveness, not a general or
AX-isolated application-launch speedup.

## Missing capture callbacks

The previous two-second stale escape could keep admitting replacements when
callbacks never returned. Snapshot admission now counts every request still
awaiting its callback, including stale generations. It permits at most two
such requests. A third request omits the optional effect and leaves logical
navigation to its existing no-effect path. A slot returns only when the
callback arrives or synchronous setup fails; reaching the 150 ms presentation
deadline does not cancel the framework request or free its callback context.

The latest generation still controls the pending marker. If its callback
returns first, admission can resume while an older callback remains pending;
the older callback cannot clear a newer pending marker. If both callbacks
never return, effects stay suspended and at most two callback contexts remain
owned by this code for the daemon lifetime. The count is not a bound on all
framework resources, overlay images, or other renderer objects. The callback
returns its admission slot before finishing its remaining cleanup.

## Verification and limits

The snapshot mock first reproduced the old defect: after two missing callbacks
and stale intervals, a third framework capture was issued. With the change,
the mock verifies the two-request bound, no-effect fallback, callbacks returned
in reverse generation order, concurrent completions, and recovery. AX mocks
verify call counts, partial and full cleanup, timeout state during removals,
permanent refusal, delayed retry delivery, exit, PID mismatch, and recovery.

The complete local Debug build and CTest suite passed (11/11). Focused AX and
snapshot tests passed under ASan/UBSan; the concurrent snapshot test passed
under TSan/UBSan. Those checks exercised the missing-callback bound; the live
campaign below did not force missing ScreenCaptureKit callbacks.

## Bounded live validation

An unsigned diagnostic Release build of the PR head `a77ccae` (SHA-256
`7436c182e2231c84a32751e67e6da4e41211490130d72a70a35a6d725bb762b8`)
ran temporarily with its local `2.1.31-lcs.14 / 0x5d` payload. The signed
installed `/opt/yabai/bin/yabai` stayed byte-identical at SHA-256
`8968238444932a59f078c91eb6f8cb2a6d7c843f21a6ea0e9e59a829466f5283`.
The input guard waited for five idle seconds and stopped work on subsequent
keyboard or pointer input. A first attempt stopped at a test-fixture error:
it queried the signed payload's legacy socket rather than the candidate's
private `~/Library/Caches/yabai/payload.sock`. The candidate was stopped and
the signed service restored; no controlled AX helper or effect workload ran in
that attempt. The corrected run verified the candidate payload handshake before
starting work. These were two separate daemon lifetimes.

The same eight-second non-root `osascript` helper was launched under the
signed daemon and under the candidate. Signed-daemon polling returned three
timeouts among seven Space queries, each at the two-second query deadline;
its other slow queries took 699 and 769 ms. Candidate polling returned 61/61
queries without timeout, maximum 267.77 ms. Candidate signposts for the
helper PID `10718` recorded seven failed notification additions (`-25204`),
one notification-0 call in each attempt, totaling 1.352 seconds across the
eight-second helper lifetime. The six later calls took 88.7–255.0 ms; five
clustered around 250–255 ms. Retry starts spread over increasing intervals.
The trace's process-name label incorrectly says “ChatGPT” for this helper;
the PID comes from the test runner. The signed daemon had no matching AX
signposts, so the query comparison does not isolate AX as the sole latency
cause. One AX call can still slightly exceed the requested timeout, and
`AXObserverCreate` is not covered by that element timeout.

Calculator was opened as a fresh normal app at PID `10840`. Its first two AX
attempts failed on notification 0 after about 255 ms each; the third attempt
registered all seven notifications and completed launch observation on that
same PID. The app was closed by the runner. This is live evidence that the
backoff recovers when a process becomes responsive. The runner did not sample
the app's frontmost AX focus while it was open, so normal-app focus acceptance
is still open.

On empty Spaces 4↔5, 12 nominal crossfades completed with the expected final
Space. Signposts recorded exactly 12 capture requests, 12 callbacks, 12 ready
snapshots and 12 teardowns, with no `no capture` marker. Two recorded
transitions passed the static bar/icon check: bar excursion at most 0.01/255,
bright-icon loss about 0.011/255, and 20–21 acquired frames each. PNG capture
perturbs pacing, so this is bounded presence/continuity evidence rather than
a frame-rate or subjective smoothness verdict. No callback was intentionally
left unresolved; the two-context lifetime cap remains mock-verified only.

The same `top -l 1 -stats pid,command,ports,mem` sampler measured Mach ports
and sampled memory for WindowServer (WS), Dock and the daemon. The local
`window_animation_duration` was `0.2`; it is separate from the 250 ms Space
crossfade duration. The table's checkpoints are not simultaneous across
processes, and `top MEM` is not a retained-allocation measure.

| Checkpoint | WS `#PORTS` / `MEM` | Daemon `#PORTS` / `MEM` |
| --- | ---: | ---: |
| Signed service, before stop | 15686 / 1979 MiB | 197 / 12 MiB |
| Signed service stopped | 15735 / 1900 MiB | — |
| Candidate before helper/app | 15765 / 2053 MiB | 226 / 11 MiB |
| Candidate before 12 effects | 15753 / 2183 MiB | 204 / 11 MiB |
| After 12 effects | 15752 / 2156 MiB | 214 / 12 MiB |
| Client alive, no new effects, 30 s | 15761 / 2050 MiB | 209 / 12 MiB |
| Client alive, no new effects, 60 s | 15769 / 2050 MiB | 209 / 12 MiB |
| Candidate stopped | 15749 / 2148 MiB | — |
| Signed service restored | 15829 / 2216 MiB | 201 / 50 MiB |

Privileged WS `footprint` was 1986 MB before stopping the signed service and
1999 MB after it stopped, sampled at different times from `top`. The WS Mach
port count did not drop when that daemon stopped. The effect segment changed
WS ports 15753→15752, then the client-alive hold reached 15769; these short,
noisy samples do not show a per-effect port slope or explain the already-high
15k absolute count. Dock and daemon restarts, interactive use and Desktop activity before
the campaign, and unrelated clients remain confounders. This does not prove
that Yabai cannot retain WS ports. No matched no-effect navigation block was
run in this lifetime, and WindowServer was never restarted.

At exit the signed service was independently rechecked: one daemon PID
`11457`, the installed SHA above, legacy payload handshake
`2.1.31-lcs.13 / 0x5d`, active Space 1, and original focused ChatGPT window
`2222`. The LaunchAgent was running. The ignored raw evidence is in
`build/live-hardening/session-1790692981050921000/` (`run.jsonl`, AX/capture
`signposts.log`, visual result JSON/frames and WS footprint files); the
aborted socket-fixture attempt is preserved separately at
`build/live-hardening/session-1790692809617318000/`. The runner and screen
recordings are local fixtures, not part of the PR. Representative cold-app
latency, long-duration port behavior, intentionally missing live callbacks,
and signed-candidate acceptance remain open. Further live port attribution
and other new measurements are deferred until a fresh reboot with few active
processes.
