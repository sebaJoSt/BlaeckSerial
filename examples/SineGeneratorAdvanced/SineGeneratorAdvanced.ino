/*
  SineGeneratorAdvanced.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit sine waves
  from the Arduino board to your PC.

  Requirements:
  - none beyond the library itself: EEPROM comes with the board's core

  Features:
  - EEPROM stores state (firmware version marker, signal activation mask)
  - Signals can be (de-)activated over a range, set with <SIGNAL_FIRST> and
    <SIGNAL_LAST> and applied with <SIGNAL_ACTIVATE> /
    <SIGNAL_DEACTIVATE>, or all at once with <SIGNAL_ACTIVATE_ALL>,
    which is the same handler with its arguments declared up front
  - Print status with <STATUS>, which also pushes a one-line summary to a host

  The commands are registered with the typed helpers, so the device describes
  itself in <BLAECK.WRITE_COMMANDS> and a host (e.g. Loggbok / Home Assistant)
  can build controls for it: the two bounds are numbers with a range and a
  state channel of their own, and the three actions are buttons.

  Splitting the range into "set the bounds, then press apply" is what makes
  that possible. One command taking first and last with magic values for
  all/none/odd/even maps to no dashboard control at all.

  <LS> and <command?> are plain onCommand() registrations - free-form help
  text is not something a dashboard can model, so it stays untyped on purpose,
  but the library still does the matching and a host still sees the names.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"
#include <EEPROM.h>

//---FIRMWARE
// FW_VERSION[6] = "X.xxx" +  '\0' (total 6 chars)
// Updating FW_VERSION initializes EEPROM
const char FW_VERSION[6] = "1.000";

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

//---EEPROM LAYOUT
// Offsets are fixed at compile time instead of handed out by an allocator at boot. The
// layout is known here, and a constant cannot drift between the write and the read the way
// an allocator can when someone reorders the calls that hand the addresses out.
constexpr int EEPROM_ADDR_FW_VERSION = 0;
constexpr int EEPROM_ADDR_SIGNAL_ACTIVATED = EEPROM_ADDR_FW_VERSION + sizeof(FW_VERSION);
constexpr int EEPROM_ADDR_SIGNAL_FIRST = EEPROM_ADDR_SIGNAL_ACTIVATED + (MAXIMUM_SIGNALS + 1);
constexpr int EEPROM_ADDR_SIGNAL_LAST = EEPROM_ADDR_SIGNAL_FIRST + sizeof(byte);
constexpr int EEPROM_BYTES = EEPROM_ADDR_SIGNAL_LAST + sizeof(byte);

// AVR writes straight through to real EEPROM, so both of these are nothing there. The cores
// that only emulate EEPROM in flash need a size up front and a commit afterwards, and
// wrapping that here is what lets the rest of the sketch read and write the same way on all
// of them. EEPROM.put() already compares before it writes, so an unchanged value costs no
// erase cycle on either kind of board.
#if defined(ARDUINO_ARCH_AVR)
inline void EepromBegin() {}
inline void EepromCommit() {}
#else
inline void EepromBegin() { EEPROM.begin(EEPROM_BYTES); }
inline void EepromCommit() { EEPROM.commit(); }
#endif

// Forward declarations for command handlers
void onSetSignalFirst(const char *command, const char *const *params, byte paramCount);
void onSetSignalLast(const char *command, const char *const *params, byte paramCount);
void onSignalActivate(const char *command, const char *const *params, byte paramCount);
void onSignalDeactivate(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);
void onList(const char *command, const char *const *params, byte paramCount);
void onHelpList(const char *command, const char *const *params, byte paramCount);
void onHelpSignalFirst(const char *command, const char *const *params, byte paramCount);
void onHelpSignalLast(const char *command, const char *const *params, byte paramCount);
void onHelpSignalActivate(const char *command, const char *const *params, byte paramCount);
void onHelpSignalDeactivate(const char *command, const char *const *params, byte paramCount);
void onHelpSignalActivateAll(const char *command, const char *const *params, byte paramCount);
void onHelpStatus(const char *command, const char *const *params, byte paramCount);
void ApplySignalRange(bool activate, byte lo, byte hi);
const char *StatusText();
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
  // Sized rather than left to the default: six commands and eight help topics is fewer than
  // the sixteen an AVR board gets for free, and the three state channels are fewer than the
  // eight it would otherwise reserve.
  Blaeck.begin(&Serial)
      .withSignals(MAXIMUM_SIGNALS)
      .withCommands(14)
      .withStateChannels(3);

  Blaeck.DeviceName = "Advanced Sine Number Generator";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = FW_VERSION;

  // Typed: each becomes a dashboard control. The bounds are numbers keeping their value on a
  // state channel of their own; applying them is a button.
  //
  // A bound is a setting, not a measurement, so it is not a signal: a signal is a column in
  // every logged row, and these two would be a constant repeated on each one. The state
  // channel is where a value that only changes when someone changes it belongs. Boxes rather
  // than sliders, because the pair is picked by number - a slider invites dragging past the
  // bound you meant.
  Blaeck.onNumberCommand("SIGNAL_FIRST", onSetSignalFirst)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withMode(BLAECK_NUMBER_MODE_BOX)
      .withDisplayName(F("First signal"))
      .withOwnState(F("Signal_First"), &signalFirst);
  Blaeck.onNumberCommand("SIGNAL_LAST", onSetSignalLast)
      .withRange(1.0f, (float)MAXIMUM_SIGNALS, 1.0f)
      .withMode(BLAECK_NUMBER_MODE_BOX)
      .withDisplayName(F("Last signal"))
      .withOwnState(F("Signal_Last"), &signalLast);
  Blaeck.onButtonCommand("SIGNAL_ACTIVATE", onSignalActivate)
      .withDisplayName(F("Activate range"));
  Blaeck.onButtonCommand("SIGNAL_DEACTIVATE", onSignalDeactivate)
      .withDisplayName(F("Deactivate range"));
  // The same handler again, with the arguments already filled in: one press activates
  // everything instead of setting both bounds first. A preset is a second command sharing the
  // handler rather than a second payload on the first, because a button is one entity per
  // command name.
  Blaeck.onButtonCommand("SIGNAL_ACTIVATE_ALL", onSignalActivate)
      .withPressPayload(F("1," STRINGIFY(MAXIMUM_SIGNALS)))
      .withDisplayName(F("Activate all signals"))
      .withIcon(F("mdi:select-all"));

  // State channels are declared up-front so the host can announce a text
  // sensor for "Status" before the first line is written. Diagnostic: a status line describes
  // the board rather than the signals it generates, so it belongs beside the device info
  // instead of among the controls.
  //
  // The value comes from a getter rather than a stored string, so a host asking for the
  // catalog is answered with the count as it is at that moment - there is nothing to go stale,
  // and a host that connects long after the last change still gets the truth.
  Blaeck.addStateChannel(F("Status"))
      .withStateText(StatusText)
      .withIcon(F("mdi:message-text"))
      .diagnostic();

  // Plain: help text is free-form, which no dashboard control can represent, so these are
  // registered with onCommand() and stay untyped. A host still sees them in the catalog and
  // can offer them in a command palette. One name per topic, so the library matches them the
  // way it matches every other command.
  //
  // STATUS is here for the same reason. Its multi-line report is worth having at the terminal,
  // but as a button it would only have offered to refresh a channel that already keeps itself
  // current - a control that does nothing the host has not already been told.
  Blaeck.onCommand("STATUS", onStatus);
  Blaeck.onCommand("LS", onList);
  Blaeck.onCommand("LS?", onHelpList);
  Blaeck.onCommand("SIGNAL_FIRST?", onHelpSignalFirst);
  Blaeck.onCommand("SIGNAL_LAST?", onHelpSignalLast);
  Blaeck.onCommand("SIGNAL_ACTIVATE?", onHelpSignalActivate);
  Blaeck.onCommand("SIGNAL_DEACTIVATE?", onHelpSignalDeactivate);
  Blaeck.onCommand("SIGNAL_ACTIVATE_ALL?", onHelpSignalActivateAll);
  Blaeck.onCommand("STATUS?", onHelpStatus);

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
