# IPC audit: socket ingress, command parsing and signal actions

Date: 2026-10-01. Base: `v8.0.0-lcs.3` (`e6800b5`). Scope: `src/ipc` and the
paths it reaches directly: the event-loop message handler
(`src/events/handlers/messages.c`), signal spawning
(`src/events/event_signal_process.c`), the serializers of the labels commands
store, the client (`src/client`, used by `src/yabai.c`), the peer check
(`src/osax/socket_identity.h`), the event loop's wake-ups
(`src/events/event_loop.c`) and the configuration-file runner
(`src/misc/helpers.h`). Navigation admission was read where it touches
ingress; its scheduling belongs to the navigation audit. Host-specific records
are kept outside the repository.

## Map

| Thread | Work | State |
| --- | --- | --- |
| Accept thread (`message_loop_run`) | `accept`, peer check (audit-token UID, designated requirement, cache of trusted code), navigation admission (`poll` up to 10 ms, `MSG_PEEK`), the cap on waiting connections, then `DAEMON_MESSAGE` | `g_message_loop`, the requirement and its cache, `g_space_navigation_queue` (mutex) |
| Event loop (`DAEMON_MESSAGE`) | Read the framed request (at most 64 KiB), run the command, write the reply, close | Rules, signals, labels and settings in the managers |

The peer check admits processes of the user (or root) that satisfy the daemon's
designated requirement. The signed `yabai` binary is that client, and any
process of the user can run it. The check therefore keeps other programs off
the socket but does not separate processes of the same user. What a same-user
process gains through the socket is what the daemon can do beyond that user's
own reach: drive windows under yabai's Accessibility grant and, before this
change, run commands under its Accessibility and Screen Recording grants
through signal actions.

## Findings

| # | Severity | Finding | Status |
| --- | --- | --- | --- |
| 1 | Medium, robustness | The event loop read requests and wrote replies with blocking calls and no bound. A client that connected and sent nothing, sent slowly, or stopped reading a reply larger than the socket buffers held every event until it exited, for example `yabai -m query --windows \| sleep 60` with enough windows. | Fixed |
| 2 | Medium, security | Signal actions inherited the daemon's TCC responsibility, so a command added with `signal --add` ran with yabai's Accessibility and Screen Recording grants. | Fixed |
| 3 | Medium, robustness | `window_animation_duration` and `window_opacity_duration` accepted negative values, `nan` and `inf`. An animation ends when its elapsed share of the duration reaches 1, which such a duration never allows: the display link kept running and the proxies stayed over the real windows. The payload drops a focus fade with such a duration, so focus opacity stopped changing. | Fixed |
| 4 | Low | `window --ratio`, `--move` and `--resize` accepted `nan` and `inf` through `sscanf`. `clampf_range` returns NaN unchanged, so a NaN split ratio stayed in the tree until a balance. | Fixed |
| 5 | Low | `window --grid` with zero rows or columns wrapped the start cell to `UINT_MAX` and gave infinite cell sizes; negative values became huge unsigned ones. The `grid` rule accepted the same values and ignored zero rows at apply time. | Fixed |
| 6 | Low | Display, Space, rule and signal labels and scratchpad names were written into JSON unescaped; a `"` or `\` made the document invalid. | Fixed |
| 7 | Low | A key given twice to `rule --add` or `signal --add` leaked its first text and compiled pattern: one rule and one signal request with repeated patterns retained about 5.4 KB together. For rules, `app!=A app=B` also kept the exclusion, so the rule matched everything except `B`. | Fixed |
| 8 | Low | The configuration file ran as `sh -c <path>`: the path was shell text, so spaces broke it, and it was started with `fork` in the multithreaded daemon. | Fixed |
| 9 | Low, usability | Decimal settings refused whole numbers: `config window_opacity_duration 0` failed while `0.0` worked. | Changed: accepted |
| 10 | Info | `daemon_fail` had no `printf` format attribute. With it, the existing calls show only signedness differences. | Added |
| 11 | Medium, robustness | Rule and signal patterns compile on the event loop, and `regcomp` builds an automaton that can grow far faster than the pattern. Nested bounds multiply its positions: `((a{255}){255}){255}` took 2.35 s and 1.8 GB before failing with `REG_ESPACE`. Optional parts in a row connect every pair of positions, with no bound at all: 2000 `a?` (4 KB) compiled in 15.8 s with a 2.9 GB peak, and 4000 `(a\|...)` alternatives peaked at 1 GB. | Fixed |
| 12 | Low, usability | A refused peer, a malformed or oversized request, and a request that timed out were closed without a word, and the client exited 0 with no output, as for a command without output. A peer refused for its signature, such as a locally built client, looked like it worked. | Fixed |
| 13 | Low, security | The event loop slept on the named semaphore `yabai_event_loop_semaphore`, opened with `O_CREAT` and no `O_EXCL`. POSIX semaphore names are shared by every account on the Mac: a process that created the name first shared the semaphore and took its wake-ups, and one that created it without permissions kept the daemon from starting (`EACCES`). | Fixed |
| 14 | Low, robustness | The first fix for finding 1 cut a reply off after a second when the client could not write it out, since the client wrote each piece to its output before reading the next: `yabai -m query --windows \| less` with a long reply and a reader who does not scroll. The client also judged failure per read: a failure line after other output went to standard output with exit 0, and the marker byte with it. | Fixed |
| 15 | Info, performance | Every `yabai -m` loads the daemon's frameworks (3.3 ms) and passes a full signature check in the daemon (2.6 ms), on each key press. | Addressed |
| 16 | Info, performance | A mouse move merged into one still queued signalled the semaphore again, so the event loop woke once more to an empty queue for each merged move. | Fixed |

### Hypotheses that did not hold

- *The accept loop spins when descriptors run out.* XNU drops a pending
  connection it cannot give a descriptor, so `accept` blocks again. With a
  64-descriptor limit and 20 connections pending, the accept thread used no
  measurable CPU over one second.
- *Peer authentication is a bottleneck.* The check costs 0.41 ms against the
  running daemon with its own requirement, and 2.6 ms (p90 3.4 ms) against a
  freshly started Apple-signed process, which stands for every client. That is
  a quarter of a trivial command, not a bottleneck; it is now cached (change 12).
- *macOS accepts back-references in extended patterns, whose matching can
  backtrack exponentially.* `(a)\1` compiles as a literal `1` and does not
  match `aa`, so matching stays a parallel automaton.
- *A bracket expression costs one position per item.* `[abc...z]{255}`
  retained 84 KB, as one position per copy would, not the 9.7 MB of
  `(a|b|...|z){255}`.

## Changes

1. **Bounded client I/O.** The handler builds the reply in memory, then reads
   the request and writes the reply through `poll` with one second each, in
   total rather than per call. The connection is made non-blocking because a
   blocking `send` waits for the whole buffer even with `MSG_DONTWAIT`; the
   reply uses `MSG_NOSIGNAL`. A stalled client loses its request or the rest of
   its reply.
2. **Disclaimed signal actions.** `event_signal_spawn` sets
   `responsibility_spawnattrs_setdisclaim`, so an action answers for its own
   privacy permissions; if the attribute cannot be set, nothing is spawned.
   Actions that only call `yabai -m` or `sudo yabai --load-sa` are unaffected.
   The configuration file keeps the daemon's permissions: it is the user's own
   file, run once at start.
3. **Finite values.** `token_value_to_finite_float` accepts decimals and whole
   numbers and refuses non-finite ones; durations must also be 0 or more.
   Window ratio, position and size must be finite. `parse_grid` serves both
   `window --grid` and the `grid` rule and requires a row, a column and no
   negative value.
4. **Escaped labels.** `ts_json_text` escapes the five label and scratchpad
   fields as titles already were.
5. **Repeated keys.** A repeated key releases the text and pattern it replaces,
   and the exclusion follows the last pair. The last value still wins, as
   before.
6. **Configuration file.** `posix_spawn` with the path as an argument: an
   executable file runs as `sh -c '"$0"' <path>`, which honours its interpreter
   line, and any other file as `sh <path>`. It inherits the standard streams and
   none of the daemon's sockets.
7. **Fuzzing.** `fuzz_daemon_message` now also runs the `rule` and `signal`
   handlers, patterns included, except rules naming a display or Space, which
   WindowServer resolves.
8. **Pattern cost.** `src/ipc/pattern.c` follows `regcomp`'s expansion through
   the pattern's structure without building anything: `?`, `*` and `+` stay
   one node, a bound becomes its minimum of copies followed by nested optional
   ones, and each node keeps its first and last positions. From those it counts
   nodes, position-set entries and transitions, and refuses a pattern past
   2^19 units with "regex pattern for key '...' is too complex". A literal
   costs seven units a character, so any literal a request can carry passes.
   Patterns compile with `REG_NOSUB`, as `regex_match` never asks where a match
   is; submatch tracking had multiplied the cost of optional parts, 1000 `a?`
   taking 1.18 s with a 1.6 GB peak with it and 0.09 s with 101 MB without.
9. **Explicit refusals.** The daemon answers a request it cannot read with the
   reason (`request timed out`, `request ended early`, `request is malformed`,
   `request is longer than 64 KiB`), and the accept thread answers a peer that
   fails the requirement (`client refused: it is not signed like the running
   yabai`) and a client arriving while 128 connections wait for the event loop
   (`too many requests are waiting`). Each waiting connection holds a
   descriptor, and near the limit, 256 under launchd, the kernel would drop new
   clients unanswered.
10. **Client.** The client, now `src/client/client.c`, reads the whole reply
    before printing any of it, so a stalled reader of its output holds the
    client and not the daemon. A failure marker at the start of any line sends
    the whole reply to standard error without the markers and makes the exit
    status 1. The request goes out with `MSG_NOSIGNAL`; if the daemon closes
    before taking all of it, the client still prints the reason it sent.
11. **yabai-msg.** The same client as its own executable, linked with
    libSystem alone. It embeds yabai's `Info.plist`, so `codesign` gives it
    the identifier `com.asmvik.yabai` and the daemon's requirement admits it
    when both are signed with the same certificate. CMake, the makefile and
    the release workflow build, sign and ship it beside `yabai`; a test checks
    that it links no framework.
12. **Trusted code.** The kernel reports a process's code-directory hash and
    whether it holds the code valid in about a microsecond
    (`csops_audittoken`, bound to the audit token, so an exec in between is
    refused). The hash covers the code pages, identifier, entitlements and
    flags; only the signature around them could differ. The accept thread
    keeps the last four hashes that passed the full check and trusts a valid
    process with one of them without checking again. The cache belongs to one
    requirement and is reset if it changes; the user check still runs. The
    Dock payload keeps the full check, unchanged.
13. **Event-loop wake-ups.** A dispatch semaphore private to the process
    replaces the named one. A merged mouse move does not signal it: the move
    it replaced was still queued, so its wake-up is still due.

## Performance

Measured on the maintainer's Apple Silicon Mac under macOS 27.2 against the
idle installed lcs.3 daemon. Medians over 30 to 300 runs, p90 in parentheses.

| Path | Time |
| --- | --- |
| `yabai -m config debug_output`, end to end | 10.6 ms (11.8) |
| `yabai --version`, client start only | 6.9 ms (7.7) |
| Empty program / empty program linked with the client's frameworks | 2.5 ms / 5.8 ms |
| Peer check, fresh Apple-signed process / running daemon | 2.6 ms (3.4) / 0.41 ms |
| `query --displays` / `--spaces` / `--windows` (8 windows) | 11.4 / 21.5 / 21.0 ms |

After the changes, without replacing the installed daemon:

| Path | Before | After |
| --- | --- | --- |
| Start-up, 200 interleaved runs: `yabai --version` / `yabai-msg` / `/usr/bin/true` | 6.1 ms | 2.1 ms / 1.5 ms |
| Peer check of a new process of code already checked | 2.6 ms (6.1 ms in `socket_identity_tests` on a loaded machine) | 0.003 ms |
| Event-loop wake-ups for three mouse moves and a window event, queued together | 4 | 2 |
| `((a{255}){255}){255}` in `rule --add` | 2.35 s, 1.8 GB, then `REG_ESPACE` | refused before `regcomp` |
| 2000 `a?` in `rule --add` | 15.8 s, 2.9 GB peak | refused before `regcomp` |

Under the limit, the costliest shapes found, 500 `a?` or `[a-z]?`, 700
alternatives, `((a{40}){40}){40}` and `((a?){100}){5}`, compile in 4 to 42 ms
with a peak of 13 to 38 MB, like the longest literal a request can carry
(15 to 18 ms, 39.5 MB). Those times are from repeated runs under a load
average around 5; a first run under a load average of 34 took up to 0.18 s,
while the memory did not change. Such a pattern also matches slowly, up to
3.5 ms for a 57-character title with 500 `[a-z]?`, against about a microsecond
for an ordinary one.

`yabai-msg` with a cached peer check is expected to save about 6.5 ms of the
10.6 ms of a trivial command; that end-to-end time needs the installed
daemon. The bounded I/O adds one `open_memstream` and a few `poll` calls per
command; a query's reply is now written after the command has run instead of
while it runs.

## Verification and limits

Each fix of the first round began with a test that failed against the base:
the bounded-read, bounded-reply and handler round-trip tests; decimal, window
and grid values; label escaping; repeated keys, with retained memory falling
from 1.08 MB to under 2 KB over 200 repeats; the configuration path; and the
signal action's responsible process. In the second round, the pattern command
test, the wake-up count, the failure reply of the handler and the busy refusal
were run against the previous behaviour and failed (the busy test hung on a
reply that never came, and now closes the connection instead). The client and
peer-check tests were written with their code; a probe showed the old
semaphore shared with a process that created the name first, and blocked by
one without permissions.

- Debug CTest: 15/15, unit runner 42/42.
- ASan/UBSan and TSan/UBSan: 15/15 each, unit runner 42/42 each.
- Analyzer: no new findings. Area headers: 27/27. Release build: no
  diagnostics, CTest 15/15.
- Fuzzing: both CTest targets for 30 s each, and the daemon target for 120 s
  with patterns now reaching the handlers (1.2 million inputs, 2112 new units,
  698 MB peak under ASan), with no failure.

Limits:

- The installed daemon was not replaced, because a binary at a new path has
  no TCC grants. The disclaim, the bounded I/O, the refusals and the peer cache
  run in test processes through the production functions; `yabai-msg` has not
  yet talked to an installed daemon, which needs both signed with the release
  certificate.
- On a Unix socket, macOS gives a closing side no way to mark the connection
  as aborted: a close with `SO_LINGER` 0, and a connection the kernel drops at
  the descriptor limit, both end in a plain EOF. A reply cut off by the bound,
  and a daemon that dies during a command, still read as complete. Telling
  them apart needs a change of the reply format, and a client would then
  misread a daemon of an earlier version during an upgrade.
- An older client binary still prints while it reads, so with a stalled
  reader of its output it can still lose the end of a long reply.
- The peer check remains no boundary between processes of the same user.
