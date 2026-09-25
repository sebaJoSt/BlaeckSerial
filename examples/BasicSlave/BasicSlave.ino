/*
  BasicSlave.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit data
  from multiple Arduino boards in a master/slave configuration to your PC.
  Upload this sketch to the slave Arduino.

  The slave does not send data on its own. The master polls each slave
  via I2C to read signal values, then forwards everything over Serial to the PC.
  No tick() call is needed on the slave — communication is handled by
  I2C callbacks registered in beginSlave().

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

// Generated values
long randomGeneratedBigNumber;
long randomGeneratedBigNumber2;

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
  UpdateRandomNumbers();
  UpdateSignals();
}

void UpdateRandomNumbers()
{
  // Random big number from 800 000 to 900 000
  randomGeneratedBigNumber = random(800000, 900001);

  // Random big number from 1 000 000 000 to 2 000 000 000
  randomGeneratedBigNumber2 = random(1000000000, 2000000001);
}

void UpdateSignals()
{
  /*
    Disable interrupts while updating multi-byte signal values to prevent
    the I2C ISR from reading a partially-updated value (torn read).
  */
  noInterrupts();
  randomBigNumber = randomGeneratedBigNumber;
  randomBigNumber2 = randomGeneratedBigNumber2;
  interrupts();
}
