/*
  WriteModes.ino

  This sketch demonstrates two different data transmission patterns:

  1. Direct Write (Sine_1):
     - Data is immediately transmitted after updating
     - Updates every 100ms using Blaeck.write()

  2. Interval Mode (Sine_2 & Sine_3):
     - Data is marked as updated but transmitted only when tickUpdated() is called
     - Sine_2:
        - updates every 2 seconds
        - Data is calculated and stored in the signal variable
        - Signal is marked as updated using Blaeck.markSignalUpdated()
     - Sine_3
        - updates every 10 seconds
        - Sames as Sine_2, but Blaeck.update() combines updating and marking in a single function call
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals
float sine_1;
float sine_2;
float sine_3;

unsigned long updateLastTimeDone_s1 = 0;
unsigned long updateInterval_s1 = 100; // 100ms interval
bool updateFirstTime_s1 = true;

unsigned long updateLastTimeDone_s2 = 0;
unsigned long updateInterval_s2 = 2000; // 2s interval
bool updateFirstTime_s2 = true;

unsigned long updateLastTimeDone_s3 = 0;
unsigned long updateInterval_s3 = 10000; // 10s interval
bool updateFirstTime_s3 = true;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial
  Blaeck.begin(
      &Serial, // Serial reference
      3        // Maximal signal count used;
  );

  Blaeck.DeviceName = "Write Modes Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  Blaeck.addSignal(F("Sine_1"), &sine_1);
  Blaeck.addSignal(F("Sine_2"), &sine_2);
  Blaeck.addSignal(F("Sine_3"), &sine_3);
}

void loop()
{
  TransmitFirstSine();

  UpdateSecondSine();
  UpdateThirdSine();

  Blaeck.tickUpdated();
}

void TransmitFirstSine()
{
  if ((millis() - updateLastTimeDone_s1 >= updateInterval_s1) || updateFirstTime_s1)
  {
    updateLastTimeDone_s1 = millis();
    updateFirstTime_s1 = false;

    float calcSine = sin(millis() * 0.00005);
    Blaeck.write("Sine_1", calcSine);
  }
}

void UpdateSecondSine()
{
  if ((millis() - updateLastTimeDone_s2 >= updateInterval_s2) || updateFirstTime_s2)
  {
    updateLastTimeDone_s2 = millis();
    updateFirstTime_s2 = false;

    sine_2 = sin(millis() * 0.00005);
    Blaeck.markSignalUpdated("Sine_2");
  }
}

void UpdateThirdSine()
{
  if ((millis() - updateLastTimeDone_s3 >= updateInterval_s3) || updateFirstTime_s3)
  {
    updateLastTimeDone_s3 = millis();
    updateFirstTime_s3 = false;

    float calcSine = sin(millis() * 0.00005);
    Blaeck.update("Sine_3", calcSine);
  }
}
