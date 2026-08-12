/*
  SineGeneratorBasic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit five sine
  waves from the Arduino board to your PC, each a fifth of a period behind the one before.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// addSignal() keeps a pointer to these, so they have to be globals.
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

  // One reused buffer builds the names, rather than a String per signal.
  char signalName[10]; // fits "Sine_" plus SIGNAL_COUNT
  for (int i = 0; i < SIGNAL_COUNT; i++)
  {
    snprintf(signalName, sizeof(signalName), "Sine_%d", i + 1);
    BlaeckSerial.addSignal(signalName, &sine[i]);
  }

  // One look covers every addSignal() above.
  if (BlaeckSerial.hasRejectedSignals())
  {
    Serial.print("Signals not added: ");
    Serial.println(BlaeckSerial.getRejectedSignalCount());
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
