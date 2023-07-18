/*
  SineGeneratorAdvanced.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit sine waves
  from the Arduino board to your PC.
  
  Requirements:
  - EEPROMEx Library
  
  Features:
  - EEPROM stores state
  - Signals can be (de-)activated with <SIGNAL_ACTIVATE>
  - Print status with <STATUS>
  - Master/Slave mode can be configured with <MASTER_SLAVE_MODE>
*/

#include <BlaeckSerial.h>
#include <EEPROMex.h>

//---FIRMWARE
// FW_VERSION[6] = "X.xxx" +  '\0' (total 6 chars)
// Updating FW_VERSION initializes EEPROM
const char FW_VERSION[6] = "1.000";

// EEPROM
struct EEPROMaddress
{ // use int for all addresses
  int firmware_version;
  int loggingActivated;
  int loggingInterval;
  int signalActivated;
  int masterSlaveMode;
  int slaveID;
} eepromaddress;

//---LOGGING
bool loggingActivated;
unsigned long loggingInterval = 1000; //[ms]

//---INSTANCES
BlaeckSerial BlaeckSerial;

//---SINGLE/MASTER/SLAVE-CONFIG
byte masterSlaveMode;
byte slaveID;

//---SIGNALS
#define MAXIMUM_SIGNALS 25
struct BlaeckSignal
{
  bool isActivated;
  float value;
} sine[MAXIMUM_SIGNALS + 1];
// unused: sine[0]

//---MEASUREMENT
unsigned long measurementLastTimeDone = 0; //[ms]
unsigned long measurementInterval = 10;    //[ms]
bool measurementFirstTime = true;

void setup()
{

  // EEPROM
  EEPROMConfiguration();

  Serial.begin(115200);
  if (masterSlaveMode == 0)
  { // Single Mode
    BlaeckSerial.begin(&Serial, MAXIMUM_SIGNALS);
  }
  else if (masterSlaveMode == 1)
  { // Master Mode
    BlaeckSerial.beginMaster(&Serial, MAXIMUM_SIGNALS, 400000L);
  }
  else if (masterSlaveMode == 2)
  { // Slave Mode
    BlaeckSerial.beginSlave(&Serial, MAXIMUM_SIGNALS, slaveID);
  }

  BlaeckSerial.DeviceName = "Advanced Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = FW_VERSION;

  BlaeckSerial.attachRead(startCommand);
  BlaeckSerial.setTimedData(loggingActivated, loggingInterval);

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
      sine[i].value = i * sin(millis() * 0.000005 * i);
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
