# Sending data

Your sketch does not decide how often it logs. The host does, and your sketch answers.

## A sketch that logs

```cpp
#include <BlaeckSerial.h>

BlaeckSerial Blaeck;

float temperature = 0.0;

float readSensor()
{
  return analogRead(A0) * 0.1;
}

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.addSignal(F("Temperature"), &temperature);
}

void loop()
{
  temperature = readSensor();
  Blaeck.tick();
}
```

`Blaeck.tick()` does two things on every pass: it reads whatever arrived on the serial port and
runs the command handlers, and it sends every signal once the interval has elapsed.

Until a host asks, it sends nothing:

```
<BLAECK.ACTIVATE,1000>      one reading per second
<BLAECK.DEACTIVATE>         stop
```

Loggbok sends these for you. You can type them into the serial monitor to see the data for
yourself.

Two smaller calls do one half each. `Blaeck.read()` only reads and dispatches, for a device
that answers commands and logs nothing. `Blaeck.timedWriteAllData()` only sends, for one that
logs and answers nothing.

Your sketch cannot set the interval, but it can read what the host asked for:

```cpp
if (Blaeck.isTimedDataActive())
  Serial.println(Blaeck.getIntervalMs());
```

That is worth doing to show the interval on a state channel, or to remember it across a power
cut.

## Sending when something happens

The interval is for logging. Some values are worth sending the moment they change, and two
calls do that whatever the interval says.

`writeAllData()` sends every signal now:

```cpp
if (temperature > 40.0)
  Blaeck.writeAllData();
```

`write()` sends one signal, and stores the value on the way:

```cpp
Blaeck.write("Temperature", readSensor());
```

Send both edges of a short-lived value, and the logged data says how long it lasted. Leave the
end of it to a host's timeout and that duration exists nowhere:

```cpp
bool pulse = false;
unsigned long pulseSince = 0;

void loop()
{
  Blaeck.tick();

  if (triggered() && !pulse)
  {
    pulse = true;
    pulseSince = millis();
    Blaeck.write("Pulse", pulse);
  }
  if (pulse && millis() - pulseSince >= 2000)
  {
    pulse = false;
    Blaeck.write("Pulse", pulse);
  }
}
```

`write()` finds the signal by walking the list and comparing names, which costs more the more
signals there are. Where it runs often, look the name up once and pass the number:

```cpp
int tempIndex = -1;

void setup()
{
  // ... after addSignal()
  tempIndex = Blaeck.findSignalIndex("Temperature");
}

void loop()
{
  Blaeck.write(tempIndex, readSensor());
}
```

## Sending only what changed

Where values change rarely, sending all of them every second is waste. `update()` stores a value
and marks the signal as changed. `tickUpdated()` is then `tick()` for the marked ones only:

```cpp
void loop()
{
  Blaeck.update("Temperature", readSensor());
  Blaeck.tickUpdated();
}
```

`update()` sends nothing by itself. That is its whole difference from `write()`.

`writeUpdatedData()` sends the changed ones at once, without waiting for the interval.

## Reading the sensors at the right moment

The first sketch on this page reads its sensor on every pass of `loop()`. That works, and it
means every reading sent is up to one pass old. Where that matters, name a function instead and
it runs immediately before signal data goes out:

```cpp
void readAllSensors()
{
  temperature = readSensor();
}

Blaeck.setBeforeWriteCallback(readAllSensors);
```

It runs in normal `loop()` context, so `Serial` and `delay()` are safe in it.

## Timestamps

By default the data carries no time and the host stamps it when it arrives. That is fine when
the link is quick and nothing buffers.

For a time the device itself stands behind, pick a mode in `setup()`. `BLAECK_MICROS` needs
nothing else - the library reads `micros()` and counts the overflows, so the number keeps
climbing past the 71 minutes a 32-bit microsecond counter holds. It counts them as data is
written, so a device that writes less often than that needs a real clock instead:

```cpp
Blaeck.setTimestampMode(BLAECK_MICROS);
```

`BLAECK_UNIX` takes the time from a clock only your sketch can reach, so it needs a callback:

```cpp
unsigned long long unixMicros()
{
  return (unsigned long long)rtc.getEpoch() * 1000000ULL;
}

Blaeck.setTimestampCallback(unixMicros);
Blaeck.setTimestampMode(BLAECK_UNIX);
```

Without one it stamps zero and every reading lands in 1970. `hasValidTimestampCallback()` says
whether you have one.

Set the mode once, in `setup()`. Timestamps from either side of a change are not comparable.

## Buffered writes

Data is either assembled in RAM and sent in one call, or written out piece by piece as it is
produced. Buffering costs `60 + signals * 30` bytes of SRAM and suits a USB bridge that dislikes
many small writes; writing directly costs no RAM at all.

The default is per board: off on AVR, where the SRAM is scarce and the bridge chips take small
writes happily, and on everywhere else. Those defaults are there because of real faults on real
boards, so change this only with a reason:

```cpp
Blaeck.setBufferedWrites(true);
```

## The catalogs

A value on the wire names its signal by position, not by name. So before any of it means
anything, a host has to have the list of signals, and the lists of commands, state channels and
event channels with it.

Your sketch does not have to send these. They go out when the device starts, again whenever a
host asks, and again whenever you change what the device declares - always ahead of the next
value, so nothing is ever read against a list that has moved.
