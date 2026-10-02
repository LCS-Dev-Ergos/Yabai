# Flashes on Desktop switches under memory pressure

Date: 2026-10-02. Base: `v8.0.0-lcs.4`. Question: whether the flashes seen when
switching Desktops while memory is short come from the crossfade's timing.

## Method

The frames WindowServer presented on both displays were recorded with
`tools/effects/frame_capture.m` (half resolution, mean luminance and colour of
each changed frame, no images) while Desktops were switched by hand, in single
presses and fast bursts in both directions. The frames were aligned with the
daemon's navigation and effects signposts. A step is suspect when a frame is
brighter than the Desktops on both sides of it, or when a crossfade's
luminance turns back on its way from one Desktop to the other. macOS reported
memory pressure at the warning level through every recording
(`kern.memorystatus_vm_pressure_level` 2, 21 to 42% of memory free), on two
6016 × 3384 displays at 60 Hz.

## Findings

1. **The white comes from the destination's browser, with or without an
   effect.** In a switch without any effect, a fast step of a burst, the centre
   of the screen turned light grey 95 ms after Dock switched (mean RGB 91/104/92,
   luminance about 100/255 against 43 for the page) and stayed so for about
   300 ms, before Microsoft Edge drew its page. Chromium stops drawing windows
   on hidden Desktops and, under memory pressure, discards their last frame;
   until its renderer draws again, the window shows its default background.
   The crossfade reveals the same background as it fades, and a step of a burst
   can capture it and show it for its whole fade.
2. **The crossfade's overlay can reach the screen after Dock's switch.** In 1 of
   20 crossfades with a visible change, the destination showed for about 67 ms,
   then the outgoing image came back and faded. The overlay reached the screen
   about 110 ms after it was ordered, while the daemon waits one refresh
   (16.7 ms at 60 Hz) before asking Dock to switch. That step's capture took
   149.7 of its 150 ms. On these displays the capture and the overlay's copy
   take 81 MB each.

| Recording | Steps | Crossfades with a visible change | Overlay after the switch | Frames brighter than both Desktops |
| --- | ---: | ---: | ---: | ---: |
| Edge as installed | 123 | 20 | 1 | 1, in a switch without effect |
| Edge with `--disable-backgrounding-occluded-windows` | 97 | 53 | 0 | 0 |

## Changes

- The crossfade is left out while macOS reports memory pressure, at the warning
  or critical level; the switch is then Dock's alone. No call reports when one
  of our windows is on screen, so a longer fixed wait before the switch would
  slow every switch without bounding the delay. The veil captures nothing and
  keeps its effect.
- Edge was relaunched with `--disable-backgrounding-occluded-windows`, which
  keeps it drawing its windows on hidden Desktops. That setting belongs to the
  host's configuration, not to this repository.

## Limits

- Both defects are rare: one late overlay in 20 crossfades and one browser
  flash in 123 steps. None in 97 steps with the browser flag is consistent with
  the cause above, not a proof.
- The daemon change has not run installed; it needs a release. Under the load
  of these recordings the warning level never cleared, so the crossfade would
  have been off throughout.
- Electron applications may show the same background; only Edge was seen
  doing so.
- The recorder reports frames that changed, at half resolution; a flash
  shorter than one refresh cannot be excluded.
