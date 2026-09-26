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
| `src/sa.m` | yabai side: install, load, handshake and the request senders. |

The payload and loader are embedded in the yabai binary. `yabai --load-sa`
(as root) installs them to `/Library/ScriptingAdditions/yabai.osax` when the
installed version differs from `OSAX_VERSION` and restarts Dock; otherwise it
runs the loader and validates the handshake. A Dock restart drops the payload,
so the usual yabairc runs `sudo yabai --load-sa` from a `dock_did_restart`
signal.

## Protocol

The socket is `/tmp/yabai-sa_$USER.socket`, mode 0600. Each connection carries
one request: an `int16_t` length, then an opcode byte and its arguments packed
without padding. The length covers the opcode and arguments and must be below
`SA_SOCKET_BUFF_LEN`. Handlers stop at the received length and reject counts
that do not fit in it.

The handshake reply is `OSAX_VERSION`, a NUL, and a `uint32_t` of attribute
bits, one per lookup that succeeded:

| Bit | Lookup | Needed for |
| --- | --- | --- |
| `0x01` | `dock_spaces`: the Dock spaces controller, via an adrp+add pair | every space operation |
| `0x02` | `dppm`: DPDesktopPictureManager | moving spaces (removed in macOS 27) |
| `0x04` | `add_space` function | creating spaces |
| `0x08` | `remove_space` function | destroying spaces |
| `0x10` | `move_space` function | moving spaces |
| `0x20` | `set_front_window` function | nothing: the caller in `window_manager.c` is compiled out |
| `0x40` | space switch animation instruction, patched to a zero duration | instant space switching |

On macOS 27.2 the handshake reports `0x4D`: spaces can be focused, created and
destroyed, and animations removed, but not moved. `--load-sa` requires the
lookups available on that macOS version: on macOS 27 it accepts `0x4D` without
requiring the unavailable moving-space and front-window lookups.

## Lookups

Each lookup has a start offset and a pattern per macOS major version. The
search starts at the Dock image base plus the offset and takes the first match
in the following `PATTERN_SEARCH_WINDOW` (0x1286a0) bytes. Pattern tokens are
two hex digits separated by spaces; a token starting with `?` matches any byte.
A getter returning a `NULL` pattern disables that lookup for the version.

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
   tools/osax/macho.py callers 'addSpace' < dock0.s     # functions sending a selector
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
   `build/debug/tools/sa_client handshake` for the attribute bits. `sa_client
   spaces` lists space ids; `sa_client focus|create|destroy <sid>` exercises the
   space handlers without yabai.
7. **SkyLight.** `window.c` queries window sub-levels with a raw MIG message
   whose id changes between releases. `window_sub_level` finds the reply id
   `SLSGetWindowSubLevel` checks for and verifies the request id (reply id
   minus 100) on on-screen windows.

## Versioning

`OSAX_VERSION` is compared for equality only. This fork suffixes it with
`-lcs.<n>` and bumps `<n>` for every payload change. When a merge brings a new
upstream version, the result is `<upstream>-lcs.1`.
