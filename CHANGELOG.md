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
  Every size now lives in the sketch.

- **A table costs nothing until it is used.** Each is allocated by its first entry rather
  than reserved up front, so a sketch pays only for what it declares.

- **`begin(Stream *, unsigned int, Stream *debug)` removed.** Name the debug stream
  on the chain instead: `begin(&Serial).withDebugStream(&Serial1)`.

- **I2C master/slave support removed.** BlaeckSerial is single-board only, and no longer
  depends on `<Wire.h>`. Nothing moved in the frame layout: the per-record
  master/slave-config and slave-ID bytes are still emitted, always `0`.

- **Command parameters are no longer trimmed.** A leading space is part of the value, so
  `<SET_LABEL, hi>` sets `" hi"` rather than `"hi"`. A switch compares exactly, so
  `<SET_ENABLE, 1>` is now rejected.

- **`String` is gone from the API; names are `const char *`.** Every overload that took a
  signal name, and the `DeviceName`, `DeviceHWVersion` and `DeviceFWVersion` members.
  Names in quotes work as before. If you kept a name in a `String`, add `.c_str()`.

### Added
- **`withNameSuffix(n)` ends a signal's name in a number.**
  `addSignal(F("Sine_"), &v).withNameSuffix(i + 1)` names a signal `Sine_1` without
  storing the name. The suffix is 0–255.
- **`addSignal(F("Name"), &value)`.** The name stays in flash instead of being copied into
  SRAM. Existing `F("...")` calls pick the new overload and get the saving without an edit;
  a name built at runtime is copied as before.
- **`printRejections(&Serial)`.** Prints what did not fit in the tables, and the `begin()`
  call that would have made room. Prints nothing when everything fitted.
  `hasRejections()` checks without printing.
- **Typed commands.** `onNumberCommand`, `onSwitchCommand`,
  `onSelectCommand`, `onTextCommand` and `onButtonCommand` say what a command accepts —
  range, step, unit, options, text length — so the device describes its own controls.
  `onCommand` and `onAnyCommand` stay.
- **Message ids.** Prefix a command or a built-in with `#42:` — `<#42:SET_AMP,0.9>` — and the
  device sends that number back, so a host can tell which one it answers. The prefix is
  optional, and is how built-ins carry an id now, in place of the `MsgID[0..3]` parameters.
- **`BLAECK.PAUSE_WRITES,<ms>` and `BLAECK.RESUME_WRITES`.** The pause holds back every
  frame for that long, so a host can close the connection while the device is quiet. It
  ends on its own; `RESUME_WRITES` only ends it early.
- **State channels.** Separate from signals: the device pushes the current value of
  something — text, bool or any number — when it chooses to, instead of on the interval
  the signals use.
- **Event channels.** A device can report that something happened — `idle_warning`,
  `resumed` — from a set of events it declares up front. An event says only which one
  happened; use a state channel to carry a value.
- **String signals.** A signal can carry text as well as numbers — up to 255 bytes, read
  from your own buffer each time it is sent.
- **Signal metadata.** A signal can say how it should be shown — unit, device class, icon,
  display name, decimals — so a host can present it without being told anything about the
  board.
- **`getIntervalMs()` and `isTimedDataActive()`** report the interval a host asked for and
  whether timed data is being sent — the two things a sketch could not previously read.
  `getIntervalMs()` returned the lock mode before, not the rate.

### Changed
- **Buffered writes are back on for the mbed boards, reversing 6.0.1.** That release turned
  them off believing a bulk write could freeze the main loop; retesting on the same Giga R1
  showed the loop still running. Only AVR still defaults to unbuffered.
- **How a host talks to the device changed.** `BLAECK.ACTIVATE` takes a plain decimal
  interval, every command is acknowledged, and a data frame carries a flag saying whether it
  was asked for or sent on the interval, in place of the magic message id that used to mark
  a requested one. A sketch is unaffected; a host that speaks the protocol directly needs
  updating.
- **`BLAECK_COMMAND_MAX_CHARS_DEFAULT` is 128** on large AVRs and non-AVR boards (was 48 and
  96), still 48 on Uno/Nano. Below 128 a percent-encoded 32-byte text value cannot fit its own
  frame. Costs ~240 bytes of SRAM.
- **`findSignalIndex()` is public.** Resolve an index once in `setup()` and use the by-index
  `write()` / `update()` calls on anything that runs often — the by-name ones compare against
  every signal in the table. Returns `-1` when no signal has that name.

### Removed
- **The interval lock (breaking).** `setIntervalMs()`, `BLAECK_INTERVAL_OFF` and
  `BLAECK_INTERVAL_CLIENT` are gone. The host owns the rate now. A sketch that wants its
  own cadence calls `writeAllData()` on its own timing and is never activated.
- **`setCommandCallback(...)` (breaking).** Deprecated since 6.0.0. Replace it with
  `onAnyCommand(cb)` and change the handler signature to
  `(const char *command, const char *const *params, byte paramCount)`.
- **`hasSignalOverflow()` and `getSignalOverflowCount()` (breaking).** Replaced by
  `hasRejectedSignals()` and `getRejectedSignalCount()`, which have counterparts for
  commands, state channels and event channels.


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
  (uses `__has_include`). All command parser defaults and buffered writes can
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
