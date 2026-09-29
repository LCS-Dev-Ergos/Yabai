# Effects lifetime hardening: offline result

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
separate call. AX may also fail to honor a requested timeout. No live latency
gain has been measured for this candidate.

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
under TSan/UBSan. These are mock and local checks. Representative cold-app
latency, live missing-callback behavior, visual continuity, focus, and signed
daemon acceptance remain untested. No live daemon or Dock swap occurred.
