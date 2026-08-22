/*
  EventTest.ino

  Every way an event channel can be declared and every way an event can fail to be sent.

  An event is fire and forget: a channel or type that was never declared is dropped in
  silence, and an event names both by position rather than by name, so the catalog has
  to reach the host first. Nothing about a lost event is visible from the sketch, which
  is what makes a driver on the other end of the port necessary.

  What to look for:
    Serial   PASS/FAIL at startup, then one 0x85 frame per accepted event
    Broker   whether a host shows an event whose catalog it never received

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

unsigned long Uptime = 0;
bool HasOptionalHardware = true;

int checks = 0;
int failures = 0;

void Check(const __FlashStringHelper *what, bool ok)
{
  checks++;
  if (!ok) failures++;
  Serial.print(ok ? F("PASS  ") : F("FAIL  "));
  Serial.println(what);
}

void setup()
{
  Serial.begin(115200);

  // Both streams on Serial on purpose: anything the library prints while a frame is open
  // would land inside it, and this is where that would show.
  Blaeck.begin(&Serial)
      .withSignals(1)
      .withEventChannels(4)
      .withEventTypes(10)
      .withCommands(6)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "Event Test";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));

  // Plain, and the channel the driver sends most of its events to.
  Blaeck.addEventChannel(F("Activity"), F("idle,resumed,stopped"));

  Blaeck.addEventChannel(F("Faults"), F("brownout,watchdog"))
      .withIcon(F("mdi:alert"))
      .diagnostic();

  Blaeck.addEventChannel(F("Doorbell"), F("ring"))
      .withDeviceClass(F("doorbell"));

  // Declared but switched off until a host enables it.
  Blaeck.addEventChannel(F("Rare"), F("seen"))
      .disabledByDefault();

  // Fixed triggers rather than one command taking a channel and a type: writeEvent()'s type
  // has to be a flash literal, so there is nothing a runtime string could be passed to.
  Blaeck.onCommand("E_ok", onFireOk);
  Blaeck.onCommand("E_appended", onFireAppended);
  Blaeck.onCommand("E_badtype", onFireBadType);
  Blaeck.onCommand("E_badchannel", onFireBadChannel);
  Blaeck.onCommand("E_disabled", onFireDisabled);
  Blaeck.onCommand("E_case", onFireCase);

  RunLocalChecks();
}

// Each fires exactly one event and says so, so a missing frame is the only difference
// between an event that went out and one that was dropped.
void Fired(const char *command)
{
  Serial.print(F("FIRED "));
  Serial.println(command);
}

void onFireOk(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Activity"), F("idle"));
  Fired(c);
}

// A type that only exists because addEventType() appended it at startup.
void onFireAppended(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Faults"), F("overheat"));
  Fired(c);
}

void onFireBadType(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Activity"), F("exploded"));
  Fired(c);
}

void onFireBadChannel(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Nope"), F("idle"));
  Fired(c);
}

// Declared, but switched off until a host enables it. Whether that stops the frame or
// only how a host files it is the question.
void onFireDisabled(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Rare"), F("seen"));
  Fired(c);
}

// A select matches its options case-insensitively. Whether an event type does too is
// not written down anywhere.
void onFireCase(const char *c, const char *const *p, byte n)
{
  (void)p; (void)n;
  Blaeck.writeEvent(F("Activity"), F("IDLE"));
  Fired(c);
}

// What the sketch can prove on its own. Everything about a dropped event is invisible
// here - that half is the driver's.
void RunLocalChecks()
{
  Serial.println();
  Serial.println(F("---- EventTest ----"));

  // Appending is how a board declares a type it only has when the hardware is fitted.
  bool appended = false;
  if (HasOptionalHardware)
    appended = Blaeck.addEventType(F("Faults"), F("overheat"));
  Check(F("addEventType appends to a declared channel"), appended);

  Check(F("a duplicate type is refused"), !Blaeck.addEventType(F("Faults"), F("overheat")));
  Check(F("a blank type is refused"), !Blaeck.addEventType(F("Faults"), F("")));
  Check(F("a type on an unknown channel is refused"), !Blaeck.addEventType(F("Nope"), F("x")));

  // Two of the three refusals are counted, not three: a duplicate is already there, so it
  // is ignored rather than dropped. The count covers channels and types together despite
  // its name, so a number above two would be a channel that failed to register - a table
  // sized too small - and there is no accessor that separates the two.
  Check(F("both dropped types are counted, and nothing else"), Blaeck.getRejectedEventChannelCount() == 2);
  Check(F("the flag is set, by those two"), Blaeck.hasRejectedEventChannels());

  Serial.print(F("---- "));
  Serial.print(checks - failures);
  Serial.print('/');
  Serial.print(checks);
  Serial.println(F(" passed ----"));
  Serial.println();
}

void loop()
{
  Uptime = millis() / 1000UL;
  Blaeck.tick();
}
