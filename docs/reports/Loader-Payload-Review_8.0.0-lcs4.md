# Loader and payload review

Date: 2026-10-02. Base: `505a0e0` (`v8.0.0-lcs.4`). Reviewed the uncommitted
`lcs-code/osax-audit` tree, including its loader, root-run install/load path,
signature requirement and changed Dock payload. This is a source and offline
review; no scripting addition was installed or loaded into Dock.
The findings below describe that pre-correction snapshot.

## Verified findings

| Severity | Location | Failure scenario and evidence |
| --- | --- | --- |
| Medium | `src/osax/loader.m:123,256-284` | `result` begins at zero and remains zero when all ten `thread_get_state` polls miss the sentinel. The loader terminates the remote thread and exits successfully. A stubbed Mach probe returned `status=0, polls=10` with no sentinel. The sentinel itself only proves that the first remote thread reached the marker after starting a second thread; the second thread's `dlopen` result is discarded by the shell code (`loader.m:81-90`). A zero loader status therefore does not prove a loaded, answering payload. |
| Medium | `src/sa/sa.m:119-134,196-230` | Installation ignores all four `system()` results from `chmod` and `codesign`, restarts Dock, and returns zero. An offline install probe forced all four commands to fail and observed `status=0, failed_commands=4, restart_requests=1`. A binary can be left unexecutable or unusable even though installation reports success. |
| Medium | `src/sa/sa.m:430-457` | A reinstall returns the install status immediately after requesting Dock's restart; it has not loaded or handshaken with the new payload. If `scripting_addition_set_socket_path()` fails after injection, `result` remains zero and skips validation. A successful `--load-sa` invocation therefore has three different meanings depending on the path taken. |
| Medium | `src/sa/sa.m:261-295,311-340` | `--load-sa` trusts any listener at the payload socket. An offline listener sent the expected version and attribute bits and the request accepted it. While Dock is not listening, a process of the login user can occupy that pathname and make validation report a payload that is not in Dock; it can also choose a version that requests a Dock restart. This is a correctness and availability exposure within the same-user trust model, not a demonstrated privilege escalation. The bounded parser fixes the earlier out-of-bounds read, but does not authenticate the listener. |
| Low | `src/sa/sa.m:268-288` and `src/osax/payload.m:1113-1159` | Both receive timeouts apply to each blocking read, not the complete message. A local listener delivering seven reply bytes 850 ms apart held the handshake for 5.98 s and was accepted despite `SA_HANDSHAKE_TIMEOUT_SECONDS=5`. A payload request delivered four bytes 600 ms apart was accepted after 2.42 s despite the one-second read timeout. A trusted peer can occupy Dock's single payload connection thread much longer than one second. |
| Low | `src/osax/payload.m:762-779` | The new scale guard rejects zero target height, but does not check finite, representable frame dimensions or the computed height before converting it to `int`. With a stubbed `SLSGetWindowBounds` returning width 800 and infinite height, UBSan reports `inf is outside the range of representable values of type 'int'` at line 766. A sufficiently extreme finite ratio can also overflow the conversion. Whether WindowServer can supply such bounds in a live session is unverified. |
| Low | `src/osax/loader.m:137-284` | After obtaining the Dock task port, several failure returns leave that send right and any allocated remote stack/code behind. The normal sentinel and timeout paths also never release them. A stubbed Mach probe observed two allocations and zero deallocations. The arm64 conversion failure left an allocated remote thread unterminated. Remote memory cannot simply be freed immediately after the sentinel: the spawned thread may still be executing the code or reading the embedded path. Cleanup needs an explicit lifetime protocol. |
| Low | `src/osax/payload.m:895-934` | The two proxy swap handlers can commit a partial transaction after a malformed request supplies fewer pairs than `count` claims. `count` is checked against remaining individual words, then `try_unpack` breaks inside the transaction loop. A synthetic two-entry request containing only one pair still changed the first window's alpha. The ordinary sender packs complete requests, so the observed risk is malformed authenticated input. |

## Conditional risks and open questions

- `src/sa/sa.m:93-116,167-178,375-380` checks the installed version and
  requirement bytes but not the ownership, modes, or integrity of the loader
  and payload it later executes as root. A root-owned, non-writable bundle
  protects the normal path. If those bundle directories become writable by
  another user, `--load-sa` can execute a replaced loader; checking the final requirement
  file with `O_NOFOLLOW` does not authenticate every parent component.
- `src/sa/sa.m:123-133` runs unqualified `chmod` and `codesign` via `system()`.
  The path arguments are fixed and contain no shell metacharacters, but the
  executables are resolved through the root process's `PATH`. The impact
  depends on the environment admitted by the sudo policy; no hostile `PATH`
  was used on the host.
- `src/sa/sa.m:144-153` accepts numeric prefixes such as `501junk` in
  `SUDO_UID` because `sscanf("%u")` does not require end-of-string. A missing
  or invalid value also reaches the zero-status validation skip noted above.
  Sudo normally supplies this variable; the malformed-environment case was
  not observed in a live load.
- `src/sa/sa.m:196-234` has no interprocess lock or atomic publication of a
  newly installed bundle. Two root `--load-sa` processes can both decide to
  reinstall and interleave removal, directory creation, file writes, and Dock
  restarts. This is a source-level race hypothesis; no concurrent live run was
  made.
- `src/osax/payload.m:1118-1129` reads the two-byte length with one `read`.
  A stream read may return only one byte; a synthetic fragmented header was
  rejected even though its peer remained connected. The daemon normally sends
  the complete framed request in one call, so practical frequency is unknown.

## Checks that held

- The changed reply path uses `MSG_NOSIGNAL`; the focused closed-peer test
  passed. The handshake parser rejects a reply lacking a NUL or attribute
  bytes, and reads attributes only within received bytes.
- The pattern scanner now checks that its entire candidate match is inside
  `__TEXT,__text`. The offline `osax_patterns` test passed on the current
  Dock image. This does not establish that decoded targets are semantically
  correct on every supported macOS release.
- The requirement installer creates the requirement file with `O_EXCL` and
  `O_NOFOLLOW`; its reader checks regular-file type, root ownership, mode and
  a bounded size.
- A fresh AppleClang Debug build completed and CTest passed 16/16 with the
  tests' expected access to `/tmp`. Targeted
  ASan/UBSan probes reproduced the findings above. None of these checks
  verifies installation, Dock behavior, or the signed production artifact.

The accompanying correction makes loader and install failures nonzero, binds
the handshake to the signed Dock process, applies overall protocol deadlines,
and rejects non-finite geometry and incomplete proxy swaps. An installation
lock serializes root operations, but bundle replacement remains in place and
remote code remains allocated while its spawned thread may use it. Live
installation and Dock validation remain separate acceptance checks.
