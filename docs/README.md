# Documentation

How the fork works:

- [Architecture](architecture.md): components, threads, events, state
  ownership, known risks and the refactor plan. Start here.
- [Navigation](navigation.md): `space --navigate`, pacing, relative requests,
  focus and raise.
- [Effects](effects.md): the snapshot crossfade, window fades and the opacity
  policy.
- [Scripting addition](osax.md): the Dock payload, its protocol and the
  macOS 27 lookups.

How it is checked:

- [Testing](testing.md): CTest presets, sanitizers, fuzzing, analyzer and the
  live checks with presented frames.
- [Performance](performance.md): measurements of navigation and effects,
  release by release.

History:

- [Reports](reports/): what each investigated release showed live (lcs.11,
  lcs.12, lcs.22 to lcs.24).
- [Research](research/): Apple's Desktop motion and the APIs behind it.
