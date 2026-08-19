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
    <SIGNAL_DEACTIVATE_RANGE>, or all at once with <SIGNAL_ACTIVATE_ALL>,
    which is the same handler with its arguments declared up front
  - Print status with <STATUS>, which also pushes a one-line summary to a host

  The commands are registered with the typed helpers, so the device describes
  itself in <BLAECK.WRITE_COMMANDS> and a host (e.g. Loggbok / Home Assistant)
  can build controls for it: the two bounds are numbers with a range and a
  mirrored state signal, and the four actions are buttons.

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
BlaeckSerial Blaeck;

//---SIGNALS
#define MAXIMUM_SIGNALS 25
// A press payload is a string literal, so the upper bound has to be spelled into one. Going
// through the macro keeps it in step with MAXIMUM_SIGNALS instead of leaving a "25" behind
// that nothing would flag when the count changes.
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
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
void ApplySignalRange(bool activate, byte lo, byte hi);
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
  Blaeck.begin(&Serial, MAXIMUM_SIGNALS + 2);

  Blaeck.DeviceName = "Advanced Sine Number Generator";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = FW_VERSION;

  // Typed: each becomes a dashboard control. The bounds are numbers with a
  // range and a mirrored signal; applying them is a button, as is STATUS.
  Blaeck.onNumberCommand("SIGNAL_FIRST", onSetSignalFirst)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withStateFromSignal(F("Signal_First"));
  Blaeck.onNumberCommand("SIGNAL_LAST", onSetSignalLast)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withStateFromSignal(F("Signal_Last"));
  Blaeck.onButtonCommand("SIGNAL_ACTIVATE_RANGE", onSignalActivateRange)
      .withDisplayName(F("Activate range"));
  Blaeck.onButtonCommand("SIGNAL_DEACTIVATE_RANGE", onSignalDeactivateRange)
      .withDisplayName(F("Deactivate range"));
  // The same handler again, with the arguments already filled in: one press activates
  // everything instead of setting both bounds first. A preset is a second command sharing the
  // handler rather than a second payload on the first, because a button is one entity per
  // command name.
  Blaeck.onButtonCommand("SIGNAL_ACTIVATE_ALL", onSignalActivateRange)
      .withPressPayload(F("1," STRINGIFY(MAXIMUM_SIGNALS)))
      .withDisplayName(F("Activate all signals"))
      .withIcon(F("mdi:select-all"));
  Blaeck.onButtonCommand("STATUS", onStatus);

  // State channels are declared up-front so the host can announce a text
  // sensor for "Status" before the first line is written.
  Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:message-text"));

  // Plain catch-all: <LS> and <command?> answer with free-form help text,
  // which no dashboard control can represent, so it stays untyped.
  Blaeck.onAnyCommand(onHelpOrList);

  // Signals for Logging with BlaeckSerial
  // Blaeck.addSignal..
  UpdateLoggingSignals();

  PrintInfo(true);
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
  Blaeck.deleteSignals();

  // Re-added first: deleteSignals() drops these too, and the typed number
  // commands above refer to them by name. F() keeps the two names in flash, so
  // re-registering costs no heap for them either.
  Blaeck.addSignal(F("Signal_First"), &signalFirst);
  Blaeck.addSignal(F("Signal_Last"), &signalLast);

  // The name is a literal plus a counter, which withNameSuffix() says without building
  // it: the prefix stays in flash and the digits are produced when the name is sent. That
  // matters most here, where this runs again on every range change - a name copied to the
  // heap would be freed and allocated afresh each time, and that churn is what fragments
  // the heap.
  for (int i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
    {
      Blaeck.addSignal(F("Sine_"), &sine[i].value).withNameSuffix(i);
    }
  }
}

int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
