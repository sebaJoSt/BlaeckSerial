# Changelog

All notable changes to this project will be documented in this file.

## [6.0.0] - 2026-04-08

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
- `scanI2CSlaves` parameter type changed from `char` to `uint8_t`.
- `_slaveFound` array zero-initialized at declaration.

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
