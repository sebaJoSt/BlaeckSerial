/*
  SineGeneratorBasic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit five
  evenly phase-shifted sine waves from the Arduino board to Loggbok.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// addSignal() keeps a pointer to these, so they have to be globals.
#define SIGNAL_COUNT 5
float sine[SIGNAL_COUNT];

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial
  Blaeck.begin(
      &Serial,     // Serial reference
      SIGNAL_COUNT // Maximal signal count used;
  );

  Blaeck.DeviceName = "Basic Sine Number Generator";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // The prefix stays in flash and the number is added when the name is sent, so
  // "Sine_1".."Sine_5" cost no SRAM at all.
  for (int i = 0; i < SIGNAL_COUNT; i++)
  {
    Blaeck.addSignal(F("Sine_"), &sine[i]).withNameSuffix(i + 1);
  }

  // Prints only if a signal was dropped, and names the call that would have made room.
  Blaeck.printRejections(&Serial);
}

void loop()
{
  UpdateSineNumbers();

  // Reads what has come in and writes the signals when the interval is up.
  Blaeck.tick();
}

void UpdateSineNumbers()
{
  float phase = millis() * 0.00005;

  for (int i = 0; i < SIGNAL_COUNT; i++)
    sine[i] = sin(phase + i * (TWO_PI / SIGNAL_COUNT));
}
