/*
  DatatypeTest.ino

  A debugging aid, not an application. Registers every datatype BlaeckSerial
  can send, each at both ends of its range, plus NaN and the infinities. Flash
  it to see whether a host decodes them all correctly.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "BlaeckSerial.h"
#include <limits.h>
#include <float.h>

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals. The limits come from limits.h and float.h, so int and double are
// right whatever width the board gives them.
bool boolTest[2] = {false, true};
byte byteTest[2] = {0, UCHAR_MAX};
short shortTest[2] = {SHRT_MIN, SHRT_MAX};
unsigned short ushortTest[2] = {0, USHRT_MAX};
int intTest[2] = {INT_MIN, INT_MAX};
unsigned int uintTest[2] = {0, UINT_MAX};
long longTest[2] = {LONG_MIN, LONG_MAX};
unsigned long ulongTest[2] = {0, ULONG_MAX};
float floatTest[2] = {-FLT_MAX, FLT_MAX};
double doubleTest[2] = {-DBL_MAX, DBL_MAX};
float floatNaN = NAN;
float floatInfinity = INFINITY;
float floatNegativeInfinity = -INFINITY;
double doubleNaN = NAN;
double doubleInfinity = INFINITY;
double doubleNegativeInfinity = -INFINITY;
char stringTest[] = "Hello Blaeck";

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
  // Reads what has come in and writes the signals when the interval is up.
  Blaeck.tick();
}
