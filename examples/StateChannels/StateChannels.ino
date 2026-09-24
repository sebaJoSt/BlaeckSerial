/*
  StateChannels.ino

  State channels are shown but never logged. This example uses one per value source:
    Temperature  pointer         reads a variable the sketch keeps
    Running      getter          derives a value from the current operating phase
    LastError    explicit write  has no value until a fault is reported

  Temperature and Running are pushed every two seconds with writeState(name).
  LastError is sent only when a fault occurs, with writeState(name, value).
  These approaches work with text, numbers and booleans, not just the types shown here.

  The simulation repeats normal -> fault -> recovery, ten seconds per phase.
  Running is true only during normal operation. LastError reports the last fault,
  not the current status. Uptime is the only signal, so it alone is logged.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

unsigned long Uptime = 0;
float Temperature = 20.0f;

enum class Phase { Normal, Fault, Recovery };
Phase CurrentPhase = Phase::Normal;

// Getters run while a frame is assembled: compute a value, but do not send anything.
bool isRunning()
{
  return CurrentPhase == Phase::Normal;
}

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(1)
      .withStateChannels(3)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "State Channels Demo";
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));

  // Pointer: Temperature must remain alive for as long as the channel uses it.
  Blaeck.addStateChannel(F("Temperature"), &Temperature)
      .withUnit(F("\xC2\xB0" "C"))
      .withDeviceClass(F("temperature"))
      .withDisplayPrecision(1);

  // Getter: evaluated when read, but still needs a push to notify the host of changes.
  Blaeck.addStateChannel(F("Running"), BlaeckBool)
      .withStateValue(isRunning);

  // Type tag only: unlike a pointer, this can represent "no value yet".
  Blaeck.addStateChannel(F("LastError"), BlaeckText)
      .withIcon(F("mdi:alert-circle"))
      .diagnostic();

  Blaeck.printRejections(&Serial);
}

void loop()
{
  Uptime = millis() / 1000;
  UpdateSimulation();
  Blaeck.tick();
}

void UpdateSimulation()
{
  static unsigned long lastUpdate = 0;
  static unsigned long lastPhase = 0;
  const unsigned long now = millis();
  if (now - lastUpdate < 2000UL)
    return;
  lastUpdate = now;

  if (now - lastPhase >= 10000UL)
  {
    lastPhase = now;
    switch (CurrentPhase)
    {
    case Phase::Normal:
      CurrentPhase = Phase::Fault;
      break;
    case Phase::Fault:
      CurrentPhase = Phase::Recovery;
      break;
    case Phase::Recovery:
      CurrentPhase = Phase::Normal;
      break;
    }

    if (CurrentPhase == Phase::Fault)
    {
      // The local text is copied into the frame before writeState() returns.
      char error[40];
      snprintf(error, sizeof(error), "simulated fault after %lu s", Uptime);
      Blaeck.writeState(F("LastError"), error);
    }
  }

  Temperature += ((isRunning() ? 21.0f : 18.0f) - Temperature) * 0.25f;
  Blaeck.writeState(F("Temperature"));
  Blaeck.writeState(F("Running"));
}
