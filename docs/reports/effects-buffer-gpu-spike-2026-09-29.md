# On-demand buffer/Metal feasibility spike

Date: 2026-09-29. Branch: `lcs-code/effects-buffer-gpu`, based on capture
correction `2ff0671`. This is an isolated tool experiment, not a production
renderer, version-8 acceptance, or a change to the installed daemon.

## Decision from the current evidence

Keep the buffer route experimental. A one-shot filtered `CMSampleBuffer` at the
landscape display's full `6016×3384` capture resolution supplied a BGRA,
IOSurface-backed pixel buffer that `CVMetalTextureCache` could bind and Metal
could blit to a private target. All 24 filtered and 24 rectangle requests in
four counterbalanced headless runs succeeded. The color spaces exposed the
same 6812-byte ICC profile (probe hash `99bc000c39985d17`), but this does
**not** prove equal pixels or presentation.

The filtered path's warm one-shot acquisition was shorter in these runs; its
first request was substantially slower. This compares different
ScreenCaptureKit contracts and offscreen preparation only. The user reported
moving between Spaces during this campaign while the installed legacy daemon
was active, then restarting Yabai to restore its baseline. The campaign did
not continuously track those moves or the daemon PID. WindowServer memory and
timing comparisons are therefore **confounded** and cannot attribute a rise
or a speedup to the filtered API. There is no basis yet to replace the
corrected rectangle renderer in version 8. The existing capture fix and
bounded callback/AX work remain the release priority.

## Contract and workload

`captureScreenshotWithRect:configuration:` returns `SCScreenshotOutput.sdrImage`
as a `CGImage` for a global rectangle. `captureSampleBufferWithFilter:configuration:`
returns a `CMSampleBuffer` under an `SCContentFilter` for a display. Equal
`contentRect`, dimensions, and ICC data do not establish that the methods
select the same desktop, menu-bar, window, cursor, alpha, or color-conversion
pixels. The filtered probe uses a display filter excluding no windows, menu
bar enabled, SDR BGRA, cursor enabled, one-shot calls and no continuous stream.
The rectangle probe matches the current local SDR screenshot settings. There
is no known filtered-buffer equivalent of the rectangle API's `displayIntent`
setting in this experiment. See Apple's [rectangle output](https://developer.apple.com/documentation/screencapturekit/scscreenshotmanager/capturescreenshot%28rect%3Aconfiguration%3Acompletionhandler%3A%29?language=objc),
[filtered sample buffer](https://developer.apple.com/documentation/screencapturekit/scscreenshotmanager/capturesamplebuffer%28contentfilter%3Aconfiguration%3Acompletionhandler%3A%29?language=objc),
and [content filter](https://developer.apple.com/documentation/screencapturekit/sccontentfilter?language=objc) contracts.

The measured display was ID 3, global/filter rectangle `(0,0,3008,1692)`
points, scale 2, output `6016×3384` pixels. The tool uses the active
`CGDisplayModeGetPixelWidth/Height`, as the production snapshot does. An
earlier `CGDisplayPixelsWide` pilot produced only `3008×1692` and its timings
are excluded. The full-resolution functional pilot overlapped local analyzer
work and is also excluded from the ranking. The final ABBA campaign used the
same diagnostic binary SHA-256
`7bdc894932701a6f62bd4996141abc24f58a2a52734a1e59a589e676b8155b56`.
Each process made 12 requests, spaced by 300 ms after preparation, then held
alive for 60 s. The order was rectangle, filtered, filtered, rectangle.

## Acquisition and preparation

Medians exclude the first request of each process. `complete` includes Metal
encoding, queueing, execution and completion callback; `GPU execution` uses
Metal's GPU start/end timestamps. The rectangle's `draw` is a CPU bitmap
context using the source `CGImage` color space, not the production
`SLWindowContext`/`SLSSetWindowColorSpace` path. The filtered probe blits to a
private offscreen texture; a real renderer could instead sample a
`CVMetalTexture` into a drawable. Neither path measures first visible change.
The bitmap `draw` timer starts after bitmap-context creation, whereas Metal
`complete` starts before texture wrapping and target allocation. Thus even
the preparation columns are instrumented at different boundaries. The final
proxy adds acquisition to `draw` for rectangle and to `complete` for filtered;
Metal's `prepare`/encode interval is already inside `complete` and is not
added twice.

| Process | First acquire ms | Warm acquire median ms | Bitmap draw / Metal complete median ms | Metal GPU execution median ms | Warm acquire + preparation proxy median ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rectangle A | 98.16 | 98.11 | 13.91 draw | — | 112.56 |
| Filtered A | 240.79 | 69.29 | 8.37 complete | 1.01 | 76.91 |
| Filtered B | 166.98 | 75.70 | 7.78 complete | 1.50 | 81.77 |
| Rectangle B | 77.11 | 83.74 | 13.46 draw | — | 96.79 |

The small sample, different API semantics and user Space navigation prevent
a controlled speedup claim. The original effect's measured phases remain
approximately 72 ms rectangle capture, 18 ms `CGContextDrawImage`, 21 ms
pre-switch wait and
19 ms Dock round trip in their own live workload. The new offscreen numbers
cannot be added to or subtracted from those end-to-end phases as if the
workloads and surfaces were identical. The original effect also showed about
1.13 W *system-wide* GPU power versus 0.06–0.07 W matched no-effect blocks;
no power or energy result exists for this prototype.

## Client-alive memory

The probe samples its own `proc_pid_rusage` physical footprint and
WindowServer's `top MEM` automatically with monotonic hold timestamps. A
separate `top` read before launch and after process exit is in the ignored
runner manifest. Cross-process `proc_pid_rusage` for WindowServer returned
`EPERM` (errno 1), recorded as `-1`, so the WindowServer column below is
**top MEM**, not physical footprint or retained allocation. Each top sample
takes roughly 0.45–0.78 s; `hold_elapsed_ms` marks its start. The client
PID stayed alive for all hold samples.

| Process | Client footprint before / after load / hold 60 MiB | WindowServer top MEM before load / after load / hold 0 / 15 / 30 / 60 / post-exit MiB |
| --- | --- | --- |
| Rectangle A | 5.41 / 5.78 / 5.78 | 2899 / 2906 / 3001 / 2817 / 2817 / 2816 / 2821 |
| Filtered A | 5.36 / 7.42 / 7.06 | 2921 / 3234 / 3141 / 3379 / 4162 / 4061 / 3926 |
| Filtered B | 5.41 / 7.44 / 7.08 | 3830 / 4245 / 4245 / 4060 / 4059 / 4059 / 3830 |
| Rectangle B | 5.36 / 5.77 / 5.77 | 3931 / 3838 / 3838 / 3835 / 3926 / 3828 / 3828 |

The filtered client's per-capture samples reached about 86 MiB while each
iteration's autorelease pool was live, then fell to about 7 MiB after the
load and stayed there through hold 60. Rectangle client samples stayed near
6 MiB. WindowServer's roughly 0.9–1.0 GiB higher post-exit level after
Filtered A persisted during the later runs, although Filtered B did not add
another comparable step and the later readings fluctuated. **This is not
evidence of buffer retention:** the user reports moving between Spaces during
the campaign with the installed legacy daemon, which had a previously
documented capture-memory defect. The runner did not continuously record
Space transitions or daemon identity; the user restarted Yabai afterward to
restore its baseline. The runner's first prelaunch top reading was 3142 MiB,
but the probe's pre-load reading
was 2899 MiB, showing that WindowServer was already varying before the
first request. At one read-only checkpoint, the installed daemon was the signed
`/opt/yabai/bin/yabai` (PID 79476, SHA-256 beginning `89682384`, signing
authority `yabai-lcs-dev`); no Yabai or Space operation was made by the
probe or runner. At that checkpoint, the active Space had ID 14,
**index 1**, display ID 1, with ChatGPT window 2222 focused. HID idle time
was about 36 s at that instant, not continuously observed. The user's report
is an attributed explanation, not a timestamped mapping from each move to a
memory sample. These WindowServer numbers cannot rank backend retention.

## Reproduce and next gates

Source: [`tools/effects/buffer_gpu_probe.m`](../../tools/effects/buffer_gpu_probe.m)
and [`tools/effects/run_headless_ab.py`](../../tools/effects/run_headless_ab.py).
The four [JSONL logs](data/effects-buffer-gpu-ab-2026-09-29/) contain metric
rows only, no screenshots. The local ignored runner manifest and pilots are
under `build/effects-spike/`. Build and smoke commands:

```sh
/usr/bin/clang -std=gnu11 -fobjc-arc -O2 -Wall -Wextra -Werror -mmacosx-version-min=26.0 \
  tools/effects/buffer_gpu_probe.m -o build/effects-spike/buffer_gpu_probe \
  -framework Foundation -framework ScreenCaptureKit -framework Metal \
  -framework CoreVideo -framework CoreMedia -framework CoreGraphics \
  -framework IOSurface -framework ImageIO
/usr/bin/python3 tools/effects/run_headless_ab.py --smoke build/effects-spike/buffer_gpu_probe
/usr/bin/python3 tools/effects/run_headless_ab.py build/effects-spike/buffer_gpu_probe 3 build/effects-spike/headless-ab-20260929
```

The smoke launches a child process without screen capture and verifies
client/WindowServer samples across a timed hold. The final campaign exited
cleanly in all four processes, with 12/12 valid captures and hold 0/15/30/60
in each. The optional `PNG_PREFIX` writes one rectangle/filtered pair for
future pixel comparison and is excluded from timings. No PNGs were saved in
this campaign. The synthetic [`metal_host_demo.m`](../../tools/effects/metal_host_demo.m)
builds but was not run: it hosts one IOSurface texture in an ordinary
`NSWindow`/`CAMetalLayer`, which cannot validate SkyLight auxiliary Space
membership, ordering, bar/icon continuity or presentation time.

Before integration, compare actual captured pixels, cursor, bar/icons and
color transforms under a controlled static scene; then measure a bounded
drawable-based host with real first-present timing, pacing, memory and
system-wide GPU energy against the corrected renderer. Attribute the
WindowServer behavior with a controlled no-other-work baseline and hold,
including behavior after client exit. A future runner should continuously
record active Space ID **and index**, input idle time and daemon PID/identity,
and flag any user navigation or daemon restart. A host demo passing would
still leave the SkyLight overlay contract and combined version-8 acceptance
open.
