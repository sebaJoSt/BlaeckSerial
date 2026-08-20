# Configuration

Two things are set outside the code that uses them: how many entries each table holds, and the
compile-time switches that decide how much of the library is built at all.

## Table sizes

`begin()` returns a handle that sizes each table. Every call on it is optional:

```cpp
Blaeck.begin(&Serial);        // per-board defaults for everything
Blaeck.begin(&Serial, 20);    // shorthand for .withSignals(20)

Blaeck.begin(&Serial)
    .withSignals(50)
    .withStateChannels(12)
    .withEventChannels(6)
    .withEventTypes(20)
    .withCommands(16)
    .withDebugStream(&Serial1);
```

The defaults are sized for the board:

| | Small AVR (≤ 2 kB SRAM) | Mega and larger AVR | ESP32, SAMD, RP2040, … |
|---|---|---|---|
| `withSignals` | 8 | 24 | 64 |
| `withStateChannels` | 3 | 8 | 32 |
| `withEventChannels` | 2 | 6 | 24 |
| `withEventTypes` | 8 | 20 | 64 |
| `withCommands` | 6 | 16 | 32 |

What each entry costs on AVR: a signal 9 bytes, an event type 5, an event channel 10, a state
channel 26, a command 48. RAM is what you are really sizing against — a Mega's 8 kB is gone at
a few hundred of anything.

Two things spend a slot you might not count. A command that reports its own value with
`withOwnState()` takes a state channel as well as a command slot. Event types are one pool
shared by every channel, so `withEventTypes()` is the sum across channels.

A table is allocated in full by the first entry added to it, and never grows. So a table your
sketch never touches costs nothing, and raising a number costs SRAM whether or not the slots
are filled. Once a table exists its size is fixed: a `with…()` call after the first entry has
been added is refused and says so on the debug stream. Both forms above run before any `add…()`
call, so either is safe.

## What did not fit

`withDebugStream()` names a stream for the library to report on. It prints the entry that was
dropped and the call that would have kept it. It may be the same stream the data goes to.

On a board with only one `Serial`, end `setup()` with `printRejections()` instead. It prints
one line per table that dropped something, nothing at all when everything fitted, and is safe
on the Blaeck stream because no frame has been written yet:

```cpp
Blaeck.printRejections(&Serial);
```

```
BlaeckSerial dropped what it had no room for:
  3 signal(s) dropped, table holds 8 - begin(&Serial).withSignals(11)
```

Or ask in code: `hasRejections()` for any table, or `hasRejectedSignals()`,
`hasRejectedCommands()`, `hasRejectedStateChannels()` and `hasRejectedEventChannels()` for one.

## Naming the device

Three fields say what the device is. A host lists it by the name and groups its signals and
controls under it:

```cpp
Blaeck.DeviceName = "Waveform Generator";
Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
Blaeck.DeviceFWVersion = "1.0";
```

They default to `"Unknown"`, `"n/a"` and `"n/a"`. They are kept as pointers, not copied — a
quoted literal is always safe, and a name built at runtime has to live in a global buffer.

## Compile-time settings

These are `#define`s, because they size members of the library's own class or remove code
outright. There are two kinds.

Switches, which remove a feature and reclaim flash and SRAM with it:

| Define | Set to 0 to drop |
|---|---|
| `BLAECK_ENABLE_SIGNAL_META` | Everything a signal declares about itself. Costs ~9 bytes of SRAM per signal |
| `BLAECK_ENABLE_COMMAND_META` | Everything a command declares. The typed helpers then behave like plain `onCommand()` |
| `BLAECK_ENABLE_STATE_CHANNELS` | State channels entirely |
| `BLAECK_ENABLE_EVENTS` | Events entirely |

Nothing has to be wrapped in an `#ifdef` for these. The calls still compile and simply store
nothing, so the same sketch builds either way.

Values, which change a limit:

| Define | Default |
|---|---|
| `BLAECK_COMMAND_MAX_CHARS_DEFAULT` | 128, or 48 on a small AVR. The command parser's buffer, three of which are kept |
| `BLAECK_BUFFERED_WRITES_DEFAULT` | `false` on AVR and mbed cores, `true` elsewhere. Also settable at runtime with `setBufferedWrites()` |
| `BLAECK_STATE_MAX_OPTION_CHARS` | 24. Room for one resolved select option while a frame is built |

> **An override must reach every translation unit — your sketch *and* `BlaeckSerial.cpp`.**
> These values size members of `class BlaeckSerial`. A setting seen by only one of them gives
> the class two different layouts and corrupts memory silently. In particular, do not `#define`
> them at the top of your `.ino`: that reaches your sketch and never the library.

### PlatformIO

```ini
build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
```

Reaches every unit. Nothing else to do.

### Arduino IDE and arduino-cli

BlaeckSerial includes a `BlaeckSerialConfig.h` if it can find one. In the Arduino build it
cannot: the sketch folder is not on the compiler's include path, so a config file next to your
`.ino` is silently ignored. There is no IDE preference and no `sketch.yaml` key that adds
compiler flags — only the core's own files can.

This is a limitation of the Arduino build system, not of this library. A sketch-local settings
header was asked for in 2015 in
[arduino-builder#15](https://github.com/arduino/arduino-builder/issues/15), which was closed,
and libraries still cannot extend the include path
([arduino-cli#501](https://github.com/arduino/arduino-cli/issues/501), open since 2019).

Three ways round it.

**a) Build with arduino-cli.** Put the config next to your sketch:

```
MySketch/
  MySketch.ino
  BlaeckSerialConfig.h
```

```bash
arduino-cli compile --fqbn <board> \
  --build-property "compiler.cpp.extra_flags=-I{build.source.path}" \
  MySketch
```

`{build.source.path}` is the sketch folder, so every unit finds the config. Nothing in your
Arduino installation is touched and the setting travels with the sketch. This is the only one
your CI can reproduce exactly.

**b) Put the config inside the library**, at `libraries/BlaeckSerial/src/BlaeckSerialConfig.h`.
That folder is already on the include path, so every unit sees it, in the IDE as well as the
CLI. The catch is that it now belongs to the library: it applies to every sketch you build, and
a library update overwrites it.

**c) Stay in the IDE.** Same layout as (a), plus a `platform.local.txt` next to your core's
`platform.txt`:

```
compiler.cpp.extra_flags=-I{build.source.path}
```

On Windows, for the AVR core, that file goes at
`C:\Users\<you>\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.8\platform.local.txt`
(macOS and Linux: `~/.arduino15/packages/…`, same tail). It is per core, so repeat it for
esp32, samd, renesas_uno and any other you build for. Without this file the config is ignored
without a word.
