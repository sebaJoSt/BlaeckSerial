/*
  StateChannelTest.ino

  Every state channel type crossed with every way of getting a value, plus the widths and
  conversions that have gone wrong before.

    A   pointer   one channel per type, reading a variable
    B   getter    one channel per type, worked out when read
    C   tag       one channel per type, carrying only what writeState() hands it

  What to look for:
    Serial   PASS/FAIL for the checks that run once at startup
    Broker   what a host actually receives, which the sketch cannot see

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"
#include <limits.h>
#include <float.h>

BlaeckSerial Blaeck;

// Whether this build writes C_once at all, and what it writes. Set true, flash, let it
// publish; set false, flash again. The value tells the two builds apart on a dashboard.
#define ARM_C_ONCE true
#define ARM_C_ONCE_VALUE 111

// ---- A: variables the channels point at ---------------------------------------------------
bool aBool = false;
byte aByte = 0;
short aShort = 0;
unsigned short aUShort = 0;
int aInt = 0;
unsigned int aUInt = 0;
long aLong = 0;
unsigned long aULong = 0;
float aFloat = 0.0f;
double aDouble = 0.0;
char aText[40] = "start";

// ---- B: what the getters are worked out from ----------------------------------------------
byte gClients = 0;
bool gFault = false;

// A signal, so a logging session has something to log.
unsigned long Uptime = 0;

int checks = 0;
int failures = 0;

void Check(const __FlashStringHelper *what, bool ok)
{
  checks++;
  if (!ok) failures++;
  Serial.print(ok ? F("PASS  ") : F("FAIL  "));
  Serial.println(what);
}

// ---- B: getters -----------------------------------------------------------------------------
bool gGetBool() { return !gFault; }
byte gGetByte() { return gClients; }
short gGetShort() { return (short)(gClients * -100); }
unsigned short gGetUShort() { return (unsigned short)(gClients * 1000); }
int gGetInt() { return gClients * -1000; }
unsigned int gGetUInt() { return gClients * 10000u; }
long gGetLong() { return (long)millis(); }
unsigned long gGetULong() { return millis(); }
float gGetFloat() { return gClients / 3.0f; }
double gGetDouble() { return gClients / 7.0; }

// Returns nullptr when there is nothing to say, which must leave the channel with no value
// rather than an empty string - an empty retained payload deletes the topic it lands on.
const char *gGetText()
{
  if (gFault) return nullptr;
  static char buf[32];
  snprintf(buf, sizeof(buf), "ok, %u client%s", gClients, gClients == 1 ? "" : "s");
  return buf;
}

// A text longer than the wire allows, to exercise the 255-byte truncation.
const char *gGetOverlong()
{
  static char buf[300];
  for (unsigned int i = 0; i < sizeof(buf) - 1; i++)
    buf[i] = (char)('a' + (i % 26));
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

// The board this was built for, so a recording says which one produced it. A harness runs
// on every core the library supports, and the widths below are what differ.
#if defined(ARDUINO_GIGA)
#define HARNESS_BOARD "Arduino Giga R1"
#elif defined(ARDUINO_AVR_MEGA2560)
#define HARNESS_BOARD "Arduino Mega 2560 Rev3"
#elif defined(ARDUINO_ARCH_ESP32)
#define HARNESS_BOARD "ESP32"
#else
#define HARNESS_BOARD "unknown board"
#endif

// int is two bytes on AVR and four on a 32-bit core, double four and eight. A driver reads
// the width off the frame, but a run is easier to read when the board has said it too.
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

// Asked for rather than only printed at boot: a Giga does not reset when DTR is
// raised, so a driver that opens the port has missed the startup lines already.
void onWidths(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  PrintWidths();
}

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  Blaeck.begin(&Serial)
      .withCommands(1)
      .withSignals(1)
      .withStateChannels(40)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "State Channel Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));

  // ---- A: pointer, one per type ----
  Blaeck.addStateChannel(F("A_bool"), &aBool).withDeviceClass(F("problem"));
  Blaeck.addStateChannel(F("A_byte"), &aByte);
  Blaeck.addStateChannel(F("A_short"), &aShort);
  Blaeck.addStateChannel(F("A_ushort"), &aUShort);
  Blaeck.addStateChannel(F("A_int"), &aInt);
  Blaeck.addStateChannel(F("A_uint"), &aUInt);
  Blaeck.addStateChannel(F("A_long"), &aLong);
  Blaeck.addStateChannel(F("A_ulong"), &aULong);
  Blaeck.addStateChannel(F("A_float"), &aFloat).withUnit(F("V")).withDisplayPrecision(3);
  Blaeck.addStateChannel(F("A_double"), &aDouble).withUnit(F("V"));
  Blaeck.addStateChannel(F("A_text"), aText);

  // ---- B: getter, one per type ----
  Blaeck.addStateChannel(F("B_bool"), BlaeckBool).withStateValue(gGetBool);
  Blaeck.addStateChannel(F("B_byte"), BlaeckByte).withStateValue(gGetByte);
  Blaeck.addStateChannel(F("B_short"), BlaeckShort).withStateValue(gGetShort);
  Blaeck.addStateChannel(F("B_ushort"), BlaeckUShort).withStateValue(gGetUShort);
  Blaeck.addStateChannel(F("B_int"), BlaeckInt).withStateValue(gGetInt);
  Blaeck.addStateChannel(F("B_uint"), BlaeckUInt).withStateValue(gGetUInt);
  Blaeck.addStateChannel(F("B_long"), BlaeckLong).withStateValue(gGetLong);
  Blaeck.addStateChannel(F("B_ulong"), BlaeckULong).withStateValue(gGetULong);
  Blaeck.addStateChannel(F("B_float"), BlaeckFloat).withStateValue(gGetFloat).withDisplayPrecision(4);
  Blaeck.addStateChannel(F("B_double"), BlaeckDouble).withStateValue(gGetDouble);
  Blaeck.addStateChannel(F("B_text"), BlaeckText).withStateText(gGetText);
  Blaeck.addStateChannel(F("B_overlong"), BlaeckText).withStateText(gGetOverlong).diagnostic();

  // ---- C: tag, one per type ----
  Blaeck.addStateChannel(F("C_bool"), BlaeckBool);
  Blaeck.addStateChannel(F("C_byte"), BlaeckByte);
  Blaeck.addStateChannel(F("C_short"), BlaeckShort);
  Blaeck.addStateChannel(F("C_ushort"), BlaeckUShort);
  Blaeck.addStateChannel(F("C_int"), BlaeckInt);
  Blaeck.addStateChannel(F("C_uint"), BlaeckUInt);
  Blaeck.addStateChannel(F("C_long"), BlaeckLong);
  Blaeck.addStateChannel(F("C_ulong"), BlaeckULong);
  Blaeck.addStateChannel(F("C_float"), BlaeckFloat);
  Blaeck.addStateChannel(F("C_double"), BlaeckDouble);
  Blaeck.addStateChannel(F("C_text"), BlaeckText);

  // Never written, and asked for its value every cycle. A channel declared by tag holds nothing
  // until something writes one, and there is no way to say that in a 0x95 frame - so the frame
  // must not be sent at all. Sent, a number would go out under the string encoding.
  Blaeck.addStateChannel(F("C_never"), BlaeckLong).diagnostic();

  // Written once, a few seconds after boot, and never again. A restart leaves the device
  // asserting nothing for it while the broker still serves the value from before - the one
  // thing a 0x95 frame cannot correct, since it has no way to say "no value".
  Blaeck.addStateChannel(F("C_once"), BlaeckLong).diagnostic();

  Blaeck.printRejections(&Serial);

  Serial.println(F("---- state channel test ----"));
  Blaeck.onCommand("WIDTHS", onWidths);

  PrintWidths();
  RunLocalChecks();
  Serial.print(F("---- "));
  Serial.print(checks - failures);
  Serial.print(F("/"));
  Serial.print(checks);
  Serial.println(failures == 0 ? F(" passed ----") : F(" passed, FAILURES ABOVE ----"));
}

// Everything observable from inside the sketch. A push that is accepted writes the converted
// value into the variable the channel reads; one that is refused leaves it untouched. That is
// the only side of a refusal a sketch can see, and it is enough to catch a guard that stopped
// guarding.
void RunLocalChecks()
{
  // A literal converts to whatever the channel was declared as, so the same 21 lands as a byte,
  // a float and a double without the call site having to match any of them.
  aByte = 0; Blaeck.writeState(F("A_byte"), 21);
  Check(F("int literal -> byte channel"), aByte == 21);

  aFloat = 0.0f; Blaeck.writeState(F("A_float"), 21);
  Check(F("int literal -> float channel"), aFloat == 21.0f);

  aDouble = 0.0; Blaeck.writeState(F("A_double"), 21);
  Check(F("int literal -> double channel"), aDouble == 21.0);

  aLong = 0; Blaeck.writeState(F("A_long"), 21);
  Check(F("int literal -> long channel"), aLong == 21);

  aBool = false; Blaeck.writeState(F("A_bool"), 21);
  Check(F("nonzero -> bool channel is true"), aBool == true);

  aBool = true; Blaeck.writeState(F("A_bool"), 0);
  Check(F("zero -> bool channel is false"), aBool == false);

  // Narrowing wraps, as it does in C++ and as it does when a sketch assigns the variable itself.
  aByte = 0; Blaeck.writeState(F("A_byte"), 300);
  Check(F("300 -> byte wraps to 44"), aByte == 44);

  aInt = 0; Blaeck.writeState(F("A_int"), 3.7f);
  Check(F("3.7 -> int truncates to 3"), aInt == 3);

  // The widths that have been wrong before: a double on AVR, an int on a 32-bit board.
  aDouble = 0.0; Blaeck.writeState(F("A_double"), 1234.5);
  Check(F("double round-trips through its own channel"), aDouble == 1234.5);

  aInt = 0; Blaeck.writeState(F("A_int"), (int)INT_MAX);
  Check(F("INT_MAX survives its own channel"), aInt == INT_MAX);

  aULong = 0; Blaeck.writeState(F("A_ulong"), (unsigned long)ULONG_MAX);
  Check(F("ULONG_MAX survives its own channel"), aULong == ULONG_MAX);

  // Refusals. Each must leave the variable exactly as it was.
  aFloat = 5.0f; Blaeck.writeState(F("A_float"), "text");
  Check(F("text refused on a numeric channel"), aFloat == 5.0f);

  strcpy(aText, "keep"); Blaeck.writeState(F("A_text"), 42);
  Check(F("number refused on a text channel"), strcmp(aText, "keep") == 0);

  aByte = 7; Blaeck.writeState(F("NoSuchChannel"), 99);
  Check(F("undeclared channel changes nothing"), aByte == 7);

  // A getter-backed channel has nowhere to store a push. Nothing here can see the refusal - the
  // channel owns no variable to leave untouched - so the debug stream is the only witness, and
  // this call is here to produce that line rather than to assert anything.
  Blaeck.writeState(F("B_byte"), 99);

  // Every channel above had to fit. A silent overflow would make half of this test nothing at
  // all, and each check would pass by never having run against a real channel.
  Check(F("no channel was dropped for want of table space"), !Blaeck.hasRejectedStateChannels());
}

void loop()
{
  Uptime = millis() / 1000;
  Blaeck.tick();
  Stir();
  PushEverything();
  WriteOnce();
}

// Moves what B is worked out from, so a host sees the getters change without anything pushing
// a value into them.
void Stir()
{
  static unsigned long last = 0;
  if (millis() - last < 2000) return;
  last = millis();

  gClients = (byte)((millis() / 2000) % 4);
  gFault = (gClients == 3);

  aByte++;
  aShort--;
  aUShort += 7;
  aInt += 1000;
  aUInt += 100000u;
  aLong -= 100000L;
  aULong += 1000000UL;
  aFloat += 0.125f;
  aDouble += 0.0625;
  snprintf(aText, sizeof(aText), "up %lu s", Uptime);
}

// Everything a host should see move. A and B take the no-value form because both already know
// where their value comes from; C is handed one.
void PushEverything()
{
  static unsigned long last = 0;
  if (millis() - last < 3000) return;
  last = millis();

  const __FlashStringHelper *readers[] = {
      F("A_bool"), F("A_byte"), F("A_short"), F("A_ushort"), F("A_int"), F("A_uint"),
      F("A_long"), F("A_ulong"), F("A_float"), F("A_double"), F("A_text"),
      F("B_bool"), F("B_byte"), F("B_short"), F("B_ushort"), F("B_int"), F("B_uint"),
      F("B_long"), F("B_ulong"), F("B_float"), F("B_double"), F("B_text"), F("B_overlong"),
      F("C_never")};
  for (byte i = 0; i < sizeof(readers) / sizeof(readers[0]); i++)
    Blaeck.writeState(readers[i]);

  // C has no variable and no getter, so each is handed a value - and the extremes are the ones
  // a width bug shows up in first.
  Blaeck.writeState(F("C_bool"), gFault);
  Blaeck.writeState(F("C_byte"), (byte)UCHAR_MAX);
  Blaeck.writeState(F("C_short"), (short)SHRT_MIN);
  Blaeck.writeState(F("C_ushort"), (unsigned short)USHRT_MAX);
  Blaeck.writeState(F("C_int"), INT_MAX);
  Blaeck.writeState(F("C_uint"), UINT_MAX);
  Blaeck.writeState(F("C_long"), LONG_MIN);
  Blaeck.writeState(F("C_ulong"), ULONG_MAX);
  Blaeck.writeState(F("C_float"), (float)-FLT_MAX);
  Blaeck.writeState(F("C_double"), (double)DBL_MAX);

  char line[48];
  snprintf(line, sizeof(line), "tick %lu, %u clients", Uptime, gClients);
  Blaeck.writeState(F("C_text"), line);
}

// C_once is written on a boot that arms it and never on one that does not, which is what
// an event-driven channel really looks like - a fault that happened last run and not this
// one. Flash with ARM_C_ONCE true, let it publish, then flash with it false: the device
// now asserts nothing for the channel while the broker goes on serving the old value, and
// no frame can say otherwise.
void WriteOnce()
{
  static bool written = false;
  // Late enough that a host connecting on DTR is attached before it fires. It happens once,
  // and a host that misses it never learns the value at all.
  if (!ARM_C_ONCE || written || Uptime < 10) return;
  written = true;
  Blaeck.writeState(F("C_once"), (long)ARM_C_ONCE_VALUE);
  Serial.println(F("C_once written, and never again this boot"));
}
