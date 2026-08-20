/*
  Commands.ino

  This is a sample sketch to show how to implement your own commands.

  It registers two kinds of command, on purpose:

    Plain    onCommand(...)         You parse the parameters yourself, and
                                    nothing is declared about the value.

    Typed    onSwitchCommand(...)   You declare what the command is. The
             onButtonCommand(...)   library validates the value before your
                                    handler runs, and describes the command
                                    so it can be turned into a control.

  Only a typed command carries enough to build a control: its kind, its
  allowed values and the signal that reports its state. Loggbok publishes
  that as an MQTT Discovery config, and Home Assistant creates the entity
  from it. A plain command is listed by name only, so it stays a command you
  send over serial - for bringing a board up or poking at it from a serial
  monitor - and never becomes a control.

  <SwitchLED> and <LED> both switch the same on-board LED, one plain and one
  typed, so the difference is visible on one piece of hardware.

  How each one reports back:

    <SwitchLED>   Serial.println(...)      Plain text, readable in a serial
                                           monitor. Not a frame, so Loggbok
                                           passes over it.
    <LED>         its state signal         LED_State says what the pin is
                                           actually doing, so the control
                                           follows the board instead of
                                           assuming the command worked.
    <Ping>        writeState(...)          A button has no state signal, so it
                                           writes the "Status" state channel
                                           and Home Assistant shows that as a
                                           text sensor.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  The command syntax for implementing your own commands:

    Command:         <COMMAND,PARAMETER01,PARAMETER02,...,PARAMETER10>
                     <-  full payload size is architecture-dependent ->
                     AVR: up to 48 chars, non-AVR: up to 128 chars
                     <-         --  max. 10 parameters ---          ->

    COMMAND:         String token (handler key used in onCommand)
    PARAMETER01..10  String tokens (convert with atoi/atol/atof as needed)
    Start Marker*:    <
    End Marker*:      >
    Separation*:      ,

      * Not allowed in COMMAND or parameter tokens

    Empty parameters are preserved positionally and default to empty string / 0,
    e.g. <COMMAND,,PARAMETER02>      <- PARAMETER01 is empty, PARAMETER02 stays in its slot
    To check if a parameter was provided: params[i][0] == '\0' means empty

  The circuit:
    - No wiring required.
    - Use the on-board LED
      Note: Most Arduinos have an on-board LED you can control. On the UNO and MEGA
            it is attached to digital pin 13. LED_BUILTIN is set to the correct LED pin
            independent of which board is used.

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
        <Print,Bye Bye,1>             String parameters

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Sets the pin number:
const int ledPin = LED_BUILTIN;

// Mirrors the LED state. Registered as a signal so the typed <LED> command
// can point at it, which is what lets its control show the state it sets.
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

  // Plain: you parse the parameters yourself. Listed by name only, so it can
  // be sent, but there is nothing to build a control from.
  Blaeck.onCommand("SwitchLED", onSwitchLED);
  Blaeck.onCommand("Print", onPrint);

  // Typed: validated by the library, and listed with what a control needs.
  // A switch is 0/1 and points at a state signal; a button carries no value.
  Blaeck.onSwitchCommand("LED", onLED).withStateFromSignal(F("LED_State"));
  Blaeck.onButtonCommand("Ping", onPing);

  // Declared up-front, so the "Status" text sensor exists before the first
  // line is ever written to it.
  Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:message-text"));
}

void loop()
{
  /* Keeps watching for serial input and dispatches registered handlers
     when input with the correct syntax is detected. tick() also writes
     the signals in a user-set interval; use Blaeck.read() instead
     if you only want commands and no data (see the Basic example).
  */
  Blaeck.tick();
}

/* Plain command. You get the raw parameters and decide what they mean,
   including what an empty one should do. Nothing about it is described,
   so it stays something you send rather than something you click.
*/
void onSwitchLED(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 1)
  {
    return;
  }
  // Detect empty parameter: <SwitchLED,> sends an empty field
  if (params[0][0] == '\0')
  {
    Serial.println("No state given, using default (OFF).");
    setLed(false);
    return;
  }
  // A plain command parses its own value, so it can accept whatever spelling suits it.
  // equalsFlash() compares the parameter against a name kept in flash: written as a plain
  // literal, "ON" and "OFF" would each sit in SRAM for the life of the sketch.
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

/* Typed switch. The library has already checked that the value is 0 or 1
   before this runs - <LED,7> is rejected and never reaches the handler -
   so there is less to guard against here.
*/
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

/* Typed button. Carries no value, so there is nothing to parse.

   A button has no state signal, so writeState() is how it answers: it writes
   the "Status" state channel, which is a frame and reaches Home Assistant as
   a text sensor. Serial.println() would only reach a serial monitor.

   The channel is written on a press and not otherwise, so the sensor updates
   when asked. Calling writeState() from loop() on a timer would instead give
   the same channel a steady heartbeat.
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

/* Exemplary command using string parameters:
   Example: <Print,Bye Bye,1>
*/
void onPrint(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 2)
  {
    return;
  }
  int mode = atoi(params[1]);
  if (mode == 0)
  {
    Serial.println(params[0]);
    return;
  }
  if (mode == 1)
  {
    Serial.print(params[0]);
    Serial.println(" Miss American Pie");
    Serial.println("Drove my Chevy to the levee but the levee was dry");
    Serial.println("And them good ole boys were drinking whiskey and rye");
    Serial.println("Singin' this'll be the day that I die");
    Serial.println("This'll be the day that I die");
    return;
  }
}

// Keeps the pin and the state signal in step, so whichever command was used
// the reported state is the same.
void setLed(bool on)
{
  ledState = on;
  digitalWrite(ledPin, on ? HIGH : LOW);
}
