# AGENTS.md

Arduino library sending binary sensor data over Serial using the Blaeck protocol.

## Layout

- `src/` — the library. The only folder compiled into a sketch
- `examples/` — sketches listed under *File → Examples*
- `extras/` — the style guide and the doc tooling. Not compiled, but installed
  alongside the library, so keep it small

## Conventions

- Call the object `Blaeck`, never `BlaeckSerial`. A variable sharing its type's
  name switches off IntelliSense for every builder chain
  ([vscode-cpptools#4251](https://github.com/microsoft/vscode-cpptools/issues/4251))
- Sources are CRLF. Check after any scripted edit
- `extras/tests/DocCodeBlocks/DocCodeBlocks.ino` is generated. It is gitignored; do not commit it
- Frame codes and byte layout belong in the
  [protocol spec](https://sebajost.github.io/blaeck-protocol/), not in the header.
  These doc comments describe what a sketch does

## Documenting the public API

Rules: [extras/API-STYLE.md](extras/API-STYLE.md). Every public name needs a doc
comment and an example, and CI fails without the comment.

```
python extras/scripts/checkdocs.py src/BlaeckSerial.h              # undocumented names
python extras/scripts/checkdocs.py src/BlaeckSerial.h --show tick  # what a hover shows
python extras/scripts/checkdocs.py src/BlaeckSerial.h --extract    # every block -> extras/tests/DocCodeBlocks/DocCodeBlocks.ino
```

Examples in `@code` blocks are extracted and compiled by CI, so one naming a method
that no longer exists breaks the build.

## Building

```
arduino-cli compile --fqbn arduino:avr:mega examples/Basic
```

CI compiles every example for AVR, ESP32 and SAMD, so a local build is only needed
to answer a specific question. `examples/TimeStampModes` needs `RTC.h` and does not
build without it.

## Releasing

The two registries are triggered by different things, so a version number is not
just a label:

- **PlatformIO** watches the repository and publishes as soon as the `version` in
  `library.properties` changes on the branch. No tag, no `pio` install, nothing
  manual. Whatever is on the branch that day becomes a release for real users.
- **Arduino Library Manager** publishes from a git tag, and ignores the version
  field until then.

So bump the version as the last step before tagging, never while developing. Bumping
it early is how 7.0.0 reached PlatformIO users on 2026-08-08 mid-development, while
Arduino stayed on 6.0.1 — and why 7.0.0 cannot be reused for the real 7.0.0.

**The crawler can be switched off, once.** This library was registered in 2020, under
PlatformIO's old system, where a repository is polled a few hours after each push.
Publishing a single version by hand ends that permanently — PlatformIO's founder:
*"Auto-crawler is enabled for legacy libraries. [Once] you published the new version
manually, auto-crawling will be disabled forever."*

Do it from the local folder before pushing, or the crawler may take the version while
you work:

```
pio account login
# bump both manifests locally, do not push yet
pio pkg publish
# then push and tag — publishing is manual from here on
```

Published versions are listed at
[registry.platformio.org/libraries/sebajost/BlaeckSerial](https://registry.platformio.org/libraries/sebajost/BlaeckSerial).
Check it before choosing a number; several bumps were never tagged, so it holds
versions the repository does not.

## Related

[BlaeckTCP](https://github.com/sebaJoSt/BlaeckTCP) is the same library over a
network and sends byte-identical frames. A change to the public API or the wire
usually needs to land there too.

<!-- CLAUDE.md is a one-line @AGENTS.md import, so Claude Code reads this file too.
     Keep the content here; that file exists only as a bridge. -->
