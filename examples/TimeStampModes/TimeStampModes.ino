/*
  TimeStampModes.ino

  Every data frame can carry a time, and this sketch shows the three modes it
  can be sent in. Pick one with TIMESTAMP_MODE below, then flash the board.

    BLAECK_NO_TIMESTAMP  no time is sent, so the host timestamps on arrival
    BLAECK_MICROS        micros() since power-up, supplied by the library
    BLAECK_UNIX          the wall clock, the only mode needing a clock source

  Requires an RTC. This sketch uses the one built into the Arduino UNO R4;
  another board just needs its own RTC library inside GetRTCUnixTimeMicros().

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "RTC.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

// The mode to run. This is the only line to change when trying another one.
#define TIMESTAMP_MODE BLAECK_UNIX

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

  Blaeck.DeviceName = "Timestamp Modes";
  Blaeck.DeviceHWVersion = "Arduino UNO R4";
  Blaeck.DeviceFWVersion = ExampleVersion;

  Blaeck.addSignal(F("Sine_1"), &sine);

  Blaeck.setTimestampMode(TIMESTAMP_MODE);

  // Only BLAECK_UNIX needs a clock source. The other two modes bring their own.
  if (TIMESTAMP_MODE == BLAECK_UNIX)
    Blaeck.setTimestampCallback(GetRTCUnixTimeMicros);
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

unsigned long long GetRTCUnixTimeMicros()
{
  RTCTime currentTime;
  RTC.getTime(currentTime);

  // The RTC counts whole seconds, so the microsecond part is always zero.
  return (unsigned long long)currentTime.getUnixTime() * 1000000ULL;
}