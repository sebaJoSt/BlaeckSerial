/*
  DatatypeTest.ino

  This is a sample sketch to test all the supported datatypes.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Signals
bool boolTest[2] = {false, true};
byte byteTest[2] = {0, 255};
short shortTest[2] = {-32768, 32767};
unsigned short ushortTest[2] = {0, 65535};
long longTest[2] = {-2147483648, 2147483647};
unsigned long ulongTest[2] = {0, 4294967295};
float floatTest[2] = {-3.4028235E+38, 3.4028235E+38};
float floatNaN = 0.0 / 0.0;
float floatInfinity = 1.0 / 0.0;
float floatNegativeInfinity = -1.0 / 0.0;
double doubleNaN = 0.0 / 0.0;
double doubleInfinity = 1.0 / 0.0;
double doubleNegativeInfinity = -1.0 / 0.0;
#ifdef __AVR__
int intTest[2] = {-32768, 32767};
unsigned int uintTest[2] = {0, 65535};
double doubleTest[2] = {-3.4028235E+38, 3.4028235E+38};
#else
int intTest[2] = {-2147483648, 2147483647};
unsigned int uintTest[2] = {0, 4294967295};
double doubleTest[2] = {-1.79769313486231570E+308, 1.79769313486231570E+308};
#endif

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);

  // Initialize BlaeckSerial
  BlaeckSerial.begin(&Serial, 26);

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
  BlaeckSerial.addSignal("Float_NaN", &floatNaN);
  BlaeckSerial.addSignal("Float_Inf", &floatInfinity);
  BlaeckSerial.addSignal("Float_NegInf", &floatNegativeInfinity);
  BlaeckSerial.addSignal("Double_min", &doubleTest[0]);
  BlaeckSerial.addSignal("Double_max", &doubleTest[1]);
  BlaeckSerial.addSignal("Double_NaN", &doubleNaN);
  BlaeckSerial.addSignal("Double_Inf", &doubleInfinity);
  BlaeckSerial.addSignal("Double_NegInf", &doubleNegativeInfinity);
}

void loop()
{
  /*Keeps watching for serial input (Serial.read) and
    transmits the updated signals at the user-set interval (Serial.write)*/
  BlaeckSerial.tickUpdated();

  static unsigned long updateLastTimeDone = 0;
  static unsigned long updateInterval = 3000;
  static bool updateFirstTime = true;

  if ((millis() - updateLastTimeDone >= updateInterval) || updateFirstTime)
  {
    updateLastTimeDone = millis();
    updateFirstTime = false;

    BlaeckSerial.write("Bool_false", boolTest[0]);
    BlaeckSerial.write("Bool_true", boolTest[1]);
    BlaeckSerial.write("Byte_min", byteTest[0]);
    BlaeckSerial.write("Byte_max", byteTest[1]);
    BlaeckSerial.write("Short_min", shortTest[0]);
    BlaeckSerial.write("Short_max", shortTest[1]);
    BlaeckSerial.write("UShort_min", ushortTest[0]);
    BlaeckSerial.write("UShort_max", ushortTest[1]);
    BlaeckSerial.write("Int_min", intTest[0]);
    BlaeckSerial.write("Int_max", intTest[1]);
    BlaeckSerial.write("UInt_min", uintTest[0]);
    BlaeckSerial.write("UInt_max", uintTest[1]);
    BlaeckSerial.write("Long_min", longTest[0]);
    BlaeckSerial.write("Long_max", longTest[1]);
    BlaeckSerial.write("ULong_min", ulongTest[0]);
    BlaeckSerial.write("ULong_max", ulongTest[1]);
    BlaeckSerial.write("Float_min", floatTest[0]);
    BlaeckSerial.write("Float_max", floatTest[1]);
    BlaeckSerial.write("Float_NaN", floatNaN);
    BlaeckSerial.write("Float_Inf", floatInfinity);
    BlaeckSerial.write("Float_NegInf", floatNegativeInfinity);
    BlaeckSerial.write("Double_min", doubleTest[0]);
    BlaeckSerial.write("Double_max", doubleTest[1]);
    BlaeckSerial.write("Double_NaN", doubleNaN);
    BlaeckSerial.write("Double_Inf", doubleInfinity);
    BlaeckSerial.write("Double_NegInf", doubleNegativeInfinity);

    BlaeckSerial.markAllSignalsUpdated();
  }
}
