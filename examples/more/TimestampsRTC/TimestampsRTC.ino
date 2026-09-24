/*
  TimestampsRTC.ino

  Timestamp data with a real-time clock using BLAECK_UNIX and a callback that returns
  microseconds since the Unix epoch. The RTC here has whole-second resolution.

  Requires an RTC. This sketch uses the one built into the Arduino UNO R4;
  another board just needs its own RTC library inside GetRTCUnixTimeMicros().
  For timestamp modes without RTC hardware, see docs/sending-data.md.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "RTC.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

// Signals
float sine;

unsigned long long GetRTCUnixTimeMicros()
{
  RTCTime currentTime;
  RTC.getTime(currentTime);

  // The RTC counts whole seconds, so the microsecond part is always zero.
  return (unsigned long long)currentTime.getUnixTime() * 1000000ULL;
}

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
  Blaeck.begin(&Serial).withSignals(1);

  Blaeck.DeviceName = "TimestampsRTC";
  Blaeck.DeviceFWVersion = ExampleVersion;

  Blaeck.addSignal(F("Sine_1"), &sine);

  Blaeck.setTimestampCallback(GetRTCUnixTimeMicros);
  Blaeck.setTimestampMode(BLAECK_UNIX);
}

void loop()
{
  UpdateSineNumbers();

  // Reads what has come in and writes the signals when the interval is up.
  Blaeck.tick();
}

void UpdateSineNumbers()
{
  sine = sin(millis() * 0.00005);
}