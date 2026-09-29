# Version 8: integration and acceptance plan

Status, 2026-09-29: foundation, corrected capture, the GPU feasibility
dossier and AX/callback hardening are integrated in `dev`. The IPC candidate
is under separate review and temporarily included in the dependent release
preparation PR. No version 8 release or installation has been performed.

## Authorization and ownership

Project-bound session excerpt, user, 2026-09-29 (translated): prefer a pull
request for current changes; the coordinating agent supervises GPT-6 Sol
implementations; after the important corrections and new implementations,
release version 8 and install it on the system. Improve this organization
where useful.

The coordinator owns integration, review, comparative acceptance and release
decisions. Implementation workers use GPT-6 Sol High in isolated worktrees.
Desktop-changing tests have one owner at a time. Build and deterministic tests
can run independently; benchmark builds and live measurements are serialized
to avoid contention and contaminated comparisons.

## Integration sequence

The fork's verified default/integration branch is `dev`; `master` mirrors
upstream. The primary checkout has unrelated uncommitted documentation and
must not be reset or included accidentally.

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

[GPU dossier PR #4](https://github.com/LCS-Dev-Ergos/Yabai/pull/4) was
integrated at `c7e76a2` after complete CI. It preserves the feasibility data
and prototype; the production renderer still uses corrected rectangle capture.
[AX/callback hardening PR #3](https://github.com/LCS-Dev-Ergos/Yabai/pull/3)
was integrated at `7cbcf5a` after review and complete CI. Its bounded live
checks passed before the request to defer new live campaigns until reboot.

The buffer/GPU feasibility experiment remains experimental. Its full-resolution
headless requests succeeded, but the user reported Space navigation during the
ABBA campaign with the legacy installed daemon, followed by a daemon restart.
Those timings and WindowServer memory deltas cannot rank the backends. Keep the
corrected rectangle renderer for this release unless later controlled,
presented-frame evidence justifies replacing it. See
`docs/reports/effects-buffer-gpu-spike-2026-09-29.md` in `dev`.

The user's subsequent report of more than 15,000 WindowServer ports adds a
Mach-port lifecycle check to live acceptance. A high total alone does not
establish a leak or ownership by Yabai. Compare per-process counts across
controlled transitions, a client-alive hold and daemon exit. Review found a
separate missing send-right release in JankyBorders notification lookup, an
unchecked 512-entry notification capacity, and a stale event-tap pointer on
failed setup. The candidate in `0960819` corrects these with focused tests and
is awaiting separate integration; its dependency is merged into the release
preparation branch so combined CI can run. Do not attribute these defects to
the WindowServer total without evidence.

A cherry-pick copies a commit; fast-forward advances a branch without copying
commits. These are different operations. Use PR review and preserve the
existing ancestry instead of copying the whole stack. The exact merge method
must follow repository settings and retain clean dependency ordering.

## Parallel implementation ownership

| Worker | Branch / worktree | Scope |
| --- | --- | --- |
| GPT-6 Sol High, baseline and GPU | `lcs-code/crossfade-capture-retention`, `4953/Yabai`; then `lcs-code/effects-buffer-gpu`, `effects-gpu/Yabai` | Prepare foundation/capture PRs, then isolated capture/presentation prototype and comparison harness |
| GPT-6 Sol High, hardening | `lcs-code/effects-lifetime-hardening`, `effects-lifetime/Yabai` | Bound failed AX observation work/retries, then bound unresolved screenshot contexts, in separate commits |

Both implementation worktrees start at `2ff0671`. The GPU worker initially
owns only an isolated tool and its report; it must not change the production
snapshot lifecycle owned by the hardening worker. Any shared interface change
requires coordinator review. The old interrupted measurement worker remains
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
cost. GPU completion alone does not prove that the user saw a frame.

Accept production integration only if the candidate demonstrates a repeatable
useful gain without worse visual continuity, memory bounds, input latency or
idle work. If its API/hosting contract is unsuitable or it does not win, keep
the simpler corrected renderer and record the result. Resolution reduction
and blur remain separate optional experiments, not memory fixes or release
requirements.

## Acceptance before release

User update, 2026-09-29: complete the main implementation/release work now;
run new live measurements only after the user reboots with few active
processes. Preserve the completed bounded live evidence. Defer the new
WindowServer rights-type comparison and further visual/performance campaigns
to that fresh-system session. Offline regression checks, CI, signing, package
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

Evidence baseline: [extended capture dossier](../reports/capture-extended-results-2026-09-29.md).
Design alternatives: [effects engine proposals](effects-engine-proposals.md).
