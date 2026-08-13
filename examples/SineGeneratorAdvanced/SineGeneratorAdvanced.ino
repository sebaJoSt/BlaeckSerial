/*
  SineGeneratorAdvanced.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit sine waves
  from the Arduino board to your PC.

  Requirements:
  - EEPROMEx Library

  Features:
  - EEPROM stores state (firmware version marker, signal activation mask)
  - Signals can be (de-)activated over a range, set with <SIGNAL_FIRST> and
    <SIGNAL_LAST> and applied with <SIGNAL_ACTIVATE_RANGE> /
    <SIGNAL_DEACTIVATE_RANGE>
  - Print status with <STATUS>, which also pushes a one-line summary to a host

  The commands are registered with the typed helpers, so the device describes
  itself in <BLAECK.WRITE_COMMANDS> and a host (e.g. Loggbok / Home Assistant)
  can build controls for it: the two bounds are numbers with a range and a
  mirrored state signal, and the three actions are buttons.

  Splitting the range into "set the bounds, then press apply" is what makes
  that possible. A single <SIGNAL_ACTIVATE,first,last> command with magic
  values for all/none/odd/even maps to no dashboard control at all.

  <LS> and <command?> are still answered by a plain onAnyCommand catch-all -
  free-form help text is not something a dashboard can model, so it stays
  untyped on purpose.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"
#include <EEPROMex.h>

//---FIRMWARE
// FW_VERSION[6] = "X.xxx" +  '\0' (total 6 chars)
// Updating FW_VERSION initializes EEPROM
const char FW_VERSION[6] = "1.000";

// EEPROM
struct EEPROMaddress
{ // use int for all addresses
  int firmware_version;
  int signalActivated;
  int signalFirst;
  int signalLast;
} eepromaddress;

//---INSTANCES
BlaeckSerial BlaeckSerial;

//---SIGNALS
#define MAXIMUM_SIGNALS 25
struct BlaeckSignal
{
  bool isActivated;
  float value;
} sine[MAXIMUM_SIGNALS + 1];
// unused: sine[0]

//---SIGNAL RANGE
// Bounds for the activate/deactivate buttons. Mirrored as signals so a
// dashboard shows the range the next button press will apply to.
byte signalFirst = 1;
byte signalLast = MAXIMUM_SIGNALS;

// Forward declarations for command handlers
void onSetSignalFirst(const char *command, const char *const *params, byte paramCount);
void onSetSignalLast(const char *command, const char *const *params, byte paramCount);
void onSignalActivateRange(const char *command, const char *const *params, byte paramCount);
void onSignalDeactivateRange(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);
void onHelpOrList(const char *command, const char *const *params, byte paramCount);
void ApplySignalRange(bool activate);
void PersistActivatedSignals();

//---MEASUREMENT
unsigned long measurementLastTimeDone = 0; //[ms]
unsigned long measurementInterval = 10;    //[ms]
bool measurementFirstTime = true;

void setup()
{

  // EEPROM
  EEPROMConfiguration();

  Serial.begin(115200);
  // +2 for the Signal_First / Signal_Last state signals
  BlaeckSerial.begin(&Serial, MAXIMUM_SIGNALS + 2);

  BlaeckSerial.DeviceName = "Advanced Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = FW_VERSION;

  // Typed: each becomes a dashboard control. The bounds are numbers with a
  // range and a mirrored signal; applying them is a button, as is STATUS.
  BlaeckSerial.onNumberCommand("SIGNAL_FIRST", onSetSignalFirst)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withStateSignal(F("Signal_First"));
  BlaeckSerial.onNumberCommand("SIGNAL_LAST", onSetSignalLast)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withStateSignal(F("Signal_Last"));
  BlaeckSerial.onButtonCommand("SIGNAL_ACTIVATE_RANGE", onSignalActivateRange);
  BlaeckSerial.onButtonCommand("SIGNAL_DEACTIVATE_RANGE", onSignalDeactivateRange);
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  // State channels are declared up-front so the host can announce a text
  // sensor for "Status" before the first line is written.
  BlaeckSerial.addStateChannel("Status").withIcon(F("mdi:message-text"));

  // Plain catch-all: <LS> and <command?> answer with free-form help text,
  // which no dashboard control can represent, so it stays untyped.
  BlaeckSerial.onAnyCommand(onHelpOrList);

  // Signals for Logging with BlaeckSerial
  // BlaeckSerial.addSignal..
  UpdateLoggingSignals();

  PrintInfo(true);
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
  if ((millis() - measurementLastTimeDone >= measurementInterval) || measurementFirstTime)
  {
    measurementLastTimeDone = millis();
    measurementFirstTime = false;

    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
    {
      float val = i * sin(millis() * 0.000005 * i);
      sine[i].value = val;
    }
  }
}

void UpdateLoggingSignals()
{
  BlaeckSerial.deleteSignals();

  // Re-added first: deleteSignals() drops these too, and the typed number
  // commands above refer to them by name.
  BlaeckSerial.addSignal("Signal_First", &signalFirst);
  BlaeckSerial.addSignal("Signal_Last", &signalLast);

  // The name is a literal plus a counter, so one reused stack buffer builds it without a
  // String per signal - this runs again on every range change, and the churn is what
  // fragments the heap.
  char signalName[10]; // fits "Sine_" plus MAXIMUM_SIGNALS
  for (int i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
    {
      snprintf(signalName, sizeof(signalName), "Sine_%d", i);
      BlaeckSerial.addSignal(signalName, &sine[i].value);
    }
  }
}

int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
