/*
  SignalMetadata.ino

  One signal of every shape a device can declare, so a dashboard's rendering can be checked
  side by side: numeric values with units and statistics, a counter, a diagnostic, binary
  sensors, a fixed set of names, and values that declare nothing at all.

  Everything moves on its own, so nothing needs poking to see it change.

  What to look for once it is logging:
    Temperature      a unit and a device class, so a host may offer °F instead
    EnergyTotal      total_increasing - the shape long-term statistics are built from
    Uptime           seconds, converted to minutes or hours by the host if it prefers
    SignalStrength   diagnostic and switched off until enabled: look for it under Diagnostic
    Heartbeat        never changes value: watch "last changed" in its more-info dialog
    Plain            declares nothing - the default rendering, for comparison
    Running / Fault  bool, one with a device class and one without
    PumpState        a closed set of names
    Note             free text

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// addSignal() keeps a pointer to these, so they have to be globals.
float Temperature = 21.0;
float EnergyTotal = 0.0;
unsigned long Uptime = 0;
int SignalStrength = -55;
int Heartbeat = 1;
float Plain = 0.0;
bool Running = true;
bool Fault = false;
char PumpState[10] = "idle";   // fits the longest option, "priming"
char Note[24] = "started";

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial, 10); // exactly the ten added below

  Blaeck.DeviceName = "Signal Metadata Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  // A measurement: goes up and down, meaningful at any instant. The device class is what lets
  // a host convert it - without one the unit is only a label.
  Blaeck.addSignal(F("Temperature"), &Temperature)
      .withUnit(F("\xC2\xB0" "C"))
      .withDeviceClass(F("temperature"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .withDisplayPrecision(1);

  // A running total that only ever grows. Reported as total_increasing so a host can build
  // consumption from it rather than averaging it.
  Blaeck.addSignal(F("EnergyTotal"), &EnergyTotal)
      .withUnit(F("kWh"))
      .withDeviceClass(F("energy"))
      .withStateClass(BLAECK_STATE_CLASS_TOTAL_INCREASING)
      .withDisplayPrecision(3);

  Blaeck.addSignal(F("Uptime"), &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT);

  // Describes the device rather than what it measures, and stays switched off until someone
  // asks for it.
  Blaeck.addSignal(F("SignalStrength"), &SignalStrength)
      .withUnit(F("dBm"))
      .withDeviceClass(F("signal_strength"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .diagnostic()
      .disabledByDefault();

  // Never changes value. Without forceUpdate an identical reading is not a state change at all,
  // so "last changed" freezes and a device still sending every second looks exactly like one
  // that died an hour ago. With it, "last changed" ticks over on every reading.
  //
  // The more-info dialog is where to see this. A history timeline merges neighbouring identical
  // states into one band, so both versions look the same there.
  Blaeck.addSignal(F("Heartbeat"), &Heartbeat)
      .forceUpdate();

  // Declares nothing: no unit, no statistics, no icon. There for comparison.
  Blaeck.addSignal(F("Plain"), &Plain);

  // A bool becomes a binary sensor. The device class says what the on-state means - without
  // one it reads as a plain On/Off.
  Blaeck.addSignal(F("Running"), &Running)
      .withDeviceClass(F("running"));
  Blaeck.addSignal(F("Fault"), &Fault);

  // A string whose values come from a fixed set. Every value reported must be in the list, so
  // this only suits a signal that cannot report anything else.
  Blaeck.addSignal(F("PumpState"), PumpState)
      .withOptions(F("idle,priming,running,fault"))
      .withDeviceClass(F("enum"));

  Blaeck.addSignal(F("Note"), Note)
      .withIcon(F("mdi:note-text"));

  // One look covers every addSignal() above.
  Blaeck.printRejections(&Serial);
}

void loop()
{
  UpdateSignals();
  Blaeck.tick();
}

void UpdateSignals()
{
  static unsigned long lastStep = 0;
  if (millis() - lastStep < 1000)
    return;
  lastStep = millis();

  Uptime = millis() / 1000;

  // A slow sweep, so the shape is obvious on a chart.
  float phase = Uptime * 0.05f;
  Temperature = 21.0f + 3.0f * sin(phase);
  Plain = 50.0f + 50.0f * sin(phase * 0.5f);
  SignalStrength = -55 + (int)(15.0f * sin(phase * 0.3f));

  // Only ever grows, which is what total_increasing means.
  EnergyTotal += 0.002f;

  Running = (Uptime % 20) < 15;
  Fault = (Uptime % 60) >= 55;

  // Walks the option list, so every declared value is exercised.
  const char *states[] = {"idle", "priming", "running", "fault"};
  strncpy(PumpState, states[(Uptime / 5) % 4], sizeof(PumpState) - 1);
  PumpState[sizeof(PumpState) - 1] = '\0';

  snprintf(Note, sizeof(Note), "up %lu s", Uptime);
}
