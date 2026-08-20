# Events

An event is something that happened: a threshold was crossed, a motor stalled, a run finished.

It is the third kind, and the one with no value. A signal is a value sampled again and again. A
state channel is a value that stands until it changes. An event is a moment, with nothing to
read afterwards.

## A sketch that reports two events

```cpp
#include <BlaeckSerial.h>

BlaeckSerial Blaeck;

float temperature = 0.0;
bool tooHot = false;

float readSensor()
{
  return analogRead(A0) * 0.1;
}

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.addSignal(F("Temperature"), &temperature);
  Blaeck.addEventChannel(F("Alarm"), F("overheated,cooled_down"))
      .withIcon(F("mdi:thermometer-alert"));
}

void loop()
{
  Blaeck.tick();

  temperature = readSensor();

  if (temperature > 60.0 && !tooHot)
  {
    tooHot = true;
    Blaeck.writeEvent(F("Alarm"), F("overheated"));
  }
  if (temperature < 55.0 && tooHot)
  {
    tooHot = false;
    Blaeck.writeEvent(F("Alarm"), F("cooled_down"));
  }
}
```

- `addEventChannel()` takes a name and every event that channel will ever report, as one
  comma-separated list.
- `writeEvent()` reports one of them. It is fire and forget: a host may show it, and it is never
  logged.
- `tooHot` is what keeps one crossing from firing an event on every pass of `loop()`. The
  library does no such filtering - you decide what counts as having happened.

Home Assistant shows an event entity that fires twice per overheating.

## The list of types

The list is not optional, and its order matters.

What travels on the wire is a pair of numbers: which channel, which type. A host reads them
against the list it was given, so position is what identifies a type. You may add to the end of
a list. Reordering it, or removing an entry from the middle, changes what every later type
means.

Types read as identifiers - `cooled_down`, not `Cooled down`. Home Assistant shows them as
written.

A type the board only has sometimes can be added on its own:

```cpp
Blaeck.addEventChannel(F("Alarm"), F("overheated,cooled_down"));

if (hasBatteryMonitor)
  Blaeck.addEventType(F("Alarm"), F("low_battery"));
```

It returns false if the type is blank, is already on the list, names a channel that was never
declared, or does not fit.

## Saying how much

An event carries nothing but its own name, so the wording is fixed when you compile. There is
no way to attach a temperature to `overheated`.

Where a number matters, something else carries it. Log it as a signal if you want it in the
history, or put it on a [state channel](state-channels.md) if it only has to be visible.

## Describing a channel

| Call | What it does |
|---|---|
| `withIcon(F("mdi:pulse"))` | A [Material Design Icons](https://pictogrammers.com/library/mdi/) name |
| `withDeviceClass(F("doorbell"))` | What kind of thing the channel reports |
| `diagnostic()` | Marks it as information about the device rather than what it does |
| `disabledByDefault()` | Registered, but switched off until someone enables it |

The channel name is copied. The type list and the strings above are stored as pointers, so they
have to be `F()` literals.

## When a channel or type does not fit

Two tables are involved. One holds the channels. The other holds the types, and every channel
draws from it - four channels of five types each need room for twenty types, not twenty each.

```cpp
Blaeck.begin(&Serial)
    .withEventChannels(4)
    .withEventTypes(20);
```

See [Configuration](configuration.md) for the defaults. A type costs 5 bytes on AVR, the
cheapest entry the library keeps, so this is a cheap table to be generous with.

```cpp
if (Blaeck.hasRejectedEventChannels())
{
  Blaeck.printRejections(&Serial);
}
```

That covers both tables. A dropped type is the quieter failure of the two: the channel still
works, and one of the things it was meant to report simply never arrives.
