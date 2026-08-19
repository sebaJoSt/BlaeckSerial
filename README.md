<a href="url"><img src="https://user-images.githubusercontent.com/388152/185908831-4eccf7a6-5f43-405d-b7fe-5225eeba302d.png" height="75"></a>
<a href="url"><img src="https://user-images.githubusercontent.com/388152/186109775-c7f1bb61-4cc0-4dc1-9969-49c2f2e1303f.png"  alt="BlaeckSerial Logo SeeSaw Font" height="70"></a>
===



BlaeckSerial is a simple Arduino library to send binary (sensor) data via Serial port to your PC using the [Blaeck protocol](https://sebajost.github.io/blaeck-protocol/). The data can be sent periodically or requested on demand with [serial commands](#blaeckserial-commands).  
Also included is a message parser which reads input in the syntax of `<HelloWorld, 12, 47>`. You can register exact command handlers (`onCommand`) and a catch-all handler (`onAnyCommand`) in your sketch.

## Getting Started

Clone this repository into `Arduino/Libraries` or use the built-in Arduino IDE Library manager to install
a copy of this library. You can find more detail about installing libraries 
[here, on Arduino's website](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-installing-a-library).

(Open Basic Example under `File -> Examples -> BlaeckSerial` for reference)

```CPP
#include <Arduino.h>
#include <BlaeckSerial.h>
```
### Instantiate BlaeckSerial
```CPP
BlaeckSerial Blaeck;
```

Name the object something other than `BlaeckSerial`. Giving a global variable the
same name as its type is legal C++ and compiles everywhere, but VS Code's IntelliSense
then reads the type as the variable and stops offering members, so `Blaeck.addSignal(...)`
gets no autocomplete and shows errors on calls that build fine
([vscode-cpptools#4251](https://github.com/microsoft/vscode-cpptools/issues/4251), open
since 2019 and closed as not planned). The examples all use `Blaeck`.

Sketches that already name the object `BlaeckSerial` keep working - this costs nothing
but editor support, and renaming the variable is the whole fix.

### Initialize Serial & BlaeckSerial
```CPP
void setup()
{
  Serial.begin(115200);

  Blaeck.begin(
    &Serial,   //Serial reference
    2          //Maxmimal signal count used;
  );
}
```

### Add signals
```CPP
 Blaeck.addSignal("Test Signal 1", &someGlobalVariable);
 Blaeck.addSignal("Test Signal 2", &anotherGlobalVariable);
```

If more signals are added than the configured capacity in `begin(...)`, extra signals are ignored.
You can detect this in your sketch:
```CPP
if (Blaeck.hasRejectedSignals()) {
  Serial.print("Ignored signals: ");
  Serial.println(Blaeck.getRejectedSignalCount());
}
```

On AVR, wrapping a name in `F()` keeps it in flash and the signal stores a 2-byte pointer
instead of a copy — worth doing on an Uno or Nano with many signals:
```CPP
 Blaeck.addSignal(F("Test Signal 1"), &someGlobalVariable);
```
A run of signals that share a prefix and end in a number needs neither: `withNameSuffix()`
keeps the prefix in flash and produces the digits when the name is sent, so the names cost
no SRAM at all. The suffix is 0–255, and `0` is a number like any other:
```CPP
 for (int i = 0; i < 8; i++) {
   Blaeck.addSignal(F("Sine_"), &sine[i]).withNameSuffix(i + 1);
 }
```
Any other name built at runtime cannot use `F()`; pass the buffer and it is copied, so the
buffer may be reused straight away:
```CPP
 char signalName[10];
 for (int i = 0; i < 8; i++) {
   snprintf(signalName, sizeof(signalName), "Sine_%d", i + 1);
   Blaeck.addSignal(signalName, &sine[i]);
 }
```


### Describe how a signal is shown

`addSignal()` returns a handle carrying optional presentation metadata, which a host
reads from the `0xF0` Signal Config frame:

```CPP
 Blaeck.addSignal("Temperature", &Temperature)
     .withUnit(F("\xC2\xB0" "C"))
     .withDeviceClass(F("temperature"))
     .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
     .withDisplayPrecision(1);
```

Every call is optional and the order does not matter. A signal that describes nothing
gets no entry in the frame and costs nothing on the wire.

| Method | | Numeric | Text | Bool |
|--------|--|:-:|:-:|:-:|
| `withUnit(F("Hz"))` | Symbol shown after the value. Non-ASCII must be UTF-8. Numeric only: a unit tells Home Assistant the state is a number | ● | | |
| `withDeviceClass(F("temperature"))` | What the value measures | ● | ● | ● |
| `withIcon(F("mdi:sine-wave"))` | Material Design Icons name | ● | ● | ● |
| `withStateClass(...)` | `BLAECK_STATE_CLASS_MEASUREMENT`, `_TOTAL` or `_TOTAL_INCREASING`. Left out means no statistics are kept | ● | | |
| `withDisplayPrecision(1)` | Decimal places. `0` means show it as an integer | ● | | |
| `diagnostic()` | Describes the device rather than what it measures | ● | ● | ● |
| `disabledByDefault()` | Registered, but switched off until someone enables it | ● | ● | ● |
| `forceUpdate()` | Report every reading, even one identical to the last | ● | ● | ● |
| `withOptions(F("a,b,c"))` | The closed set of values reported. Home Assistant needs `withDeviceClass(F("enum"))` alongside it, and every value reported must be in the list | | ● | |
| `withDisplayName(F("Output"))` | Label shown in place of the name. The name still identifies the signal — the symbol list, a logged column, and anything a host builds from those | ● | ● | ● |

The three booleans take an argument, so `diagnostic(isDebugBuild)` works as well.

Each datatype returns its own handle, so a modifier that cannot mean anything for that signal
does not compile: a string has no decimals and no statistics, and a bool becomes a
`binary_sensor`, which has no unit either.

Metadata is only paid for by the signals that have it: a described signal costs about 15
bytes of SRAM, one you say nothing about costs 2. Set `BLAECK_ENABLE_SIGNAL_META` to `0`
to drop even those: the methods still compile and store nothing, so no `#ifdef` is needed.

### Update your variables and don't forget to `tick()`!
```CPP
void loop()
{
  UpdateYourVariables();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  Blaeck.tick();
}
```

## BlaeckSerial commands

See the [protocol documentation](https://sebajost.github.io/blaeck-protocol/protocol/commands) for the full list of serial commands and their parameters.

### Interval lock mode

By default, timed data is client-controlled (`BLAECK.ACTIVATE` / `BLAECK.DEACTIVATE`).
You can lock interval behavior from sketch code:

```CPP
// Fixed interval lock: always send every 500 ms, ignore ACTIVATE/DEACTIVATE
Blaeck.setIntervalMs(500);

// Off lock: disable timed data and ignore ACTIVATE
Blaeck.setIntervalMs(BLAECK_INTERVAL_OFF);

// Back to client control (default behavior)
Blaeck.setIntervalMs(BLAECK_INTERVAL_CLIENT);
```

`setTimedData(...)` has been removed. Use `setIntervalMs(...)` instead.

### Command handler API

Available callbacks:
- `onCommand(...)` and `onAnyCommand(...)` for parsed incoming commands
- `setBeforeWriteCallback(...)` before data is written

Command parser defaults are architecture-aware:
- AVR (`__AVR__`): 48 command chars, 6 registered handlers (12 on larger-SRAM AVR such as the Mega 2560), 24 command-name chars, 10 params
- Non-AVR: 96 command chars, 12 registered handlers, 40 command-name chars, 10 params

See [Configuration](#configuration) to change them.

```CPP
void onSwitchLED(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount < 1) return;
  int state = atoi(params[0]);
  digitalWrite(LED_BUILTIN, state == 1 ? HIGH : LOW);
}

void onAny(const char *command, const char *const *params, byte paramCount)
{
  // Optional catch-all hook
}

void setup()
{
  // ...
  Blaeck.onCommand("SwitchLED", onSwitchLED);
  Blaeck.onAnyCommand(onAny);
}
```

### Describe what a command controls

The typed helpers register the same way, and return a handle describing the control so a host
can build an entity for it — a number with a range, a select with its options, a text field
with a length limit:

```CPP
 Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
     .withRange(0.0f, 2.0f, 0.01f)
     .withUnit(F("Hz"))
     .withStateFromSignal(F("Frequency"));

 Blaeck.onSelectCommand("SET_WAVE", onSetWave)
     .withOptions(F("Sine,Square,Triangle,Sawtooth"))
     .withStateFromSignal(F("WaveName"));

 Blaeck.onTextCommand("SET_LABEL", onSetLabel)
     .withMaxLength(sizeof(DeviceLabel) - 1)
     .withStateFromSignal(F("DeviceLabel"))
     .config();
```

The kind is the helper's name because it decides the entity; most modifiers are optional. Two are
not: a number is bounded by definition and a select *is* its option list, so `onNumberCommand()`
and `onSelectCommand()` return a handle offering only `withRange()` (resp. `withOptions()`), and
the rest of the chain opens once that is called. Ordering it differently does not compile. The
`step` is part of `withRange()` for the same reason — a host's default is 1, so omitting it would
quietly turn a range in tenths into an integer one; pass `0` to leave the resolution to the host on
purpose. Each helper returns the handle for its own kind, so `.withRange(...)` on a text command
does not compile rather than quietly doing nothing.

| | Number | Switch | Select | Button | Text |
|---|:-:|:-:|:-:|:-:|:-:|
| `withRange(min, max, step)` — required | ● | | | | |
| `withOptions(F("A,B,C"))` — required | | | ● | | |
| `withUnit(F("Hz"))` | ● | | | | |
| `withMode(BLAECK_NUMBER_MODE_BOX)` | ● | | | | |
| `withDeviceClass(F("temperature"))` | ● | ● | | ● | |
| `withMaxLength(32)` | | | | | ● |
| `withStateFromSignal(F("Name"))` | ● | ● | ● | ● | ● |
| `withOwnState(F("Name"), getter)` | ● | ● | ● | ● | ● |
| `withDisplayName(F("Frequency"))` | ● | ● | ● | ● | ● |
| `config()` / `diagnostic()` | ● | ● | ● | ● | ● |

Both make a dashboard show what the device holds rather than what was last sent. They differ in
where that value lives: `withStateFromSignal` names a signal you already added, so the value is
logged alongside what it controls, while `withOwnState` creates a state channel the command owns,
which is not logged. Naming versus creating is why only one of the two takes a source argument.

`withDisplayName` labels the control something other than its name. A command name is an
identifier — the host sends `SET_FREQ` back to invoke it — so it is written to be matched rather
than read, and a display name is how the screen says "Frequency" while the wire keeps saying
`SET_FREQ`.

`withMode` asks for a typed box or a dragged slider. It is a hint about entry and not about
validity — the range is what bounds the value — so leaving it out is the right answer for most
controls and lets the host choose. Say it where one form is clearly wrong: a setpoint read to two
decimals is not reachable by dragging, and a slider card computes `min + n*step` in floating point,
so it hands back 21.200000000000003 for a step of 0.1.

`withDeviceClass` names what the control acts on, and the vocabulary is the host's, not ours — a
different one per kind. A number takes a measurable quantity (`temperature`, `pressure`, `power`,
and some fifty more), a switch takes only `outlet` or `switch`, a button only `restart`, `identify`
or `update`. A select and a text field take none, which is why the modifier is not offered there.

On a number it does more than pick an icon: for the classes a host knows how to convert, the value
is shown in the reader's own units. That conversion runs both ways and never reaches the device —
a control declared in Celsius is typed in Fahrenheit and arrives back in Celsius — so the range
stays expressed in the unit the sketch declared and `withRange` keeps meaning what it says. Declare
the matching `withUnit` alongside it, or the host converts from an assumption.

All metadata strings must be `F()` literals: they are stored as pointers, never copied.

Set `BLAECK_ENABLE_COMMAND_META` to `0` to drop the metadata: the modifiers still compile and
store nothing, so the typed helpers behave exactly like `onCommand(...)`.

### Buffered writes

On boards with UART-to-USB bridges (e.g. **Arduino Uno R4 WiFi**), rapid
individual `Serial.write()` calls can cause bytes to be dropped.  BlaeckSerial
can assemble entire frames in RAM and send them with a single write.

| Board family | Default |
| ------------ | ------- |
| AVR (Uno, Mega, Nano) | OFF (saves SRAM) |
| Everything else (R4 WiFi, ESP32, ARM, …) | **ON** |

Override at runtime:

```CPP
Blaeck.setBufferedWrites(true);   // force ON
Blaeck.setBufferedWrites(false);  // force OFF

Serial.println(Blaeck.isBufferedWrites() ? "ON" : "OFF");
```

## Configuration

How many signals, state channels, event channels, event types and commands a
device holds is set in the sketch, on the `begin()` chain — see
[Table sizes](#table-sizes) below. What remains here are the compile-time
settings: the command parser's buffer size, the buffered-writes default, and the
`BLAECK_ENABLE_*` switches (`BLAECK_ENABLE_COMMAND_META`,
`BLAECK_ENABLE_SIGNAL_META`, `BLAECK_ENABLE_STATE_CHANNELS`,
`BLAECK_ENABLE_EVENTS`), which remove code rather than slots and so reclaim
flash. All are plain `#define`s with `#ifndef` guards, so any value you define
first wins.

> [!IMPORTANT]
> An override **must reach every translation unit** — your sketch *and*
> `Blaeck.cpp`. These values size members of `class BlaeckSerial`, so a
> setting seen by only one of them gives the class two different layouts (an ODR
> violation) and corrupts memory silently. It is all-or-nothing, never half.
>
> In particular, **do not** `#define` them at the top of your `.ino` — that
> reaches your sketch only, never the library.

### PlatformIO

```ini
build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
```

Reaches every unit. Nothing else to do.

### Arduino IDE / arduino-cli

A `BlaeckSerialConfig.h` in your sketch folder is **not** found by default: the
sketch folder is not on the compiler's include path, so the `__has_include` in
`BlaeckSerial.h` finds nothing and your settings are silently ignored. There is
no IDE preference and no `sketch.yaml` key for compiler flags — only the core's
own config files can add them.

This is an Arduino build-system limitation, not a library one: a sketch-local
settings header was requested in 2015 ([arduino-builder#15](https://github.com/arduino/arduino-builder/issues/15),
closed) and libraries still cannot extend the include path
([arduino-cli#501](https://github.com/arduino/arduino-cli/issues/501), open since 2019).

Three ways round it:

**a) Build with arduino-cli.** Put `BlaeckSerialConfig.h` next to your `.ino`:

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

`{build.source.path}` expands to the sketch folder, so the config is found by
every unit. Nothing in your Arduino installation is touched and the setting
stays with the sketch — the only option your CI can reproduce exactly.

**b) Put the config inside the library**, at
`libraries/BlaeckSerial/src/BlaeckSerialConfig.h`. That folder is already on the
include path, so every unit sees it, in the IDE as well as the CLI. The catch is
that it belongs to the library, not the sketch: it applies to every sketch you
build, and a library update overwrites it.

**c) Stay in the IDE, per sketch.** Same sketch-folder layout as (a), plus a
`platform.local.txt` next to your core's `platform.txt` containing:

```
compiler.cpp.extra_flags=-I{build.source.path}
```

On Windows, for the AVR core, that file goes at
`C:\Users\<you>\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.8\platform.local.txt`
(macOS/Linux: `~/.arduino15/packages/…`, same tail). This is per core — repeat it
for esp32, samd, renesas_uno, … Without this file the config is silently ignored.

> [!NOTE]
> Use `compiler.cpp.extra_flags` rather than `build.extra_flags` here. 13 of the
> 27 boards in `arduino:avr` set their own `build.extra_flags={build.usb_flags}`
> (Leonardo, Micro, Yún and the other 32u4 boards), and a board-specific value
> overrides the platform-level one — so a `build.extra_flags` line would silently
> do nothing on those boards. No board overrides `compiler.cpp.extra_flags`.
>
> If you scope it per board with `boards.local.txt` instead (e.g.
> `mega.compiler.cpp.extra_flags=…`) and the board already defines the property
> you are setting, append to it rather than replacing it.

### Example config file

```CPP
// BlaeckSerialConfig.h
#define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
#define BLAECK_BUFFERED_WRITES_DEFAULT false
```

> [!NOTE]
> How many signals, state channels, event channels, event types or commands a
> device holds is not set here. Say it in the sketch, on the `begin()` chain:
>
> ```CPP
> Blaeck.begin(&Serial)
>     .withSignals(50)
>     .withStateChannels(12)
>     .withCommands(16);
> ```
>
> Every one of those calls is optional and starts from a per-board default, and
> a table is only allocated once something is added to it — so a table your
> sketch never uses costs nothing.

### Table sizes

`begin()` returns a handle that sizes each table, and every call on it is
optional:

```CPP
Blaeck.begin(&Serial);              // per-board defaults for everything
Blaeck.begin(&Serial, 20);          // shorthand for .withSignals(20)

Blaeck.begin(&Serial)
    .withSignals(50)
    .withStateChannels(12)
    .withEventChannels(6)
    .withEventTypes(20)
    .withCommands(16)
    .withDebugStream(&Serial1);
```

| | small AVR (≤2 kB SRAM) | Mega and larger AVR | ESP32, SAMD, RP2040, … |
|---|---|---|---|
| `withSignals` | 8 | 24 | 64 |
| `withStateChannels` | 3 | 8 | 32 |
| `withEventChannels` | 2 | 6 | 24 |
| `withEventTypes` | 8 | 20 | 64 |
| `withCommands` | 6 | 16 | 32 |

A table is allocated in full by the first entry added to it and never grows, so
raising a number costs SRAM whether or not the slots are filled — while a table
your sketch never touches costs nothing at all.

Once a table exists its size is fixed: a `with…()` call after the first entry
was added is refused and says so on the debug stream. Both forms above run
before any `add…()`, so either is safe.

What did not fit is reported. `withDebugStream()` gives the line naming the
entry that was dropped and the call that would have kept it. On a board with
only one `Serial`, end `setup()` with `printRejections(&Serial)` instead — one
summary line per table that dropped something, nothing at all when everything
fitted, and safe on the Blaeck stream itself because no frame has been written
yet:

```
BlaeckSerial dropped what it had no room for:
  3 signal(s) dropped, table holds 8 - begin(&Serial).withSignals(11)
```

Or ask directly: `hasRejections()`, or the per-table `hasRejectedSignals()`,
`hasRejectedCommands()`, `hasRejectedStateChannels()` and
`hasRejectedEventChannels()`.

## Protocol

Full protocol specification with version history: [sebajost.github.io/blaeck-protocol](https://sebajost.github.io/blaeck-protocol/blaeckserial/overview)

