# Documenting the public API

How the doc comments in `src/BlaeckSerial.h` are written, and why.

The audience is someone new to the library reading a hover in an editor, not
someone reading the header top to bottom. Most of these rules follow from that:
a hover is read in isolation, out of order, at the moment somebody is deciding
what to type next.

**What these rules govern:** the doc comment attached to a public name — what an
editor shows on hover. Not every comment in the file. The `BLAECK_ENABLE_*`
documentation at the top of the header is read by someone configuring a build, not
writing a sketch, so it names the frames a switch removes and is right to; the same
goes for comments inside private members and function bodies. Rule 3 in particular
would strip information those readers need.

Drawn from four style guides that agree more than they differ — [Go Doc
Comments](https://go.dev/doc/comment), the [Rust API
Guidelines](https://rust-lang.github.io/api-guidelines/documentation.html),
[Javadoc](https://www.oracle.com/technical-resources/articles/java/javadoc-tool.html),
and the [Arduino Language Reference](https://docs.arduino.cc/language-reference/)
— with the differences settled in favour of Arduino, since that is who reads this.

## The rules

**1. The first sentence stands alone.** It is what autocomplete shows and where
most readers stop. No clause of context before it.

**2. Verb first, third person, no "you".** *"Sends every signal's current value"*,
not *"Send the signals"* or *"You can send"*. Booleans borrow Go's phrasing:
*"Reports whether…"*.

A field is not an action, so it says what the value is: *"The board this firmware
runs on"*, not *"Names the board this firmware runs on"*. Go does the same for
variables — *"Version is the Unicode edition from which the tables are derived."*

**3. No wire format.** No frame codes, no byte layout, not even the word "frame".
Say what happens and what breaks if the order is wrong. Whoever implements a host
reads the [protocol spec](https://sebajost.github.io/blaeck-protocol/); whoever
reads this header is writing a sketch.

`<BLAECK.WRITE_DATA>` and friends are an exception — a user types those into
Serial Monitor, so they are interface, not encoding.

**And no host by name.** Say "a host", not "Home Assistant". It is one consumer,
reached through a bridge, and naming it makes the library sound like it serves only
that — while tying the docs to someone else's release schedule. A `@warning` that
read *"stops accepting it in 2027.4"* had to be rewritten the moment that date
moved. Where behaviour genuinely varies, "a host may" is both shorter and true.

There is one distinction worth drawing, and the library already draws it: a host
that **records** values needs only names and types, while one that **presents**
them also reads what the device declares about itself. `withUnit`, `withIcon`,
`withDeviceClass`, `withStateClass`, `diagnostic` and their neighbours mean
something only to the second kind — which is why `BLAECK_ENABLE_SIGNAL_META`
can remove all of them and everything else keeps working. Say so where it matters,
rather than implying every host cares.

What not to claim is **which program enforces a rule**. A device class from the
wrong list may be refused by whatever reads the frames or by whatever that feeds,
and a sketch sees one thing either way: the entity never appears. So describe the
effect, not the actor — "the entity never appears" is observable and stays true,
where "Loggbok validates it" is a guess about someone else's code unless you have
opened it. The term is defined above `class BlaeckSerial`.

**4. Numbers, not adjectives.** Ranges, caps, defaults, per-board differences,
costs. *"up to 255"*, *"about 25 bytes"*, *"24 on a Mega, 8 on an Uno"* — never
*"a reasonable number"*. A number you cannot support is worse than none: check it
or leave it out.

**5. Every public field gets its own comment, with its default.** One comment over
a group shows the group's text whichever member is hovered, which is how
`DeviceName` ended up documented as "set these variables".

**6. Hazards go last, in their own paragraph.** Silent failures especially — the
ones with no crash and no error, where the only symptom is something quietly not
happening.

**7. Every public name gets a `@code` block.** A signature says what a call looks
like; only a worked call says why you would make one. See [`@code` blocks](#code-blocks)
below.

**8. What it does for the caller, not how it works inside.** Cost is the licensed
exception, and on a microcontroller it is often the point: SRAM, flash, blocking,
allocation.

**9. Description first, parameters after.** All four guides put the summary first;
none lead with parameters. Name them inside the sentence where that reads
naturally, and break them out only when there are three or more, or when one
carries a constraint that would otherwise be guesswork.

**10. Entry points explain the concept; everything else stays tight.** `begin()`,
`addSignal()`, `onXCommand()`, `tick()` are where a beginner lands. Define by
contrast — a beginner's question is not "what is a signal" but "which of the three
do I want", and one clause of contrast answers it where a definition does not.

**11. A reference may add to a doc, never carry it.** *"As `tick()`, with a
messageID"* is fine — it still means something alone. *"Not copied, as DeviceName
is not"* is not: it sends the reader somewhere else to find out what the warning
was about.

**12. Prominence tracks likelihood.** `@warning` for what the common path can
reach. `@note` for what only an unusual one can. A lifetime rule that every quoted
literal satisfies is a note, however true it is.

**13. A blank line before `@code`.** Prose stops, code starts; it reads as a change
of register only when it is set apart.

**14. Say what it buys before what it costs.** Where a design was chosen, explaining
it only through its risks makes it read as a defect nobody fixed. The shared
event-type table was documented as something that accumulates — true, and entirely
downside, when the reason it exists is that a channel with two types costs two slots
instead of reserving room for the largest.

Only where there *is* a tradeoff. A plain limit — a capacity clamped at 255 — buys
nothing, and inventing an upside for it would be worse than saying nothing.

**15. Name a method in prose as a call.** `writeState(channelName)` or `tick()` —
never "the channel form of writeState", never "the state writer". Written as a call,
with no space before the parenthesis, the name is checked against the header on every
build, so a rename that leaves the prose behind fails instead of shipping.

This is what makes it safe to point a `@warning` at another method at all, and the
pointing is usually the useful part: *"use writeState(channelName)"* saves a reader
the search that *"send it as a number instead"* would cost them. `@code` blocks are
compiled and cannot rot; before this check, prose was the one place in the header a
rename could quietly survive.

A name that is not a call is invisible to the checker — inside `@code`, in a
parenthetical like *"a float (32-bit)"*, or spelled out in words.

**16. State a hazard once, on the call that can commit it.** *"A value on a channel
that was never declared is dropped"* was written on `addStateChannel`, where the
reader is already declaring one; on `writeState`, where the mistake is actually made;
and again in that method's `@param`. Three hovers for one fact, and the two on the
wrong name teach nothing — nobody hovers `addStateChannel` wondering whether to skip
it.

An `@code` block usually settles the ordering on its own. Every warning here that
names a prerequisite — `deleteSignals` needing `writeSymbols()`, `onNumberCommand`
needing `withRange()` — has a block directly beneath it that shows the two calls in
order. Where the block already demonstrates it, the warning is repeating what the
next two lines show.

What a block cannot show is a path it does not take: a three-line example of
`writeState()` has no room for "and this vanishes if you skipped setup()". So the
ordering facts stay — once each, on the call that drops the value.

## Format

Doxygen `/*!` blocks, in the Adafruit house style, because it renders structured
in hover and can generate a reference site later.

Order: `@brief` → prose → `@param` → `@return` → `@note` / `@warning` → `@code`.

Ceremony scales with the call. Full slots for entry points and anything taking
three or more parameters; `@brief` plus `@code` for a one-line getter. The Arduino
reference is not uniform either — short entries are short.

```cpp
  /*!
    @brief   Copies the option at a given position from a select command's list.

    Lets a sketch show what is selected without keeping its own copy of the names.

    @param   command  Name the select command was registered with.
    @param   index    Position in the list given to withOptions(), counting from 0.
    @param   out      Buffer the name is copied into. Left empty unless true is returned.
    @param   outSize  Size of that buffer, terminator included.
    @return  True if the name was copied. False if the command is not a select, the
             index is past the end of the list, or the name would not fit.
    @note    A name too long for the buffer is refused rather than shortened: a
             truncated name would not match any option the device declared.

    @code
      char name[12];
      Blaeck.getSelectOptionNameAt("SET_WAVE", waveIndex, name, sizeof(name));
    @endcode
  */
```

### Overloads

Share one comment where they differ only in the type of an argument — there is
nothing different to say. Give one each where they differ in what they *do*,
writing the second as a delta from the first.

`tick()` is what settled this: it shared a comment with `tick(messageID)`, so
hovering the no-arg form a sketch calls in `loop()` explained a parameter it does
not take.

## `@code` blocks

Every one is extracted and compiled, so a block calling a method that has since been
renamed fails the build instead of being shown on hover as instructions that do not
work. This is the only reason rule 7 is affordable; Rust requires one on every public
item for the same reason, and checks them the same way.

The generated sketch carries `#line` directives, so a failure names the header and
the line of the offending block:

```
src/BlaeckSerial.h:1969:8: error: 'class BlaeckSerial' has no member named ...
```

which matters because the generated sketch is gitignored — without them, the error
would point at the one file a contributor does not have.

That imposes three things:

- **Complete statements, not fragments of a chain.** Each block becomes a
  function body. A dangling `.withRange(...)` cannot compile, and would not teach
  much anyway.
- **Draw on the shared vocabulary.** `Temperature`, `Frequency`, `waveIndex`,
  `readSensor()` and the rest live in `extras/tests/DocCodeBlocks/preamble.h`. Reach for an
  existing name before adding one: blocks that all speak of `Temperature` teach the
  library faster than blocks that each invent a cast of characters.
- **Call the instance `Blaeck`.** Never `BlaeckSerial` — a global variable with the
  same name as its type switches off IntelliSense for everything derived from it
  ([vscode-cpptools#4251](https://github.com/microsoft/vscode-cpptools/issues/4251)).
  The sketches under `examples/` were renamed for this; the docs have to agree with
  them.

A block that shows a handler is written as a whole function — that is the natural
shape for a command callback, and the extractor emits it at file scope. `void loop()`
is welcome too; it is renamed on the way into the generated sketch so several blocks
can use it.

The folder and its `.ino` share a name because `arduino-cli` requires it, so renaming
one means renaming both. `preamble.h` is reached by an `#include` resolved against
that sketch, so getting it half right fails the build with an error pointing at the
include rather than at the rename.
Moving it means updating `DEFAULT_EXTRACT` in `checkdocs.py`, `.gitignore`, and the
workflow that compiles it.

## Checking

```
python extras/scripts/checkdocs.py src/BlaeckSerial.h              # what is undocumented
python extras/scripts/checkdocs.py src/BlaeckSerial.h --show tick  # what a hover will show
python extras/scripts/checkdocs.py src/BlaeckSerial.h --extract    # every block -> extras/tests/DocCodeBlocks/DocCodeBlocks.ino
```

`--show` reads `Cursor.raw_comment`, the same attachment clangd hovers, so a doc can
be checked against what an editor will display without opening one.

When something in `preamble.h` breaks, every block fails at once and the same error
repeats once per block — which reads as one cause, not many, because each names its
own line in the header. To see only the first:

```
arduino-cli compile --fqbn arduino:avr:mega --build-property compiler.cpp.extra_flags=-fmax-errors=1 extras/tests/DocCodeBlocks
```

CI runs the first and builds the third.

Four things fail a build: a public name with no comment, one with no `@code` block, a
block that does not compile, and prose naming a method the header does not declare.
The rest are reported and counted - a section divider standing in for a comment, a
comment too short to say anything, and a missing blank line before `@code`.

The fourth reads names out of the parse, not out of the file's text. Deriving them
from the source with a regex matches the mentions inside the comments too, so every
reference declares itself and the check passes on anything - which it did, silently,
until a deliberately broken name was fed to it. Names from `preamble.h` count as
declared: prose naming `readSensor()` points at the blocks' cast, not at the library.

Rule 7 became a gate once the header met it. It was only counted while 84 names
lacked a block, because a red build nobody can fix is a red build everybody learns to
ignore.

What they cannot catch: whether any of it is **true**. Every rule above is about
form. Accuracy comes from reading the implementation before writing the comment,
and several docs in this header were wrong on the first attempt in ways no checker
would have flagged.
