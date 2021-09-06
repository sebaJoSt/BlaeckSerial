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

    <BLAECK.WRITE_SYMBOLS>            Write the symbol list to the PC
    <BLAECK.WRITE_DATA>               Write the data to the PC
    <BLAECK.ACTIVATE,60>              The data is written every 60s to the PC
                                      Minimum: 1[seconds] Maximum: 32767[seconds]
    <BLAECK.DEACTIVATE>               Stops writing the data every 60s
    <BLAECK.WRITE_VERSION>            Writes the BlaeckSerial version number
    <BLAECK.WRITE_DEVICE_NAME>        Writes the device name previously defined with public variable DeviceName
    <BLAECK.WRITE_DEVICE_HW_VERSION>  Writes the device's hardware version previously defined with public variable DeviceHWVersion
    <BLAECK.WRITE_DEVICE_FW_VERSION>  Writes the device's firmware version previously defined with public variable DeviceFWVersion
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Signals
float randomSmallNumber;
long randomBigNumber;

#define ExampleVersion "1.0.0"

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);

  //Setup BlaeckSerial
  BlaeckSerial.begin(
    &Serial,   //Serial reference
    2          //Maxmimal signal count used;
  );

  BlaeckSerial.DeviceName = "RandomNumberGenerator";
  BlaeckSerial.DeviceHWVersion = "ArduinoXYZ RevX";
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
