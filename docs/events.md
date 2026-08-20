# Events

An event is something that happened: a threshold was crossed, a motor stalled, a run finished.

It is the third of the three kinds, and the one with no value at all. A signal is a value
sampled over and over. A state channel is a value that stands until it changes. An event has
neither - it is an occurrence, at one moment, with nothing to read afterwards.

## Declaring a channel

An event channel names every event it can ever report, up front:

```cpp
Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"))
    .withIcon(F("mdi:pulse"));
```

The list is not optional. What travels on the wire is a pair of numbers - which channel, which
type - so a host can only read an event against the list it was given. That is also why the
order matters: position fixes each type's number, so you may add to the end of a list, but
never reorder it.

Add a type that only some boards have:

```cpp
Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"));

if (hasBatteryMonitor)
  Blaeck.addEventType(F("Activity"), F("low_battery"));
```

`addEventType()` returns false if the type is blank, is already on the list, names a channel
that was never declared, or does not fit.

Event types are written as identifiers - `idle_warning`, not `Idle warning`. Home Assistant
shows them as they are.

## Reporting an event

```cpp
Blaeck.writeEvent(F("Activity"), F("idle_warning"));
```

Fire and forget. A host may show it, and it is never logged as data.

The type has to be one the channel declared. An event on a channel or type that was never
declared is dropped without a word.

Because an event carries nothing else, the wording is fixed when the sketch is compiled. If you
need to say how long, how many or how far, that is a value: report it on a state channel, or
log it as a signal.

## Describing a channel

| Call | What it does |
|---|---|
| `withIcon(F("mdi:pulse"))` | A [Material Design Icons](https://pictogrammers.com/library/mdi/) name |
| `withDeviceClass(F("doorbell"))` | What kind of thing the channel reports |
| `diagnostic()` | Marks it as information about the device rather than what it does |
| `disabledByDefault()` | Registered, but switched off until someone enables it |

Strings must be `F()` literals. The channel name is copied; the types and everything above are
stored as pointers.

## Sizing

Two tables are involved. One holds the channels. The other holds the types, and it is shared by
every channel - four channels of five types each need room for twenty types, not for twenty per
channel.

```cpp
Blaeck.begin(&Serial)
    .withEventChannels(4)
    .withEventTypes(20);
```

A type costs 5 bytes on AVR, the cheapest entry the library keeps. See
[Configuration](configuration.md) for the defaults.

```cpp
if (Blaeck.hasRejectedEventChannels())
{
  Blaeck.printRejections(&Serial);
}
```

A dropped type is worse than a dropped channel: the channel still reports, but one of the things
it was meant to say is missing.
