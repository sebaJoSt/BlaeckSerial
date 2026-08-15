/*
  TimeStampModes.ino

  This is a sample sketch to show the different timestamp modes of the BlaeckSerial library. This example uses the
  included RTC library of the Arduino UNO R4 board but it can be easily adapted to other real-time clock libraries.

  Comment/uncomment the desired timestamp mode in setup() to test it.
*/

#include "Arduino.h"
#include "RTC.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals
float sine;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  // Setup the Real Time Clock
  RTC.begin();

  // Set the start time (UTC). The date is arbitrary - it only gives the RTC
  // something to count from, so the timestamps in the data are plausible.
  // Replace it with a real time source if you need the actual wall clock.
  RTCTime startTime(13, Month::AUGUST, 2025, 14, 00, 00, DayOfWeek::WEDNESDAY, SaveLight::SAVING_TIME_ACTIVE);
  RTC.setTime(startTime);

  // Setup BlaeckSerial
  Blaeck.begin(
      &Serial, // Serial reference
      1        // Maximal signal count used;
  );

  Blaeck.DeviceName = "Sine Generator";
  Blaeck.DeviceHWVersion = "Arduino UNO R4";
  Blaeck.DeviceFWVersion = ExampleVersion;

  Blaeck.addSignal(F("Sine_1"), &sine);

  // Unix timestamp from RTC transmitted with the data
  Blaeck.setTimestampMode(BLAECK_UNIX);
  Blaeck.setTimestampCallback(GetRTCUnixTimeMicros);

  // micros() are transmitted with the data
  // Blaeck.setTimestampMode(BLAECK_MICROS);

  // default mode, no time information transmitted with the data
  // Blaeck.setTimestampMode(BLAECK_NO_TIMESTAMP);
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
  sine = sin(millis() * 0.00005);
}

unsigned long long GetRTCUnixTimeMicros()
{
  RTCTime currentTime;
  // Get current time from RTC (seconds precision)
  RTC.getTime(currentTime);

  return (unsigned long long)currentTime.getUnixTime() * 1000000ULL;
}