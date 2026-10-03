# Development and releases

How a change goes from a branch to a signed release, and how versions are
numbered.

## Versions

yabai follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).
A release is `MAJOR.MINOR.PATCH`, tagged `vMAJOR.MINOR.PATCH`:

| Change | Bump | Example |
|---|---|---|
| A command, setting or behaviour removed or changed so that existing configuration or bindings break | MAJOR | `9.0.0` |
| A command, setting or behaviour added; a default changed that configuration can restore | MINOR | `8.1.0` |
| A fix that changes no interface | PATCH | `8.0.1` |

A candidate that needs live testing before its release is a pre-release of
that version, `-rc.1`, `-rc.2` and so on (`v8.1.0-rc.1`). CI publishes it as a
pre-release, and the Dotfiles updater never selects it.

`MAJOR`, `MINOR` and `PATCH` in `src/yabai.c` give the version `yabai --version`
prints and the manual link of `yabai --help`. They always hold the version
being prepared; a pre-release tag adds its suffix only to the tag.

Up to 8.0.0 releases were tagged `v<version>-lcs.<n>`. Under SemVer those are
pre-releases of `<version>`, so `8.0.0` follows `8.0.0-lcs.5`. Reports and
changelog entries of those releases keep their names.

The scripting-addition payload has a version of its own, `OSAX_VERSION`, which
changes only with the payload; see [scripting addition](OSAX.md#versioning).

## Branches

- `dev` integrates every change and carries the release tags. Every push and
  pull request to it runs CI: Release build and tests, ASan/UBSan, the TSan
  subset, the analyzer and both fuzz targets.
- `master` mirrors the original yabai repository. Changes by its author are
  reviewed there and ported to `dev` when worth it.
- Work happens on a short branch from `dev`, named after its kind and subject:
  `feat/`, `fix/`, `perf/`, `refactor/`, `docs/`, `test/`, `chore/`, for
  example `feat/navigation-effect-settings`.
- A release is prepared on `release/<version>`.

## A change

1. Branch from `dev`.
2. Commit in small steps that each build and pass their tests:
   `type(scope): Capitalized summary.` followed by one-line bullets. Code and
   its tests go together; documentation and the changelog may follow in a
   `docs` commit.
3. Add the change under `[Unreleased]` in [CHANGELOG.md](../CHANGELOG.md),
   under Added, Changed, Fixed or Removed, written for someone who uses yabai.
4. Run the local gates (see [testing](TESTING.md)):

   ```sh
   cmake --preset debug && cmake --build --preset debug && ctest --preset debug
   cmake --preset sanitize && cmake --build --preset sanitize && ctest --preset sanitize
   cmake --preset release && cmake --build --preset release && ctest --preset release
   tools/analyze.sh
   ```

   Concurrency changes also run the TSan subset, and parser changes the fuzz
   preset, as CI does.
5. Open a pull request to `dev`. It merges with a merge commit
   (`type(scope): Merge ...`) once CI passes and the review is done.

A change to the manual's source, `doc/yabai.asciidoc`, regenerates `doc/yabai.1`
in the same commit:

```sh
asciidoctor -b manpage doc/yabai.asciidoc -o doc/yabai.1
```

## A release

1. Decide the version from `[Unreleased]` with the table above.
2. Branch `release/<version>` from `dev`. In one commit,
   `chore(release): Prepare version <version>.`:
   - set `MAJOR`, `MINOR` and `PATCH` in `src/yabai.c`;
   - rename `[Unreleased]` to `[<version>] - <date>`, open a new empty
     `[Unreleased]`, and add a "Verification and limits" section that says what
     was and was not checked live;
   - point the `[Unreleased]` link at `v<version>...HEAD` and add the
     `[<version>]` comparison link.
3. Merge it into `dev` (`chore(release): Merge version <version> preparation.`)
   after CI passes.
4. Tag the merge commit with an annotated tag and push it:

   ```sh
   git tag -a v<version> -m "yabai <version>"
   git push origin v<version>
   ```

   CI signs the binaries with the project certificate, checks the signature
   and that `yabai --version` matches the tag, and publishes the tarball with
   its SHA-256 and Nix hash.
5. In the Dotfiles, `scripts/update-yabai.sh --apply` pins the release in
   `darwin/yabai-package.nix`; that commit also carries any change to the
   bindings or `yabairc` the release needs. The system switch that installs
   it is the maintainer's.

A fix to a release that `dev` has moved past branches from its tag, is released
as the next PATCH from `release/<version>`, and is merged back into `dev`.
