# State channels

A state channel holds a value you send on demand, rather than one sampled on a schedule. Use
it for status displays, diagnostics, and control feedback: an uptime counter, what mode the
device is in, or what position a slider was last set to. They appear in Home Assistant as
sensors.

Unlike a signal, which can be sent at regular intervals, a state channel sits there until you
call `writeState()` to send it.

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

    if (uptime == 5)
    {
      strcpy(status, "running");
      Blaeck.writeState(F("Status"));
    }
  }
}
```

- `addStateChannel()` takes a name and the address of a variable to read. The variable has to
  be a global, as a signal's does.
- `writeState()` sends the value. There is nothing to pass, because the channel already knows
  where to look.
- You decide when to call `writeState()`. Uptime sends every second when it changes. Status
  sends once when the uptime reaches 5 seconds.

## What a channel can carry

A state channel accepts the same types as a signal: the nine numeric types, `bool` (shown as a
binary sensor), and a `char` buffer for text.

A channel can also be declared with no variable at all, naming its type with a tag instead of a
pointer, and carrying only what you hand `writeState()` each time:

```cpp
void setup()
{
  Blaeck.addStateChannel(F("Status"), BlaeckText);
}

void reportStatus()
{
  char text[40];
  snprintf(text, sizeof(text), "running at %u Hz", frequency);
  Blaeck.writeState(F("Status"), text);
}
```

Text longer than 255 bytes is cut short.

The tag is what the second argument would have said as a pointer. `BlaeckText`, `BlaeckBool`,
and one per numeric type - `BlaeckByte`, `BlaeckShort`, `BlaeckUShort`, `BlaeckInt`,
`BlaeckUInt`, `BlaeckLong`, `BlaeckULong`, `BlaeckFloat`, `BlaeckDouble`. On AVR a `double` is
four bytes, so `BlaeckDouble` names the same channel `BlaeckFloat` does, exactly as `double *`
and `float *` already do.

Text is dropped on a text channel that already has a variable too. That channel reads its own
value, so a pushed line would show until the next catalog poll and then be replaced without a
word. Send it with `writeState(F("Status"))` and no text, which reports what the channel holds.

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

Blaeck.addStateChannel(F("Status"), BlaeckText).withStateText(statusText);
```

Nothing has to push this one to keep it current: it is worked out when it is read, so it cannot
be stale. Push it anyway with `writeState(F("Status"))` when something happens that a host
should see at once.

The function runs while a frame is being built. It may read variables and format text, and
nothing else - a `write()`, `writeState()` or `writeEvent()` inside it breaks the frame it
interrupts.

Only text takes a function. A numeric channel points at a variable, and anything a function
would have worked out can be put in one first.

A channel has one source, not two. `withStateText()` on a channel declared with a variable is
ignored and says so on the debug stream, rather than quietly taking over from the variable.

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
