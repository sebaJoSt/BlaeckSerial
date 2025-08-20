/*
  TimeStampMode.ino

  This is a sample sketch to show the different timestamp modes of the BlaeckSerial library. This example uses the
  included RTC library of the Arduino UNO R4 board but it can be easily adapted to other real-time clock libraries.

  Comment/uncomment the desired timestamp mode in setup() to test it.
*/

#include "Arduino.h"
#include "RTC.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

// Signals
float sine;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup the Real Time Clock
  RTC.begin();

  // Set the start time (UTC)
  RTCTime startTime(13, Month::AUGUST, 2025, 14, 00, 00, DayOfWeek::WEDNESDAY, SaveLight::SAVING_TIME_ACTIVE);
  RTC.setTime(startTime);

  // Setup BlaeckSerial
  BlaeckSerial.begin(
      &Serial, // Serial reference
      1        // Maximal signal count used;
  );

  BlaeckSerial.DeviceName = "Sine Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino UNO R4";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  BlaeckSerial.addSignal(F("Sine_1"), &sine);

  // Unix timestamp transmitted with the data
  BlaeckSerial.setTimestampMode(BLAECK_UNIXTIME);
  BlaeckSerial.setTimestampCallback(GetRTCUnixTime);

  // micros() are transmitted with the data
  // BlaeckSerial.setTimestampMode(BLAECK_MICROS);

  // default mode, no time information transmitted with the data
  // BlaeckSerial.setTimestampMode(BLAECK_NO_TIMESTAMP);
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
  sine = sin(millis() * 0.00005);
}

unsigned long GetRTCUnixTime()
{
  RTCTime currentTime;
  // Get current time from RTC
  RTC.getTime(currentTime);

  return currentTime.getUnixTime();
}