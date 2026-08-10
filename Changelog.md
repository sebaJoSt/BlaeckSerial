# Changelog

All notable changes to this project will be documented in this file.

## [7.0.0] - 2026-08-08

This release makes a device self-describing. Alongside the signals it always declared,
a board now declares its commands, plus message and event channels (both new), so a
host (e.g. Loggbok) can turn the lot into Home Assistant MQTT auto-discovery — a sensor
per signal, a control per command, a text sensor per channel, an entity per event —
without being configured for that board in advance.

### Breaking
- **I2C master/slave support removed.** All I2C master/slave functionality
  has been removed: `beginMaster(...)` / `beginSlave(...)`, the `MasterSlaveConfig`
  modes, slave discovery/scanning, per-signal `prefixSlaveID`, the `@<slaveID>:`
  command-routing prefix, master-side command-catalog aggregation, and the
  `<Wire.h>` dependency. BlaeckSerial is now single-board only.
  The on-the-wire frame layout is unchanged — the per-record master/slave-config
  and slave-ID bytes are still emitted (always `0`), so existing Blaeck hosts
  (e.g. Loggbok) need no changes.

- **A typed command now requires its value.** A frame without one is rejected
  (`BLAECK_ACK_MISSING_VALUE`) instead of reaching the handler, which previously
  acknowledged the command as accepted and then ignored it. Valueless usage — query,
  toggle — belongs on `onCommand`, which declares no contract and is passed through
  untouched.
- **Command parameters are no longer trimmed.** The tokenizer splits on commas and does
  nothing else, so a leading space is part of the value: `<SET_LABEL, hi>` sets `" hi"`.
  Hand-typed frames like `<SET_ENABLE, 1>` are now rejected rather than silently accepted.

### Added
- **`BLAECK_COMMAND_MAX_CHARS_DEFAULT` is 128** on large AVRs and non-AVR boards (was 48 and
  96), still 48 on Uno/Nano. Below 128 a percent-encoded 32-byte text value cannot fit its own
  frame. Costs ~240 bytes of SRAM.
- **`findSignalIndex()` is public.** The by-index `write()` and `update()` calls were
  reachable but their indices were not: the lookup that produces one was private. Resolve
  an index once in `setup()` and use the by-index calls on anything that runs often — the
  by-name calls build a temporary `String` from their argument on every call, which is a
  heap allocation per write. Returns `-1` when no signal has that name.
- **The command catalog states how long a command may be.** `0xA0` opens with
  `CommandPayloadMax`: the characters a device can receive between the delimiters. One
  receive buffer serves every command, so a host subtracts the name and its comma to get
  what is left for parameters, and can refuse an over-long value instead of watching the
  device drop it on arrival. This matters most for text: a value is percent-encoded before
  it is framed, so a non-ASCII character costs three characters or more per byte, and an
  advertised `maxLength` in bytes can be unreachable well before it is met.
- **Typed commands (`0xA0` / `0xA5`).** `onNumberCommand`, `onSwitchCommand`,
  `onSelectCommand`, `onTextCommand` and `onButtonCommand` register a command
  together with what it accepts — range, step, unit, options, text length — so the
  device describes its own controls. They join `onCommand` / `onAnyCommand` from
  6.0.0, which stay for commands that carry no metadata. Values outside the declared
  range, bad select indices, over-long text and a missing value are rejected before the
  handler runs, and a frame that did not fit — more parameters than the device accepts, or
  longer than its receive buffer — is rejected for every command, `onCommand` included.
  Every dispatch is acknowledged with an accept/reject status and reason code, plus two hashes:
  one over the command as received, one over its name alone. A sender that matches the first knows
  the device got exactly what it wrote; one that matches only the second knows which command was
  acknowledged even though the bytes differed - which is the only way a frame the device could not
  take in whole gets reported rather than acknowledged into silence.
  The state a control shows is named as a bare `F("Frequency")` — the signal that
  mirrors it — or as `BlaeckOwnState(F("Offset"), OffsetState)`, which makes the command
  carry its own state instead: it declares a message channel of that name, asks the getter
  for the value whenever a host polls the channel catalog, and announces it once at
  registration, so a host already connected when the board resets is corrected. Push a
  change with `writeCommandState(command)` — the handler's own parameter, so no name has to
  be kept in step. That channel belongs to the command: `addMessageChannel()` and
  `writeMessage()` refuse its name, so its value comes from the getter and nowhere else, and
  a host that knows it backs a control announces no separate sensor for it. Independent of
  the signal table, so a device that adds no signals at all can still report what its
  controls are set to. Requires `BLAECK_ENABLE_COMMAND_META`.
- **Message channels (`0x90` / `0x95`).** `addMessageChannel(channelName[, icon[,
  diagnostic[, getStateText]]])` declares a free-text status/log channel and
  `writeMessage(channelName, text[, messageID])` sends a line on it. Channels are
  declared up-front; messages on undeclared channels are dropped, and a message is
  never stored as signal data. The optional `getStateText` makes a channel report a
  current value, which the library fetches whenever a host polls the catalog — so a
  control backed by that channel is right without anything having been pushed. Size the
  tables with `BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT` and `BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT`.
  Requires `BLAECK_ENABLE_MESSAGES`.
- **Event channels (`0x80` / `0x85`).** `addEventChannel(channelName[, icon[,
  diagnostic[, eventTypes]]])` declares a channel and the closed set of events it may
  report — `F("idle_warning,resumed")`, position defining each index — or
  `addEventType(channelName, F("..."))` adds them one at a time, for a list built
  conditionally; `writeEvent(channelName, F("..."))` reports one occurrence. An event carries no text, so its wording is
  fixed at compile time — use a message channel for anything with a runtime value.
  Events on undeclared channels or types are dropped. Size the tables with
  `BLAECK_EVENT_MAX_CHANNELS_DEFAULT`, `BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT` and
  `BLAECK_EVENT_MAX_TYPES_DEFAULT` (types share one pool across channels,
  so no channel needs sizing for the worst case).
  Requires `BLAECK_ENABLE_EVENTS`.
- **String signals (`addSignal(name, char *value)`).** New `Blaeck_string` data type
  for textual values (labels, states, small JSON). The value lives in a user-owned
  buffer read live on each transmit: `write(name/index, char *value)` repoints the
  buffer and transmits that one signal, or update the buffer in place and let the
  periodic transmit pick it up. Up to 255 bytes.
- New `WaveformGenerator` example: one fully controllable waveform, exercising
  typed commands, message channels, event channels and string signals together.

### Changed
- Increased the default AVR command-handler limit on larger-SRAM AVR boards
  (for example Arduino Mega 2560) from 4 to 12 handlers, while keeping smaller
  AVR boards conservative at 6 handlers. This allows command-rich examples such
  as `WaveformGenerator` to register their full command set without custom
  compile-time overrides.
- `BlaeckSerial.h` includes the `CRC.h` umbrella header instead of `<CRC32.h>` and
  `<CRC16.h>` individually, preventing a collision with core headers seen on
  ArduinoCore-mbed. No functional change; flash is byte-identical.

### Fixed
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
