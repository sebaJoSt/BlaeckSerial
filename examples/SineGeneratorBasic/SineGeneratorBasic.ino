/*
  SineGeneratorBasic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit five sine
  waves from the Arduino board to your PC, each a fifth of a period behind the one before.
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

  // The prefix stays in flash and the number is produced when the name is sent, so
  // "Sine_1".."Sine_5" cost no SRAM at all. Building them with snprintf into a reused
  // buffer works too, and is what you need for a name that is not a prefix plus a
  // number - but every such name is then copied to the heap.
  for (int i = 0; i < SIGNAL_COUNT; i++)
  {
    Blaeck.addSignal(F("Sine_"), &sine[i]).withNameSuffix(i + 1);
  }

  // One look covers every addSignal() above.
  if (Blaeck.hasRejectedSignals())
  {
    Serial.print("Signals not added: ");
    Serial.println(Blaeck.getRejectedSignalCount());
  }
  // Or ask about every table at once, with the call that would have made room:
  //   Blaeck.printRejections(&Serial);
}

void loop()
{
  UpdateSineNumbers();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  Blaeck.tick();
}

void UpdateSineNumbers()
{
  float phase = millis() * 0.00005;

  for (int i = 0; i < SIGNAL_COUNT; i++)
    sine[i] = sin(phase + i * (TWO_PI / SIGNAL_COUNT));
}
