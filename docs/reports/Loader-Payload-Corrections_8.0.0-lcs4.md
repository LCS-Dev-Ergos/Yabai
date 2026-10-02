# Loader and payload corrections

These changes address the findings in
`Loader-Payload-Review_8.0.0-lcs4.md` on top of `dev` at `505a0e0` and the
payload-request audit. The payload version advances to `2.1.31-lcs.16`.

## Corrected behavior

- The loader exits nonzero if its sentinel is not observed or terminating its
  remote starter thread fails. It releases local task/thread rights on exit,
  frees the remote stack after that thread terminates, and frees remote code
  when no remote thread started. It keeps code that a spawned pthread may
  still execute; a zero loader exit is followed by the root caller's
  authenticated payload handshake.
- The root installer fails if `chmod` or either ad-hoc signing step fails,
  uses absolute `/usr/bin/codesign` and `/bin/rm` without shell resolution,
  rejects a malformed `SUDO_UID`, checks root ownership, file type, executable
  bits and protected modes through the installed bundle, and locks
  installation/uninstallation across processes.
- `--load-sa` returns zero only after an expected-version, expected-attribute
  reply from the Apple-signed Dock process. A successful install or requested
  Dock restart returns nonzero and asks for a retry after Dock returns.
  The listener's audit-token PID must match the running Dock PID; its
  signature must satisfy `anchor apple and identifier "com.apple.dock"`.
- The payload and handshake use one monotonic deadline per complete message.
  The payload accepts a fragmented two-byte header within that deadline.
  Window scaling rejects non-finite or unrepresentable geometry before
  integer conversion or WindowServer calls. Both proxy swap handlers reject
  an incomplete list before creating a transaction, while retaining the
  sender's one-word zero sentinel.

The offline verification did not run `sudo yabai --load-sa`, install into
`/Library`, or terminate Dock.

## Offline evidence

- AppleClang Debug build completed; CTest passed 17/17, including new
  `loader_tests` with Mach mocks and the existing payload/daemon suite.
- Focused ASan/UBSan build passed `loader_tests`, `payload_tests` and
  `yabai_tests` (3/3). Regression cases cover the missing loader sentinel,
  failed setup and thread conversion, a failing signer, malformed UID,
  unauthorized same-user listener, slow handshake and request trickles,
  fragmented request header, non-finite window bounds and incomplete proxy
  swaps.
- The host Dock's designated requirement, read without loading it, is
  `identifier "com.apple.dock" and anchor apple`.

## Still open for review

- The injected pthread's `dlopen` return is not carried back by shell code.
  The authenticated handshake now provides the success check at the caller.
  Remote code is deliberately retained once a pthread may be executing it;
  reclaiming it requires a separate, proven lifetime protocol.
- The lock prevents two root installers from interleaving, but publication
  still replaces the installed bundle in place. An interrupted install can
  leave no usable bundle until the next `--load-sa` retry.
- Tests did not exercise a real Dock injection, macOS restart timing, the
  production signed artifact, or on-host permissions of an installed bundle.
  Those require separate live acceptance before integration.
