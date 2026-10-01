# Documentation

How the fork works:

- [Architecture](ARCHITECTURE.md): components, threads, events, state
  ownership, known risks and the refactor plan. Start here.
- [Navigation](NAVIGATION.md): `space --navigate`, pacing, relative requests,
  focus and raise.
- [Effects](EFFECTS.md): the snapshot crossfade, window fades and the opacity
  policy.
- [Scripting addition](OSAX.md): the Dock payload, its protocol and the
  macOS 27 lookups.

How it is checked:

- [Testing](TESTING.md): CTest presets, sanitizers, fuzzing, analyzer and the
  live checks with presented frames.
- [Performance](PERFORMANCE.md): measurements of navigation and effects,
  release by release.

Design:

- [Version 8 delivery plan](design/VERSION-8-DELIVERY-PLAN.md): integration
  sequence, ownership, reliability work and acceptance gates for version 8.
- [Effects engine proposals](design/EFFECTS-ENGINE-PROPOSALS.md): renderer
  alternatives and the evaluation plan.

Research:

- [Desktop motion](research/DESKTOP-MOTION.md): Apple's Desktop motion and the
  APIs behind it.

## Reports

[Reports](reports/) record what each investigated release or candidate showed
live. Files are named `Title-Words_VERSION-lcsN.md`, where the version is the
release the report examines or the candidate it led to. Raw measurements that
support a report are kept in [reports/data](reports/data/).

Version 7.1.25 (lcs.11 to lcs.32):

- [Opacity conflict](reports/Opacity-Conflict_7.1.25-lcs11.md)
- [Title-bar flash](reports/Title-Bar-Flash_7.1.25-lcs12.md)
- [Space crossfade](reports/Space-Crossfade_7.1.25-lcs22.md)
- [Snapshot crossfade](reports/Snapshot-Crossfade_7.1.25-lcs23.md)
- [Snapshot continuity](reports/Snapshot-Continuity_7.1.25-lcs24.md)
- [WindowServer capture retention](reports/WindowServer-Capture-Retention_7.1.25-lcs32.md)

Version 8.0.0-lcs.1:

- [Capture validation plan](reports/Capture-Validation-Plan_8.0.0-lcs1.md)
- [Crossfade capture backend](reports/Crossfade-Capture-Backend_8.0.0-lcs1.md)
- [Capture live results](reports/Capture-Live-Results_8.0.0-lcs1.md)
- [Capture decision data](reports/Capture-Decision-Data_8.0.0-lcs1.md)
- [Capture extended results](reports/Capture-Extended-Results_8.0.0-lcs1.md)
- [Effects buffer/GPU spike](reports/Effects-Buffer-GPU-Spike_8.0.0-lcs1.md)
- [Effects lifetime hardening](reports/Effects-Lifetime-Hardening_8.0.0-lcs1.md)

Version 8.0.0-lcs.2:

- [Effects GPU review](reports/Effects-GPU-Review_8.0.0-lcs2.md)
