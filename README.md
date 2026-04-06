<a href="url"><img src="https://user-images.githubusercontent.com/388152/185908831-4eccf7a6-5f43-405d-b7fe-5225eeba302d.png" height="75"></a>
<a href="url"><img src="https://user-images.githubusercontent.com/388152/186109775-c7f1bb61-4cc0-4dc1-9969-49c2f2e1303f.png"  alt="BlaeckSerial Logo SeeSaw Font" height="70"></a>
===



BlaeckSerial is a simple Arduino library to send binary (sensor) data via Serial port to your PC. The data can be sent periodically or requested on demand with [serial commands](#blaeckserial-commands). It supports Master/Slave configuration to include data from additional slave boards connected to the master Arduino over I2C.  
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
BlaeckSerial BlaeckSerial;
```
### Initialize Serial & BlaeckSerial
```CPP
void setup()
{
  Serial.begin(9600);

  BlaeckSerial.begin(
    &Serial,   //Serial reference
    2          //Maxmimal signal count used;
  );
}
```

### Add signals
```CPP
 BlaeckSerial.addSignal("Test Signal 1", &someGlobalVariable);
 BlaeckSerial.addSignal("Test Signal 2", &anotherGlobalVariable);
```

If more signals are added than the configured capacity in `begin(...)`, extra signals are ignored.
You can detect this in your sketch:
```CPP
if (BlaeckSerial.hasSignalOverflow()) {
  Serial.print("Ignored signals: ");
  Serial.println(BlaeckSerial.getSignalOverflowCount());
}
```

### Update your variables and don't forget to `tick()`!
```CPP
void loop()
{
  UpdateYourVariables();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}
```

## BlaeckSerial commands

Here's a full list of serial commands handled by this library:

| Command                      | Description                                                                      |
| ---------------------------- | -------------------------------------------------------------------------------- |
| `<BLAECK.GET_DEVICES>`       | Writes the device information including the device name, hardware version, firmware version and library version |
| `<BLAECK.WRITE_SYMBOLS> `    | Writes symbol list including datatype information.                               |
| `<BLAECK.WRITE_DATA> `       | Writes the binary data.                                                          |
| `<BLAECK.ACTIVATE,first,second,third,fourth byte>`| Activates writing the binary data in user-set interval [ms]<br />Min: 0ms  Max: 4294967295ms<br /> e.g. `<BLAECK.ACTIVATE,96,234>` The data is written every 60 seconds (60 000ms)<br />first Byte: 0b01100000 = 96 DEC<br />second Byte: 0b11101010 = 234 DEC|
| `<BLAECK.DEACTIVATE> `       | Deactivates writing in intervals.                                                |

### Interval lock mode

By default, timed data is client-controlled (`BLAECK.ACTIVATE` / `BLAECK.DEACTIVATE`).
You can lock interval behavior from sketch code:

```CPP
// Fixed interval lock: always send every 500 ms, ignore ACTIVATE/DEACTIVATE
BlaeckSerial.setIntervalMs(500);

// Off lock: disable timed data and ignore ACTIVATE
BlaeckSerial.setIntervalMs(BLAECK_INTERVAL_OFF);

// Back to client control (default behavior)
BlaeckSerial.setIntervalMs(BLAECK_INTERVAL_CLIENT);
```

`setTimedData(...)` has been removed. Use `setIntervalMs(...)` instead.

### Command handler API

Available callbacks:
- `onCommand(...)` and `onAnyCommand(...)` for parsed incoming commands
- `setCommandCallback(...)` (deprecated, still supported with runtime warning)
- `setBeforeWriteCallback(...)` before data is written

Command parser defaults are architecture-aware:
- AVR (`__AVR__`): 48 command chars, 4 registered handlers, 24 command-name chars, 10 params
- Non-AVR: 96 command chars, 12 registered handlers, 40 command-name chars, 10 params

These defaults can be overridden by placing a `BlaeckSerialConfig.h` file in your sketch folder:
```CPP
// BlaeckSerialConfig.h
#define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
#define BLAECK_COMMAND_MAX_HANDLERS_DEFAULT 8
#define BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT 48
#define BLAECK_COMMAND_MAX_PARAMS_DEFAULT 16
#define BLAECK_BUFFERED_WRITES_DEFAULT false
```

PlatformIO users can also use compiler flags in `platformio.ini`:
```ini
build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
```

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
  BlaeckSerial.onCommand("SwitchLED", onSwitchLED);
  BlaeckSerial.onAnyCommand(onAny);
}
```

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
BlaeckSerial.setBufferedWrites(true);   // force ON
BlaeckSerial.setBufferedWrites(false);  // force OFF

Serial.println(BlaeckSerial.isBufferedWrites() ? "ON" : "OFF");
```

## Messages

The messages are in the following format:
````
|Header|--       Message        --||-- EOT  --|
<BLAECK:<MSGKEY>:<MSGID>:<ELEMENTS>/BLAECK>\r\n
````

Type| MSGKEY | Elements| Description
----|--------|---------------------------------|---------------------------------------------
Symbol List | B0 | **`<MasterSlaveConfig><SlaveID><SymbolName><DTYPE>`** | **Up to n symbols.** Response to request for available symbols `<BLAECK.WRITE_SYMBOLS>`
~~Data~~ | ~~B1~~ | ~~**`<SymbolID><DATA>`**`<StatusByte><CRC32>`~~ | Deprecated (Used in BlaeckSerial version 4.3.1 or older)
~~Data~~ | ~~D1~~ | ~~`<RestartFlag>:<TimestampMode><Timestamp(4)>:`**`<SymbolID><DATA>`**`<StatusByte><CRC32>`~~ | Deprecated (Used in BlaeckSerial version 5.x)
Data | D2 | `<RestartFlag>:<SchemaHash>:<TimestampMode><Timestamp(8)>:`**`<SymbolID><DATA>`**`<StatusByte><StatusPayload><CRC32>` | **Up to n data items.** Response to request for data `<BLAECK.WRITE_DATA>`
~~Devices~~ | ~~B2~~ | ~~`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion>`~~ | Deprecated (Used in BlaeckSerial version 3.0.3 or older)
Devices | B3 | **`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion><LibraryName>`** | **Up to n device items.** Response to request for device information `<BLAECK.GET_DEVICES>`
Restarted | C0 | **`<MasterSlaveConfig><SlaveID><DeviceName><DeviceHWVersion><DeviceFWVersion><LibraryVersion><LibraryName>`** | Only first device. Send with the functions `writeRestarted()` and `tick()` first time after device restarted. 
  

 Element|Type    |  DESCRIPTION:
 -------|--------| ---------------------------------------------------------------------------
 `MSGKEY`| byte |  Message Key, A unique key for the type of message being sent; 1 byte transmitted
 `MSGID` | ulong|  Message ID,  A unique message ID which echoes back to transmitter to indicate a response to a message (0 to 4294967295); 4 bytes transmitted
 `DATA`  | (varying)| Message Data, varying data types and length depending on message
 `SymbolID` | uint | Symbol ID number
 `SymbolName` |String0 | Symbol Name - Null Terminated String
 `DTYPE` | byte | DataType  0=bool, 1=byte, 2=short, 3=ushort, 4=int, 5=uint, 6=long, 7=ulong, 8=float, 9=double
  `MasterSlaveConfig`     | byte | 0=Single device, 1=Master, 2=Slave
   `SlaveID`              | byte |             Slave Address
   `DeviceName`           | String0 |          set with public variable `DeviceName`
   `DeviceHWVersion`      | String0 |          set with public variable `DeviceHWVersion`
   `DeviceFWVersion`      | String0 |          set with public variable `DeviceFWVersion`
   `LibraryVersion`       | String0 |          set with preprocessor macro `BLAECKSERIAL_VERSION`
   `LibraryName`          | String0 |          set with preprocessor macro `BLAECKSERIAL_NAME` 
   `StatusByte`           | byte |             1 byte; 0: Normal Transmission, 1: One or more I2C slaves were skipped/unavailable in this frame
   `StatusPayload` (StatusByte=0) | byte |             4 bytes; Reserved (`0x00 0x00 0x00 0x00`)
   `StatusPayload` (StatusByte=1) | byte |             4 bytes; Byte0=SkippedSlaveCount, Byte1=FirstSkippedSlaveID (0xFF unknown), Byte2=FirstSkipReason (0x01 preflight no response, 0x02 runtime timeout/short/malformed/CRC mismatch), Byte3=reserved (0x00)
   `CRC32`                | byte |             4 bytes; CRC order: 32; CRC Polynom (hex): 4C11DB7; Initial value (hex): FFFFFFFF; Final XOR value (hex): FFFFFFFF; reverse data bytes: true; reverse CRC result before Final XOR: true; (http://zorc.breitbandkatze.de/crc.html)
   `RestartFlag`          | byte | Restart Flag, 1 if device restarted since last transmission, 0 otherwise; 1 byte transmitted
   `SchemaHash`           | uint16 | CRC16-CCITT (init=0x0000, poly=0x1021) of signal name bytes + datatype code byte for each signal in order; 2 bytes transmitted (little-endian). Used by hubs and clients to detect signal layout changes at runtime.
   `TimestampMode`        | byte | Timestamp Mode, 0=No timestamp, 1=Microseconds, 2=Unix time; 1 byte transmitted  
   `Timestamp`            | uint64 | Timestamp value (only present if TimestampMode > 0); 8 bytes transmitted. Mode 1: microseconds with overflow tracking. Mode 2: Unix epoch microseconds.
         
   
 
 MSGID is supported by `<BLAECK.GET_DEVICES>`, `<BLAECK.WRITE_SYMBOLS>` and `<BLAECK.WRITE_DATA>`:
 ````
 <BLAECK.GET_DEVICES, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
 <BLAECK.WRITE_SYMBOLS, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
 <BLAECK.WRITE_DATA, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
 ````
 
 ## Decoding Examples
 
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
 
 Byte | DESCRIPTION:
----|---------------------------------------------
8   | `MSGKEY`: B0 -> Symbol List
10-13| `MSGID`: Hex: 00 FF 00 00 -> Decimal: 65280
15  | `MasterSlaveConfig`: Hex: 00 -> Decimal: 0 -> Single device
16  | `SlaveID`: Hex: 00 -> Decimal: 0 -> Slave Address: 0
17-29| `SymbolName`: ASCII: Small Number + Null Termination '\0'
30   | `DTYPE`: Hex: 08 -> Float
31  | `MasterSlaveConfig`: Hex: 00 -> Decimal: 0 -> Single device
32  | `SlaveID`: Hex: 00 -> Decimal: 0 -> Slave Address: 0
33-43| `SymbolName`: ASCII: Big Number + Null Termination '\0'
44   | `DTYPE`: Hex: 06 -> Long

 ### Data Decoding
 Example from `Basic.ino`:
 `<BLAECK.WRITE_DATA, 255, 255, 255, 255>`:
 ````
ASCII: <  B  L  A  E  C  K  :  °  :  °  °  °  °  :  °  :  °  °  :  °  :  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  °  /  B  L  A  E  C  K  >  \r \n
HEX:   3C 42 4C 41 45 43 4B 3A D2 3A FF FF FF FF 3A 00 3A C8 29 3A 00 3A 00 00 B8 1E FD 40 01 00 D8 E6 32 7C 00 00 00 00 00 XX XX XX XX 2F 42 4C 41 45 43 4B 3E 0D 0A
Byte:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52
````
 
 Byte | DESCRIPTION:
----|---------------------------------------------
8   | `MSGKEY`: D2 -> Data
10-13| `MSGID`: Hex: FF FF FF FF -> Decimal: 4294967295
15  | `RestartFlag`: Hex: 00 -> Device has not restarted
17-18| `SchemaHash`: Hex: C8 29 -> CRC16-CCITT of signal names + datatype codes (little-endian)
20  | `TimestampMode`: Hex: 00 -> No timestamp
22-23| `SymbolID`: Hex: 00 00 -> Decimal: 0
24-27| `DATA`: Float -> 4 Bytes; Hex: B8 1E FD 40 -> Float: 7.91
28-29| `SymbolID`: Hex: 01 00 -> Decimal: 1
30-33| `DATA`: Long  -> 4 Bytes; Hex: D8 E6 32 7C -> Long: 2083710680
34   | `StatusByte`: 0 -> Normal Transmission
35-38| `StatusPayload`: 4 Bytes (all `0x00` when `StatusByte=0`)
39-42| `CRC32`: 4 Bytes (calculated from bytes 8-38)
43-52| `/BLAECK>\r\n`

## Data Types

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

## Timestamp Modes

| Mode Value | Name | Description |
|------------|------|-------------|
| 0 | `BLAECK_NO_TIMESTAMP` | No timestamp data included |
| 1 | `BLAECK_MICROS` | Microsecond timestamps using `micros()` |
| 2 | `BLAECK_UNIX` | Unix epoch timestamps (requires callback). `BLAECK_RTC` kept as deprecated alias. |


