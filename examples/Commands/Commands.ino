/*
  Commands.ino

  This is a sample sketch to show how to use BlaeckSerial to
  implement your own serial command. The example implements
  the command <SwitchLED> which turns the on-board LED on
  or off.

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

    Empty parameters are not allowed,
    e.g. don't do: <COMMAND,,PARAMETER02>   <- PARAMETER02 shifts into PARAMETER01
               do: <COMMAND,PARAMETER01,PARAMETER02>

  The circuit:
    - No wiring required.
    - Use the on-board LED
      Note: Most Arduinos have an on-board LED you can control. On the UNO and MEGA
            it is attached to digital pin 13. LED_BUILTIN is set to the correct LED pin
            independent of which board is used.

  Using the sketch:
    - Upload the sketch to your Arduino.
    - Open the Serial Monitor and set the baudrate to 9600 baud.
    - Type the following command and press enter:
        <SwitchLED,1>    Turn on the LED
        <SwitchLED,0>    Turn off the LED
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Sets the pin number:
const int ledPin = LED_BUILTIN;
bool onSwitchLED(const char *command, const char *const *params, byte paramCount);
bool onSomeCommand(const char *command, const char *const *params, byte paramCount);
bool onPrint(const char *command, const char *const *params, byte paramCount);

void setup()
{
  // Set the digital pin as output:
  pinMode(ledPin, OUTPUT);

  // Initialize Serial port
  Serial.begin(9600);

  // Setup BlaeckSerial
  BlaeckSerial.begin(&Serial, 0);

  // Register command handlers (new style)
  BlaeckSerial.onCommand("SwitchLED", onSwitchLED);
  BlaeckSerial.onCommand("SomeCommand", onSomeCommand);
  BlaeckSerial.onCommand("Print", onPrint);
}

void loop()
{
  /* Keeps watching for serial input and dispatches registered handlers
     when input with the correct syntax is detected.
     Instead of BlaeckSerial.read you can use BlaeckSerial.tick
     if you want to add signals and write them in a user-set interval
     (see Basic example).
  */
  BlaeckSerial.read();
}

bool onSwitchLED(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 1)
  {
    return false;
  }
  int state = atoi(params[0]);
  if (state == 1)
  {
    digitalWrite(ledPin, HIGH);
    Serial.println("LED is ON.");
    return true;
  }
  if (state == 0)
  {
    digitalWrite(ledPin, LOW);
    Serial.println("LED is OFF.");
    return true;
  }
  return false;
}

bool onSomeCommand(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  // Do something
  return true;
}

/* Exemplary command using string parameters:
   Example: <Print,Bye Bye,1>
*/
bool onPrint(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount < 2)
  {
    return false;
  }
  int mode = atoi(params[1]);
  if (mode == 0)
  {
    Serial.println(params[0]);
    return true;
  }
  if (mode == 1)
  {
    Serial.print(params[0]);
    Serial.println(" Miss American Pie");
    Serial.println("Drove my Chevy to the levee but the levee was dry");
    Serial.println("And them good ole boys were drinking whiskey and rye");
    Serial.println("Singin' this'll be the day that I die");
    Serial.println("This'll be the day that I die");
    return true;
  }
  return false;
}
