# State channels

A state channel reports a current value that is shown but never logged: a status line, an
uptime, what a control is set to.

That is the whole difference from a signal. A signal is sampled on every interval and kept as
history, so a year of it fills a table. A state channel has one value, the one it holds now.
Use a signal for what you want to look back at, and a state channel for what only matters at
this moment.

## Declaring a channel

Point the channel at a variable and it reads that variable whenever the value is wanted:

```cpp
unsigned long uptime = 0;

Blaeck.addStateChannel(F("Uptime"), &uptime).withUnit(F("s"));
```

The types are the same as a signal's: the nine numeric ones, `bool`, and a `char` buffer. As
with a signal, the variable has to be a global.

A channel with no variable behind it carries only what you write to it:

```cpp
Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:message-text"));
```

Unlike a signal name, a channel name is copied, so a name built at runtime needs no buffer kept
alive afterwards.

## Sending a value

`writeState()` sends one. Which form you use depends on what the channel was given:

```cpp
char text[40];
snprintf(text, sizeof(text), "running at %u Hz", frequency);
Blaeck.writeState(F("Status"), text);
```

That is for a channel declared with no variable. Text longer than 255 bytes is cut short.

```cpp
uptime = millis() / 1000UL;
Blaeck.writeState(F("Uptime"));
```

That is for one that points at a variable: there is nothing to pass, because the channel
already knows where to look. It is the only way to send a numeric channel — passing text to one
is dropped, and the debug stream says so.

Send a value whenever it changes. Without a `writeState()` the value is only read when a host
asks for it, so a dashboard can sit on an old one for a long time.

## Text a channel builds for itself

A text channel can name a function that produces its value instead:

```cpp
const char *statusText()
{
  static char text[40];
  snprintf(text, sizeof(text), "up %lu s", millis() / 1000UL);
  return text;
}

Blaeck.addStateChannel(F("Status")).withStateText(statusText);
```

The function runs when a host asks, so the value cannot go stale and the sketch never has to
push one to keep it in step. Push anyway with `writeState(F("Status"))` when something happens
that a host should see at once.

The function runs while a frame is being assembled. It must only read variables and format
text — a `writeState()`, `writeEvent()` or `write()` inside it corrupts the frame being built.

Only text channels take a function. A numeric channel points at the variable, and anything a
function would have worked out can be assigned to a variable first.

## Describing a channel

The same idea as a signal, and mostly the same calls:

| Call | What it does | Numbers | Text | Bool |
|---|---|:-:|:-:|:-:|
| `withUnit(F("s"))` | Unit shown after the value | ● | | |
| `withStateClass(...)` | `BLAECK_STATE_CLASS_MEASUREMENT`, `_TOTAL` or `_TOTAL_INCREASING` | ● | | |
| `withDisplayPrecision(1)` | Number of decimal places | ● | | |
| `withStateText(getter)` | The function that produces the value | | ● | |
| `withOptions(F("a,b,c"))` | The complete set of values this channel reports | | ● | |
| `withDeviceClass(F("timestamp"))` | What the value is | ● | ● | ● |
| `withIcon(F("mdi:pulse"))` | A [Material Design Icons](https://pictogrammers.com/library/mdi/) name | ● | ● | ● |
| `diagnostic()` | Marks it as information about the device | ● | ● | ● |
| `disabledByDefault()` | Registered, but switched off until someone enables it | ● | ● | ● |
| `forceUpdate()` | Report every value, even one identical to the last | ● | ● | ● |

`withOptions()` needs `withDeviceClass(F("enum"))`, and every value the channel reports has to
be in the list.

Device classes come from a different list for each type. `F("temperature")` on a number,
`F("timestamp")` or `F("date")` on text, `F("door")` or `F("motion")` on a `bool` — a `bool`
channel is a binary sensor, and some names mean different things on the two lists. A name from
the wrong list does not fail quietly: the entity never appears at all.

Strings must be `F()` literals, and are stored as pointers rather than copied.

## Channels a command owns

A command that reports its own value with `withOwnState()` creates a channel here. It is the
command's: `addStateChannel()` refuses the name, and it is sent with `writeCommandState()`
rather than `writeState()`. See [Commands](commands.md).

It comes out of this table all the same, so count it when sizing.

## When a channel does not fit

The state channel table has a fixed size, and every channel counts against it, including the
ones commands own — see [Configuration](configuration.md).

```cpp
if (Blaeck.hasRejectedStateChannels())
{
  Blaeck.printRejections(&Serial);
}
```

A command whose channel was dropped keeps no state at all, so a full table costs a control's
value rather than just a channel.
