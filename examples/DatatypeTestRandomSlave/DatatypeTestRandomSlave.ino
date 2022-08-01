/*
  DatatypeTestSlave.ino

  This is a sample sketch to test all the supported datatypes.

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Generated values
bool boolGeneratedRandom;
byte byteGeneratedRandom;
short shortGeneratedRandom;
unsigned short uShortGeneratedRandom;
int intGeneratedRandom;
unsigned int uIntGeneratedRandom;
long longGeneratedRandom;
float floatGeneratedRandom;
double doubleGeneratedRandom;

//Signals
bool boolRandom;
byte byteRandom;
short shortRandom;
unsigned short uShortRandom;
int intRandom;
unsigned int uIntRandom;
long longRandom;
float floatRandom;
double doubleRandom;

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);
  //Initialize BlaeckSerial Slave with ID 1
  BlaeckSerial.beginSlave(&Serial, 20, 1);

  BlaeckSerial.DeviceName = "DatatypeTest Random Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  BlaeckSerial.addSignal("Bool", &boolRandom);
  BlaeckSerial.addSignal("Byte", &byteRandom);
  BlaeckSerial.addSignal("Short", &shortRandom);
  BlaeckSerial.addSignal("UShort", &uShortRandom);
  BlaeckSerial.addSignal("Int", &intRandom);
  BlaeckSerial.addSignal("UInt", &uIntRandom);
  BlaeckSerial.addSignal("Long", &longRandom);
  BlaeckSerial.addSignal("Float", &floatRandom);
  BlaeckSerial.addSignal("Double", &doubleRandom);
}

void loop()
{
  UpdateRandomNumbers();
  UpdateSignals();

  BlaeckSerial.tick();
}

void UpdateRandomNumbers()
{
  // bool (0 or 1)
  boolGeneratedRandom = random(0, 2);

  // byte from 0 to 255
  byteGeneratedRandom = random(0, 256);

  // short from -32768 to 32767
  shortGeneratedRandom = random(-32768, 32768);

  // unsigned short from 0 to 65535
  uShortGeneratedRandom = random(0, 65536);

  // int from -32768 to 32767
  intGeneratedRandom = random(-32768, 32768);

  // unsigned int from 0 to 65535
  uIntGeneratedRandom = random(0, 65536);

  // long from -2147483648 to 2147483647
  longGeneratedRandom = random(2147483647);
  if (boolGeneratedRandom == 1) longGeneratedRandom *= -1;

  // float from -9.99999 to 9.99999(datatype limits: -3.4028235E+38 to 3.4028235E+38)
  floatGeneratedRandom = randomDouble(-9.99999, 9.99999);

  // double from -9.99999 to 9.99999(datatype limits: -3.4028235E+38 to 3.4028235E+38)
  doubleGeneratedRandom = randomDouble(-9.99999, 9.99999);
}

void UpdateSignals()
{
  /*
    On the slave the I2C interrupt could happen between one byte of a multi-byte signal being changed and the next, thus sending corrupted data to the master.
    To prevent that, you would need to disable interrupts, change the multi-byte signal's value, and then enable interrupts again
  */
  noInterrupts();
  boolRandom  = boolGeneratedRandom;
  byteRandom = byteGeneratedRandom;
  shortRandom = shortGeneratedRandom;
  uShortRandom = uShortGeneratedRandom;
  intRandom = intGeneratedRandom;
  uIntRandom = uIntGeneratedRandom;
  longRandom = longGeneratedRandom;
  floatRandom = floatGeneratedRandom;
  doubleRandom = doubleGeneratedRandom;
  interrupts();
}

double randomDouble(double minf, double maxf)
{
  return minf + random(1UL << 31) * (maxf - minf) / (1UL << 31);  // use 1ULL<<63 for max double values)
}
