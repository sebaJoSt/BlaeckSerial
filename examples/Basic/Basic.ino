/*
  Basic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit data
  from the Arduino board to your PC every minute (or the user-set interval).

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  Setup:
    Upload the sketch to your Arduino.
    Open the Serial Monitor and set the baudrate to 9600 baud.
    Type the following commands and press enter:

    <BLAECK.GET_DEVICES>              Writes the device's information to the PC
    <BLAECK.WRITE_SYMBOLS>            Writes the symbol list to the PC
    <BLAECK.WRITE_DATA>               Writes the data to the PC
    <BLAECK.ACTIVATE,96,234>          The data is written every 60 seconds (60 000ms)
                                      first Byte:  0b01100000 = 96 DEC
                                      second Byte: 0b11101010 = 234 DEC
                                      Minimum: 0[milliseconds] Maximum: 4 294 967 295[milliseconds]
    <BLAECK.DEACTIVATE>               Stops writing the data every 60s
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Signals
float randomSmallNumber;
long randomBigNumber;

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);

  // Setup BlaeckSerial
  BlaeckSerial.begin(
      &Serial, // Serial reference
      2        // Maximal signal count used;
  );

  BlaeckSerial.DeviceName = "Random Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  BlaeckSerial.addSignal("Small Number", &randomSmallNumber);
  BlaeckSerial.addSignal("Big Number", &randomBigNumber);

  /*Uncomment this function for initial settings
    first parameter: timedActivated
    second parameter: timedInterval_ms */
  // BlaeckSerial.setTimedData(true, 60000);
}

void loop()
{
  UpdateRandomNumbers();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}

void UpdateRandomNumbers()
{
  // Random small number from 0.00 to 10.00
  randomSmallNumber = random(1001) / 100.0;

  // Random big number from 2 000 000 000 to 2 100 000 000
  randomBigNumber = random(2000000000, 2100000001);
}
