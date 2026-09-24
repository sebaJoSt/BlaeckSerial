/*
  WriteModes.ino

  Three signals get the same value once per second. Only the sending method differs:

    Immediate  write() stores the value and sends it immediately.
    Marked     assign the variable, then markSignalUpdated() flags it for sending.
    Updated    update() stores the value and flags it in one call.

  Marked and Updated behave identically. tickUpdated() sends the latest values of flagged
  signals on the host's interval; intermediate values are not queued.

  Try this:
    Set the host's logging interval to 5000 ms, or send <BLAECK.ACTIVATE,5000>.
    Immediate sends each new value once per second. After the initial interval-driven
    response, Marked and Updated send their latest values every five seconds.
    Send <BLAECK.DEACTIVATE>: interval-driven sends stop, but the explicit write() calls
    keep sending Immediate. Deactivation does not stop the sketch's own writes.

  These are binary data frames, not readable text in a serial monitor.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals
float Immediate = 0.0f;
float Marked = 0.0f;
float Updated = 0.0f;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup BlaeckSerial
  Blaeck.begin(&Serial).withSignals(3);

  Blaeck.DeviceName = "Write Modes Demo";
  Blaeck.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  Blaeck.addSignal(F("Immediate"), &Immediate);
  Blaeck.addSignal(F("Marked"), &Marked);
  Blaeck.addSignal(F("Updated"), &Updated);
}

void loop()
{
  UpdateSignals();

  // Reads what has come in and sends the signals marked above.
  Blaeck.tickUpdated();
}

void UpdateSignals()
{
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();
  if (now - lastUpdate < 1000UL)
    return;
  lastUpdate = now;

  const float value = sin(now * 0.00005f);

  Blaeck.write("Immediate", value);

  Marked = value;
  Blaeck.markSignalUpdated("Marked");

  Blaeck.update("Updated", value);
}
