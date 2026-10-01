# Version 8: integration and acceptance plan

Status, 2026-09-30: all scoped corrections are integrated in `dev`. Signed
`v8.0.0-lcs.1` was published at `93feb9b` on 2026-09-29; the Nix package was
built from Dotfiles `4ceea80`, and version 8 was subsequently observed active
on the exact pinned generation. Two further changes merged into `dev` on
2026-09-30, [PR #7](https://github.com/LCS-Dev-Ergos/Yabai/pull/7) (early
snapshot skip) and [PR #8](https://github.com/LCS-Dev-Ergos/Yabai/pull/8)
(fast policy for absolute numeric requests), and tag `v8.0.0-lcs.2` marks
`f33d1ac`. System activation is performed manually by the maintainer;
automated tooling must not run the switch. New live measurements remain
deferred until a reboot with few active processes. Exact runtime identity
evidence and unverified coverage are kept in a local, unpublished release
record.

## Process and ownership

Current changes are published as pull requests. Implementation proceeds in
isolated worktrees, each on its own branch, and is integrated after review.
Release and installation of version 8 follow the important corrections and new
implementations. System activation is a manual maintainer step; this
constraint supersedes any earlier, broader permission to install.

Integration, review, comparative acceptance and release decisions are made by
the maintainer. Desktop-changing tests have one owner at a time. Build and
deterministic tests can run independently; benchmark builds and live
measurements are serialized to avoid contention and contaminated comparisons.

## Integration sequence

The verified default and integration branch is `dev`; `master` mirrors
upstream.

1. Publish the six existing refactor commits ending at `9a56727` in a
   foundation PR to `dev` (remote base `dc26aa8`). Preserve their history.
2. Publish the capture correction and measurement dossier ending at `2ff0671`
   in a dependent draft PR to `dev`. Until the foundation merges its diff
   includes those commits. This base also runs the existing PR CI. Recheck
   the reduced diff and CI after integration of the dependency.
3. Review and integrate AX observation and capture-lifetime hardening as
   focused changes on the accepted baseline.
4. Compare the isolated buffer/GPU prototype before deciding whether its
   production integration belongs in version 8.
5. Validate the combined candidate, prepare release notes and a signed
   `v8.0.0-lcs.1` release, verify its artifact, update the Nix package and
   activate the system. Verify the actual daemon, matching payload and
   end-to-end behavior after activation; retain the previous generation for
   rollback.

Integrated after review and complete CI:
[foundation PR #1](https://github.com/LCS-Dev-Ergos/Yabai/pull/1) at `aa142f8`
and [capture PR #2](https://github.com/LCS-Dev-Ergos/Yabai/pull/2) at `5f6d991`.
The foundation includes `6349ebf`, explicit Core Foundation and Objective-C
ownership contracts; analyzer baselines and gate behavior were preserved.
Build, sanitizers, fuzzing and analysis passed on both PR heads. The capture
merge tree equals its tested head `6690ded`. These integrations did not change
the installed daemon or publish a release.

Subsequent integration: [GPU feasibility dossier PR #4](https://github.com/LCS-Dev-Ergos/Yabai/pull/4)
merged at `c7e76a2`, then [AX/callback hardening PR #3](https://github.com/LCS-Dev-Ergos/Yabai/pull/3)
at `7cbcf5a`, each after all four CI checks passed. The dossier preserves the
feasibility data and prototype; the production renderer still uses corrected
rectangle capture. The hardening candidate completed 12
requests/callbacks/ready overlays/teardowns and two recorded bar/icon checks;
its helper responsiveness and application-observation recovery are documented
in [effects lifetime hardening](../reports/Effects-Lifetime-Hardening_8.0.0-lcs1.md).
The signed lcs.32 baseline was restored after that campaign. Final code-only
[IPC PR #6](https://github.com/LCS-Dev-Ergos/Yabai/pull/6) then merged at
`267df9e`, followed by [release preparation PR #5](https://github.com/LCS-Dev-Ergos/Yabai/pull/5)
at `93feb9b`. Both complete PR check sets passed; the final merge tree equals
the tested combined candidate. Release and final runtime identity are recorded
in the local release record mentioned above.

After the release, PR #7 merged at `e7e8915` and PR #8 at `f33d1ac`, each
after all four CI jobs passed. PR #7 skips a pending snapshot presentation
when fast input is accepted; PR #8 applies the same fast policy to absolute
numeric requests. Their live evidence is kept in a local, unpublished
validation record, because it contains host-specific identity data.

The buffer/GPU feasibility experiment remains experimental. Its full-resolution
headless requests succeeded, but Space navigation took place during the ABBA
campaign with the legacy installed daemon, followed by a daemon restart.
Those timings and WindowServer memory deltas cannot rank the backends. Keep the
corrected rectangle renderer for this release unless later controlled,
presented-frame evidence justifies replacing it. See
[the buffer/GPU spike report](../reports/Effects-Buffer-GPU-Spike_8.0.0-lcs1.md).

A later report of more than 15,000 WindowServer ports adds a Mach-port
lifecycle check to live acceptance. A high total alone does not establish a
leak or ownership by Yabai. Compare per-process counts across controlled
transitions, a client-alive hold and daemon exit. Review found a separate
missing send-right release in JankyBorders notification lookup, an unchecked
512-entry notification capacity, and a stale event-tap pointer on failed
setup. Their narrow corrections and regression tests were integrated through
code-only PR #6. Do not attribute these defects to the WindowServer total
without evidence. The machine diagnostic report stays local and is excluded
from public commit history.

A cherry-pick copies a commit; fast-forward advances a branch without copying
commits. These are different operations. Use PR review and preserve the
existing ancestry instead of copying the whole stack. The exact merge method
must follow repository settings and retain clean dependency ordering.

## Parallel implementation ownership

| Work stream | Branch | Scope |
| --- | --- | --- |
| Baseline and GPU | `lcs-code/crossfade-capture-retention`; then `lcs-code/effects-buffer-gpu` | Prepare foundation/capture PRs, then isolated capture/presentation prototype and comparison harness |
| Hardening | `lcs-code/effects-lifetime-hardening` | Bound failed AX observation work/retries, then bound unresolved screenshot contexts, in separate commits |

Both implementation branches start at `2ff0671`. The GPU work initially owns
only an isolated tool and its report; it must not change the production
snapshot lifecycle owned by the hardening work. Any shared interface change
requires maintainer review. The earlier interrupted measurement work remains
inactive. No new continuous capture service is assumed.

## Required reliability work

- AX: stop repeating blocking notification registrations after an
  unresponsive application fails; use a bounded retry policy with recovery.
  Preserve normal applications, partial-registration cleanup and process
  lifecycle. Do not blacklist application names based on the helper incident.
- Capture: bound outstanding callbacks even if ScreenCaptureKit never calls
  back. A timeout does not cancel framework ownership. Late results must be
  safe, ignored when superseded, and allow recovery where possible. Navigation
  continues without the optional effect when capture admission is closed.
- Keep the tested `captureScreenshotWithRect` memory fix. Do not restore the
  legacy rectangle API as an unverified fallback.

## Renderer decision

Prefer the smallest candidate that improves the complete transition. First
verify whether on-demand sample-buffer capture can preserve the current
rectangle-capture contract. GPU upload of a CGImage is a distinct fallback
experiment; neither is assumed to be zero-copy.

Compare against the existing path with the same physical dimensions, display,
SDR output, cursor policy, Space pair, duration and load, in both run orders.
Separate acquisition, preparation, Dock/focus, first presented change and
frame pacing. Track memory with the client alive, global GPU power and idle
cost. GPU completion alone does not prove that a frame was seen.

Accept production integration only if the candidate demonstrates a repeatable
useful gain without worse visual continuity, memory bounds, input latency or
idle work. If its API/hosting contract is unsuitable or it does not win, keep
the simpler corrected renderer and record the result. Resolution reduction
and blur remain separate optional experiments, not memory fixes or release
requirements.

## Acceptance before release

Scope decision, 2026-09-29: complete the main implementation and release work
first; run new live measurements only after a reboot with few active
processes. Preserve the completed bounded live evidence. The new WindowServer
rights-type comparison and further visual/performance campaigns wait for that
fresh-system session. Offline regression checks, CI, signing, package
preparation and minimal installed-identity verification may proceed; pending
live coverage must remain explicit in the release handoff.

- Focused tests for AX success, failures, partial cleanup, retries and process
  destruction; callback loss, lateness, cancellation and recovery. Relevant
  sanitizers and combined CI must pass on the final candidate.
- Live navigation correctness with isolated moves and rapid bursts,
  application launches and focus, including two displays when available.
- Preserve Desktop icons and SketchyBar (including clicks); reject black or
  white flashes on first-Space and reverse transitions. PNG captures alone
  cannot establish smooth frame pacing.
- Bounded repeated effects and a client-alive idle hold on the combined
  candidate. Reuse the completed baseline dossier rather than repeating it
  without a changed hypothesis. Check pending-resource bounds separately.
- Exercise moving cursor and display reconfiguration. State tested color and
  OS coverage explicitly; HDR support must not be claimed from SDR trials.
- Verify signed daemon/payload socket authentication and rejection behavior
  under the production build. Unsigned local-test policy must stay disabled
  in release artifacts.
- Verify release archive hash, signing identity, version, Nix evaluation/build
  and actual installed binary/payload. Installation success alone is not live
  behavioral acceptance.

Evidence baseline: [extended capture dossier](../reports/Capture-Extended-Results_8.0.0-lcs1.md).
Design alternatives: [effects engine proposals](EFFECTS-ENGINE-PROPOSALS.md).
