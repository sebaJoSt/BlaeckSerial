# Protocol

## Binary frame format

Messages use the following binary format:
````
<BLAECK:<MSGKEY>:<MSGID>:<ELEMENTS>/BLAECK>\r\n
````

| Part | Bytes | Content |
|------|-------|---------|
| Header | 8 | `<BLAECK:` (fixed) |
| MSGKEY | 1 | Message type code (see table below) |
| MSGID | 4 | Message ID, little-endian |
| ELEMENTS | varying | Payload (colon-separated, type-dependent) |
| Trailer | 10 | `/BLAECK>\r\n` (fixed) |

## Message Keys

Type| MSGKEY | Elements| Description
----|--------|---------------------------------|---------------------------------------------
Symbol List | B0 | **`<MasterSlaveConfig><SlaveID><SymbolName><DTYPE>`** | **Up to n symbols.** Response to `<BLAECK.WRITE_SYMBOLS>`
~~Data~~ | ~~B1~~ | ~~**`<SymbolID><DATA>`**`<StatusByte><CRC32>`~~ | Deprecated (v4.3.1 or older)
~~Data~~ | ~~D1~~ | ~~`<RestartFlag>:<TimestampMode><Timestamp(4)>:`**`<SymbolID><DATA>`**`<StatusByte><CRC32>`~~ | Deprecated (v5.x)
Data | D2 | `<RestartFlag>:<SchemaHash>:<TimestampMode><Timestamp(8)>:`**`<SymbolID><DATA>`**`<StatusByte><StatusPayload><CRC32>` | **Up to n data items.** Response to `<BLAECK.WRITE_DATA>`
~~Devices~~ | ~~B2~~ | ~~`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion>`~~ | Deprecated (v3.0.3 or older)
Devices | B3 | **`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion><LibraryName>`** | **Up to n device items.** Response to `<BLAECK.GET_DEVICES>`
Restarted | C0 | **`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion><LibraryName>`** | Only first device. Sent by `writeRestarted()` and `tick()` on first call after restart.

## Elements

 Element|Type    |  Description
 -------|--------| ---------------------------------------------------------------------------
 `MSGKEY`| byte |  Message Key, a unique key for the type of message being sent; 1 byte transmitted
 `MSGID` | ulong|  Message ID, echoes back to identify the response (0 to 4294967295); 4 bytes transmitted
 `DATA`  | (varying)| Signal value, size depends on datatype
 `SymbolID` | uint | Signal index; 2 bytes transmitted
 `SymbolName` |String0 | Signal name, null-terminated
 `DTYPE` | byte | Datatype code (see [Supported datatypes](#supported-datatypes))
 `MasterSlaveConfig`     | byte | 0=Single device, 1=Master, 2=Slave
 `SlaveID`              | byte |             Slave address
 `DeviceName`           | String0 |          set with public variable `DeviceName`
 `DeviceHWVersion`      | String0 |          set with public variable `DeviceHWVersion`
 `DeviceFWVersion`      | String0 |          set with public variable `DeviceFWVersion`
 `LibraryVersion`       | String0 |          set with preprocessor macro `BLAECKSERIAL_VERSION`
 `LibraryName`          | String0 |          set with preprocessor macro `BLAECKSERIAL_NAME`
 `StatusByte`           | byte |             Status code (see [Status codes](#status-codes) below)
 `StatusPayload`        | byte |             4 bytes, interpretation depends on `StatusByte` (see [Status codes](#status-codes) below)
 `CRC32`                | byte |             4 bytes; CRC order: 32; CRC Polynom (hex): 4C11DB7; Initial value (hex): FFFFFFFF; Final XOR value (hex): FFFFFFFF; reverse data bytes: true; reverse CRC result before Final XOR: true; (http://zorc.breitbandkatze.de/crc.html)
 `RestartFlag`          | byte | 1 if device restarted since last transmission, 0 otherwise; 1 byte transmitted
 `SchemaHash`           | uint16 | CRC16-CCITT (init=0x0000, poly=0x1021) of signal name bytes + datatype code byte for each signal in order; 2 bytes transmitted (little-endian). Used by hubs and clients to detect signal layout changes at runtime.
 `TimestampMode`        | byte | 0=No timestamp, 1=Microseconds, 2=Unix time; 1 byte transmitted
 `Timestamp`            | uint64 | Timestamp value (only present if TimestampMode > 0); 8 bytes transmitted. Mode 1: microseconds with overflow tracking. Mode 2: Unix epoch microseconds.

MSGID is supported by `<BLAECK.GET_DEVICES>`, `<BLAECK.WRITE_SYMBOLS>` and `<BLAECK.WRITE_DATA>`:
````
<BLAECK.GET_DEVICES, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
<BLAECK.WRITE_SYMBOLS, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
<BLAECK.WRITE_DATA, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
````

## Status codes

| StatusByte | Name | StatusPayload (4 bytes) |
|------------|------|-------------------------|
| `0x00` | Normal | Reserved (`0x00 0x00 0x00 0x00`) |
| `0x01` | I2C slave skip | Byte 0: skipped slave count. Byte 1: first skipped slave ID (`0xFF` = unknown). Byte 2: skip reason (`0x01` = preflight no response, `0x02` = runtime timeout/short/malformed/CRC mismatch). Byte 3: reserved (`0x00`). |

## Supported datatypes

| Datatype         | DTYPE | Bytes |
|------------------|-------|-------|
| `bool`           | 0     | 1     |
| `byte`           | 1     | 1     |
| `short`          | 2     | 2     |
| `unsigned short` | 3     | 2     |
| `int`            | 4     | 2     |
| `unsigned int`   | 5     | 2     |
| `long`           | 6     | 4     |
| `unsigned long`  | 7     | 4     |
| `float`          | 8     | 4     |
| `double`         | 9     | 8     |

BlaeckSerial automatically handles platform differences in data type sizes:

**AVR Platforms (Arduino Uno, Nano, Mega, etc.):**
- `int` and `unsigned int` are 2 bytes
- `double` has no precision advantage over `float` (both 4 bytes)

**32-bit Platforms (ESP32, ESP8266, Arduino Due, etc.):**
- `int` and `unsigned int` are 4 bytes and get automatically mapped to `long`/`unsigned long` protocol types
- `double` provides true 8-byte double precision

| User Type | AVR Platform | 32-bit Platform |
|-----------|--------------|-----------------|
| `bool` | `DTYPE 0` (1 byte) | `DTYPE 0` (1 byte) |
| `byte` | `DTYPE 1` (1 byte) | `DTYPE 1` (1 byte) |
| `short` | `DTYPE 2` (2 bytes) | `DTYPE 2` (2 bytes) |
| `unsigned short` | `DTYPE 3` (2 bytes) | `DTYPE 3` (2 bytes) |
| `int` | `DTYPE 4` (2 bytes) | **`DTYPE 6`** (4 bytes) |
| `unsigned int` | `DTYPE 5` (2 bytes) | **`DTYPE 7`** (4 bytes) |
| `long` | `DTYPE 6` (4 bytes) | `DTYPE 6` (4 bytes) |
| `unsigned long` | `DTYPE 7` (4 bytes) | `DTYPE 7` (4 bytes) |
| `float` | `DTYPE 8` (4 bytes) | `DTYPE 8` (4 bytes) |
| `double` | **`DTYPE 8`** (4 bytes) | `DTYPE 9` (8 bytes) |

## Timestamp modes

| Mode Value | Name | Description |
|------------|------|-------------|
| 0 | `BLAECK_NO_TIMESTAMP` | No timestamp data included |
| 1 | `BLAECK_MICROS` | Microsecond timestamps using `micros()` |
| 2 | `BLAECK_UNIX` | Unix epoch timestamps (requires callback). `BLAECK_RTC` kept as deprecated alias. |

## Decoding examples

### Symbol List Decoding
Example from `Basic.ino`:
`<BLAECK.WRITE_SYMBOLS, 0, 255, 0, 0>`:
````
ASCII: <  B  L  A  E  C  K  :  °  :  °  °  °  °  :  °  °  S  m  a  l  l     N  u  m  b
HEX:   3C 42 4C 41 45 43 4B 3A B0 3A 00 FF 00 00 3A 00 00 53 6D 61 6C 6C 20 4E 75 6D 62
Byte:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26
-----------------------------------------------------------------------------------
ASCII: e  r  °  °  °  °  B  i  g     N  u  m  b  e  r  °  °  /  B  L  A  E  C  K  >
HEX:   65 72 00 08 00 00 42 69 67 20 4E 75 6D 62 65 72 00 06 2F 42 4C 41 45 43 4B 3E
Byte:  27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 56 47 48 49 50 51 52
````

 Byte | Description
----|---------------------------------------------
8   | `MSGKEY`: B0 → Symbol List
10-13| `MSGID`: Hex: 00 FF 00 00 → Decimal: 65280
15  | `MasterSlaveConfig`: Hex: 00 → Decimal: 0 → Single device
16  | `SlaveID`: Hex: 00 → Decimal: 0 → Slave Address: 0
17-29| `SymbolName`: ASCII: Small Number + Null Termination '\0'
30   | `DTYPE`: Hex: 08 → Float
31  | `MasterSlaveConfig`: Hex: 00 → Decimal: 0 → Single device
32  | `SlaveID`: Hex: 00 → Decimal: 0 → Slave Address: 0
33-43| `SymbolName`: ASCII: Big Number + Null Termination '\0'
44   | `DTYPE`: Hex: 06 → Long

### Data Decoding
Example from `Basic.ino`:
`<BLAECK.WRITE_DATA, 255, 255, 255, 255>`:
````
ASCII: <  B  L  A  E  C  K  :  °  :  °  °  °  °  :  °  :  °  °  :  °  :  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  /  B  L  A  E  C  K  >  \r \n
HEX:   3C 42 4C 41 45 43 4B 3A D2 3A FF FF FF FF 3A 00 3A C8 29 3A 00 3A 00 00 B8 1E FD 40 01 00 D8 E6 32 7C 00 00 00 00 00 XX XX XX XX 2F 42 4C 41 45 43 4B 3E 0D 0A
Byte:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52
````

 Byte | Description
----|---------------------------------------------
8   | `MSGKEY`: D2 → Data
10-13| `MSGID`: Hex: FF FF FF FF → Decimal: 4294967295
15  | `RestartFlag`: Hex: 00 → Device has not restarted
17-18| `SchemaHash`: Hex: C8 29 → CRC16-CCITT of signal names + datatype codes (little-endian)
20  | `TimestampMode`: Hex: 00 → No timestamp
22-23| `SymbolID`: Hex: 00 00 → Decimal: 0
24-27| `DATA`: Float → 4 Bytes; Hex: B8 1E FD 40 → Float: 7.91
28-29| `SymbolID`: Hex: 01 00 → Decimal: 1
30-33| `DATA`: Long  → 4 Bytes; Hex: D8 E6 32 7C → Long: 2083710680
34   | `StatusByte`: 0 → Normal Transmission
35-38| `StatusPayload`: 4 Bytes (all `0x00` when `StatusByte=0`)
39-42| `CRC32`: 4 Bytes (calculated from bytes 8-38)
43-52| `/BLAECK>\r\n`
