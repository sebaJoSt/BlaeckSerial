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

// The board this was built for, so a recording says which one produced it. A harness runs
// on every core the library supports, and the widths below are what differ.
#if defined(ARDUINO_GIGA)
#define HARNESS_BOARD "Arduino Giga R1"
#elif defined(ARDUINO_AVR_MEGA2560)
#define HARNESS_BOARD "Arduino Mega 2560 Rev3"
#elif defined(ARDUINO_ARCH_ESP32)
#define HARNESS_BOARD "ESP32"
#else
#define HARNESS_BOARD "unknown board"
#endif

// int is two bytes on AVR and four on a 32-bit core, double four and eight. A driver reads
// the width off the frame, but a run is easier to read when the board has said it too.
void PrintWidths()
{
  Serial.print(F("board "));
  Serial.print(F(HARNESS_BOARD));
  Serial.print(F(", int "));
  Serial.print((unsigned)sizeof(int));
  Serial.print(F(" bytes, long "));
  Serial.print((unsigned)sizeof(long));
  Serial.print(F(", float "));
  Serial.print((unsigned)sizeof(float));
  Serial.print(F(", double "));
  Serial.println((unsigned)sizeof(double));
}

// Asked for rather than only printed at boot: a Giga does not reset when DTR is
// raised, so a driver that opens the port has missed the startup lines already.
void onWidths(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  PrintWidths();
}

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Initialize BlaeckSerial
  Blaeck.begin(&Serial, 27);

  // Used by Loggbok to identify the device
  Blaeck.DeviceName = "Datatype Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = ExampleVersion;

  Blaeck.onCommand("WIDTHS", onWidths);
  PrintWidths();

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
