/*
  DatatypeTestSlave.ino

  This is a sample sketch to test all the supported datatypes.

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Signals
bool boolTest[2] = {false, true};
byte byteTest[2] = {0, 255};
short shortTest[2] = {-32768, 32767};
unsigned short ushortTest[2] = {0, 65535};
int intTest[2] = {-32768, 32767};
unsigned int uintTest[2] = {0, 65535};
long longTest[2] = {-2147483648, 2147483647};
unsigned long ulongTest[2] = {0, 4294967295};
float floatTest[2] = {-3.4028235E+38, 3.4028235E+38};
double doubleTest[2] = {-3.4028235E+38, 3.4028235E+38};
/*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);
  //Initialize BlaeckSerial Slave with ID 1
  BlaeckSerial.beginSlave(&Serial, 20, 1);

  // Add signals to BlaeckSerial
  BlaeckSerial.addSignal("Bool_false", &boolTest[0]);
  BlaeckSerial.addSignal("Bool_true", &boolTest[1]);
  BlaeckSerial.addSignal("Byte_min", &byteTest[0]);
  BlaeckSerial.addSignal("Byte_max", &byteTest[1]);
  BlaeckSerial.addSignal("Short_min", &shortTest[0]);
  BlaeckSerial.addSignal("Short_max", &shortTest[1]);
  BlaeckSerial.addSignal("UShort_min", &ushortTest[0]);
  BlaeckSerial.addSignal("UShort_max", &ushortTest[1]);
  BlaeckSerial.addSignal("Int_min", &intTest[0]);
  BlaeckSerial.addSignal("Int_max", &intTest[1]);
  BlaeckSerial.addSignal("UInt_min", &uintTest[0]);
  BlaeckSerial.addSignal("UInt_max", &uintTest[1]);
  BlaeckSerial.addSignal("Long_min", &longTest[0]);
  BlaeckSerial.addSignal("Long_max", &longTest[1]);
  BlaeckSerial.addSignal("ULong_min", &ulongTest[0]);
  BlaeckSerial.addSignal("ULong_max", &ulongTest[1]);
  BlaeckSerial.addSignal("Float_min", &floatTest[0]);
  BlaeckSerial.addSignal("Float_max", &floatTest[1]);
  BlaeckSerial.addSignal("Double_min", &doubleTest[0]);
  BlaeckSerial.addSignal("Double_max", &doubleTest[1]);
}

void loop()
{
  BlaeckSerial.read();
  
  Serial.println("This is text sent from the slave sketch. This text is not transmitted to master via I2C.");
}
