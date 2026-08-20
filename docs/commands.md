# Commands

A command is something sent to your sketch from outside: change a setpoint, switch a relay on,
press a button. You write a function, register it under a name, and BlaeckSerial calls it when
that name arrives.

## Handling a command

This is a command as it arrives on the serial port:

```
<SET_FREQ,1.5>
```

Angle brackets around it, the name first, then the parameters, separated by commas. You can
type one into the serial monitor yourself.

You write a function that runs when it arrives, and register it in `setup()`. A whole sketch
that answers this one command:

```cpp
#include <BlaeckSerial.h>

BlaeckSerial Blaeck;

float frequency = 1.0;

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  frequency = atof(params[0]);
}

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial);

  Blaeck.addSignal(F("Frequency"), &frequency);
  Blaeck.onNumberCommand("SET_FREQ", onSetFreq).withRange(0.0, 2.0, 0.01);
}

void loop()
{
  Blaeck.tick();
}
```

- The handler is a plain function, written outside `setup()` and `loop()`. Every handler has
  these three parameters, whatever the command carries. `params` holds the parameters as text -
  here `params[0]` is `1.5` - and `paramCount` says how many arrived.
- `onNumberCommand()` in `setup()` ties the name `SET_FREQ` to the function, and says the
  control is a number between 0 and 2.
- `Blaeck.tick()` in `loop()` is what reads the serial port and calls the handler. A sketch
  that answers commands but logs nothing calls `Blaeck.read()` instead.

Home Assistant shows this as a number input. Setting it to 1.5 is what sent the command above.

The name travels on the wire, so write it as an identifier: `SET_FREQ`, not `Set Frequency`. It
may not start with `#`, `@` or `BLAECK.`.

## The kinds of control

| Register with | Control | The handler reads |
|---|---|---|
| `onNumberCommand()` | Number input or slider | `atof(params[0])` |
| `onSwitchCommand()` | Switch | `params[0]`, always `"0"` or `"1"` |
| `onSelectCommand()` | Drop-down list | `atoi(params[0])`, the position in the list |
| `onButtonCommand()` | Button | nothing, a press carries no value |
| `onTextCommand()` | Text field | `params[0]` |
| `onCommand()` | none | whatever was sent |

Use `onCommand()` for something you drive yourself, from a terminal or your own program.
It registers the handler and nothing else, so no control appears anywhere.

A number needs `withRange()` and a select needs `withOptions()`, and both have to come first in
the chain. Without them there is no control to build, so the compiler asks for them rather than
letting the command ship half declared.

BlaeckSerial checks a value before your handler runs. A number outside the range, a position
that is not in the list, a switch value that is neither `0` nor `1`, text longer than you
allowed: all are refused and reported, and the handler never sees them.

## Describing a command

Every kind takes these:

| Call | What it does |
|---|---|
| `withDisplayName(F("Frequency"))` | Label shown instead of the name. The wire keeps using the name |
| `withIcon(F("mdi:tune"))` | A [Material Design Icons](https://pictogrammers.com/library/mdi/) name |
| `config()` | Files it as a setting. Home Assistant keeps it off the generated dashboard |
| `diagnostic()` | Files it as something about the board rather than what it does |

The rest belong to one kind:

**Number**

```cpp
Blaeck.onNumberCommand("SET_TEMP", onSetTemp)
    .withRange(5.0, 30.0, 0.5)
    .withUnit(F("\xC2\xB0" "C"))
    .withDeviceClass(F("temperature"))
    .withMode(BLAECK_NUMBER_MODE_BOX);
```

`withRange(min, max, step)` is the range accepted. `step` is display resolution only - a value
between two steps is still accepted, so round it yourself if the sketch needs that.
`withUnit()` labels the input. `withDeviceClass()` names the quantity and needs the matching
unit; Home Assistant then converts for a reader whose units differ, and the value arrives back
in the unit you declared. `withMode()` asks for a typed box or a slider; leave it out and Home
Assistant decides.

**Select**

```cpp
Blaeck.onSelectCommand("SET_WAVE", onSetWave)
    .withOptions(F("Sine,Square,Triangle,Sawtooth"));
```

The handler is handed the position in the list, counting from 0, whether the option was sent by
name or by number. Do not name an option `none` - Home Assistant reads that as nothing
selected. `getSelectOptionNameAt()` gives you the text of a position, so the sketch does not
have to keep the names twice.

**Text**

```cpp
Blaeck.onTextCommand("SET_LABEL", onSetLabel)
    .withMaxLength(sizeof(DeviceLabel) - 1)
    .withMode(BLAECK_TEXT_MODE_PASSWORD);
```

`withMaxLength()` is the longest value accepted, 255 at most and 255 by default. Anything
longer is refused, so your handler can copy what it is given. `withMode()` asks Home Assistant
to mask the field while it is typed. That hides the value on screen and nowhere else - it still
travels the wire as plain characters.

**Switch**

`withDeviceClass(F("outlet"))` for a mains socket, `F("switch")` for anything else. It changes
the icon and the wording only.

**Button**

`withDeviceClass()` takes `F("restart")`, `F("identify")` or `F("update")`. `withPressPayload()`
gives the press a fixed set of parameters:

```cpp
Blaeck.onButtonCommand("ACTIVATE_ALL", onActivateRange)
    .withPressPayload(F("1,40"))
    .withDisplayName(F("Activate all"));
```

The handler then reads `params[0]` and `params[1]` as it would from any other sender. Nothing
checks a press payload, so a typo in it is only found by what the handler does with it.

Every string here must be an `F()` literal. They are stored as pointers and never copied.

## The state of a control

Sending a value is a request, not a fact. It can be clamped, refused, lost on the way, or
replaced by the device on its next boot. So a control reports its own value back, and the
dashboard shows what the device holds rather than what was last typed into it.

Say which value that is, and send it from the handler once it has changed:

```cpp
float offset = 0.0;

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  offset = atof(params[0]);
  Blaeck.writeCommandState(command);
}

Blaeck.onNumberCommand("SET_OFFSET", onSetOffset)
    .withRange(-100.0, 100.0, 0.1)
    .withOwnState(F("Offset"), &offset);
```

`withOwnState()` gives the command a channel of its own, read straight from the variable.
`writeCommandState()` sends it, and `command` is the name the handler was already given.
Without that call the value is only sent when a host asks for it.

The channel costs a slot in the state channel table - see [State channels](state-channels.md).

The other way is to report on a signal:

```cpp
float frequency = 1.0;

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  frequency = atof(params[0]);
  Blaeck.write("Frequency", frequency);
}

Blaeck.addSignal(F("Frequency"), &frequency);
Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
    .withRange(0.0, 2.0, 0.01)
    .withStateFromSignal(F("Frequency"));
```

`withStateFromSignal()` points the command at a signal you already added. There is no
`writeCommandState()` here - the signal reports the value, so `Blaeck.write()` sends it. Leave
that call out and the control catches up on the next logged reading.

That is the whole difference between the two: a signal is logged, a state channel is not. Use a
signal for a setpoint that belongs in the data next to what it controls.

A button has neither. Home Assistant's button subscribes to nothing, so a state it declared
could never arrive.

## When a command does not fit

The command table has a fixed size. Registering more commands than it holds drops the extra
ones - see [Configuration](configuration.md) for how to make it bigger.

Ask whether that happened:

```cpp
if (Blaeck.hasRejectedCommands())
{
  Blaeck.printRejections(&Serial);
}
```
