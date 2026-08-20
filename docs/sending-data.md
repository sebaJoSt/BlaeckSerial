# Sending data

Signal values travel in one of three ways: on a schedule the host asks for, on an occasion your
sketch decides, or only when something has changed. This page covers all three, and what a
sketch can attach to the data on the way out.

## The host owns the schedule

Your sketch does not choose how often it logs. A host asks for an interval and can stop it
again:

```
<BLAECK.ACTIVATE,1000>
<BLAECK.DEACTIVATE>
```

`Blaeck.tick()` in `loop()` is what honours it. It reads whatever arrived, dispatches commands,
and sends every signal once the interval has elapsed - so it does nothing at all until a host
has asked for data:

```cpp
void loop()
{
  temperature = readSensor();
  Blaeck.tick();
}
```

Two smaller calls do the halves separately. `Blaeck.read()` only reads and dispatches, for a
device that answers commands but logs nothing. `Blaeck.timedWriteAllData()` only sends when
due, for one that logs but answers nothing.

Ask what the host decided:

```cpp
if (Blaeck.isTimedDataActive())
  Serial.println(Blaeck.getIntervalMs());
```

Nothing on the device can set the interval. Reading it is for showing it, or for keeping it
across a power cut.

## Sending on your own occasion

`writeAllData()` sends every signal now, whatever the schedule says:

```cpp
if (temperature > 40.0)
  Blaeck.writeAllData();
```

`write()` sends one signal, and stores the value as it goes:

```cpp
Blaeck.write("Temperature", readSensor());
```

Both are independent of the interval, so a board no host has activated can still send when
something happens.

A name is looked up by walking the signals and comparing names, which costs more the more
signals there are. In anything that runs often, resolve the name once and use the number:

```cpp
int tempIndex = Blaeck.findSignalIndex("Temperature");

Blaeck.write(tempIndex, readSensor());
```

Writing both edges of a short-lived value is what makes the logged data say how long it lasted:

```cpp
if (triggered && !pulse)
{
  pulse = true;
  Blaeck.write("Pulse", pulse);
}
if (pulse && millis() - pulseSince >= 2000)
{
  pulse = false;
  Blaeck.write("Pulse", pulse);
}
```

## Sending only what changed

Where values change rarely, sending all of them on every interval is waste. `update()` stores a
value and marks the signal as changed; `tickUpdated()` then sends only the marked ones:

```cpp
void loop()
{
  Blaeck.update("Temperature", readSensor());
  Blaeck.tickUpdated();
}
```

`update()` sends nothing by itself - that is the whole difference from `write()`.

`writeUpdatedData()` sends the changed ones now, without waiting for the interval.
`markSignalUpdated()` marks one without changing its value, `markAllSignalsUpdated()` marks
every signal, `clearAllUpdateFlags()` forgets the marks, and `hasUpdatedSignals()` says whether
any are set.

## Sampling just before a write

Rather than reading sensors on a timer of your own and hoping the two line up, name a function
to run immediately before signal data goes out:

```cpp
void readAllSensors()
{
  temperature = readSensor();
}

Blaeck.setBeforeWriteCallback(readAllSensors);
```

It runs in normal `loop()` context, so `Serial` and `delay()` are safe in it.

## Timestamps

By default the data carries no time and the host stamps it on arrival. Two modes change that:

```cpp
Blaeck.setTimestampMode(BLAECK_MICROS);
```

`BLAECK_MICROS` needs nothing further. The library supplies `micros()` and tracks the overflow,
so the count keeps climbing past the 71 minutes a 32-bit microsecond counter holds. It tracks
that overflow as data is written, so a device writing less often than every 71 minutes misses
one and wants a real clock instead.

```cpp
unsigned long long unixMicros()
{
  return (unsigned long long)rtc.getEpoch() * 1000000ULL;
}

Blaeck.setTimestampCallback(unixMicros);
Blaeck.setTimestampMode(BLAECK_UNIX);
```

`BLAECK_UNIX` takes the time from a clock only your sketch can reach - an RTC, or an
NTP-backed time. Without a callback it stamps zero, so every reading lands in 1970. Check with
`hasValidTimestampCallback()`, which is the one that answers whether the data really carries a
time.

Set the mode in `setup()`. Switching it partway through a log restarts the overflow tracking,
and the timestamps either side of the change do not belong on one axis.

You can also stamp a write yourself, for values recorded earlier or a clock the callback cannot
reach:

```cpp
Blaeck.writeAllData(42, 1723600000000000ULL);
```

The number before it is a message ID: a host sends one with a request and gets it back on the
answer. Leave both out and the library supplies them.

## Buffered writes

A frame is either assembled in RAM and sent in one call, or written out piece by piece as it is
produced. Buffering costs `60 + signals * 30` bytes of SRAM and is easier on a USB bridge;
writing directly costs no RAM at all.

The default is per board: off on AVR and on the mbed cores, on everywhere else. Change it if
you have a reason to:

```cpp
Blaeck.setBufferedWrites(true);
```

`isBufferedWrites()` reports what is in force, which is worth checking on AVR, where it is off
unless asked for.

## What a host reads first

Before any of this means anything, a host needs the catalogs: the signal list, and the metadata,
commands, state channels and event channels that go with it. They are sent when the device
starts and again whenever anything in them changes, so a sketch rarely calls one itself.

`writeSymbols()`, `writeSignalConfig()`, `writeCommands()`, `writeStateChannels()`,
`writeEventChannels()` and `writeDevices()` send them on demand. A value names its signal or
channel by position in one of these lists, which is why a list that changed is always sent
ahead of the next value.
