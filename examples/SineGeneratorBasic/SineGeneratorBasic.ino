/*
  SineGeneratorBasic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit five sine
  waves from the Arduino board to your PC. Each wave is a fifth of a period behind the one
  before it, so a plot shows five curves rather than one drawn five times.

  The signals are added in a loop, which is how a sketch with more than a handful of them
  usually does it: the name is built into a reused buffer, and one check afterwards says
  whether they all fit.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Signals. addSignal() keeps a pointer to each, so they have to be globals.
#define SIGNAL_COUNT 5
float sine[SIGNAL_COUNT];

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial
  BlaeckSerial.begin(
      &Serial,     // Serial reference
      SIGNAL_COUNT // Maximal signal count used;
  );

  BlaeckSerial.DeviceName = "Basic Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial. The name is a literal plus a counter, so one reused stack
  // buffer builds it without a String per signal.
  char signalName[10]; // fits "Sine_" plus SIGNAL_COUNT
  for (int i = 0; i < SIGNAL_COUNT; i++)
  {
    snprintf(signalName, sizeof(signalName), "Sine_%d", i + 1);
    BlaeckSerial.addSignal(signalName, &sine[i]);
  }

  // One look covers every addSignal() above: anything past the capacity begin() was given is
  // dropped, as is everything if the board had no RAM for the table at all.
  if (BlaeckSerial.hasSignalOverflow())
  {
    Serial.print("Signals not added: ");
    Serial.println(BlaeckSerial.getSignalOverflowCount());
  }

  /*Uncomment for fixed interval lock (ms)
    - ignores ACTIVATE/DEACTIVATE while locked */
  // BlaeckSerial.setIntervalMs(60000);
}

void loop()
{
  UpdateSineNumbers();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}

void UpdateSineNumbers()
{
  float phase = millis() * 0.00005;

  for (int i = 0; i < SIGNAL_COUNT; i++)
    sine[i] = sin(phase + i * (TWO_PI / SIGNAL_COUNT));
}
