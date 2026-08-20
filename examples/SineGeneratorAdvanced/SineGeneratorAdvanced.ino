/*
  SineGeneratorAdvanced.ino

  This example sends configurable sine-wave signals from the Arduino to Loggbok.
  Loggbok forwards the data and device metadata through MQTT to Home Assistant.

  Requirements:
  - none beyond the library itself: EEPROM comes with the board's core

  Features:
  - EEPROM stores which signals are activated.
  - Typed commands activate or deactivate signal ranges in the BlaeckSerial catalog.
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
struct BlaeckSignal
{
  bool isActivated;
  float value;
} sine[MAXIMUM_SIGNALS + 1];
// unused: sine[0]

//---SIGNAL RANGE
// Bounds for the activate/deactivate buttons. Stored as command-owned state so
// Home Assistant shows the range the next button press will apply to.
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
void onSignalActivateAll(const char *command, const char *const *params, byte paramCount);
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
  // Sized explicitly for the five commands and two state channels this example declares.
  Blaeck.begin(&Serial)
      .withSignals(MAXIMUM_SIGNALS)
      .withCommands(5)
      .withStateChannels(2);

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
  Blaeck.onButtonCommand("SIGNAL_ACTIVATE_ALL", onSignalActivateAll)
      .withDisplayName(F("Activate all signals"))
      .withIcon(F("mdi:select-all"));

  UpdateLoggingSignals();
}

void loop()
{
  UpdateSineNumbers();

  // Processes Loggbok commands and transmits updated signals.
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
