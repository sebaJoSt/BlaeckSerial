# Changelog

All notable changes to this project will be documented in this file.

## [7.0.0] - 2026-08-08

This release makes a device self-describing. Alongside the signals it always declared,
a board now declares its commands, plus state and event channels (both new), so a
host (e.g. Loggbok) can turn the lot into Home Assistant MQTT auto-discovery — a sensor
per signal, a control per command, a text sensor per channel, an entity per event —
without being configured for that board in advance.

### Breaking
- **Table sizes moved from build flags to the sketch.** `begin()` returns a handle
  that sizes every table: `begin(&Serial).withSignals(50).withStateChannels(12)
  .withEventChannels(6).withEventTypes(20).withCommands(16)`. Each call is optional
  and starts from a per-board default, so `begin(&Serial)` alone is enough for most
  sketches and `begin(&Serial, 20)` still means twenty signals.
  The 6.x macros that sized these tables are gone: `BLAECK_COMMAND_MAX_HANDLERS_DEFAULT`,
  `BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT` and `BLAECK_COMMAND_MAX_PARAMS_DEFAULT`.
  A number in the sketch cannot disagree with itself the way a build flag seen by
  only one translation unit could. `BLAECK_COMMAND_MAX_CHARS_DEFAULT`,
  `BLAECK_BUFFERED_WRITES_DEFAULT` and `BLAECK_ENABLE_*` stay — they size a buffer or
  reclaim flash, which no runtime number can.

- **A table costs nothing until it is used.** Each table is allocated by the first
  entry added to it rather than reserved up front, so a sketch that declares no
  state channels, no event channels and no commands pays for none of them: 503
  bytes of SRAM back on an Uno. Because of that, the linker's "global variables"
  figure no longer counts these tables.

- **`begin(Stream *, unsigned int, Stream *debug)` removed.** Name the debug stream
  on the chain instead: `begin(&Serial).withDebugStream(&Serial1)`.

- **I2C master/slave support removed.** All I2C master/slave functionality
  has been removed: `beginMaster(...)` / `beginSlave(...)`, the `MasterSlaveConfig`
  modes, slave discovery/scanning, per-signal `prefixSlaveID`, the `@<slaveID>:`
  command-routing prefix, master-side command-catalog aggregation, and the
  `<Wire.h>` dependency. BlaeckSerial is now single-board only.
  The on-the-wire frame layout is unchanged — the per-record master/slave-config
  and slave-ID bytes are still emitted (always `0`), so existing Blaeck hosts
  (e.g. Loggbok) need no changes.

- **Command parameters are no longer trimmed.** A leading space is part of the value, so
  `<SET_LABEL, hi>` sets `" hi"` rather than `"hi"`. Write `<SET_LABEL,hi>` to send the
  value alone. Most commands are unaffected in practice — a number or a select index
  parses the same either way, and an untyped `onCommand` handler simply receives the space
  — but a switch compares its value exactly, so `<SET_ENABLE, 1>` is rejected where
  `<SET_ENABLE,1>` is accepted.

- **`String` is gone from the API; names are `const char *`.** Every overload that took a
  signal name as a `String` — `addSignal`, `write`, `update`, `findSignalIndex`,
  `markSignalUpdated`, `setSignalName` — now takes a `const char *`, as do the
  `DeviceName`, `DeviceHWVersion` and `DeviceFWVersion` members. Each of them only ever
  called `.c_str()` on its argument and copied the result, so the `String` bought nothing
  and cost a heap allocation, a copy and a free on every single call.
  Sketches that pass string literals — every example in this library, unmodified — need
  no change. A sketch holding a `String` passes `name.c_str()`, which saves it the
  allocation too. Worth 850–1050 bytes of flash for a typical sketch and about 3.1 KB for
  one that writes many signals by name, plus 12 bytes of SRAM and roughly 22 bytes of heap.
  A name is still copied, so a `char` buffer built with `snprintf` can be reused the
  moment the call returns, exactly as before. The three device members are the exception:
  they are not copied, so whatever they point at must outlive them — a string literal does.

### Added
- **`withNameSuffix(n)` ends a signal's name in a number.**
  `addSignal(F("Sine_"), &v).withNameSuffix(i + 1)` names a signal `Sine_1` without
  storing the name anywhere: the prefix stays in flash and the digits are produced when
  the name is sent. A hundred such signals save about a kilobyte of heap against building
  the names with `snprintf`, which copies each one. The suffix is 0–255, and `0` is a
  number like any other rather than "no suffix". It works on a copied name too, though
  there it saves only the digits. Available on every signal handle, so it composes with
  the metadata calls in any order.
- **`addSignal(F("Name"), &value)`.** Eleven new overloads that take the name as a flash
  string. The name then stays in flash and the signal keeps a 2-byte pointer to it instead
  of a copy, which is most of what a signal costs in SRAM on AVR: about 18 bytes per signal
  for a 12-character name, against 3 for the same name passed as a plain literal. Existing
  `addSignal(F("..."), ...)` calls — which worked before by building a `String` — pick the
  new overload automatically and get the saving without an edit. A name built at runtime
  still uses the existing overloads and is still copied, so a reused `char` buffer works
  exactly as before.
- **`printRejections(&Serial)`.** One summary of everything the tables had no room
  for, naming the `begin()` call that would have kept it, and printing nothing when
  all of it fitted — so it can end `setup()` unconditionally. For boards with a
  single `Serial`, where `withDebugStream()` has nowhere to go. `hasRejections()`
  asks the same question without printing.
- **Typed commands (`0xA0` / `0xA5`).** `onNumberCommand`, `onSwitchCommand`,
  `onSelectCommand`, `onTextCommand` and `onButtonCommand` register a command
  together with what it accepts — range, step, unit, options, text length — so the
  device describes its own controls. They join `onCommand` / `onAnyCommand` from
  6.0.0, which stay for commands that carry no metadata. 
  Requires `BLAECK_ENABLE_COMMAND_META`.
- **Correlation ids on commands.** A sender may prefix a command with `#42:` —
  `<#42:SET_AMP,0.9>` — and the device echoes that number in the ack's message-id
  header, so a host can tell which command an ack answers when several of the same
  name are outstanding. Hand-typed commands are unaffected: no prefix, no change.
  The ack's frame hash covers the command as written, after the prefix, so it still
  verifies the bytes while the id does the pairing. A name may not begin with `#`
  or `@`; such a name is refused at registration rather than left unreachable.
- **State channels (`0x90` / `0x95`).** `addStateChannel(channelName[, value])` declares a
  channel that carries the current value of something — text, bool, or any numeric type,
  the overload settling the type and with it the handle (`withUnit()`/`withStateClass()`/
  `withDisplayPrecision()` on a numeric one, `withStateText()`/`withOptions()` on a text
  one) — and `writeState(channelName[, text])` reports it. Pass a pointer and the library
  reads that variable whenever the value is wanted, exactly as `addSignal()` does, so a
  host polling the catalog gets the truth without anything having been pushed. Channels are
  declared up-front; values on undeclared channels are dropped, and a state is never stored
  as signal data. Size the table with `begin(&Serial).withStateChannels(n)`.
  Requires `BLAECK_ENABLE_STATE_CHANNELS`.
- **Event channels (`0x80` / `0x85`).** `addEventChannel(channelName[, icon[,
  diagnostic[, eventTypes]]])` declares a channel and the closed set of events it may
  report — `F("idle_warning,resumed")`, position defining each index — or
  `addEventType(channelName, F("..."))` adds them one at a time, for a list built
  conditionally; `writeEvent(channelName, F("..."))` reports one occurrence. An event carries no text, 
  so its wording is fixed at compile time — use a state channel for anything with a runtime value.
  Events on undeclared channels or types are dropped. Size the tables with
  `begin(&Serial).withEventChannels(n).withEventTypes(n)` (types share one pool across
  channels, so no channel needs sizing for the worst case).
  Requires `BLAECK_ENABLE_EVENTS`.
- **String signals (`addSignal(name, char *value)`).** New `Blaeck_string` data type
  for textual values (labels, states, small JSON). The value lives in a user-owned
  buffer read live on each transmit: `write(name/index, char *value)` repoints the
  buffer and transmits that one signal, or update the buffer in place and let the
  periodic transmit pick it up. Up to 255 bytes.
- **`withDisplayName(F("Output"))` on a signal.** A label a host shows in place of the name,
  for when the name is doing a second job. A signal's name is what a host that logs calls its
  stored column, so an author writes `F("Output [V]")` to keep the table self-describing — and
  then reads the unit twice on screen. Presentation only: the name still identifies the signal
  everywhere, so declaring a display name on one already deployed relabels it and moves nothing.
  Optional in the `0xF0` frame (signal meta flags bit 11), so a host that does not read it is
  unaffected. Typed commands take it too.
- New `WaveformGenerator` example: one fully controllable waveform, exercising
  typed commands, state channels, event channels and string signals together.

### Changed
- **`F("")` now means "not declared".** `withUnit`, `withIcon` and `withDeviceClass` stored an
  empty literal and announced it as a blank; it is now read the same as leaving the call out,
  so a value built conditionally can say "nothing" without the sketch branching around the
  call. It also keeps a blank device class off a host that validates the field against a
  fixed list and would reject the entity outright.
- **A blank options list is refused on a signal too.** `withOptions(F(""))` — or a list with a
  blank field in it — was stored and announced, leaving a host a closed set it cannot offer or
  report. It is now refused where it is declared and said so on the debug stream, which is what
  a select command already did; the check is written once and shared by both.
- **A signal no longer keeps its name in a `String`.** The entry holds a pointer the
  library owns (or the flash address, for an `F()` name), which takes a signal entry from
  22 bytes to 19 on AVR and removes a heap allocation per signal per `0xB0` frame — the
  catalog writer used to copy the whole entry, `String` and all, once per signal. A name
  is still copied unless it came from `F()`.
- **A signal only pays for metadata once it has some.** `unit`, `device class`, `icon`,
  `options`, the flags word and the display precision used to sit in every signal entry
  whether or not the sketch set them; they now live in a record allocated by the first
  `with*()` call that describes the signal. A signal entry goes from 19 bytes to 10 on
  AVR, so a sketch that describes nothing — the common case — saves 9 bytes per signal,
  while a fully described signal costs about 6 bytes more than before. The 0xF0 Signal
  Metadata frame and the public API are unchanged. If the heap cannot hold a record the
  description is dropped and the signal itself keeps working; `printRejections()` says
  how many.
- **A signal entry is 9 bytes on AVR, down from 19.** Metadata moved out (above), the
  datatype enum is pinned to a byte instead of the `int` a compiler picks by default, the
  two flags a signal carries share a byte with the name-suffix bit, and the suffix itself
  takes the ninth. The datatype change shrinks a state channel entry too. Nothing about it
  is visible from a sketch: the enumerators, the wire format and the schema hash are
  unchanged.
- **An incoming frame is parsed once instead of twice.** `read()` ran two parsers over the
  same bytes: the 6.x one filling `COMMAND`, `PARAMETER[]` and `STRING_01`, and the 7.0 one
  filling the token pointers the registered handlers use. The built-in `BLAECK.*` commands
  now read the same parse as everything else, and the 6.x parser is gone with its buffers —
  160 bytes of SRAM on a Mega, 80 on an Uno, about 1 KB of flash, 128 bytes less stack while
  parsing, and half the work per command. All four buffers were private, so nothing about
  this is visible from a sketch, and message ids are assembled from the same fields in the
  same order.
- **A `BLAECK.*` command is no longer truncated before it is matched.** The parse buffer was
  sized for a registered command name (24 characters on AVR), which is shorter than
  `BLAECK.WRITE_STATE_CHANNELS`. It now holds whichever of the two is longer — 4 bytes on
  AVR — and a static assertion fails the build if a longer built-in is ever added. As a
  side effect an over-long unknown command can no longer be truncated onto a shorter
  registered one and run it.
- `deleteSignals()` now gives back the names and metadata the signal table held instead
  of only rewinding the index.
- `BlaeckSerial.h` includes the `CRC.h` umbrella header instead of `<CRC32.h>` and
  `<CRC16.h>` individually, preventing a collision with core headers seen on
  ArduinoCore-mbed. No functional change; flash is byte-identical.
- **`BLAECK_COMMAND_MAX_CHARS_DEFAULT` is 128** on large AVRs and non-AVR boards (was 48 and
  96), still 48 on Uno/Nano. Below 128 a percent-encoded 32-byte text value cannot fit its own
  frame. Costs ~240 bytes of SRAM.
- **`findSignalIndex()` is public.** Resolve an index once in `setup()` and use the by-index
  `write()` / `update()` calls on anything that runs often — the by-name ones compare against
  every signal in the table. Returns `-1` when no signal has that name.

### Fixed
- **`setSignalName()` left the schema hash stale.** Renaming a signal changed what the
  `0xB0` catalog says without changing the hash a host uses to notice, so the host went on
  using the catalog it already had — under the old names. The hash is now recomputed.
- **`BLAECK_ENABLE_STATE_CHANNELS=0` compiles again.** The state channel handle names
  `StateChannelEntry` in a return type and its fields in the `withIcon()`/`diagnostic()`
  chain, but the type was itself compiled away with the feature, so a sketch that turned
  state channels off could not build at all. The type is now always declared — only the
  table is conditional — which is what the feature flags promise everywhere else: the
  calls still compile and store nothing, so a sketch needs no `#ifdef`.
- **Compile-time configuration now has a documented, working route.** A
  `BlaeckSerialConfig.h` in the sketch folder — the method described since 6.0.0 —
  is never found under the Arduino IDE or arduino-cli, because the sketch folder is
  not on the compiler's include path. Anyone who set overrides that way on 6.x was
  silently running the built-in defaults. PlatformIO `build_flags` always worked.
  README.md now documents three routes that do work, and warns that an override must
  reach every translation unit — one seen by the sketch but not by `BlaeckSerial.cpp`
  gives `class BlaeckSerial` two layouts (an ODR violation), which shows up as
  corrupted state rather than a compiler error.

### Removed
- **`setCommandCallback(...)` (breaking).** Deprecated since 6.0.0 and warned about
  at runtime ever since. Sketches still using it now fail to compile: replace
  `setCommandCallback(cb)` with `onAnyCommand(cb)` and change the handler signature
  from `(char *command, int *parameter, char *string01)` to
  `(const char *command, const char *const *params, byte paramCount)`. Frees roughly
  167 bytes of flash per sketch on AVR.


## [6.0.1] - 2026-04-27

### Changed
- `BLAECK_BUFFERED_WRITES_DEFAULT` now defaults to `false` on ArduinoCore-mbed
  boards (Giga R1, Portenta, Nicla, Opta, Nano 33 BLE, Nano RP2040 Connect)
  in addition to AVR. On these boards a bulk `Serial.write(buf, len)` over
  USB CDC can get stuck permanently if the host closes the port while a
  write is in progress, freezing the sketch's main loop until reset.
  Per-byte writes (the unbuffered path) avoid this. Other targets
  (Uno R4 WiFi, ESP32, SAMD, non-mbed RP2040, ...) continue to default to
  buffered writes. Override at compile time via `BLAECK_BUFFERED_WRITES_DEFAULT`
  or at runtime via `setBufferedWrites()`.

### Fixed
- Reduced PlatformIO warning noise in library code:
  - `OnReceiveHandler(int numBytes)` now forwards `numBytes` into `wireSlaveReceive(int numBytes)` instead of leaving it unused.
  - `wireSlaveReceive(int numBytes)` now drains potential extra incoming bytes after reading the mode byte, keeping the Wire RX path clean for malformed multi-byte writes.
  - Added explicit signed/unsigned comparison handling in `addSignal(...)` capacity checks to avoid repeated signedness warnings.


## [6.0.0] - 2026-04-21

### Added
- Buffered frame writes: all protocol frames (B0, B3, C0, D2) can be assembled
  in RAM before a single `Serial.write()` call.  Prevents byte-dropping on
  boards with UART-to-USB bridges (e.g. Arduino Uno R4 WiFi).
  - Enabled by default on non-AVR boards, disabled on AVR to save SRAM.
  - Runtime control: `setBufferedWrites(bool)` / `isBufferedWrites()`.
- Compile-time configuration via `BlaeckSerialConfig.h` in the sketch folder
  (uses `__has_include`) — *see 7.0.0: a config file in the sketch folder is not
  found by the Arduino IDE or arduino-cli, so following this literally had no
  effect there. PlatformIO `build_flags` always worked.*
  All command parser defaults and buffered writes can
  now be overridden without modifying library source. PlatformIO users can
  also use `-D` compiler flags.
- Preprocessor version macros: `BLAECKSERIAL_VERSION`, `BLAECKSERIAL_VERSION_MAJOR`,
  `BLAECKSERIAL_VERSION_MINOR`, `BLAECKSERIAL_VERSION_PATCH`, `BLAECKSERIAL_NAME`.

### Changed
- **Breaking change:** Data message format updated from `D1` (0xD1) to `D2` (0xD2)
- **Breaking change:** Timestamps are now 8 bytes (uint64) instead of 4 bytes (uint32)
  - `BLAECK_MICROS` mode: tracks `micros()` overflow internally, produces monotonic uint64 (no more ~71 minute wrap)
  - `BLAECK_RTC` mode: epoch seconds from callback are converted to microseconds (uint64)
- **Breaking change:** Timestamp parameter type changed from `unsigned long` to `unsigned long long` in all public write methods
- Renamed `BLAECK_RTC` to `BLAECK_UNIX` (`BLAECK_RTC` kept as deprecated alias)
- **Breaking change:** Timestamp callback signature changed from `unsigned long (*)()` to `unsigned long long (*)()`. For `BLAECK_UNIX` mode, the callback must now return microseconds since epoch instead of seconds.
- D2 frame tail updated to `<StatusByte><StatusPayload><CRC32>` (CRC32 is now always present as a separate field).
- Added interval lock API aligned with blaecktcpy/BlaeckTCP: `setIntervalMs(...)` with `BLAECK_INTERVAL_CLIENT`, `BLAECK_INTERVAL_OFF`, or fixed millisecond values.
  - When locked to fixed/off mode, incoming `BLAECK.ACTIVATE`/`BLAECK.DEACTIVATE` commands are ignored.
  - Removed public `setTimedData(...)`; use `setIntervalMs(...)` for timed-data configuration.
- Added command registration API:
  - `onCommand(const char* command, void (*handler)(const char*, const char* const*, byte))`
  - `onAnyCommand(void (*handler)(const char*, const char* const*, byte))`
  - `clearAllCommandHandlers()`
- Added architecture-based command parser defaults:
  - AVR: smaller defaults for command length/handler table/command name length
  - non-AVR: larger defaults (96 chars, 12 handlers, 40 command-name chars)
- Deprecated `setCommandCallback(...)` in favor of `onCommand(...)` / `onAnyCommand(...)` (legacy callback remains supported with runtime warning).
- `LIBRARY_NAME` and `LIBRARY_VERSION` public String members replaced by
  `BLAECKSERIAL_NAME` and `BLAECKSERIAL_VERSION` preprocessor macros.
- Debug/diagnostic output separated from protocol stream.  `begin`,
  `beginMaster`, and `beginSlave` accept an optional `Stream *DebugRef`
  parameter (overload).  When omitted, diagnostics are silently suppressed
  so the data channel stays clean.
- Corrected I2C handler names: `OnSendHandler` → `OnReceiveHandler`,
  `OnReceiveHandler` → `OnRequestHandler` (wiring unchanged, names only).
- I2C slave packs multiple data-point chunks per `onRequest` response,
  reducing round-trips (e.g. 25 floats: 9 requests instead of 25 on AVR).
  Buffer size auto-detected from Wire.h, overridable with `BLAECK_WIRE_BUFFER_SIZE`.
- Dedicated `indexBytes` buffer in slave data-point transmit replaces
  fragile `intCvt` reuse for signal index vs. value.
- Command parser now preserves empty fields between consecutive commas instead
  of collapsing them (`strtok` replaced with manual comma-scanner). For example,
  `<CMD,,42>` now correctly places `42` in parameter slot 1 instead of shifting
  it into slot 0. Empty fields default to `0` (legacy `PARAMETER[]`) or empty
  string (`onCommand` handlers — detectable via `params[i][0] == '\0'`).
- `scanI2CSlaves` parameter type changed from `char` to `uint8_t`.
- `_slaveFound` array zero-initialized at declaration.
- Restart frame (0xC0) is now sent from `read()` instead of `tick()`, so commands-only sketches also notify the host after reset.

### Fixed
- Fixed timer burst issue: when the main loop is delayed beyond the timed
  interval, `timedWriteData` no longer fires multiple times in rapid succession
  to catch up. It now skips missed intervals and resumes at the next boundary.
- CRC desync in updated-only slave mode: CRC bytes were written even when
  the data block was skipped, causing potential protocol desync on the master.
- Uninitialized `_slaveID` in Single/Master mode could produce wrong device
  metadata in `writeDevices`/`writeSymbols`.
- `writeData` now forwards its requested signal range to `writeLocalData`
  in all three modes instead of hardcoding the full range.
- `write(signalName, double)` sent stale data when the signal's DataType
  did not match; the `writeLocalData` call was outside the type-check branch.
- `timedWriteUpdatedData(msg_id)` ignored the `msg_id` parameter and used
  a hardcoded constant instead.
- Fixed `Wire.setClock()` called before `Wire.begin()` in `beginMaster()`.
  On AVR, `Wire.begin()` resets the I2C clock to 100 kHz, so the
  user-specified frequency had no effect.


## [5.0.1] - 2025-11-13

### Removed
- Removed timestamp parameter overloads from `tick()` and `tickUpdated()` methods to fix chronological ordering issues with fast timed intervals. All timestamps are now captured at transmission time to ensure proper sequential ordering.


## [5.0.0] - 2025-09-04
This is a major rewrite, not all changes are listed here.

### Added
- Added timestamp support with three modes:
  - `BLAECK_NO_TIMESTAMP` (0): No timestamp data included
  - `BLAECK_MICROS` (1): Microsecond timestamps using `micros()`
  - `BLAECK_UNIX` (2): Unix epoch timestamps (requires callback)
  - `setTimestampMode(BlaeckTimestampMode mode)`
- Added RestartFlag in data messages to indicate device restart status
- Added new functions:
  - `write()` method overloads to write a single signal
  - `update()` method overloads to update a single signal
  - `tickUpdated()` and `timedWriteUpdatedData()` methods for writing only the updated signals
  - `markSignalUpdated()` and `markAllSignalsUpdated()` to mark signals as updated and `clearAllUpdateFlags()` to clear the update flags
  - `hasUpdatedSignals()` to check if any Signals are marked as updated
- Added `SignalCount` to get the number of signals added

### Changed
- **Breaking change:** On 32 bit Architecture: Data type int and unsigned int are now treated correctly as 4 byte and they use DTYPE 6 (Blaeck_long) and DTYPE 7 (Blaeck_ulong) respectively.
- **Breaking change:** `writeData()` renamed to `writeAllData()`
- **Breaking change:** `timedWriteData()` renamed to `timedWriteAllData()`
- **Breaking change:** Data message format updated from MSGKEY `B1` to `D1`
- **Breaking change:** Enhanced data transmission protocol with timestamp support, data message structure now includes: `<RestartFlag>:<TimestampMode><Timestamp>:<SymbolID><DATA><StatusByte><CRC32>`
- **Breaking change:** Deprecated old data format `<SymbolID><DATA><StatusByte><CRC32>` (used in BlaeckSerial version 4.3.1 or older)
- **Breaking change:** Renamed callback functions for improved clarity:
  - `attachUpdate()` → `setBeforeWriteCallback()`
  - `attachRead()` → `setCommandCallback()`
- Updated message parsing to handle new data format structure
- Updated the examples + some new examples added


## [4.3.1] - 2025-02-06

### Changed
- Fixed ESP32 compiler errors by changing I2C `Wire.write` to `Wire.print` when transmitting char arrays


## [4.3.0] - 2024-04-16

### Added
- Added `BlaeckSerial::writeRestarted()` to send a message when the BlaeckSerial device is restarted. The message is sent only once during runtime.
- `BlaeckSerial::writeRestarted()` is called in `BlaeckSerial::tick()`


## [4.2.0] - 2023-12-05

### Removed
- Removed `BlaeckSerial::addSignal(PGM_P const *signalNameTable, int signalNameIndex, ..);` because it is easier and has the same effect to use the `F()` Macro

### Changed
- Example `SignalNamesInFlashLessRAMUsage.ino` now uses the `F()` Macro to store the signal names


## [4.1.0] - 2023-11-20

### Added
- **Only for AVR Architecture:** Signal names can now be stored in flash memory to save RAM with the new addSignal functions `BlaeckSerial::addSignal(PGM_P const *signalNameTable, int signalNameIndex, ..);`. This is especially helpful for the ATmega328P (Arduino Uno/Nano), which only has 2048 bytes of RAM
- New example `SignalNamesInFlashLessRAMUsage.ino` added to show how it works

### Changed
- Changed from `StreamRef->write('\0')` to `StreamRef->print('\0')` because `Call of overloaded function is ambiguous` error was thrown when compiling for Arduino Due and other boards


## [4.0.0] - 2023-07-18

### Added
- New public const `LIBRARY_NAME`

### Changed
- **Breaking change:** `<BLAECK_ACTIVATE, 1st, 2nd, 3rd, 4th byte in milliseconds>` new parameters and new range from 0..4294967295[ms]
- **Breaking change:** Include `LIBRARY_NAME` in response to `<BLAECK.GET_DEVICES>`, new message key: `MSGKEY: B3`
- Public const `BLAECKSERIAL_VERSION` changed to `LIBRARY_VERSION`
- Updated dependencies: CRC to version 1.0.0
- Changed reference from HardwareSerial to Stream
- Formatted source code with VSCode
- Updated examples


## [3.0.3] - 2023-06-13

### Changed
- Changed minimum timed interval finally to 0ms
- Empty parameters are now 0 again (Behaviour change was accidentally introduced in 3.0.2)
- Double (8byte precision) integration for 32bit (non AVR) processors


## [3.0.2] - 2023-02-02

### Changed
- Some code changes to make library work on xmc and esp32 architecture
- Changed minimum timed interval back to 100ms


## [3.0.1] - 2022-09-01

### Added
- New SHT31 temperature & humidity sensor example `SHT31TempHumiditySensor`


## [3.0.0] - 2022-08-22

### Added
- CRC32 error detection integrated for serial data and I2C data
- `BlaeckSerial::tick(unsigned long msg_id)`
- New sine wave example `SineGeneratorBasic`
- New sine wave example `SineGeneratorAdvanced`
- New datatype test example `DatatypeTestRandomMaster`
- New datatype test example `DatatypeTestRandomSlave`

### Changed
- `BlaeckSerial::tick()` and `BlaeckSerial::timedWriteData()` message id changed to new default: `msg_id = 185273099`
- Changed minimum timed interval from 100ms to 10ms
- Renamed example `DatatypeTestMaster` to `DatatypeTestLimitsMaster`
- Renamed example `DatatypeTestSlave` to `DatatypeTestLimitsSlave`


## [2.0.1] - 2021-11-15

### Changed
- `BLAECKSERIAL_VERSION` fixed


## [2.0.0] - 2021-11-15

### Changed
- `<BLAECK.WRITE_SYMBOLS>` responds with
`<MasterSlaveConfig><SlaveID><SymbolName><DTYPE>`
- Slave prefix is now an optional argument (default: true) of addSignal
- Improved slave handling when no signal is added to a slave
- Examples updated

### Added

New command: `<BLAECK.GET_DEVICES>`

### Removed

`<BLAECK.WRITE_VERSION, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>`


## [1.0.4] - 2021-06-28

### Added

New command: `<BLAECK.WRITE_VERSION, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>`


## [1.0.3] - 2021-03-30

### Changed

Improved examples


## [1.0.2] - 2020-11-09

### Added

New public function: `attachUpdate(void (*updateCallback)());`


## [1.0.1] - 2020-05-11

### Changed
- Reduced memory footprint
- Optimizations to reduce compiler warnings


## [1.0.0] - 2020-05-08

Initial release.

[6.0.2]: https://github.com/sebaJoSt/BlaeckSerial/compare/6.0.1...6.0.2
[6.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/6.0.0...6.0.1
[6.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/5.0.1...6.0.0
[5.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/5.0.0...5.0.1
[5.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/4.3.1...5.0.0
[4.3.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/4.3.0...4.3.1
[4.3.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/4.2.0...4.3.0
[4.2.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/4.1.0...4.2.0
[4.1.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/4.0.0...4.1.0
[4.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/3.0.3...4.0.0
[3.0.3]: https://github.com/sebaJoSt/BlaeckSerial/compare/3.0.2...3.0.3
[3.0.2]: https://github.com/sebaJoSt/BlaeckSerial/compare/3.0.1...3.0.2
[3.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/3.0.0...3.0.1
[3.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/2.0.1...3.0.0
[2.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/2.0.0...2.0.1
[2.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.4...2.0.0
[1.0.4]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.3...1.0.4
[1.0.3]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.2...1.0.3
[1.0.2]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.1...1.0.2
[1.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.0...1.0.1
[1.0.0]: https://github.com/sebaJoSt/BlaeckSerial/releases/tag/1.0.0
