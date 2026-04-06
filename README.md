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

## Protocol

For the binary wire format, message keys, elements, status codes, data types, timestamp modes, and decoding examples, see [docs/protocol.md](docs/protocol.md).

