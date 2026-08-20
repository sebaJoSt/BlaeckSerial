/*
  Commands.ino

  How to add your own commands. There are two kinds:

    Plain   onCommand()          You parse the parameters yourself.
                                 Serial only.

    Typed   onSwitchCommand()    You declare what the command is. The library
            onButtonCommand()    checks the value first, and describes the
                                 command well enough that Loggbok can publish
                                 it and Home Assistant can show a control.

  <SwitchLED> and <LED> switch the same LED, one plain and one typed, so the
  difference is easy to see.

  A typed switch also names a state signal ("LED_State"). That is what the
  control follows, so it shows what the board really did instead of assuming
  the command worked.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  Command syntax:

    <COMMAND,PARAMETER01,PARAMETER02,...,PARAMETER10>

    Parameters arrive as text; convert with atoi/atol/atof as needed. An empty
    one keeps its slot, so <SwitchLED,> below reads as params[0][0] == '\0'.

  The circuit:
    - No wiring required, the on-board LED is used.
      LED_BUILTIN is pin 13 on the UNO and MEGA, and the right pin elsewhere.

  Typed, and so also controls in Home Assistant:

        <LED,1>                       Turn on the LED
        <LED,0>                       Turn off the LED
        <LED,7>                       Rejected: a switch only accepts 0 or 1
        <Ping>                        Takes no value. Answers on the "Status"
                                      state channel with how long the board
                                      has been running.

  Plain, and so serial only:

        <SwitchLED,1>                 Turn on the LED
        <SwitchLED,0>                 Turn off the LED
        <SwitchLED,ON>                Also accepts text: a plain command
        <SwitchLED,OFF>               parses its value itself
        <SwitchLED,>                  Empty parameter -> uses default (OFF)
        <Print,Hello,3>               Two parameters: text and a count
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Sets the pin number:
const int ledPin = LED_BUILTIN;

// Mirrors the LED. Registered as a signal so <LED> can point at it.
bool ledState = false;

void onSwitchLED(const char *command, const char *const *params, byte paramCount);
void onLED(const char *command, const char *const *params, byte paramCount);
void onPing(const char *command, const char *const *params, byte paramCount);
void onPrint(const char *command, const char *const *params, byte paramCount);
void setLed(bool on);

void setup()
{
  // Set the digital pin as output:
  pinMode(ledPin, OUTPUT);

  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial, room for one signal
  Blaeck.begin(&Serial, 1);

  // Names the device wherever it turns up
  Blaeck.DeviceName = "Command Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // The state signal the typed switch below refers to
  Blaeck.addSignal(F("LED_State"), &ledState);

  // Plain: listed by name only, so it can be sent but not turned into a control.
  Blaeck.onCommand("SwitchLED", onSwitchLED);
  Blaeck.onCommand("Print", onPrint);

  // Typed: checked by the library and described well enough for a control.
  // A switch is 0/1 and points at a state signal; a button carries no value.
  Blaeck.onSwitchCommand("LED", onLED).withStateFromSignal(F("LED_State"));
  Blaeck.onButtonCommand("Ping", onPing);

  // Where <Ping> answers. Declared here so the sensor exists from the start.
  Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:message-text"));
}

void loop()
{
  // Handles incoming commands, and writes the signals on the interval.
  Blaeck.tick();
}

// Plain command: the parameters arrive as text and you decide what they mean.
void onSwitchLED(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 1)
  {
    return;
  }
  // <SwitchLED,> sends an empty field
  if (params[0][0] == '\0')
  {
    Serial.println("No state given, using default (OFF).");
    setLed(false);
    return;
  }
  // Parsing it yourself means accepting whatever spelling suits you.
  // equalsFlash() compares against a name kept in flash instead of SRAM.
  if (Blaeck.equalsFlash(params[0], F("ON")))
  {
    setLed(true);
    Serial.println("LED is ON.");
    return;
  }
  if (Blaeck.equalsFlash(params[0], F("OFF")))
  {
    setLed(false);
    Serial.println("LED is OFF.");
    return;
  }

  int state = atoi(params[0]);
  if (state == 1)
  {
    setLed(true);
    Serial.println("LED is ON.");
    return;
  }
  if (state == 0)
  {
    setLed(false);
    Serial.println("LED is OFF.");
    return;
  }
}

// Typed switch: the library rejects <LED,7> before this runs, so the value
// here is always 0 or 1.
void onLED(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 1 || params[0][0] == '\0')
  {
    return;
  }
  setLed(atoi(params[0]) == 1);
  Serial.println(ledState ? "LED is ON." : "LED is OFF.");
}

/* Typed button: no value to parse.

   A button has no state signal, so it answers on the "Status" state channel
   instead. That is a frame and reaches Home Assistant; Serial.println() would
   only reach a serial monitor.
*/
void onPing(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  // %lu is fine on AVR; only float formatting (%f) is left out of printf there.
  char text[40];
  unsigned long seconds = millis() / 1000UL;
  snprintf(text, sizeof(text), "alive, running for %lu s", seconds);
  Blaeck.writeState(F("Status"), text);
}

/* Exemplary command using two parameters:
   Example: <Print,Hello,3>
*/
void onPrint(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 2)
  {
    return;
  }
  // params[0] is text, params[1] is how many times to repeat it.
  int repeats = atoi(params[1]);
  for (int i = 0; i < repeats; i++)
  {
    Serial.println(params[0]);
  }
}

// Keeps the pin and the signal in step, whichever command was used.
void setLed(bool on)
{
  ledState = on;
  digitalWrite(ledPin, on ? HIGH : LOW);
}
