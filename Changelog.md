# Changelog

All notable changes to this project will be documented in this file.

## [3.0.1] - 2022-09-01

### Added
- New SHT31 temperature & humidity sensor example `SHT31TempHumiditySensor`


## [3.0.0] - 2022-08-22

### Added
- CRC32 error detection integrated for serial data and IC2 data
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

[3.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/2.0.1...3.0.0
[2.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/2.0.0...2.0.1
[2.0.0]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.4...2.0.0
[1.0.4]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.3...1.0.4
[1.0.3]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.2...1.0.3
[1.0.2]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.1...1.0.2
[1.0.1]: https://github.com/sebaJoSt/BlaeckSerial/compare/1.0.0...1.0.1
[1.0.0]: https://github.com/sebaJoSt/BlaeckSerial/releases/tag/1.0.0
