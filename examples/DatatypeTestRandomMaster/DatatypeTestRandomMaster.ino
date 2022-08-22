/*
  DatatypeTestRandomMaster.ino

  This is a sample sketch to test all the supported datatypes.

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

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
  Serial.begin(115200);
  //Initialize BlaeckSerial Master
  BlaeckSerial.beginMaster(&Serial, 9, 400000L);

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

  BlaeckSerial.tick();
}

void UpdateRandomNumbers()
{
  // bool (0 or 1)
  boolRandom = random(0, 2);

  // byte from 0 to 255
  byteRandom = random(0, 256);

  // short from -32768 to 32767
  shortRandom = random(-32768, 32768);

  // unsigned short from 0 to 65535
  uShortRandom = random(0, 65536);

  // int from -32768 to 32767
  intRandom = random(-32768, 32768);

  // unsigned int from 0 to 65535
  uIntRandom = random(0, 65536);

  // long from -2147483648 to 2147483647
  longRandom = random(2147483647);
  if (boolRandom == 1) longRandom *= -1;

  // float from -9.99999 to 9.99999(datatype limits: -3.4028235E+38 to 3.4028235E+38)
  floatRandom = randomDouble(-9.99999, 9.99999);

  // double from -9.99999 to 9.99999(datatype limits: -3.4028235E+38 to 3.4028235E+38)
  doubleRandom = randomDouble(-9.99999, 9.99999);
}

double randomDouble(double minf, double maxf)
{
  return minf + random(1UL << 31) * (maxf - minf) / (1UL << 31);  // use 1ULL<<63 for max double values)
}
