/*
  EventMetadataTest.ino

  SignalMetadataTest and CommandMetadataTest asked whether what a signal or a command
  declares about itself survives to Home Assistant. This asks the same question of an
  event channel: one channel per thing a channel can declare - icon, category, device
  class, disabled-by-default, and a type list longer than the two-entry minimum - so a
  driver can check that each one arrives, and that a fired occurrence actually carries
  the right event_type once it does.

  EventTest already asks the other question about events - does the library refuse what
  it should, and drop what was never declared. Nobody had asked whether a channel a host
  builds actually looks the way its declaration says it should, the way SignalMetadataTest
  and CommandMetadataTest did for signals and commands. This is that harness, for events.

  writeEvent()'s type has to be a flash literal (see EventTest's comment on why a runtime
  string cannot select one), so there is one fire command per declared type rather than
  one command per channel - a driver can press each and confirm both that the channel
  survived discovery and that the specific type it asked for is what Home Assistant shows.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// A host that refuses to log with zero signals declared needs one to have something to do;
// what it says is not this harness's question.
unsigned long Uptime = 0;

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

// Every fire command does the same thing - send one named occurrence and say so on the
// debug stream, so a missing frame at the driver is the only difference between an event
// that went out and one that was refused.
void Fired(const char *command)
{
  Serial.print(F("CMD "));
  Serial.println(command);
}

// ---- Bare: the baseline every other channel is read against --------------------------------
void onFBareFirst(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Bare"), F("first")); Fired(c); }
void onFBareSecond(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Bare"), F("second")); Fired(c); }

// ---- Icon --------------------------------------------------------------------------------
void onFIconPing(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Icon"), F("ping")); Fired(c); }
void onFIconPong(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Icon"), F("pong")); Fired(c); }

// ---- Diag: filed as diagnostic rather than describing the device's work -------------------
void onFDiagCheck(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Diag"), F("check")); Fired(c); }
void onFDiagWarn(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Diag"), F("warn")); Fired(c); }

// ---- Hidden: registered but switched off until someone enables it -------------------------
void onFHiddenTrace(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Hidden"), F("trace")); Fired(c); }
void onFHiddenDump(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Hidden"), F("dump")); Fired(c); }

// ---- Class_button: the closed set's standard names, though none are required --------------
void onFButtonStart(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Class_button"), F("press_start")); Fired(c); }
void onFButtonEnd(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Class_button"), F("press_end")); Fired(c); }

// ---- Class_doorbell: "ring" is the one type this device class requires --------------------
void onFDoorbellRing(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Class_doorbell"), F("ring")); Fired(c); }

// ---- Class_motion ------------------------------------------------------------------------
void onFMotionDetected(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Class_motion"), F("detected")); Fired(c); }
void onFMotionCleared(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Class_motion"), F("cleared")); Fired(c); }

// ---- Multi_type: four types rather than two, so a longer list's order is checked too ------
void onFMultiAlpha(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Multi_type"), F("alpha")); Fired(c); }
void onFMultiBeta(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Multi_type"), F("beta")); Fired(c); }
void onFMultiGamma(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Multi_type"), F("gamma")); Fired(c); }
void onFMultiDelta(const char *c, const char *const *p, byte n) { (void)p; (void)n; Blaeck.writeEvent(F("Multi_type"), F("delta")); Fired(c); }

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(1)
      .withEventChannels(8)
      .withEventTypes(17)
      .withCommands(18)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "Event Metadata Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.onCommand("WIDTHS", onWidths);

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));

  // ---- Bare, no modifiers --------------------------------------------------------------------
  Blaeck.addEventChannel(F("Bare"), F("first,second"));
  Blaeck.onButtonCommand("F_bare_first", onFBareFirst);
  Blaeck.onButtonCommand("F_bare_second", onFBareSecond);

  // ---- Icon -----------------------------------------------------------------------------------
  Blaeck.addEventChannel(F("Icon"), F("ping,pong"))
      .withIcon(F("mdi:pulse"));
  Blaeck.onButtonCommand("F_icon_ping", onFIconPing);
  Blaeck.onButtonCommand("F_icon_pong", onFIconPong);

  // ---- Diagnostic category ---------------------------------------------------------------------
  Blaeck.addEventChannel(F("Diag"), F("check,warn"))
      .diagnostic();
  Blaeck.onButtonCommand("F_diag_check", onFDiagCheck);
  Blaeck.onButtonCommand("F_diag_warn", onFDiagWarn);

  // ---- Disabled by default, entity created but hidden until enabled -----------------------------
  Blaeck.addEventChannel(F("Hidden"), F("trace,dump"))
      .disabledByDefault();
  Blaeck.onButtonCommand("F_hidden_trace", onFHiddenTrace);
  Blaeck.onButtonCommand("F_hidden_dump", onFHiddenDump);

  // ---- Device class, the closed three-value set --------------------------------------------
  Blaeck.addEventChannel(F("Class_button"), F("press_start,press_end"))
      .withDeviceClass(F("button"));
  Blaeck.onButtonCommand("F_button_start", onFButtonStart);
  Blaeck.onButtonCommand("F_button_end", onFButtonEnd);

  // "ring" is the one type withDeviceClass(F("doorbell")) requires the channel to declare.
  Blaeck.addEventChannel(F("Class_doorbell"), F("ring"))
      .withDeviceClass(F("doorbell"));
  Blaeck.onButtonCommand("F_doorbell_ring", onFDoorbellRing);

  Blaeck.addEventChannel(F("Class_motion"), F("detected,cleared"))
      .withDeviceClass(F("motion"));
  Blaeck.onButtonCommand("F_motion_detected", onFMotionDetected);
  Blaeck.onButtonCommand("F_motion_cleared", onFMotionCleared);

  // ---- More than two types, to check the list is carried whole and in order -------------------
  Blaeck.addEventChannel(F("Multi_type"), F("alpha,beta,gamma,delta"));
  Blaeck.onButtonCommand("F_multi_alpha", onFMultiAlpha);
  Blaeck.onButtonCommand("F_multi_beta", onFMultiBeta);
  Blaeck.onButtonCommand("F_multi_gamma", onFMultiGamma);
  Blaeck.onButtonCommand("F_multi_delta", onFMultiDelta);

  PrintWidths();
  Serial.println(F("---- EventMetadataTest: 8 channels, 17 types declared ----"));
}

void loop()
{
  Uptime = millis() / 1000UL;
  Blaeck.tick();
}
