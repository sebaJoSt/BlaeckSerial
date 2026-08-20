# Signals

A signal is a variable your sketch sends: a temperature, a counter, a switch position.
BlaeckSerial reads it every time it sends data, so you only have to keep the variable up to
date.

## Registering a signal

Call `addSignal()` in `setup()`, once per variable. Pass a name and the address of the
variable:

```cpp
float temperature;

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.addSignal(F("Temperature"), &temperature);
}
```

BlaeckSerial stores the pointer, not a copy. The variable must therefore be a global, or
anything else that stays alive for as long as the sketch runs. A local variable inside
`setup()` does not work.

These types are accepted:

| | Types |
|---|---|
| Numbers | `byte`, `short`, `unsigned short`, `int`, `unsigned int`, `long`, `unsigned long`, `float`, `double` |
| Boolean | `bool` |
| Text | `char` array |

For text, pass the buffer itself, not its address:

```cpp
char status[16] = "idle";

Blaeck.addSignal(F("Status"), status);
```

## Naming a signal

Wrap the name in `F()`. The name then stays in flash and the signal stores a 2-byte pointer
instead of a copy of the text. On an Uno or a Nano with many signals this is the difference
between fitting and not fitting.

For a series of signals that share a name and end in a number, use `withNameSuffix()`:

```cpp
for (int i = 0; i < 8; i++)
{
  Blaeck.addSignal(F("Sine_"), &sine[i]).withNameSuffix(i + 1);
}
```

This registers `Sine_1` to `Sine_8`. The prefix stays in flash and the digits are produced when
the name is sent, so the names cost no RAM at all. The suffix is a number from 0 to 255.

If you have to build a name at runtime, pass the buffer. It is copied, so you can reuse the
buffer immediately:

```cpp
char signalName[10];

for (int i = 0; i < 8; i++)
{
  snprintf(signalName, sizeof(signalName), "Sine_%d", i + 1);
  Blaeck.addSignal(signalName, &sine[i]);
}
```

## Describing a signal

`addSignal()` is all Loggbok needs to write a signal to the database. Everything you add after
it is for Home Assistant.

A name alone does not say what a number means. `2500` could be a pressure, a runtime or a speed.
You can add that information to `addSignal()` by writing further calls after it:

```cpp
Blaeck.addSignal(F("Temperature"), &temperature)
    .withUnit(F("\xC2\xB0" "C"))
    .withDeviceClass(F("temperature"))
    .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
    .withDisplayPrecision(1);
```

Home Assistant shows this as a temperature sensor in degrees, with one decimal place, and keeps
statistics for it.

Add as many of these as you like, in any order, or none at all.

| Call | What it does | Numbers | Text | Bool |
|---|---|:-:|:-:|:-:|
| `withUnit(F("Hz"))` | Unit shown after the value. Non-ASCII must be UTF-8 | ● | | |
| `withDeviceClass(F("temperature"))` | What the value measures | ● | ● | ● |
| `withStateClass(...)` | `BLAECK_STATE_CLASS_MEASUREMENT`, `_TOTAL` or `_TOTAL_INCREASING`. Leave it out and no statistics are kept | ● | | |
| `withDisplayPrecision(1)` | Number of decimal places. `0` shows an integer | ● | | |
| `withIcon(F("mdi:sine-wave"))` | A [Material Design Icons](https://pictogrammers.com/library/mdi/) name | ● | ● | ● |
| `withDisplayName(F("Output"))` | Label shown instead of the name. The name still identifies the signal | ● | ● | ● |
| `withOptions(F("a,b,c"))` | The complete set of values this signal reports | | ● | |
| `diagnostic()` | Marks it as information about the device rather than a measurement | ● | ● | ● |
| `disabledByDefault()` | Registered, but switched off until someone enables it | ● | ● | ● |
| `forceUpdate()` | Report every reading, even one identical to the last | ● | ● | ● |

Three of them only work together with another:

- `withOptions()` needs `withDeviceClass(F("enum"))`. Every value the signal reports has to be
  in the list, and a unit is ignored next to it.
- `withDeviceClass()` needs a unit Home Assistant accepts for that class. `temperature` with
  `F("bar")` is refused and the signal never appears at all.
- `withStateClass()` needs a unit as well. Home Assistant keeps no statistics for a number
  with nothing to count in.

The last three calls in the table also take an argument, so `diagnostic(isDebugBuild)` works.

A call that cannot mean anything for that type does not compile. A text signal has no decimal
places, and a `bool` has no unit.

All strings must be `F()` literals. They are stored as pointers and never copied.

Metadata is only paid for by the signals that use it. A described signal costs about 15 bytes
of RAM, one you say nothing about costs 2.

## When a signal does not fit

The signal table has a fixed size. If you register more signals than it holds, the extra ones
are dropped — see [Configuration](configuration.md) for how to make it bigger.

Ask whether that happened:

```cpp
if (Blaeck.hasRejectedSignals())
{
  Serial.print("Signals dropped: ");
  Serial.println(Blaeck.getRejectedSignalCount());
}
```
