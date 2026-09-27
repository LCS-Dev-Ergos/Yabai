# Space navigation with a fade

This fork adds one daemon request for keyboard and bar navigation:

```sh
yabai -m space --navigate focus next crossfade 0.25
yabai -m space --navigate focus prev 0.95 0.1
yabai -m space --navigate focus 3 crossfade 0.25
yabai -m space --navigate move 3 0.95 0.1
```

Arguments are the action (`focus` or `move`), a space selector, the effect and
its duration in `[0,1]` seconds. The effect is `crossfade`, a
[crossfade of the display](effects.md#desktop-crossfade), or the starting
opacity in `(0,1]` of the destination's windows. A zero duration disables it.
`next` and `prev` wrap to the first/last space, like the previous shell
script. Other selectors follow the ordinary space selectors.
`move` sends the focused window to the destination and follows it. It does
not move or reorder the Desktop itself; that is what `space --move` and
`space --display` do.

The daemon resolves the selector, optionally moves the window, finds the
frontmost eligible window, sets opacity, switches space and focuses the
window within one event-loop request. The active display is read from
WindowServer, and the already frontmost window is focused without an AXRaise
round trip. One `SLSCopyManagedDisplaySpaces` reply per request answers the
Desktop order, displays, visibility and type; a Desktop it does not know is
asked about individually. It does not spawn `jq` or child yabai
clients, and never changes `window_opacity_duration` or per-window overrides.
The `move` action still raises the moved window at its destination.
Custom opacity is preserved, including windows already dimmer than the fade's
starting value. Hidden, minimized and sticky windows are excluded.

Only a hidden destination can fade. A destination already visible on another
display is focused without dimming its windows. Fullscreen destinations do
not fade, and moving windows into or out of native fullscreen is rejected.
Selecting the current space is a successful no-op that preserves focus.

On repeated navigation less than 180 ms apart, the daemon skips starting a
new fade and still executes the navigation. There is no sleeping debounce or
animation queue; requests that arrive while another waits are merged as
described below. Once navigation pauses, fades resume. An already running fade
may finish within its requested duration.

The scripting addition must be available. A failed space-focus request
restores any dimmed windows immediately instead of falling back to a gesture.
As with separate move/focus commands, a successful window move is not rolled
back if the subsequent focus request fails.

## Pacing

Requests can arrive faster than a switch completes: a held key repeats every
30 ms, and a busy Edge or VS Code handles an activation hundreds of
milliseconds after it was sent. An activation handled after the user had moved
on brought its Desktop back into view. Navigation therefore runs as steps of one
Desktop from a bounded queue, in the order requested:

- A step runs at once when the previous one is at least 100 ms old. After a
  step that activated an application, the next one also waits until that
  application reports the window focused, or 150 ms have passed.
- `next` and `prev` count from the Desktop the previous step switched to. Each
  Desktop number is one step; the same number twice in a row queues once.
- Only the last queued step activates an application. The steps before it
  switch Desktop and show their effect: a crossfade then lasts as long as the
  interval between steps, up to the requested duration, so each one ends as
  the next begins.
- At most ten steps wait; a request that does not fit is refused.
- A click after a request, or any other command except queries, empties the
  queue. A failed step drops the rest.

The client gets its answer when its request is queued; failures of later steps
go to the debug log. `yabai -m config space_navigation_pacing off` runs each
request at once instead, as before, with merged relative requests moving
several Desktops in one switch.

Signposts in subsystem `com.lcs.yabai`, category `navigation`, mark each
request, step, activation and the focus that confirms it.

## Relative navigation

`next` and `prev` count from the Desktop the previous navigation switched to,
for up to one second, while that Desktop is still visible. WindowServer's
active display can briefly follow an application to a window it has on
another display: a Chromium browser activated on one display makes its window
on the other display key. Counting from that display wrapped `next` back to
Desktop 1. A mouse click or any other daemon command except queries ends this,
and so does the second: the active display then decides again, so a Desktop
chosen by clicking, Command-Tab or `window --focus` is where counting starts.

When the destination's application has another window visible on a different
display, navigation activates the application on the destination window and
raises it through Accessibility, which such an application honors. The raise
completes before the next request runs, so it cannot pull a Desktop back into
view. Unlike `window --focus`, it posts no synthesized click: Edge, busy with
the switch, handled that click hundreds of milliseconds late, after rapid
navigation had moved on, and the click activated Edge again and brought its
Desktop back. An application that is already active keeps the raise of
`window --focus`, click included: activation alone does not move its key
window, and Edge kept keyboard focus on its window of the Desktop left behind.
Other applications keep the cheaper focus without a raise. A tiled window's
Desktop is the Space of its view, so only floating and unmanaged windows need
WindowServer: one query lists the application's window numbers on the Desktops
visible on other displays. The decision is made before the switch, because
afterwards WindowServer is busy showing the new Desktop and each synchronous
call waits for about a frame.

Between two windows of the application that is already active, upstream
leaves 40 ms between deactivating the old window and activating the new one,
because some applications miss a focus change whose events arrive together
([#2694](https://github.com/asmvik/yabai/pull/2694)). Until then it left 10 ms.
The destination window shows inactive until the activation lands, which reads
as a title-bar flash, so navigation uses 10 ms. It does not sleep in the event
loop: the activation runs as a later event, dropped if another navigation or
command comes first, or if focus moves to another application. Focus moving to
another window of the same application does not drop it: that is usually an
earlier navigation's activation handled late by a busy application, and
without the new activation its Desktop would come back into view.

Navigation records the window it focused, so the activation handler that
follows uses it instead of asking the application, which is busy with the
switch, for its focused window. A later focus notification from the
application still replaces it.

A navigation waits on WindowServer and the applications involved for tens of
milliseconds, while a held key repeats every 30 ms. The daemon's accept thread
lets a `focus next|prev` request join the one already waiting in the event
queue and answers it at once, so each uninterrupted group has one waiting
request. A repeat, arriving
less than 50 ms after the previous relative request, keeps the pending step; a
separate key press adds one, and the other direction takes one back. A group
that a repeat starts is marked, and the pacing queue keeps at most one such
step pending, so a held key moves one Desktop per step and stops within one
step of its release, while three quick presses still move three Desktops.
Any other request closes the group,
so requests keep their order. Later repeats form a new group even while an
earlier closed group still waits. This keeps queries from disabling merging
for all subsequent repeats, without moving navigation across a query or
another command. Absolute selectors and `move` are not merged. A merged
client's successful return acknowledges queuing, not completed navigation.

## Integration

The Dotfiles `home/desktop/yabai/space.sh` can keep its existing entry points
and delegate directly with `exec`:

```sh
exec "$yabai" -m space --navigate "$action" "$selector" "$effect" "$duration"
```

Update the package and script together. `crossfade` needs payload
`2.1.31-lcs.10`: an older payload either refuses it, and navigation switches
without an effect, or, `2.1.31-lcs.9`, also leaves the destination transparent
(see [effects](effects.md#desktop-crossfade)).

## Verification boundaries

`navigation_tests` executes the production navigation implementation with
simulated OS calls. It covers repeat bursts, opacity restoration on failure,
custom opacity, window eligibility, display focus, window moves, the starting
Desktop of relative navigation and the raise decision.
`navigation_spaces_tests` reads a constructed `SLSCopyManagedDisplaySpaces`
reply and checks its lookups and fallbacks. `navigation_queue_tests` covers request recognition and merging, and the
daemon fuzz target runs the request check. The upstream unity-test executable
also checks numeric argument validation. None of these tests establishes
visual quality, application focus behavior or live Dock behavior.

On 2026-09-26, a local 16-switch comparison at 150 ms intervals measured the
old script at 310 ms median completion latency (926 ms maximum; one failed
request), versus 25–48 ms median for direct space-focus commands. Child CPU
was about 0.95 s versus 0.19–0.20 s. These are local workload observations,
not a Hyprland comparison or measurements of the new command.

WindowServer also consumed about 47% of one CPU core at rest in that session;
its total CPU during switching cannot all be attributed to the fade. Compare
the new command live under the same workload before claiming a CPU or latency
improvement.

Live testing of lcs.4 found 4.4–4.6 s median latency for a rapid 16-request
burst in another session (old-script control: 1.1 s, with five failures).
Stack sampling identified waits in AXRaise and repeated AX focused-window
queries. The subsequent daemon correction removes those calls from ordinary
navigation; its live latency still needs verification after activation.
WindowServer consumed about 82% of one core even at rest in that session.

Payload `2.1.31-lcs.3` adds the [shared, time-based opacity engine](effects.md).
The activated lcs.5 latency checks and remaining signal-dispatch bottleneck
are recorded in [performance investigation](performance.md). Frame timing
and visual acceptance remain separate from command completion.
