<img width="590" height="257" alt="blaeckSerial-light" src="https://github.com/user-attachments/assets/bef5e2e5-f0c0-4b70-b08f-f4b4d061268c" /><a href="url"><img src="https://user-images.githubusercontent.com/388152/185908831-4eccf7a6-5f43-405d-b7fe-5225eeba302d.png" height="75"></a>
<a href="url"><img src="https://user-images.githubusercontent.com/388152/186109775-c7f1bb61-4cc0-4dc1-9969-49c2f2e1303f.png"  alt="BlaeckSerial Logo SeeSaw Font" height="70"></a>

![Uploading<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 40 590 257" width="590" height="257">
  <title>blaeckSerial</title>
  <defs>
    <clipPath id="k-clip-light"><rect x="0" y="100" width="700" height="100"/></clipPath>
    <clipPath id="e-clip-light"><path clip-rule="evenodd" d="M200 40H700V260H200Z M336.44 163H440V256.25Z"/></clipPath>
  </defs>
  <g id="wordmark">
    <g id="b">
      <path fill="#121318" d="M0 40H26V106.14A50 50 0 1 1 26 193.86V200H0Z"/>
      <path id="b-droplet" fill="#3355C4" d="M40.00 118.00L66.77 137.09A22 22 0 1 1 32.57 150.03Z"/>
      <path id="b-shine" fill="none" stroke="#F2EEE6" stroke-width="4" stroke-linecap="round" d="M56.26 142.20A13 13 0 0 1 66.22 150.55"/>
    </g>
    <g fill="none" stroke="#121318" stroke-width="26">
      <path id="l" d="M129 40V200"/>
      <g id="a"><path d="M206 113A37 37 0 1 0 206 187A37 37 0 1 0 206 113"/><path d="M243 100V200"/></g>
      <g id="e"><path clip-path="url(#e-clip-light)" d="M322 113A37 37 0 1 0 322 187A37 37 0 1 0 322 113"/><path id="e-bar" stroke-width="26" d="M285 150H359"/></g>
      <path id="c" d="M456.16 123.84A37 37 0 1 0 456.16 176.16"/>
      <g id="k"><path d="M484 40V200"/><g clip-path="url(#k-clip-light)"><path d="M484 172L584 67"/><path d="M513 142L573 217"/></g></g>
    </g>
  </g>
  <text id="product" x="433.5" y="276.8" text-anchor="middle" fill="#5E6169" font-family="'IBM Plex Mono', ui-monospace, Menlo, monospace" font-weight="500" font-size="66.5">Serial</text>
</svg>
 blaeckSerial-light.svg…]()

===

BlaeckSerial is an Arduino library. It sends any value your sketch holds - sensor readings,
calculated results, text - over the serial port as binary data, using the
[Blaeck protocol](https://sebajost.github.io/blaeck-protocol/).

It is the first part of a chain:

1. **Your Arduino sketch** uses BlaeckSerial to register each variable it sends as a *signal* -
   a temperature, a counter, a switch position. You can also register the commands the board
   accepts and the events it fires.
2. **Loggbok**, a data logging tool, reads the signals over the serial port and stores
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
**File > Examples > BlaeckSerial**.

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
