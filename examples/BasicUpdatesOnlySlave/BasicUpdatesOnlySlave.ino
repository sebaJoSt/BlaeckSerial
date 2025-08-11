/*
  BasicUpdatesOnlySlave.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit data
  from multiple Arduino boards in a master/slave configuration to your PC.
  Upload this sketch to the slave Arduino.
  You can find more documentation about the test circuit, setup, etc. in
  the example BasicMaster.ino

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Signals
long randomBigNumber;
long randomBigNumber2;

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);

  /*Setup BlaeckSerial slave with ID 1
    First parameter: Serial reference
    Second parameter: Maximal signal count used;
    Third parameter: Slave ID: Each slave device on the bus
     should have a unique 7-bit address:
     Minimum value: 0
     Maximal value; 127
  */
  BlaeckSerial.beginSlave(&Serial, 2, 1);

  BlaeckSerial.DeviceName = "Big Random Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial slave
  BlaeckSerial.addSignal("Big Number", &randomBigNumber);
  BlaeckSerial.addSignal("Big Number 2", &randomBigNumber2);
}

void loop()
{

  UpdateRandomBigNumber();
  UpdateRandomBigNumber2();
}

void UpdateRandomBigNumber()
{
  /*
    On the slave the I2C interrupt could happen between one byte of a multi-byte signal being changed and the next, thus sending corrupted data to the master.
    To prevent that, you would need to disable interrupts, change the multi-byte signal's value, and then enable interrupts again
  */
  noInterrupts();
  // Random big number from 1000 to 2000
  randomBigNumber = random(1000, 2001);
  BlaeckSerial.markSignalUpdated(0);
  interrupts();
}

void UpdateRandomBigNumber2()
{
  static unsigned long updateLastTimeDone = 0;
  static unsigned long updateInterval = 3000;
  static bool updateFirstTime = true;

  if ((millis() - updateLastTimeDone >= updateInterval) || updateFirstTime)
  {
    updateLastTimeDone = millis();
    updateFirstTime = false;

    noInterrupts();
    // Random big number from 3000 to 4000
    randomBigNumber2 = random(3000, 4001);
    BlaeckSerial.markSignalUpdated(1);
    interrupts();
  }
}
