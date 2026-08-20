<a href="url"><img src="https://user-images.githubusercontent.com/388152/185908831-4eccf7a6-5f43-405d-b7fe-5225eeba302d.png" height="75"></a>
<a href="url"><img src="https://user-images.githubusercontent.com/388152/186109775-c7f1bb61-4cc0-4dc1-9969-49c2f2e1303f.png"  alt="BlaeckSerial Logo SeeSaw Font" height="70"></a>
===

BlaeckSerial is an Arduino library. It sends any value your sketch holds - sensor readings,
calculated results, text - over the serial port as binary data, using the
[Blaeck protocol](https://sebajost.github.io/blaeck-protocol/).

It is the first part of a chain:

1. **Your Arduino sketch** uses BlaeckSerial to register each variable it sends as a *signal* —
   a temperature, a counter, a switch position. You can also register the commands the board
   accepts and the events it fires.
2. **Loggbok**, a measurement logging tool, reads the signals over the serial port and stores
   them in a database. It is also an MQTT bridge: it publishes the signals and commands to a
   broker.
3. **Home Assistant** subscribes to that broker and creates one entity for each: a sensor for
   a signal, a slider or button for a command.

Because your sketch declares what it has, no part of the chain has to be set up by hand. A
signal with a unit arrives in Home Assistant as a sensor with that unit.

Loggbok is an internal tool and is not publicly released. The protocol is documented, so you
can write your own host. The examples in this repository also work with a plain serial monitor.

## A first sketch

This sketch sends two values:

```cpp
#include <BlaeckSerial.h>

BlaeckSerial Blaeck;

float temperature;
long  pressure;

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.DeviceName = "Weather Station";

  Blaeck.addSignal(F("Temperature"), &temperature);
  Blaeck.addSignal(F("Pressure"), &pressure);
}

void loop()
{
  ReadSensors();

  Blaeck.tick();
}
```

Three calls do the work:

- `begin(&Serial)` hands BlaeckSerial the serial port you opened on the line above. On a board
  with more than one port you can pass `&Serial1` instead.
- `addSignal(...)` registers a variable. BlaeckSerial keeps a pointer to it and reads it
  whenever it sends data, so you only have to keep the variable up to date.
- `tick()` reads incoming commands and sends the values when they are due. Call it in every
  `loop()`.

The host decides how often data is sent. It sends `<BLAECK.ACTIVATE,1000>` to get one frame
per second, and `<BLAECK.DEACTIVATE>` to stop. Your sketch does not need to know the interval.

## Documentation

| Guide | What it covers |
|---|---|
| [Signals](docs/signals.md) | Registering values, naming them, and describing how they are shown |
| [Commands](docs/commands.md) | Reacting to commands, and declaring them as controls |
| [State channels](docs/state-channels.md) | Reporting a value that is displayed but not logged |
| [Events](docs/events.md) | Reporting that something happened |
| [Sending data](docs/sending-data.md) | Intervals, sending it yourself, timestamps, buffered writes |
| [Configuration](docs/configuration.md) | Table sizes and compile-time settings |

## Examples

The examples are in `examples/`. In the Arduino IDE, open them with
**File → Examples → BlaeckSerial**.

Start with **Basic**. **WaveformGenerator** uses every feature and shows what a complete
dashboard looks like.

## Reference

Every method is documented in `src/BlaeckSerial.h`, with an example. Your editor shows it when
you hover over a call.

The frame formats are described in the
[Blaeck protocol specification](https://sebajost.github.io/blaeck-protocol/blaeckserial/overview).

## Help and licence

For questions and bug reports, see [SUPPORT.md](SUPPORT.md). To contribute, see
[CONTRIBUTING.md](CONTRIBUTING.md). BlaeckSerial is released under the MIT licence
([LICENSE.md](LICENSE.md)).
