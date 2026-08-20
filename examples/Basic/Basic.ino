/*
  Basic.ino

  The smallest useful sketch: two numbers, registered as signals, sent out on
  the interval the host asks for.

  A signal is a value that gets sampled and logged. Registering one is all it
  takes - tick() reads it and writes the frame, so nothing here has to know
  when a transmission is due.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals
float randomSmallNumber;
long randomBigNumber;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial
  Blaeck.begin(
      &Serial, // Serial reference
      2        // Maximal signal count used
  );

  // Names the device wherever it turns up
  Blaeck.DeviceName = "Random Number Generator";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // F() keeps the name in flash instead of SRAM, which is worth having on an
  // Uno or Nano and costs nothing anywhere else.
  Blaeck.addSignal(F("Small Number"), &randomSmallNumber);
  Blaeck.addSignal(F("Big Number"), &randomBigNumber);
}

void loop()
{
  UpdateRandomNumbers();

  // Reads what has come in and writes the signals when the interval is up.
  Blaeck.tick();
}

void UpdateRandomNumbers()
{
  // Random small number from 0.00 to 10.00
  randomSmallNumber = random(1001) / 100.0;

  // Random big number from 2 000 000 000 to 2 100 000 000
  randomBigNumber = random(2000000000, 2100000001);
}
