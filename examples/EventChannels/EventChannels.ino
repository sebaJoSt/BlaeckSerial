/*
  EventChannels.ino

  Every shape an event channel can declare, so a host's rendering can be checked side by side.
  An event is an occurrence, not a state: it carries no value and nothing to switch off again,
  which is what separates it from a bool signal.

  Four channels is what a Mega holds by default (BLAECK_EVENT_MAX_CHANNELS_DEFAULT), so this
  sketch uses exactly four. Their ten types fit the shared pool of sixteen.

  What to look for once it is logging:
    Doorbell    device class doorbell, which requires a "ring" type - see the note below
    Button      device class button, using Home Assistant's own names for press and hold
    Motion      device class motion, a pair of types rather than one
    System      no device class, diagnostic, and one type added conditionally

  An event entity's state is the timestamp of the last occurrence; which one it was arrives as
  an "event_type" attribute. So the more-info dialog is where to watch these, not the state.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial BlaeckSerial;

// Set true to declare a type only some builds have, to show addEventType() appending to a
// channel whose list is not fully known at compile time.
#define HAS_OVERHEAT_SENSOR true

// addSignal() keeps a pointer to these, so they have to be globals. A logging session needs
// something to log; these also let the event count be checked against what arrived.
unsigned long Uptime = 0;
unsigned long EventCount = 0;

void setup()
{
  Serial.begin(115200);

  BlaeckSerial.begin(&Serial, 2);

  BlaeckSerial.DeviceName = "Event Channels Demo";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = "1.0";

  BlaeckSerial.addSignal("Uptime", &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT);
  BlaeckSerial.addSignal("EventCount", &EventCount)
      .withStateClass(BLAECK_STATE_CLASS_TOTAL_INCREASING);

  // A doorbell must be able to report "ring". Home Assistant warns about a doorbell without it
  // today and stops accepting one in 2027.4.
  BlaeckSerial.addEventChannel("Doorbell", F("ring"))
      .withIcon(F("mdi:doorbell"))
      .withDeviceClass(F("doorbell"));

  // Home Assistant publishes standard names for a button, and says none of them are required:
  // declare only the interactions the hardware can actually produce. These four are a press
  // and a hold, each with a start and an end.
  BlaeckSerial.addEventChannel("Button",
                               F("press_start,press_end,long_press_start,long_press_end"))
      .withIcon(F("mdi:gesture-tap-button"))
      .withDeviceClass(F("button"));

  // A device class does not fix the type names - only doorbell requires one. A motion channel
  // reports whatever it declares.
  BlaeckSerial.addEventChannel("Motion", F("motion_detected,motion_cleared"))
      .withDeviceClass(F("motion"));

  // No device class: a plain event channel, filed under Diagnostic and switched off until
  // someone enables it.
  BlaeckSerial.addEventChannel("System", F("started,config_changed"))
      .withIcon(F("mdi:cog"))
      .diagnostic()
      .disabledByDefault();

  // addEventType() appends to a channel already declared, for a list that is not fully known
  // at compile time. It is the only way to build one conditionally: the types passed to
  // addEventChannel() are a flash literal, so they cannot be assembled at runtime.
  if (HAS_OVERHEAT_SENSOR)
    BlaeckSerial.addEventType("System", F("overheated"));

  if (BlaeckSerial.hasRejectedSignals())
  {
    Serial.print(F("Signals not added: "));
    Serial.println(BlaeckSerial.getRejectedSignalCount());
  }
  if (BlaeckSerial.hasRejectedEventChannels())
  {
    Serial.print(F("Event channels or types not declared: "));
    Serial.println(BlaeckSerial.getRejectedEventChannelCount());
  }

  BlaeckSerial.writeEvent("System", F("started"));
  EventCount++;
}

void loop()
{
  Uptime = millis() / 1000;
  BlaeckSerial.tick();
  FireEvents();
}

// One occurrence every five seconds, walking every declared type so each is exercised.
void FireEvents()
{
  static unsigned long lastFired = 0;
  static byte step = 0;

  if (millis() - lastFired < 5000)
    return;
  lastFired = millis();

  switch (step)
  {
  case 0: BlaeckSerial.writeEvent("Doorbell", F("ring")); break;
  case 1: BlaeckSerial.writeEvent("Button", F("press_start")); break;
  case 2: BlaeckSerial.writeEvent("Button", F("press_end")); break;
  case 3: BlaeckSerial.writeEvent("Button", F("long_press_start")); break;
  case 4: BlaeckSerial.writeEvent("Button", F("long_press_end")); break;
  case 5: BlaeckSerial.writeEvent("Motion", F("motion_detected")); break;
  case 6: BlaeckSerial.writeEvent("Motion", F("motion_cleared")); break;
  case 7: BlaeckSerial.writeEvent("System", F("config_changed")); break;
  case 8:
    if (HAS_OVERHEAT_SENSOR)
      BlaeckSerial.writeEvent("System", F("overheated"));
    break;
  }

  // A type the channel never declared is dropped by the device: writeEvent() resolves the name
  // against the 0x80 catalog and sends nothing when it does not match. Uncomment to watch it
  // produce no event at all, rather than an event a host has to reject.
  // BlaeckSerial.writeEvent("Motion", F("motion_maybe"));

  EventCount++;
  step = (step + 1) % 9;
}
