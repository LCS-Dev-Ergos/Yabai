# Capture candidate: decision-oriented live validation

Branch: `lcs-code/crossfade-capture-retention`, candidate `2131b94`, base
`9a56727`. The unsigned local build is tested here. Keep the renderer fixed
while assessing this capture replacement.

## Ordered gates

1. Release build with `YABAI_ALLOW_UNSIGNED_LOCAL=ON`; focused snapshot,
   scheduling and queue tests. This local option must not enter a release.
2. Temporarily run the matching local daemon/payload. Record binary hash,
   configuration, topology and PIDs. Restore the signed service and payload
   even if a gate fails. Payload replacement restarts Dock, not WindowServer.
3. On empty Spaces 3/4, warm once, then six effect-free and twelve isolated
   crossfade switches. Record destination, client return time and WindowServer
   footprint at every step. Hold the client alive after the workload. Stop
   on keyboard or pointer input, daemon failure, incorrect destination or growth over 300 MiB.
4. Require actual `snapshot ready` evidence: stable memory with effects skipped
   is not acceptance. Retain phase signposts. `prepare` is cumulative from the
   request start; subtract capture time before calling it drawing/preparation.
5. After the memory gate, record presented frames separately: empty-to-empty
   icons/bar; distinct window/empty content in both directions. PNG recording
   changes timing, so use separate non-PNG blend trials for timing analysis.
6. Only after those gates pass, measure bursts and occupied/cross-display cases.
   Preserve final focus. Do not add display reconfiguration, arbitrary new
   applications or renderer changes to the initial run.

## Decision rules

- Cumulative growth: reject the backend candidate and isolate configuration
  differences from the successful standalone probe.
- Stable memory but no ready snapshots: investigate API configuration, geometry,
  permission and deadline; do not call the memory issue fixed in production.
- Ready snapshots with visual regressions: fix that contract before optimizing.
- Stable memory and coherent visuals: retain this focused fix. Then choose
  superseded-work cancellation if wasted captures dominate, cold AX/query
  reduction if event-loop stalls dominate, or an isolated buffer/GPU experiment
  if full-frame preparation remains a major cost.
- Client return, state queries, alpha values and actual presentation are
  different measurements. Idle CPU does not measure WindowServer/GPU cost.

Raw local evidence: `build/live-validation/`. No release or installation for
ordinary daily use is implied by a passing bounded local test.
