# Configuration

Two things are decided outside the code that uses them: how many entries each table holds, and
which parts of the library are built at all.

## Table sizes

Everything you register lives in a table with a fixed number of slots. `begin()` returns a
handle that sizes them, and every call on it is optional:

```cpp
void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(50)
      .withStateChannels(12)
      .withEventChannels(6)
      .withEventTypes(20)
      .withCommands(16)
      .withDebugStream(&Serial1);
}
```

`Blaeck.begin(&Serial)` alone gives every table the default for the board, and
`Blaeck.begin(&Serial, 20)` is shorthand for `.withSignals(20)`.

The defaults:

| | Small AVR (2 kB SRAM or less) | Mega and larger AVR | ESP32, SAMD, RP2040, ... |
|---|---|---|---|
| `withSignals` | 8 | 24 | 64 |
| `withStateChannels` | 3 | 8 | 32 |
| `withEventChannels` | 2 | 6 | 24 |
| `withEventTypes` | 8 | 20 | 64 |
| `withCommands` | 6 | 16 | 32 |

RAM is what you are sizing against, not the entry count. On AVR a signal costs 9 bytes, an
event type 5, an event channel 10, a state channel 26, and a command 48 - the largest there is.
A Mega's 8 kB is gone at a few hundred of anything, where an ESP32 has room for thousands.

Two slots are easy to miss. A command that reports its own value with `withOwnState()` takes a
state channel as well as a command slot. Event types share one table across every channel, so
`withEventTypes()` is the sum, not the largest.

A table is allocated in full by the first entry added to it, and never grows. So a table your
sketch never touches costs nothing, and raising a number costs SRAM whether or not you fill the
slots. Once a table exists its size is fixed: a `with...()` call after the first entry has been
added is refused and says so. Put the whole chain in `setup()` before any `add...()` call and
that cannot happen.

## Finding out what did not fit

An entry that has no slot is dropped. Your sketch still runs, still logs, and is simply missing
a signal or a control - which is why it is worth asking.

`withDebugStream()` names a stream for the library to report on. It prints each entry as it is
dropped, with the call that would have kept it. It may be the same stream the data goes to.

On a board with only one `Serial`, end `setup()` with this instead:

```cpp
Blaeck.printRejections(&Serial);
```

It prints one line per table that dropped something, and nothing at all when everything fitted:

```
BlaeckSerial dropped what it had no room for:
  3 signal(s) dropped, table holds 8 - begin(&Serial).withSignals(11)
```

It is safe on the Blaeck stream because no data has been written yet at the end of `setup()`.

To ask in code: `hasRejections()` for any table at all, or `hasRejectedSignals()`,
`hasRejectedCommands()`, `hasRejectedStateChannels()` and `hasRejectedEventChannels()` for one.

## Naming the device

Three fields say what the device is. A host lists it by the name, and groups its signals and
controls under it:

```cpp
Blaeck.DeviceName = "Waveform Generator";
Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
Blaeck.DeviceFWVersion = "1.0";
```

They start as `"Unknown"`, `"n/a"` and `"n/a"`. They are kept as pointers rather than copied, so
a quoted literal is always safe and a name built at runtime has to live in a global buffer.

## Compile-time settings

The rest are `#define`s. Four of them switch a feature off, which reclaims the flash and SRAM it
would have cost:

| Define | Set to 0 to drop |
|---|---|
| `BLAECK_ENABLE_SIGNAL_META` | Everything a signal declares about itself. Saves about 9 bytes of SRAM per signal |
| `BLAECK_ENABLE_COMMAND_META` | Everything a command declares. The typed helpers then behave like plain `onCommand()` |
| `BLAECK_ENABLE_STATE_CHANNELS` | State channels |
| `BLAECK_ENABLE_EVENTS` | Events |

Your sketch needs no `#ifdef` around any of it. The calls still compile and simply store
nothing, so the same sketch builds either way.

Three more change a limit:

| Define | Default |
|---|---|
| `BLAECK_COMMAND_MAX_CHARS_DEFAULT` | 128, or 48 on a small AVR. The command parser's buffer, and the library keeps three of them |
| `BLAECK_BUFFERED_WRITES_DEFAULT` | `false` on AVR and the mbed cores, `true` elsewhere. Also settable while running with `setBufferedWrites()` |
| `BLAECK_STATE_MAX_OPTION_CHARS` | 24. Room for one resolved select option while a frame is built |

> [!IMPORTANT]
> An override has to reach **both** your sketch and `BlaeckSerial.cpp`. These values size
> members of `class BlaeckSerial`. A setting seen by only one of the two gives the class two
> different layouts, and that corrupts memory without a word about it.
>
> So do not `#define` them at the top of your `.ino`. That reaches your sketch and never the
> library.

### PlatformIO

```ini
build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
```

This reaches every file. Nothing else to do.

### Arduino IDE and arduino-cli

BlaeckSerial includes a `BlaeckSerialConfig.h` if it can find one. In an Arduino build it
cannot: your sketch folder is not on the compiler's include path, so a config file next to your
`.ino` is ignored without a word. No IDE preference and no `sketch.yaml` key adds compiler
flags - only the core's own files can.

This is the Arduino build system rather than this library. A sketch-local settings header was
asked for in 2015 in
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

`{build.source.path}` is your sketch folder, so every file finds the config. Nothing in your
Arduino installation is touched and the setting travels with the sketch. This is the only one
your CI can reproduce exactly.

**b) Put the config in the library**, at `libraries/BlaeckSerial/src/BlaeckSerialConfig.h`. That
folder is already on the include path, so every file sees it, in the IDE as well as the CLI. The
catch is that it now belongs to the library: it applies to every sketch you build, and updating
the library overwrites it.

**c) Stay in the IDE.** Same sketch folder as (a), plus a `platform.local.txt` next to your
core's `platform.txt`, containing:

```
compiler.cpp.extra_flags=-I{build.source.path}
```

On Windows, for the AVR core, that file goes at
`C:\Users\<you>\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.8\platform.local.txt`.
On macOS and Linux it is `~/.arduino15/packages/...`, same tail. It is per core, so repeat it
for esp32, samd, renesas_uno and anything else you build for.
