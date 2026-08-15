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
- `extras/DocExamples/DocExamples.ino` is generated. It is gitignored; do not commit it
- Frame codes and byte layout belong in the
  [protocol spec](https://sebajost.github.io/blaeck-protocol/), not in the header.
  These doc comments describe what a sketch does

## Documenting the public API

Rules: [extras/API-STYLE.md](extras/API-STYLE.md). Every public name needs a doc
comment and an example, and CI fails without the comment.

```
python extras/scripts/checkdocs.py src/BlaeckSerial.h                    # undocumented names
python extras/scripts/checkdocs.py src/BlaeckSerial.h --show tick        # what a hover shows
python extras/scripts/checkdocs.py src/BlaeckSerial.h --extract                 # @code blocks, as a sketch
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

## Related

[BlaeckTCP](https://github.com/sebaJoSt/BlaeckTCP) is the same library over a
network and sends byte-identical frames. A change to the public API or the wire
usually needs to land there too.

<!-- CLAUDE.md is a one-line @AGENTS.md import, so Claude Code reads this file too.
     Keep the content here; that file exists only as a bridge. -->
