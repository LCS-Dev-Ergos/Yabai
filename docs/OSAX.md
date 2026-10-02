# Scripting addition

The scripting addition (SA) lets yabai do what only Dock.app can: focus,
create, destroy and move spaces, and change window opacity, layer, shadow and
order. It is a payload injected into Dock that listens on a Unix socket for
requests from yabai. It depends on private Dock code located by byte patterns,
so most macOS releases break part of it.

## Components

| Path | Role |
| --- | --- |
| `src/osax/payload.m` | Dylib loaded into Dock. Locates Dock internals, serves the socket. |
| `src/osax/arm64_payload.m`, `x64_payload.m` | Per-architecture search offsets and patterns, selected by macOS version. |
| `src/osax/pattern.h` | The pattern search, shared by the payload and `tools/osax/pattern_check`. |
| `src/osax/loader.m` | Injects the payload into the running Dock. |
| `src/osax/common.h` | Socket path, `OSAX_VERSION`, attribute bits and opcodes shared with yabai. |
| `src/sa/sa.m` | yabai side: install, load, handshake and the request senders. |

The payload and loader are embedded in the yabai binary. `yabai --load-sa`
(as root) installs them to `/Library/ScriptingAdditions/yabai.osax` when the
installed version differs from `OSAX_VERSION` and restarts Dock; otherwise it
runs the loader and validates the handshake. Installation or a requested Dock
restart returns a nonzero status because the new payload has not yet answered.
Run `sudo yabai --load-sa` again after Dock returns; only a verified handshake
returns zero. The usual yabairc runs it from a `dock_did_restart` signal.

## Protocol

The socket is `~/Library/Caches/yabai/payload.sock` in an owner-only directory.
The payload checks each peer's audit-token UID and Yabai's designated
code-signing requirement before reading a request. The requirement is saved
by the root installer in `Contents/Resources/daemon.requirement`; a changed
requirement causes a reinstall. Unsigned development builds require the
explicit `YABAI_ALLOW_UNSIGNED_LOCAL` build option. Each connection carries
one request: an `int16_t` length, then an opcode byte and its arguments packed
without padding. The length covers the opcode and arguments and must be below
`SA_SOCKET_BUFF_LEN`. Handlers stop at the received length and reject counts
that do not fit in it.

Every request is a new connection from the same daemon, which waits while the
payload checks it. The payload remembers the code-directory hashes of peers
that passed, as the daemon does for its clients, so a request costs a kernel
query instead of a signature check of about half a millisecond. The complete
framed request, header included, has one second to arrive, as long as the
daemon waits for an answer, so a peer that stops sending, or sends a byte at
a time, cannot hold the one thread that serves requests. A daemon that stopped
waiting before its connection was accepted fails the peer check, so its
request is not read.
Replies, the handshake and the status byte of an opacity batch, are sent with
`MSG_NOSIGNAL`: a daemon that stopped waiting has closed its end, and Dock
neither ignores nor catches `SIGPIPE`.

The handshake reply is `OSAX_VERSION`, a NUL, and a `uint32_t` of attribute
bits, one per lookup that succeeded:

| Bit | Lookup | Needed for |
| --- | --- | --- |
| `0x01` | `dock_spaces`: the Dock spaces controller, via an adrp+add pair | every space operation |
| `0x02` | `dppm`: DPDesktopPictureManager | moving spaces before macOS 27, which removed it |
| `0x04` | `add_space` function | creating spaces |
| `0x08` | `remove_space` function | destroying spaces |
| `0x10` | `move_space` function; since macOS 27, the Dock's handler for moved spaces | moving spaces |
| `0x20` | `set_front_window` function | nothing: the caller in `window_manager/focus.c` is compiled out |
| `0x40` | space switch animation instruction, patched to a zero duration | instant space switching |

On macOS 27.2 the handshake reports `0x5D`. `--load-sa` reads the reply until
the payload closes the connection, within one five-second deadline. It accepts
only the Apple-signed Dock process as the listener and refuses a reply without
the NUL or the four bytes after it. `--load-sa` requires the lookups
available on that macOS version: on macOS 27 it does not require `dppm` or
`set_front_window`.

## Moving spaces on macOS 27

Mission Control runs in WindowManager.app since macOS 27. It moves a space in
WindowServer, then reports the move to the Dock, whose handler updates the
Dock's spaces and tells WallpaperAgent which space became first on each
display. The payload does the same for a move request, on the Dock's main
queue where the Dock applies those reports:

1. `SLSMoveManagedSpaceToDisplayIndex` moves the space in WindowServer, to its
   position after the destination space among the display's other spaces. The
   request is dropped when the destination space is not on its display any
   more, or when the source display would keep no user space.
2. The `move_space` lookup is that handler, a Swift method of the Dock spaces
   controller. It takes the space id, the display UUID as a Swift `String`, and
   the space to insert after as an optional id. The payload calls it with
   `swiftcall`, passing the controller in `x20` through `swift_context`, and
   makes the string with `String(cString:)` from the Swift runtime.
3. When the handler returns false, the payload calls `refreshSpacesIfNeeded`
   on the controller, which rebuilds the Dock's spaces from WindowServer, as the
   Dock does when it cannot apply a report.

The same request reorders spaces on one display, which the handler supports.
Before a space visible on its display moves away, yabai passes a replacement
space from that display, which the payload shows there; the payload drops the
request when that space is not on the display.

## Lookups

Each lookup has a start offset and a pattern per macOS major version. The
search starts at the Dock image base plus the offset and takes the first match
in the following `PATTERN_SEARCH_WINDOW` (0x1286a0) bytes that ends within
`__TEXT,__text`: past the end of Dock's code a match would be no instruction.
Pattern tokens are two hex digits separated by spaces; a token starting with
`?` matches any byte. A getter returning a `NULL` pattern disables that lookup
for the version.

Since macOS 27, Dock ships two arm64e slices. The `arm64e.x1` slice (cpusubtype
12) starts functions with `pacibsppc` (`FE A7 C1 DA`) instead of `pacibsp`
(`7F 23 03 D5`), so function patterns wildcard their first instruction. A Mac
runs only one slice, so both are checked statically.

## Updating for a new macOS release

Configure and build a preset (`cmake --preset debug`, `cmake --build --preset
debug`). The tools land in `build/debug/tools/`.

1. **Check.** `ctest --preset debug` runs `pattern_check` against the local
   Dock with the patterns for the running version. Each lookup prints its
   offset per slice, `MISS`, or `FAIL` when a match is outside `__text` or a
   global does not resolve into a data section. `-v 26.6` selects another
   version's patterns; a path argument checks another Dock binary.
2. **Explore.** Extract the slices and disassemble them:

   ```sh
   tools/osax/macho.py extract $DOCK /tmp/dock
   llvm-objdump --macho -d /tmp/dock/Dock.0 | tools/osax/macho.py annotate $DOCK > dock0.s
   tools/osax/macho.py callers 'addSpace' < dock0.s      # functions sending a selector
   tools/osax/macho.py methods $DOCK | grep -i space     # Objective-C IMPs
   tools/osax/macho.py ivars $DOCK Spaces                # ivar offsets
   ```

   Swift classes appear under their mangled names, for example
   `_TtC8DockCore13DisplaySpaces`. Pass `--slice 1` for the second slice.
3. **Pattern.** Take enough bytes from the target to be unique, wildcarding
   branch targets, adrp pages and register-dependent immediates. Check it with
   `pattern_check -p <offset> '<pattern>'`, which prints the payload's match and
   every match in `__text` for each slice. Choose an offset before the target
   and within the window, leaving room for code growth.
4. **Update.** Add the version to the getters in `arm64_payload.m` and to
   `verify_os_version` in `payload.m`. If Dock methods the handlers call have
   changed, adapt the handlers too.
5. **Version.** Bump `OSAX_VERSION` (see below), or `--load-sa` keeps the old
   payload.
6. **Test live.** Install the new build, run `sudo yabai --load-sa`, then
   inspect the load result and query spaces with the signed `yabai` binary.
   The unsigned `sa_client` can exercise the payload handlers directly only
   in an explicit unsigned local development build; a signed payload rejects it.
7. **SkyLight.** `window.c` queries window sub-levels with a raw MIG message
   whose id changes between releases. `window_sub_level` finds the reply id
   `SLSGetWindowSubLevel` checks for and verifies the request id (reply id
   minus 100) on on-screen windows.

## Versioning

`OSAX_VERSION` is compared for equality only. This fork suffixes it with
`-lcs.<n>` and bumps `<n>` for every payload change. When a merge brings a new
upstream version, the result is `<upstream>-lcs.1`.
