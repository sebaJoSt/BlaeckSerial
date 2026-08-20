/*
  DatatypeTest.ino

  This is a sample sketch to test all the supported datatypes.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

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
char stringTest[] = "Hello Blaeck";
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
  Serial.begin(115200);

  // Initialize BlaeckSerial
  Blaeck.begin(&Serial, 27);

  // Used by Loggbok to identify the device
  Blaeck.DeviceName = "Datatype Test";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  Blaeck.addSignal(F("Bool_false"), &boolTest[0]);
  Blaeck.addSignal(F("Bool_true"), &boolTest[1]);
  Blaeck.addSignal(F("Byte_min"), &byteTest[0]);
  Blaeck.addSignal(F("Byte_max"), &byteTest[1]);
  Blaeck.addSignal(F("Short_min"), &shortTest[0]);
  Blaeck.addSignal(F("Short_max"), &shortTest[1]);
  Blaeck.addSignal(F("UShort_min"), &ushortTest[0]);
  Blaeck.addSignal(F("UShort_max"), &ushortTest[1]);
  Blaeck.addSignal(F("Int_min"), &intTest[0]);
  Blaeck.addSignal(F("Int_max"), &intTest[1]);
  Blaeck.addSignal(F("UInt_min"), &uintTest[0]);
  Blaeck.addSignal(F("UInt_max"), &uintTest[1]);
  Blaeck.addSignal(F("Long_min"), &longTest[0]);
  Blaeck.addSignal(F("Long_max"), &longTest[1]);
  Blaeck.addSignal(F("ULong_min"), &ulongTest[0]);
  Blaeck.addSignal(F("ULong_max"), &ulongTest[1]);
  Blaeck.addSignal(F("Float_min"), &floatTest[0]);
  Blaeck.addSignal(F("Float_max"), &floatTest[1]);
  Blaeck.addSignal(F("Float_NaN"), &floatNaN);
  Blaeck.addSignal(F("Float_Inf"), &floatInfinity);
  Blaeck.addSignal(F("Float_NegInf"), &floatNegativeInfinity);
  Blaeck.addSignal(F("Double_min"), &doubleTest[0]);
  Blaeck.addSignal(F("Double_max"), &doubleTest[1]);
  Blaeck.addSignal(F("Double_NaN"), &doubleNaN);
  Blaeck.addSignal(F("Double_Inf"), &doubleInfinity);
  Blaeck.addSignal(F("Double_NegInf"), &doubleNegativeInfinity);
  Blaeck.addSignal(F("String_test"), stringTest);
}

void loop()
{
  /*Keeps watching for serial input (Serial.read) and
    transmits the updated signals at the user-set interval (Serial.write)*/
  Blaeck.tickUpdated();

  static unsigned long updateLastTimeDone = 0;
  static unsigned long updateInterval = 3000;
  static bool updateFirstTime = true;

  if ((millis() - updateLastTimeDone >= updateInterval) || updateFirstTime)
  {
    updateLastTimeDone = millis();
    updateFirstTime = false;

    Blaeck.write("Bool_false", boolTest[0]);
    Blaeck.write("Bool_true", boolTest[1]);
    Blaeck.write("Byte_min", byteTest[0]);
    Blaeck.write("Byte_max", byteTest[1]);
    Blaeck.write("Short_min", shortTest[0]);
    Blaeck.write("Short_max", shortTest[1]);
    Blaeck.write("UShort_min", ushortTest[0]);
    Blaeck.write("UShort_max", ushortTest[1]);
    Blaeck.write("Int_min", intTest[0]);
    Blaeck.write("Int_max", intTest[1]);
    Blaeck.write("UInt_min", uintTest[0]);
    Blaeck.write("UInt_max", uintTest[1]);
    Blaeck.write("Long_min", longTest[0]);
    Blaeck.write("Long_max", longTest[1]);
    Blaeck.write("ULong_min", ulongTest[0]);
    Blaeck.write("ULong_max", ulongTest[1]);
    Blaeck.write("Float_min", floatTest[0]);
    Blaeck.write("Float_max", floatTest[1]);
    Blaeck.write("Float_NaN", floatNaN);
    Blaeck.write("Float_Inf", floatInfinity);
    Blaeck.write("Float_NegInf", floatNegativeInfinity);
    Blaeck.write("Double_min", doubleTest[0]);
    Blaeck.write("Double_max", doubleTest[1]);
    Blaeck.write("Double_NaN", doubleNaN);
    Blaeck.write("Double_Inf", doubleInfinity);
    Blaeck.write("Double_NegInf", doubleNegativeInfinity);
    Blaeck.write("String_test", stringTest);

    Blaeck.markAllSignalsUpdated();
  }
}
