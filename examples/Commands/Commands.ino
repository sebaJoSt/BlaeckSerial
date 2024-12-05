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
    StringCommand:   <COMMAND, STRING01  ,PARAMETER02,...,PARAMETER10>
                     <-           --  max. 64 chars ---             ->
                     <-         --  max. 10 Parameters ---          ->

    COMMAND:         String
    PARAMETER01..10  Int 16 Bit
    STRING01:        max. 15 chars
    Start Marker*:    <
    End Marker*:      >
    Separation*:      ,

      * Not allowed in COMMAND, PARAMETER & STRING01

    Empty PARAMETER are not allowed,
    e.g. don't do: <COMMAND,,PARAMETER02>   <- PARAMETER02 will be stored in PARAMETER01
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

void setup()
{
  // Set the digital pin as output:
  pinMode(ledPin, OUTPUT);

  // Initialize Serial port
  Serial.begin(9600);

  // Setup BlaeckSerial
  BlaeckSerial.begin(&Serial, 0);

  // Setup read callback function by passing a function
  BlaeckSerial.attachRead(startCommand);
}

void loop()
{
  /* Keeps watching for serial input and fires the callback function
     startCommand when a input with the correct syntax is detected.
     Instead of BlaeckSerial.read you can use BlaeckSerial.tick
     if you want to add signals and write them in a user-set interval
     (see Basic example).
  */
  BlaeckSerial.read();
}

// Implement the function, don't forget the arguments
void startCommand(char *command, int *parameter, char *string01)
{
  /* Compares the user input to the string "SwitchLED"
     strcmp takes the two strings to be compared as parameters
     and returns 0 if the strings are equal*/
  if (strcmp(command, "SwitchLED") == 0)
  {
    // parameter[0] is the first parameter after the command: PARAMETER01
    if (parameter[0] == 1)
    {
      // Turns on the LED
      digitalWrite(ledPin, HIGH);
      Serial.println("LED is ON.");
    }
    if (parameter[0] == 0)
    {
      // Turns off the LED
      digitalWrite(ledPin, LOW);
      Serial.println("LED is OFF.");
    }
  }

  /* Here you can add more commands:*/
  if (strcmp(command, "SomeCommand") == 0)
  {
    // Do something
  }

  /* Exemplary command using the string parameter STRING01:
     Example: <Print,Bye Bye,1>
  */
  if (strcmp(command, "Print") == 0)
  {
    if (parameter[1] == 0)
    {
      Serial.println(string01);
    }
    if (parameter[1] == 1)
    {
      // Print
      Serial.print(string01);
      Serial.println(" Miss American Pie");
      Serial.println("Drove my Chevy to the levee but the levee was dry");
      Serial.println("And them good ole boys were drinking whiskey and rye");
      Serial.println("Singin' this'll be the day that I die");
      Serial.println("This'll be the day that I die");
    }
  }
}
