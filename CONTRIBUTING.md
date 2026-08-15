# Contributing to BlaeckSerial

First, thank you for taking the time to contribute to this project.

You can submit changes via GitHub Pull Requests.

Please:

1. Document every public name you add. The rules are in
   [extras/API-STYLE.md](extras/API-STYLE.md), and CI fails on a public name with
   no comment.
2. Give it an example. Examples are extracted from the header and compiled, so one
   naming a method that no longer exists breaks the build rather than shipping as
   instructions that do not work.
3. Call the object `Blaeck` in examples and doc comments, never `BlaeckSerial` —
   a variable sharing its type's name switches off autocomplete in VS Code. The
   README explains it under *Instantiate BlaeckSerial*.
4. Keep changes in step with
   [BlaeckTCP](https://github.com/sebaJoSt/BlaeckTCP) where they touch the shared
   API or the wire format. The two libraries send byte-identical frames.

## Checking your work

```
python extras/checkdocs.py src/BlaeckSerial.h                     # undocumented public names
python extras/checkdocs.py src/BlaeckSerial.h --show tick         # what a hover will show
python extras/checkdocs.py src/BlaeckSerial.h --extract out.ino   # every example, as a sketch
arduino-cli compile --fqbn arduino:avr:mega examples/Basic
```

CI runs the first, compiles the third, and builds every example for AVR, ESP32 and
SAMD — so a local build is only needed to answer a specific question.

## Layout

| | |
|---|---|
| `src/` | the library. The only folder compiled into a sketch |
| `examples/` | sketches shown under *File → Examples* in the IDE |
| `extras/` | the style guide, the doc checker, and the preamble its extracted examples build against. Ignored by the Arduino tools, and installed alongside the library |

Sources are CRLF. The Arduino Library Manager strips dot-prefixed paths from what
it distributes, so `.github/` and friends never reach a user; everything else does.
