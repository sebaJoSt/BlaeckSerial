# Changelog

All notable changes to this project will be documented in this file.

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
