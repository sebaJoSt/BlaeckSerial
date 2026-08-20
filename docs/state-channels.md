# State channels

A state channel reports a value that is shown but never logged: a status line, an uptime, what
a control is set to.

That is the difference from a signal. A signal is sampled on every interval and kept, so a
month of it fills a table you can look back at. A state channel has one value, the one it holds
now. Nothing is kept, and nothing is written to the database.

## A sketch with two channels

```cpp
#include <BlaeckSerial.h>

BlaeckSerial Blaeck;

unsigned long uptime = 0;
char status[40] = "starting";

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.addStateChannel(F("Uptime"), &uptime).withUnit(F("s"));
  Blaeck.addStateChannel(F("Status"), status).withIcon(F("mdi:message-text"));
}

void loop()
{
  Blaeck.tick();

  if (millis() / 1000UL != uptime)
  {
    uptime = millis() / 1000UL;
    Blaeck.writeState(F("Uptime"));
  }
}
```

- `addStateChannel()` takes a name and the address of the variable to read. The variable has to
  be a global, as a signal's does.
- `writeState()` sends the value. There is nothing to pass, because the channel already knows
  where to look.
- The `if` is the point of the whole thing. A state channel is sent when it changes, not on a
  schedule, so nothing goes out on the passes where the second has not ticked over.

Home Assistant shows two sensors. Neither ends up in the database.

## What a channel can carry

The types are a signal's: the nine numeric ones, `bool`, and a `char` buffer. A `bool` channel
becomes a binary sensor.

Pass no variable and the channel carries only what you hand it:

```cpp
Blaeck.addStateChannel(F("Status"));

char text[40];
snprintf(text, sizeof(text), "running at %u Hz", frequency);
Blaeck.writeState(F("Status"), text);
```

Text longer than 255 bytes is cut short. This second form is for text channels only - passing
text to a numeric channel is dropped, and the debug stream says so.

Channel names are copied, unlike signal names, so a name built in a buffer needs nothing kept
alive afterwards.

## Text the channel works out for itself

Sending on change means remembering to send. A text channel can name a function instead, and
that function runs whenever the value is wanted:

```cpp
const char *statusText()
{
  static char text[40];
  snprintf(text, sizeof(text), "up %lu s", millis() / 1000UL);
  return text;
}

Blaeck.addStateChannel(F("Status")).withStateText(statusText);
```

Nothing has to push this one to keep it current: it is worked out when it is read, so it cannot
be stale. Push it anyway with `writeState(F("Status"))` when something happens that a host
should see at once.

The function runs while a frame is being built. It may read variables and format text, and
nothing else - a `write()`, `writeState()` or `writeEvent()` inside it breaks the frame it
interrupts.

Only text takes a function. A numeric channel points at a variable, and anything a function
would have worked out can be put in one first.

## Describing a channel

The calls are the ones on [Signals](signals.md), and they mean the same here: `withUnit()`,
`withStateClass()` and `withDisplayPrecision()` on a number, `withIcon()`, `withDeviceClass()`,
`diagnostic()`, `disabledByDefault()` and `forceUpdate()` on anything.

Two are particular to a state channel:

| Call | What it does |
|---|---|
| `withStateText(getter)` | Names the function that produces the value. Text only |
| `withOptions(F("a,b,c"))` | The complete set of values this channel reports. Text only, and needs `withDeviceClass(F("enum"))` |

Device classes come from a different list for each type: `F("temperature")` on a number,
`F("timestamp")` or `F("date")` on text, `F("door")` or `F("motion")` on a `bool`. Some names
are on two lists meaning different things - `battery` is a percentage on a number and low or
normal on a `bool`. A name from the wrong list does not fail quietly: the entity never appears.

## Channels a command owns

A command that reports its own value with `withOwnState()` creates a channel here. It belongs
to the command: `addStateChannel()` refuses the name, and it is sent with `writeCommandState()`
instead. See [Commands](commands.md).

It takes a slot from the same table all the same.

## When a channel does not fit

The table has a fixed size. Count the channels commands own along with your own - see
[Configuration](configuration.md).

```cpp
if (Blaeck.hasRejectedStateChannels())
{
  Blaeck.printRejections(&Serial);
}
```

A command whose channel was dropped keeps no state at all, so a full table costs a control its
value rather than costing you a channel.
