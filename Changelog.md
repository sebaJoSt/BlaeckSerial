# Changelog

All notable changes to this project will be documented in this file.

## [7.0.0] - 2026-08-08

### Breaking
- **Client compatibility:** string signals introduce a new signal value type
  (`0xA`) in the binary data frame. Clients must support this variable-length
  type to decode any frame containing a string signal. Depending on their
  implementation, clients without support may lose sync, drop data, or fail on
  such frames. Devices using only numeric signals remain compatible.
- **I2C master/slave removal**, detailed under Removed below. Together with the
  new client requirement above, this is why 7.0.0 is a major release.

### Added
- Command acknowledgement (`0xA5` frame): after dispatching an inbound command
  the device replies on the serial host with an FNV-1a hash of the received
  command plus an accept/reject status and reason code, so a host (e.g. Loggbok)
  can confirm the command was applied. Like `0xA0`, the frame carries no CRC;
  hosts that don't recognize the key ignore it.
- **Message frames (`0x90` / `0x95`).** New `addMessageChannel(channelName[, icon[,
  diagnostic[, getStateText]]])` declares a free-text status/log channel, and
  `writeMessage(channelName, text[, messageID])` sends a line on it. Declared
  channels are advertised in a `0x90` "Message Channel List" frame in response to
  `BLAECK.WRITE_MESSAGE_CHANNELS`, so a host (e.g. Loggbok) can announce one Home
  Assistant text sensor per channel before the first line arrives. The `0x95` frame
  identifies its channel by index into that catalog, so the catalog must reach the
  host first; messages on undeclared channels are dropped. The text is
  length-prefixed (LE uint16, capped at 65535 bytes), neither frame carries a CRC,
  and a message is never stored as signal data. Set `BLAECK_ENABLE_MESSAGES` to `0`
  to compile the feature out (the API remains as no-ops); table size and name length
  are tunable via `BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT` and
  `BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT`.
- **Event frames (`0x80` / `0x85`).** New `addEventChannel(channelName[, icon[,
  diagnostic]])` declares an event channel and `addEventType(channelName,
  F("..."))` gives it the closed set of events it may report; call order defines
  each type's index. `writeEvent(channelName, F("..."))` then reports one
  occurrence. Declared channels are advertised in a `0x80` "Event Channel List"
  frame in response to `BLAECK.WRITE_EVENT_CHANNELS`, so a host (e.g. Loggbok) can
  announce one Home Assistant event entity per channel, including its list of types,
  before the first event arrives. An event carries no text: the `0x85` frame holds
  only the channel and event type indices, so the wording is fixed at compile time
  and a host cannot interpret an event without the catalog. Use a message channel
  for anything carrying a runtime value. Events on undeclared channels or types are
  dropped, and neither frame carries a CRC. Event types share one pool across all
  channels, so no channel has to be sized for the worst case. Set
  `BLAECK_ENABLE_EVENTS` to `0` to compile the feature out (the API remains as
  no-ops); table sizes are tunable via `BLAECK_EVENT_MAX_CHANNELS_DEFAULT`,
  `BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT` and `BLAECK_EVENT_MAX_TYPES_DEFAULT`.
- **Home Assistant entity category on commands.** The typed command helpers take
  an optional trailing `BLAECK_CAT_CONFIG` or `BLAECK_CAT_DIAGNOSTIC`, carried in
  bits 5-6 of the `0xA0` command flags. A host maps it to Home Assistant's
  `entity_category`, which moves the entity off auto-generated dashboards and
  groups it under *Configuration* or *Diagnostic* on the device page. Use it for
  controls that set up the board rather than operate it; the default,
  `BLAECK_CAT_NONE`, leaves the command a primary control. Message and event
  channels keep their existing `diagnostic` flag: they are read-only, and Home
  Assistant reserves `config` for entities the user can change.
- **A message channel can be a command's state source.** The typed command helpers
  take an optional trailing `BLAECK_STATE_MESSAGE`, saying that `stateSignal` names a
  channel declared with `addMessageChannel()` rather than a signal; the `0xA0` entry
  carries it as one byte after the state signal name. The default,
  `BLAECK_STATE_SIGNAL`, leaves existing declarations unchanged. A message channel is
  independent of the signal table, so a device that adds no signals can still report
  what its controls are set to.
  Paired with it, `addMessageChannel()` takes an optional `getStateText`: a function
  returning the channel's value as text, called while the `0x90` catalog is built and
  carried in it under channel flag bit 2. A host learns the value by polling, so
  nothing has to be pushed to keep it in step, and there is no stored copy to go stale.
  It runs mid-frame, so format and return rather than sampling slow hardware; build the
  text in a function-local static. Register none, or return `nullptr`, for a plain log
  channel. A host that already holds a catalog will not re-read it, so also call
  `writeMessage()` in `setup()` to cover a device restart - `WaveformGenerator` shows
  the pattern.
- **Disabled catalogs now answer with an empty list.** With `BLAECK_ENABLE_EVENTS`,
  `BLAECK_ENABLE_MESSAGES` or `BLAECK_ENABLE_COMMAND_META`
  set to `0`, the matching poll (`BLAECK.WRITE_EVENT_CHANNELS`,
  `BLAECK.WRITE_MESSAGE_CHANNELS`, `BLAECK.WRITE_COMMANDS`) still replies, with a
  frame containing no entries, instead of staying silent. A host gates these polls
  on the library version and cannot see the build flags, so a silent device used
  to make it wait out its full timeout on every setup. An empty catalog is already
  the legal "nothing declared" case, so hosts need no special handling. Costs
  roughly 330 bytes of flash and 28 bytes of SRAM per disabled feature; builds
  with the features enabled are unaffected.
- **Text command (`onTextCommand`).** New typed command helper
  `onTextCommand(command, handler, stateSignal = nullptr, maxLength = 255)` for
  a Home Assistant text entity. The host sends the value percent-encoded (so
  commas and other frame delimiters survive); the device percent-decodes it in
  place before the handler runs and rejects values longer than `maxLength`
  (`0xA5` reason `TOO_LONG`). The max length is advertised in the `0xA0` command
  entry (flag bit 4, LE uint16).
  Requires `BLAECK_ENABLE_COMMAND_META`.
- **String signals (`addSignal(name, char *value)`).** New signal data type
  `Blaeck_string` (symbol code `0xA`) for reporting textual values (labels,
  states, small JSON, etc.). On the wire the value is length-prefixed: a
  1-byte length followed by that many UTF-8 bytes. The value lives in a
  user-owned buffer and is read live on each transmit. Matching
  `write(name/index, char *value)` setters repoint the buffer and transmit
  that one signal; you may also update the buffer in place
  and let the periodic transmit pick it up. A string is transmitted up to 255
  bytes.
- Added the `WaveformGenerator` example, registered with the typed command
  helpers (`onNumberCommand` / `onSelectCommand` / `onSwitchCommand` /
  `onTextCommand` / `onButtonCommand`) so the device is self-describing for
  Loggbok / Home Assistant MQTT auto-discovery.

### Changed
- Increased the default AVR command-handler limit on larger-SRAM AVR boards
  (for example Arduino Mega 2560) from 4 to 12 handlers, while keeping smaller
  AVR boards conservative at 6 handlers. This allows command-rich examples such
  as `WaveformGenerator` to register their full command set without custom
  compile-time overrides.
- `BlaeckSerial.h` now includes the `CRC.h` umbrella header instead of
  `<CRC32.h>` and `<CRC16.h>` individually, after the individual headers were
  seen to collide with core headers on ArduinoCore-mbed. Preventive here — the
  collision does not reproduce on `arduino:mbed_giga` 4.6.0 with CRC 1.0.4 —
  and there is no functional change;
  flash is byte-identical, since the unused CRC variants are declarations only.

### Fixed
- **Compile-time configuration now has a documented, working route.** A
  `BlaeckSerialConfig.h` in the sketch folder — the method described since
  6.0.0 — is never found under the Arduino IDE or arduino-cli: the sketch
  folder is not on the compiler's include path, so the `__has_include` in
  `BlaeckSerial.h` finds nothing and the settings are silently ignored.
  Anyone who set overrides that way on 6.x was running the built-in defaults,
  and registrations beyond the real handler limit failed quietly. PlatformIO
  `build_flags` were unaffected and always worked. README.md now documents
  three routes that do work (arduino-cli `--build-property`, a config inside
  the library's `src/`, or `platform.local.txt`), and warns that an override
  must reach every translation unit — a setting seen by the sketch but not by
  `BlaeckSerial.cpp` gives `class BlaeckSerial` two layouts (an ODR
  violation). This is an Arduino build-system limitation, not a library one:
  see arduino/arduino-builder#15 (closed) and arduino/arduino-cli#501 (open).
- `writeMessage()`'s length cap no longer trips `-Wtype-limits` on AVR, where
  `size_t` is 16-bit and the comparison could never be true. No behaviour
  change; `strlen` could not exceed the cap there in any case.

### Removed
- **`setCommandCallback(...)` (breaking).** Deprecated since 6.0.0 in favour of
  `onCommand(...)` / `onAnyCommand(...)`, which it has warned about at runtime
  ever since. A full major cycle of notice is enough, and 7.0 is the window;
  keeping it would carry it to 8.0. Sketches still using it now fail to compile
  instead of silently taking the legacy path — replace
  `setCommandCallback(cb)` with `onAnyCommand(cb)` and adjust the handler
  signature from `(char *command, int *parameter, char *string01)` to
  `(const char *command, const char *const *params, byte paramCount)`.
  No example in the library used it. Frees roughly 167 bytes of flash per
  sketch on AVR, plus the callback pointer and its warning flag in RAM.
- **I2C master/slave support (breaking).** All I2C master/slave functionality
  has been removed: `beginMaster(...)` / `beginSlave(...)`, the `MasterSlaveConfig`
  modes, slave discovery/scanning, per-signal `prefixSlaveID`, the `@<slaveID>:`
  command-routing prefix, master-side command-catalog aggregation, and the
  `<Wire.h>` dependency. BlaeckSerial is now single-board only.
  The on-the-wire frame layout is unchanged — the per-record master/slave-config
  and slave-ID bytes are still emitted (always `0`), so existing Blaeck hosts
  (e.g. Loggbok) need no changes.

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
