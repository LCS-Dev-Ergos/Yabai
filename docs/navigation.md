# Space navigation with a fade

This fork adds one daemon request for keyboard and bar navigation:

```sh
yabai -m space --navigate focus next 0.95 0.1
yabai -m space --navigate focus prev 0.95 0.1
yabai -m space --navigate focus 3 0.95 0.1
yabai -m space --navigate move 3 0.95 0.1
```

Arguments are the action (`focus` or `move`), a space selector, the starting
opacity in `(0,1]`, and the fade duration in `[0,1]` seconds. A zero duration
disables the fade. `next` and `prev` wrap to the first/last space, like the
previous shell script. Other selectors follow the ordinary space selectors.
`move` sends the focused window to the destination and follows it. It does
not move or reorder the Desktop itself: `space --move` and `space --display`
remain separate operations and are still unsupported on macOS 27.

The daemon resolves the selector, optionally moves the window, finds the
frontmost eligible window, sets opacity, switches space and focuses the
window within one event-loop request. It does not spawn `jq` or child yabai
clients, and never changes `window_opacity_duration` or per-window overrides.
Custom opacity is preserved, including windows already dimmer than the fade's
starting value. Hidden, minimized and sticky windows are excluded.

Only a hidden destination can fade. A destination already visible on another
display is focused without dimming its windows. Fullscreen destinations do
not fade, and moving windows into or out of native fullscreen is rejected.
Selecting the current space is a successful no-op that preserves focus.

On repeated navigation less than 180 ms apart, the daemon skips starting a
new fade and still executes the navigation. There is no sleeping debounce,
discarded key event or animation queue. Once navigation pauses, fades resume.
An already running fade may finish within its requested duration.

The scripting addition must be available. A failed space-focus request
restores any dimmed windows immediately instead of falling back to a gesture.
As with separate move/focus commands, a successful window move is not rolled
back if the subsequent focus request fails.

## Integration

The Dotfiles `home/desktop/yabai/space.sh` can keep its existing entry points
and delegate directly with `exec`:

```sh
exec "$yabai" -m space --navigate "$action" "$selector" "$fade_from" "$fade_duration"
```

Update the package and script together. This command requires the new daemon
but works with payload `2.1.31-lcs.2`; it does not change the payload protocol.

## Verification boundaries

`navigation_tests` executes the production navigation implementation with
simulated OS calls. It covers repeat bursts, opacity restoration on failure,
custom opacity, window eligibility, display focus and window moves. The
upstream unity-test executable also checks numeric argument validation.
Neither test establishes visual quality or live Dock behavior.

On 2026-09-26, a local 16-switch comparison at 150 ms intervals measured the
old script at 310 ms median completion latency (926 ms maximum; one failed
request), versus 25–48 ms median for direct space-focus commands. Child CPU
was about 0.95 s versus 0.19–0.20 s. These are local workload observations,
not a Hyprland comparison or measurements of the new command.

WindowServer also consumed about 47% of one CPU core at rest in that session;
its total CPU during switching cannot all be attributed to the fade. Compare
the new command live under the same workload before claiming a CPU or latency
improvement. The payload's existing linear fade remains unchanged; native
compositor transitions and time-based easing are separate work.
