/*
  SineGeneratorAdvanced.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit sine waves
  from the Arduino board to your PC.

  Requirements:
  - EEPROMEx Library

  Features:
  - EEPROM stores state (firmware version marker, signal activation mask)
  - Signals can be (de-)activated with <SIGNAL_ACTIVATE>
  - Print status with <STATUS>
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

// Forward declarations for command handlers
void onSignalActivate(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);
void onHelpOrList(const char *command, const char *const *params, byte paramCount);

//---MEASUREMENT
unsigned long measurementLastTimeDone = 0; //[ms]
unsigned long measurementInterval = 10;    //[ms]
bool measurementFirstTime = true;

void setup()
{

  // EEPROM
  EEPROMConfiguration();

  Serial.begin(115200);
  BlaeckSerial.begin(&Serial, MAXIMUM_SIGNALS);

  BlaeckSerial.DeviceName = "Advanced Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = FW_VERSION;

  BlaeckSerial.onCommand("SIGNAL_ACTIVATE", onSignalActivate);
  BlaeckSerial.onCommand("STATUS", onStatus);
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

  for (int i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
    {
      String signalName = "Sine_";
      BlaeckSerial.addSignal(signalName + i, &sine[i].value);
    }
  }
}

int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
