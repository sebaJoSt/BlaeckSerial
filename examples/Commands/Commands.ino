/*
  Commands.ino

  This is a sample sketch to show how to use BlaeckSerial to
  implement your own serial commands.

  It registers two kinds of command, on purpose:

    Plain    onCommand(...)         You parse the parameters yourself, and
                                    nothing is declared about the value.

    Typed    onSwitchCommand(...)   You declare what the command is. The
             onButtonCommand(...)   library validates the value before your
                                    handler runs, and describes the command
                                    so a host (e.g. Loggbok / Home Assistant)
                                    can create an entity for it by itself.

  Every registered command is listed in <BLAECK.WRITE_COMMANDS>, plain ones
  included, so a host can offer a complete command palette. What differs is
  the metadata: a plain entry says only "this command exists", while a typed
  entry carries its kind, its allowed values and its state signal - which is
  what a dashboard needs to build a control for it.

  <SwitchLED> and <LED> both switch the same on-board LED, one plain and one
  typed, so you can compare the two entries side by side in that list.

  A typed switch also names a state signal (here "LED_State"), so a
  dashboard can display the current state, not just send new ones.

  How a command reports back differs accordingly:

    <SwitchLED>   Serial.println(...)      Serial Monitor only. A Blaeck host
                                           skips anything that is not a frame.
    <LED>         its state signal         The dashboard follows LED_State.
    <Ping>        writeState(...)          A button has no state signal, so it
                                           pushes a line to a named state
                                           channel ("Status"). It is written
                                           only on a press, so the host gets an
                                           answer when it asks for one.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  The command syntax for implementing your own commands:

    Command:         <COMMAND,PARAMETER01,PARAMETER02,...,PARAMETER10>
                     <-  full payload size is architecture-dependent ->
                     AVR: up to 48 chars, non-AVR: up to 96 chars
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

  Using the sketch:
    - Upload the sketch to your Arduino.
    - Open the Serial Monitor and set the baudrate to 115200 baud.
    - Type the following commands and press enter:

        Your own commands:
        <SwitchLED,1>                 Turn on the LED   (plain)
        <SwitchLED,0>                 Turn off the LED  (plain)
        <SwitchLED,ON>                Also accepts text: a plain command
        <SwitchLED,OFF>               parses its value itself
        <SwitchLED,>                  Empty param -> uses default (OFF)
        <LED,1>                       Turn on the LED   (typed switch)
        <LED,0>                       Turn off the LED  (typed switch)
        <LED,7>                       Rejected: a switch only accepts 0 or 1
        <Ping>                        Typed button, takes no value. The board
                                      answers on the "Status" state channel
                                      with how long it has been running, so the
                                      reply reaches a host and not just the
                                      Serial Monitor.
        <Print,Bye Bye,1>             String parameters

        Built-in Blaeck commands:
        <BLAECK.GET_DEVICES>          Writes the device's information to the PC
        <BLAECK.WRITE_SYMBOLS>        Writes the symbol list to the PC
        <BLAECK.WRITE_COMMANDS>       Writes the command list to the PC
                                      All four commands appear here. The typed
                                      ones carry their kind and metadata, the
                                      plain ones only their name.
        <BLAECK.WRITE_DATA>           Writes the data to the PC
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Sets the pin number:
const int ledPin = LED_BUILTIN;

// Mirrors the LED state. Registered as a signal so the typed <LED> command
// can point at it, which lets a dashboard show the state it is controlling.
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
  BlaeckSerial.begin(&Serial, 1);

  // Reported by <BLAECK.GET_DEVICES>, and used by a host to name the device
  BlaeckSerial.DeviceName = "Command Demo";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // The state signal the typed switch below refers to
  BlaeckSerial.addSignal(F("LED_State"), &ledState);

  // Plain: you parse the parameters yourself. Listed by name only, so a host
  // knows the command exists but cannot build a control for it.
  BlaeckSerial.onCommand("SwitchLED", onSwitchLED);
  BlaeckSerial.onCommand("Print", onPrint);

  // Typed: validated by the library, and listed with the metadata a host needs
  // to create an entity. A switch is 0/1 and mirrors a state signal; a button
  // carries no value.
  BlaeckSerial.onSwitchCommand("LED", onLED).withStateSignal(F("LED_State"));
  BlaeckSerial.onButtonCommand("Ping", onPing);

  // State channels are declared up-front so the host can announce a text
  // sensor for "Status" before the first line is written.
  BlaeckSerial.addStateChannel(F("Status")).withIcon(F("mdi:message-text"));
}

void loop()
{
  /* Keeps watching for serial input and dispatches registered handlers
     when input with the correct syntax is detected. tick() also writes
     the signals in a user-set interval; use BlaeckSerial.read() instead
     if you only want commands and no data (see the Basic example).
  */
  BlaeckSerial.tick();
}

/* Plain command. You get the raw parameters and decide what they mean,
   including what an empty one should do. Nothing about this command is
   advertised, so a host cannot discover it.
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
  if (BlaeckSerial.equalsFlash(params[0], F("ON")))
  {
    setLed(true);
    Serial.println("LED is ON.");
    return;
  }
  if (BlaeckSerial.equalsFlash(params[0], F("OFF")))
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

   A button has no state signal, so writeState() is how it reports back: it
   pushes a line to a named 0x95 state channel, which a host can surface as
   a text sensor. Serial.println() would only reach the Serial Monitor - a
   Blaeck host skips anything that is not a frame.

   The channel is only written on a button press, so a sensor bound to it
   updates when asked rather than continuously. Calling writeState() from
   loop() on a timer instead would give the same channel a steady heartbeat.
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
  BlaeckSerial.writeState(F("Status"), text);
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
// the dashboard sees the same value.
void setLed(bool on)
{
  ledState = on;
  digitalWrite(ledPin, on ? HIGH : LOW);
}
