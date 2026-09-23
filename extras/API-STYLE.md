# Writing comments

How comments in `src/` are written. There are two kinds, for two readers.

## Doc comments on public names

This is what an editor shows when someone hovers a call. The reader is writing a
sketch, has probably not read the rest of the header, and wants to know what to type.

- **Start with what the call does**, in one plain sentence: *"Sends every signal's
  current value."* That sentence is what autocomplete shows, so it has to stand
  alone. A field says what it holds: *"The board this firmware runs on."*
- **Then add only what the signature doesn't tell you.** That usually means limits,
  defaults, what it costs in RAM or time when that matters on a small board, and what
  goes wrong if it is used wrongly. Stop there. A getter needs a line and an example.
- **Write for a sketch, not for a host.** No frame codes or byte layouts; those are in
  the [protocol spec](https://sebajost.github.io/blaeck-protocol/). Built-in commands
  like `<BLAECK.WRITE_DATA>` are fine, since people type them. Say "a host" rather than
  naming a program.
- **Put a warning on the call where the mistake happens**, and only there. Something
  every call depends on, such as `begin()` coming first, is said once on `begin()`.
- **Numbers have to be right.** If you give a size, measure it. A template declared
  without a body makes the compiler print a `sizeof`, even for a board you can't run:

  ```cpp
  template <int N> struct Show;
  Show<sizeof(blaeck_detail::StateChannelEntry)> a;   // error: 'Show<26> a' has incomplete type
  ```

- **Name other methods as calls**, like `writeState(channelName)` or `tick()`. The
  checker matches those against the header, so a rename can't leave stale prose behind.

### Format

Doxygen `/*!` blocks, in this order: `@brief`, a short paragraph if needed, `@param`
and `@return` where they add something, `@note` or `@warning`, a blank line, then
`@code`.

```cpp
  /*!
    @brief   Copies the name of a select command's option at a given position.

    @param   command  The name the select command was registered with.
    @param   index    Position in the withOptions() list, starting at 0.
    @param   out      Where the name is copied. Left empty if this returns false.
    @param   outSize  Size of out, including the terminator.
    @return  False if the command is not a select, the index is past the end, or the
             name doesn't fit. A name is never cut short.

    @code
      char name[12];
      Blaeck.getSelectOptionNameAt("SET_WAVE", waveIndex, name, sizeof(name));
    @endcode
  */
```

Overloads that differ only in an argument's type share one comment. Overloads that
do different things get one each.

### `@code` blocks

Every public name needs one, and CI compiles them all, so an example can't go stale
without failing the build. Each block becomes the body of a function, which means:

- Write complete statements, not a loose `.withRange(...)`.
- Use the shared names in `extras/tests/DocCodeBlocks/preamble.h` (`Temperature`,
  `readSensor()` and so on) before adding new ones.
- Call the instance `Blaeck`, never `BlaeckSerial`. A variable named like its type
  breaks IntelliSense for every chained call
  ([vscode-cpptools#4251](https://github.com/microsoft/vscode-cpptools/issues/4251)).
- A handler can be written as a whole function, and so can `void loop()`.

## Other comments

Comments inside functions, on private members, and in the `.cpp`. The reader is
changing the library.

- **Only where the code doesn't explain itself.** Say why, not what.
- **Keep it to a sentence or two.** How a bug was found, what was tried first and
  what was rejected all belong in the commit message.
- **Don't copy values or layouts the code or the spec already has.** They go stale.
  A pointer to the spec page is enough.

The `BLAECK_ENABLE_*` switches at the top of the header are read by someone
configuring a build. Those comments may say which frames a switch removes.

## Checking

```
python extras/scripts/checkdocs.py src/BlaeckSerial.h              # undocumented names
python extras/scripts/checkdocs.py src/BlaeckSerial.h --show tick  # what a hover shows
python extras/scripts/checkdocs.py src/BlaeckSerial.h --extract    # every block -> extras/tests/DocCodeBlocks/DocCodeBlocks.ino
```

The build fails on a public name with no comment, a comment with no `@code` block, a
block that doesn't compile, or prose naming a method the header doesn't declare.
Sentences that appear on more than one name are listed but don't fail; a property
repeated on sibling fields is fine, an instruction repeated in several places usually
isn't.

The checker only looks at form. Whether a comment is true comes from reading the code
it describes.
