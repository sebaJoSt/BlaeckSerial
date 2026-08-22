/*
  SignalTimingTest.ino

  SignalMetadataTest asked whether what a signal declares survives to Home Assistant.
  This asks a different question: what timestamp does a value land in TimescaleDB with,
  and does it land at all the moment it is written rather than on the next periodic tick?

  Four things get exercised here, each against the actual database rows a host writes,
  not against what Home Assistant shows:

    - the three timestamp modes (PC / MICROS / UNIX) a device can choose, switched live
      through a select command so one flash covers all three;
    - the landmine in the library's own docs: BLAECK_UNIX with no callback stamps every
      row at the Unix epoch instead of failing - reproduced on demand rather than only
      once at boot, since setTimestampCallback(nullptr) re-arms it at any time;
    - write()'s per-call timestamp override, which should land in the row exactly as
      given, independent of whatever mode is otherwise active;
    - forceUpdate(), which the source has no dedup logic backing at all - every write()
      and every periodic tick reaches the wire whether the value moved or not, so a
      forced and an unforced signal holding the same frozen value should log identically.

    A rapid burst of write() calls (faster than the periodic interval) checks the fourth
    thing incidentally: whether out-of-cadence rows survive back-to-back, in order, with
    none dropped.

    A fifth thing, added afterwards: update() + writeUpdatedData() is a separate path from
    write() and from tick()'s own periodic sends (tick() always sends every signal, dirty
    or not - onlyUpdated is never true there in this sketch). update() only flips a signal's
    dirty flag; nothing reaches the wire until writeUpdatedData() is called, and that call
    should carry only the signals marked dirty since the last flush - not a full row.

  drive_signal_timing.py fires the commands and reads the rows back with psycopg2 - lgbk's
  own TimescaleDB connection, not the MQTT/HA path, since the question here is what got
  stored, not what got displayed.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// The board this was built for, so a recording says which one produced it.
#if defined(ARDUINO_GIGA)
#define HARNESS_BOARD "Arduino Giga R1"
#elif defined(ARDUINO_AVR_MEGA2560)
#define HARNESS_BOARD "Arduino Mega 2560 Rev3"
#elif defined(ARDUINO_ARCH_ESP32)
#define HARNESS_BOARD "ESP32"
#else
#define HARNESS_BOARD "unknown board"
#endif

// ---- signals -----------------------------------------------------------------------------
unsigned long Uptime = 0;      // periodic only, seconds since boot - the metronome every
                                // other row is read against.
long Periodic = 0;             // periodic only, increments every loop() pass - never
                                // touched by a command, so any jump in it is loop() alone.
long Pushed = 0;                // changed only by Fire_push - proves write() lands its own
                                // row between two periodic ticks rather than waiting for one.
long ExplicitTS = 0;            // changed only by Fire_explicit_ts, always with the same
                                // hardcoded timestamp, independent of TimestampMode.
long Burst = 0;                 // changed only by Fire_burst, five times back-to-back.
float FrozenForced = 42.0f;     // forceUpdate() on, value never changes.
float FrozenPlain = 42.0f;      // forceUpdate() off, same frozen value, for comparison.
long Marked = 0;                 // changed only by Fire_mark via update() - stays dirty,
                                  // off the wire, until Fire_flush calls writeUpdatedData().

// ---- TimestampMode select state -----------------------------------------------------------
// Index into "PC,MICROS,UNIX_calibrated,UNIX_no_callback", mirrored back as this select's
// own state so a driver can see which mode is currently live.
byte TimestampModeIdx = 0;

// A fixed, recognizable epoch far from "now" (2030-01-01T00:00:00Z) plus millis() since the
// mode was chosen, in microseconds - lets a driver confirm BLAECK_UNIX actually reads this
// callback rather than something else, and that it keeps advancing.
const unsigned long long UNIX_CALIBRATED_BASE_US = 1893456000000000ULL;
unsigned long long calibratedStartMillis = 0;

unsigned long long unixMicrosFake()
{
  return UNIX_CALIBRATED_BASE_US + (unsigned long long)(millis() - calibratedStartMillis) * 1000ULL;
}

// A second fixed epoch, arbitrary and unrelated to the one above (2000-01-01T00:00:00Z), used
// only as the explicit per-write override - it must land exactly regardless of TimestampMode.
const unsigned long long EXPLICIT_TS_US = 946684800000000ULL;

long burstBase = 0;

void PrintWidths()
{
  Serial.print(F("board "));
  Serial.print(F(HARNESS_BOARD));
  Serial.print(F(", int "));
  Serial.print((unsigned)sizeof(int));
  Serial.print(F(" bytes, long "));
  Serial.print((unsigned)sizeof(long));
  Serial.print(F(", float "));
  Serial.print((unsigned)sizeof(float));
  Serial.print(F(", double "));
  Serial.println((unsigned)sizeof(double));
}

void onWidths(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  PrintWidths();
}

void onTimestampMode(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)paramCount;
  TimestampModeIdx = (byte)atoi(params[0]);

  switch (TimestampModeIdx)
  {
  case 0: // PC - no device timestamp at all, lgbk stamps on arrival
    Blaeck.setTimestampMode(BLAECK_NO_TIMESTAMP);
    break;
  case 1: // MICROS - device-relative, library resyncs it against PC periodically
    Blaeck.setTimestampMode(BLAECK_MICROS);
    break;
  case 2: // UNIX_calibrated - a real callback, restarted at "now" in the fake epoch
    calibratedStartMillis = millis();
    Blaeck.setTimestampCallback(unixMicrosFake);
    Blaeck.setTimestampMode(BLAECK_UNIX);
    break;
  case 3: // UNIX_no_callback - the documented landmine, re-armed on demand: every row
          // after this stamps at the Unix epoch (1970-01-01) until a callback is set again.
    Blaeck.setTimestampCallback(nullptr);
    Blaeck.setTimestampMode(BLAECK_UNIX);
    break;
  default:
    break;
  }

  Blaeck.writeCommandState(command);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onFirePush(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Pushed++;
  Blaeck.write("Pushed", Pushed);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onFireExplicitTs(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  ExplicitTS++;
  Blaeck.write("ExplicitTS", ExplicitTS, EXPLICIT_TS_US);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onFireBurst(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  for (byte i = 1; i <= 5; i++)
  {
    burstBase++;
    Burst = burstBase;
    Blaeck.write("Burst", Burst);
  }
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onFireMark(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Marked++;
  Blaeck.update("Marked", Marked); // dirty flag only - no wire write happens here.
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onFireFlush(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Blaeck.writeUpdatedData(); // sends only signals dirtied since the last flush.
  Serial.print(F("CMD "));
  Serial.println(command);
}

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(8)
      .withCommands(7)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "Signal Timing Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.onCommand("WIDTHS", onWidths);

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));
  Blaeck.addSignal(F("Periodic"), &Periodic);
  Blaeck.addSignal(F("Pushed"), &Pushed);
  Blaeck.addSignal(F("ExplicitTS"), &ExplicitTS);
  Blaeck.addSignal(F("Burst"), &Burst);
  Blaeck.addSignal(F("FrozenForced"), &FrozenForced).forceUpdate();
  Blaeck.addSignal(F("FrozenPlain"), &FrozenPlain);
  Blaeck.addSignal(F("Marked"), &Marked);

  Blaeck.onSelectCommand("TimestampMode", onTimestampMode)
      .withOptions(F("PC,MICROS,UNIX_calibrated,UNIX_no_callback"))
      .withOwnState(F("TimestampMode_state"), &TimestampModeIdx);

  Blaeck.onButtonCommand("Fire_push", onFirePush);
  Blaeck.onButtonCommand("Fire_explicit_ts", onFireExplicitTs);
  Blaeck.onButtonCommand("Fire_burst", onFireBurst);
  Blaeck.onButtonCommand("Fire_mark", onFireMark);
  Blaeck.onButtonCommand("Fire_flush", onFireFlush);

  PrintWidths();
  Serial.println(F("---- SignalTimingTest: 8 signals, 7 commands declared ----"));
}

void loop()
{
  Uptime = millis() / 1000UL;
  Periodic++;
  Blaeck.tick();
}
