# Scripting-addition audit: payload requests and the daemon's side

Date: 2026-10-02. Base: `v8.0.0-lcs.4` (`505a0e0`). Scope: the Dock payload's
socket and request handlers (`src/osax/payload.m`,
`src/osax/window_opacity_handlers.c`, and the window-fade files where requests
reach them), the pattern search that locates Dock's internals
(`src/osax/pattern.h`, `tools/osax/pattern_check.m`), the payload's peer check
(`src/osax/socket_identity.h`), and the daemon's request senders and handshake
(`src/sa/sa.m`, `src/sa/sa_opacity.c`). The loader and the root-run install
and load path are reviewed separately. Host-specific records are kept outside
the repository.

## Map

| Thread | Work | State |
| --- | --- | --- |
| Payload connection thread (`handle_connection`, in Dock) | `accept`, peer check (audit-token UID or root, the requirement the root installer saved, cache of trusted code), read one request of at most 4095 bytes, run its handler, reply to a handshake or an opacity batch, close | The Dock instances found at load, `message_end`, the cache |
| Fade worker (in Dock) | Opacity frames and deadlines | `window_fades` under `window_fade_lock` |
| Dock main thread | Display links; Space create, destroy and move work the handlers dispatch to it | Dock's own |
| Daemon: event loop and animation threads | `scripting_addition_*`: build the request in a 4 KiB buffer, connect, send, wait up to one second for the reply or the close | `g_sa_socket_file` |
| `sudo yabai --load-sa` | Handshake: payload version and attribute bits | none |

The payload admits processes of the user, and root, that satisfy the daemon's
designated requirement: the daemon and `--load-sa`. As on the daemon's socket,
that keeps other programs off the socket but is no boundary between processes
of the same user. The daemon does not check who listens on the payload socket
(see below).

## Findings

| # | Severity | Finding | Status |
| --- | --- | --- | --- |
| 1 | Medium, robustness | A reply could terminate Dock with `SIGPIPE`. The payload set `SO_NOSIGPIPE` after `accept`, but macOS refuses that option (`EINVAL`) on a connection whose peer has closed, and the handshake and the opacity batch replied with plain `send`. Dock neither ignores nor catches `SIGPIPE`. A daemon that closes before the payload reaches its connection fails the peer check, so the exposure is narrower: a daemon whose one-second wait ends while the payload checks its connection, which happens when the payload falls about a second behind with requests queued. Dock then exits, the Desktop is redrawn, and Space operations fail until the payload is loaded again. | Fixed |
| 2 | Info, performance | The payload checked every request's signature in full, and the daemon waited for it: a median of 0.45 and 0.52 ms in two runs of 200 checks, up to 12 ms, against the running daemon. Every navigation step's Space focus, each opacity batch of a fade and every mouse move of a modifier drag paid it. | Fixed |
| 3 | Low, robustness | The payload read requests with no timeout. A peer that connected and stopped sending, a daemon stopped in a debugger for instance, held the payload's only connection thread; each later request then cost the daemon its full one-second wait. | Fixed |
| 4 | Low, robustness | `--load-sa`, which runs as root, read the handshake reply with one `recv`, looked for its NUL beyond the bytes received, and read the attribute bits after it unchecked: a reply of 1023 bytes without a NUL read four bytes past the buffer (ASan: stack-buffer-overflow, read of size 4). It also waited forever for a listener that accepted and never answered. The reply comes from whatever listens on the payload socket, normally the payload. | Fixed |
| 5 | Low, robustness | The pattern search did not stop at the end of Dock's code. On macOS 27.2, four of the five lookups search windows that run past the end of `__TEXT,__text` (`0x226adc` and `0x2172c8` in the two slices) into stubs, strings and other read-only data, up to `0x2886a0`. A miss or an early false match there would hand a non-instruction to a call or a patch. `pattern_check` reports such a match as `FAIL`; the payload used it. Every current lookup matches inside the section. | Fixed |
| 6 | Low, robustness | `window --toggle pip` scaled the window to a quarter of the display width and computed its height in whole points: a window more than about 400 times wider than high (on a 1600-point display) got a height of 0 and an infinite scale, and bounds that are not finite overflowed the conversion to `int` (UBSan: `inf is outside the range of representable values of type 'int'`). A failed read of the window's transform left it uninitialized, and that garbage chose between scaling and restoring. | Fixed |

### Open hypotheses

- *Dock state read off its main thread.* The Space focus handler, and the
  lookups of the destroy and pre-27 move handlers, read and write Dock's
  private objects (`_displaySpaces`, `_currentSpace`, `spacesForDisplay:`) on
  the payload thread while Dock's main thread may change them. A race could
  crash Dock. There are no Dock crash reports on the audited Mac, and moving
  the work to the main thread would put Dock's main-thread latency on every
  navigation step, so nothing changes without evidence.
- *A reference kept per Space focus.* The handlers store `[space retain]`
  through `object_setInstanceVariable`. If the runtime also retains for that
  ivar, every focus keeps one reference on a Space that lives on anyway; a
  destroyed Space would then never be freed. Unverified and bounded.
- *The listener is not authenticated.* A process of the user can bind the
  payload socket while no payload listens, then read the daemon's requests or
  answer `--load-sa`'s handshake (which can make it reinstall the payload and
  restart Dock). That process could stop or restart Dock directly, so this
  stays within the documented model; checking Dock's signature at the
  handshake would close it.
- *`SA_OPCODE_WINDOW_FOCUS`.* Its handler asks for Dock's own process serial
  number instead of the window owner's and reads an uninitialized connection.
  The daemon never sends it (the caller is compiled out), and the lookup it
  needs is disabled on macOS 27.

### Checked and sound

- A request the daemon has given up on is dropped, not applied late: once
  the client has closed, `LOCAL_PEERTOKEN` fails with `ENOTCONN`, so the peer
  check refuses the connection before its request is read.
- Request framing and handler bounds: lengths stay below 4 KiB, counts are
  checked against the bytes left, opacity values are validated; the fuzzer
  covers them.
- The daemon ignores `SIGPIPE` and refuses a request that does not fit before
  connecting.
- `accept` does not spin when descriptors run out (see the IPC audit).
- A display link whose fades have ended stops itself on its next frame.

## Changes

1. **Replies without `SIGPIPE`.** Both replies go through `payload_reply`,
   which sends with `MSG_NOSIGNAL`; the `SO_NOSIGPIPE` option is gone.
2. **Cached peer check.** The connection thread uses
   `yabai_socket_peer_is_trusted_cached`, the daemon's cache of trusted code
   from the IPC audit: a daemon whose code-directory hash already passed, and
   whose code the kernel still holds valid, is trusted after a kernel query.
   The UID check still runs on every connection.
3. **Read deadline.** `read_message` gives the whole request one second, as
   long as the daemon waits: each read waits only for the time left
   (`SO_RCVTIMEO`), so a peer that sends a byte at a time cannot stretch it.
   Without the deadline, a request trickled in a byte every 300 ms was
   accepted after 2.4 s. A connection whose sender has already gone cannot
   take the timeout and is not read.
4. **Handshake.** `--load-sa` sends with `MSG_NOSIGNAL`, reads until the
   payload closes the connection, giving the whole reply five seconds, and
   `scripting_addition_parse_handshake` accepts only a reply with a NUL and
   four bytes after it, and a version that fits the caller's buffer.
5. **Bounded pattern search.** `hex_find_seq` takes an end and returns only a
   match that ends before it, reading no byte past it. The payload passes the
   end of `__TEXT,__text`; `pattern_check` passes the end of each slice's
   section, and its output on macOS 27.2 is unchanged for both slices.
6. **Window scale.** The handler returns when the window's bounds or
   transform cannot be read, when the bounds are not finite, or when the
   scaled height, computed in floating point, would be under one point or
   beyond 65536.
7. **Payload version** `2.1.31-lcs.15`.

## Performance

Measured on the maintainer's Apple Silicon Mac under macOS 27.2, against the
running lcs.4 daemon and the installed requirement, 200 checks per run.

| Check per request | Median | p90 |
| --- | --- | --- |
| Full signature check (before) | 0.45 / 0.52 ms | 0.54 / 0.80 ms |
| Kernel query of the cache (after) | 0.001 ms | 0.002 ms |
| Whole cached check, from the IPC audit | 0.003 ms | |

The daemon waits for this check on every scripting-addition request. The
end-to-end gain needs the new payload in Dock and has not been measured.

## Verification and limits

The reply, window-scale and handshake fixes began with tests that failed on
the base: the payload raised `SIGPIPE` on both replies, set a transform for
the wide window, and the base's handshake parsing read past its buffer under
ASan. A first version bounded each read rather than the whole message, and
guarded only a zero height; tests of a trickled request and reply, of
infinite bounds and of a very tall window failed on it (UBSan reported the
overflowing conversion) and pass now. The read-deadline and pattern-bound
tests were written with the interfaces they needed.

- Debug CTest: 16/16, unit runner 43/43.
- ASan/UBSan and TSan/UBSan: 16/16 each. Release: 16/16.
- Analyzer: no findings for the payload and loader, no new ones for the
  daemon. Area headers: 27/27.
- `pattern_check` against the local Dock: the same output as before the
  change, both slices.
- Fuzzing: the payload target for 120 s (18 million inputs, 452 MB peak under
  ASan) and the daemon target for 30 s, with no failure.

Limits:

- The new payload has not run in Dock. Installing it needs root and restarts
  Dock: `sudo yabai --load-sa` with a build of this change reinstalls it,
  since the version differs. The cache, the read deadline and the replies run
  in test processes through the production functions; the cache itself is
  covered by `socket_identity_tests`.
- The proxy-swap handlers check their count against the words left, but an
  entry is one word when skipped and two otherwise: a malformed request that
  passes the peer check can commit a partial transaction. The daemon always
  packs complete requests.
- The open hypotheses above remain.
