# lcs.12 live follow-up

2026-09-27. Active daemon: signed `7.1.25-lcs.12`, PID 15093. The read-only
handshake returned payload `2.1.31-lcs.6` with attributes `0x5D`. The user
reported visibly smoother transitions and better perceived performance, with
a small title-bar flash in VS Code and other applications during transitions.

## Configuration and bounded comparisons

The switch restored ordinary opacity to enabled, active alpha 1, normal alpha
0.975 and focus duration 100 ms. Navigation remains 0.9 to target over 150 ms.
Window geometry animations are 200 ms cubic ease-out. Display scaling was kept.

The current Desktop 6 contains VS Code and 7 contains ChatGPT. This differs
from the previous ChatGPT/Edge workload, so the old and new latency figures
are not a controlled release comparison.

Two rounds of alpha sampling saw 10–11 intermediate readings for ChatGPT but
only one for Code; Code's synchronous reads/focus were substantially slower.
Alpha queries are intrusive observations and do not measure presented frames.
The attempted six-second title-bar video capture exited 1 without producing a
file; no rendered-frame or visual root-cause claim is made from that attempt.

For each configuration, 16 absolute navigation requests were launched at
150 ms intervals with a matched idle interval. Desktop and focus were restored
after every trial. The first experiment used ABCCBA order:

| Condition | Focus duration | Normal alpha | Trial median client latencies |
| --- | --- | --- | --- |
| A | 100 ms | 0.975 | 1361, 728 ms |
| B | 0 ms | 0.975 | 1244, 1417 ms |
| C | 0 ms | 1.0 | 729, 899 ms |

A second on/off/off/on experiment disabled only ordinary opacity; navigation
fading stayed enabled. On medians were 1753/945 ms and off medians 1635/1567 ms.
WindowServer CPU time per active workload was 3.22/2.74 s on and 3.05/3.05 s off.
These variable, queue-building absolute-request workloads do not demonstrate a
repeatable configuration speedup. No navigation request failed. The user also
reported that disabling ordinary opacity left the title-bar flash unchanged.
The original opacity settings were restored; this is not a confirmed config bug.

Raw local evidence is in ignored `build/lcs12-config-comparison.json`,
`build/lcs12-opacity-comparison.json`, their per-trial files and
`build/lcs11-alpha-traces-lcs12-active.json`. The earlier incomplete configuration
attempts rejected integer `0`; the completed comparisons use decimal values
and restore settings in `finally` blocks.

## Navigation hot path

A five-second, 1 ms daemon stack sample during 40 relative requests with four
interleaved queries found 346 sampled stacks in `space_navigation_needs_raise`:
300 at its visibility check and 46 around `window_space`. This is sampled stack
residency, not an exact duration or fraction of WindowServer CPU. The workload
completed without errors; the final drain was 1.993 s under sampling. Its median
1.1 ms client acknowledgement includes coalesced requests and is not visual latency.
Source: `build/lcs12-repeat-profile.json` and its `.sample.txt` file.

The old raise decision asks whether every other application window's Space is
visible, then resolves its display again if visible. Visibility itself resolves
the display and asks its current Space. Same-display windows cannot trigger
the cross-display focus-stealing case this check protects against.

The follow-up resolves each candidate's display once, skips same-display
visibility queries, and only asks the other display's current Space when needed.
It adds no cross-event cache. It retains the raise for a visible window of the
same application on another display, and ignores hidden/unknown destinations.

The regression failed before the change on an unnecessary visibility query and
passed afterward. It also covers the visible cross-display browser, a hidden
Space on the other monitor and a missing Space. Debug and ASan/UBSan passed 6/6;
static analysis passed. End-to-end latency and the title-bar flash still need
separate live acceptance; fewer queries are not proof that either is resolved.
