# Capture replacement: extended decision measurements

Date: 2026-09-29. This extends the [initial decision data](capture-decision-data-2026-09-29.md) for focused fix `2131b94` on `9a56727`. The tested branch is `lcs-code/crossfade-capture-retention`; no renderer change, release, or permanent installation was made. The candidate used the local unsigned `lcs.14` payload. The opt-in diagnostic binary measured here is SHA-256 `7bbdd822f38c0cce1398341c097e152d6e98896d6122fcfa62a04e6fc653d227`; the ordinary Release binary remained byte-identical to the earlier baseline (`b4bc68c8319503020ac4ba3b1615178f460ead41c013815528c0c01d4b5aafef`).

## Decision from this campaign

Keep the focused capture replacement as the candidate for the next implementation phase. In one daemon lifetime, 108 of 110 long-run crossfade requests became ready; all 110 callbacks returned. WindowServer memory oscillated about 100 MiB above the stabilized baseline at checkpoints but did not climb cumulatively, and returned close to baseline with the client **still alive** through a 120-second hold. The former +300 MiB threshold was an annotation, not a stop; this long run stayed below it. This passes the bounded long-run memory criterion **for this workload**, not production or every display mode. Earlier +500/+674 MiB `top MEM` excursions were real observations, but later live-client holds showed transient recovery; they are not evidence of retained growth.

The present crossfade has a strong GPU cost signal: about 1.13 W system-wide sampled GPU power during 12-switch blocks versus 0.06–0.07 W for matched no-effect blocks, in either order. This does not predict the power or latency of a new buffer/GPU renderer. The previous phase trace found ~18 ms `CGContextDrawImage`, ~72 ms capture, a deliberate ~21 ms pre-switch wait, and variable Dock time. A narrow buffer/GPU feasibility prototype plus removal of measured redundant work/system calls is therefore justified; compare actual first visible change, focus, memory, and power with the current candidate. Do not treat renderer replacement alone as a latency cure.

## Workload and memory

The same candidate PID `77700` ran first-Space and portrait cases, 50 no-effect switches, and the long crossfade sequence. Primary display ID 3 was logical `3008×1692` / capture `6016×3384`. WindowServer PID was `453`. `top MEM` is a sampled process measure, not retained allocations; privileged `footprint --swapped --wired` gives a separate category view at different timestamps.

| Segment / checkpoint | Requests → ready | WindowServer `top MEM`, MiB | Daemon RSS, MiB |
| --- | ---: | ---: | ---: |
| No effect, before / 10 / 20 / 30 / 40 / 50 | 50 switches | 1939 / 1886 / 1983 / 1889 / 1888 / 1887 | 62.89 → 62.86 |
| Stabilized crossfade baseline | — | **1881** | 62.86 |
| Crossfade 10 / 20 / 30 / 40 / 50 / 60 | 60 → 59 | 1897 / 1992 / 1994 / 1898 / 1992 / 1994 | 63.05 → 63.23 |
| Crossfade 70 / 80 / 90 / 100 / 110 | 110 → **108** | 1994 / 1992 / 1994 / 1994 / 1994 | 63.33 → 63.34 |
| Client alive, hold 0 / 15 / 30 / 60 / 120 s | No new requests | 1888 / 1886 / 1886 / 1982 / **1891** | 63.23 → 63.25 |
| After candidate exit | — | 1888 | — |

The 50 no-effect switches took 34.1 wall seconds, daemon CPU 0.10 s, WindowServer CPU 14.24 s. The 110-effect segment took 125.7 wall seconds, daemon CPU 2.66 s, WindowServer CPU 55.13 s; that segment **includes a 30-second hold and privileged footprint collection after the first 60 attempts**, so its totals are not a per-switch controlled CPU comparison. WindowServer also serves other apps, and `top` checkpoints contribute work. CPU figures cannot be assigned wholly to Yabai. OS memory-pressure level remained 1, free memory 46–51%, swap stayed 458.31 MB in the long segment; there was no host-responsiveness stop.

The end-of-load marker preceded the first post-load `top` sample by 11.1 s because `footprint` and idle wait ran first. Subsequent `top` samples were ~26.1, 41.1, 71.1 and 131.2 s after that marker; “hold 0” is **not** immediate. Privileged WindowServer footprint total was 1896 MB at baseline, 1893 at 50-ready load, 1896 after its hold, 1896 at long-run end, 1994 during the later idle hold, 1893 during hold 60 and 1896 during hold 120. The hold 30 `top` (1886 MiB) and footprint (1994 MB) were ~15 s apart; do not compare them as simultaneous. Dirty graphics-owned memory shifted 642→740→646 MB across baseline/hold30/hold120, whereas dirty IOSurface stayed ~613 MB and region count returned to 175. The fluctuations persisted **without new captures** and recovered while the client lived. `footprint`'s 8216 MB historical `phys_footprint_peak` is not a session peak.

Signposts across that daemon lifetime recorded 116 capture requests (including 6 visual), 116 callbacks, 114 ready snapshots/first alpha writes/teardowns, and two `no capture` skips in the long segment. The skipped first requests after setup and after a checkpoint/hold had callbacks arrive roughly 168 and 200 ms after their request markers, beyond the 150 ms deadline. This temporal association does not prove that setup or AX caused them. The request has an arrival event and a deadline event; the trace analysis maps request token to generation and takes the first event, rather than equating counters. All callbacks returned here, so a perpetually missing callback remains a separate lifetime-hardening case, not a recurrence of the fixed capture-retention API.

## GPU and CPU comparison

The first GPU attempt is **invalid**: its privileged AppleScript command remained alive for the entire `powermetrics` run and generated repeated AX observation retries, stretching 12 no-effect switches to 87.8 s. The corrected attempt detached `powermetrics` with closed standard streams, waited for the AppleScript launcher to exit, allowed the observer to settle, then started matched empty-Space 3↔4 blocks. Samples overlap **every** block. Values below are system-wide GPU power from 1-second samples, trimmed by 1.1 s at each block edge; no process-level GPU attribution is possible.

| Block order | Switches / ready | GPU samples, trimmed mean mW | Wall s | Daemon CPU s | WindowServer CPU s |
| --- | ---: | ---: | ---: | ---: | ---: |
| none A | 12 / — | 6 / **72.8** | 7.58 | .02 | 3.17 |
| crossfade A | 12 / 11 | 5 / **1132.6** | 7.73 | .29 | 3.52 |
| crossfade B | 12 / 12 | 6 / **1132.2** | 7.77 | .30 | 3.60 |
| none B | 12 / — | 4 / **62.0** | 7.54 | .02 | 3.27 |

Empty-Space idle B/C were 69.8/35.3 mW (6 samples each). Idle A began on Space 1 with ChatGPT visible and measured 513 mW trimmed; it is **not** an equivalent empty-Space baseline. The A and B order reversal supports a workload signal of roughly +1.06–1.07 W global GPU power for this existing effect. Short blocks, sample edges, display content and other apps limit energy extrapolation. The 11/12 ready in crossfade A includes a 206.8 ms late-callback skip; a request is not automatically a rendered effect.

## Visual coverage and cold path

First-Space 3→1 and 1→3 were repeated twice: 4/4 bar checks passed (maximum bar change 0.03/255). The full-frame PNG recorder captured 21–26 frames per pass. Below-bar downsampled near-black fraction remained zero, there was no black excursion, and the captured mid-frames showed a visible blend with bar/icons present. The recorder yielded 1–3 intermediate luminance frames and no >5% backtrack for those passes. **PNG recording perturbs frame pacing and is not a reliable smoothness or frame-rate measurement**; these data establish presence of blending and absence of a black frame *among acquired frames*. Earlier non-PNG phase timings remain the latency evidence.

On portrait display ID 2 (logical `1692×3008`, physical `3384×6016`, origin `(3008,-660)`), an empty temporary Space was created, 11→12 and 12→11 were recorded, and 2/2 bar checks passed. Acquired mid-frames showed the desktop blend; below-bar near-black fraction never exceeded the endpoints (maximum 0.036). The temporary Space was removed and original Space IDs/windows verified unchanged. Portrait endpoint luminance differed by only ~2/255, so this recording cannot support a strong monotonicity or pacing claim.

The earlier first-visit VS Code `SPACE_CHANGED` of 404–416 ms was not reproduced; in the corrected GPU session 51 handlers had median 0.87 ms, maximum 13.92 ms. The new opt-in trace did isolate a different confound: the first sampler's live `osascript` PID `74962` was independently identified by `ps`, while Yabai's process-name marker labeled it “ChatGPT.” Its AX notification registration produced 147 `kAXErrorCannotComplete` calls, ~145.9 s summed across retries, up to 1.11 s each. In the corrected session the launcher/termination helper PIDs had 14/13 failed additions totaling 6.10/6.30 s, outside measured GPU blocks. This explains the multi-second diagnostic-helper launch events; it does **not** establish that a normal user-app cold launch or the older VS Code stall has the same cause. A clean, representative cold-app trace remains open if that latency becomes the next target.

Current SDR/local capture was used. Moving-pointer behavior, HDR mode, live display reconfiguration and application-heavy navigation were not accepted by this campaign; no HDR or resolution setting was changed. These are compatibility checks for a later candidate, not a reason to repeat the completed SDR memory/GPU campaign.

## Provenance and restoration

Raw ignored fixtures: `build/live-validation/extended-session/` (invalid GPU helper), `extended-continuation/` (corrected GPU), and `extended-final/` (visual and long memory). Each contains `run.jsonl` and `signposts.log`; the corrected GPU run also has `gpu-power.txt`, and the final run has privileged `footprint-*.txt` and visual PNGs. The compact [machine-readable summary](data/capture-extended-summary-2026-09-29.json) preserves the decision values without committing user-screen recordings. Reproduce local calculations with `python3 build/live-validation/analyze-extended.py build/live-validation/extended-continuation` and the visual analyzer using the bundled workspace Python with NumPy; these scripts are ignored local fixtures, not release tooling.

The opt-in diagnostic build and ordinary Release build succeeded. Diagnostic `ctest` passed 11/11. The ordinary binary's SHA-256 matched the previously tested baseline; ordinary CTest was not rerun in this extension. At exit the single signed installed daemon was PID `79476`; `/opt/yabai/bin/yabai` SHA-256 `8968238444932a59f078c91eb6f8cb2a6d7c843f21a6ea0e9e59a829466f5283`, and its **legacy socket** handshake returned `2.1.31-lcs.13 / 0x5D`. Original Space 1 and focused window 2222 were restored; no diagnostic daemon or power sampler remained.
