/*
  Signals.ino

  Numbers, bool and text, sampled and logged on the interval the host asks for.
  A signal keeps a pointer to a variable: update that variable and tick() reads its current
  value when a frame is due. No write() call is needed.

  Metadata describes how a host should show the value; it does not change what is logged.
  Compare these once Loggbok is logging and forwarding to Home Assistant:
    Temperature [C]  a measurement with a unit, one decimal, and the display name Temperature
    TemperaturePlain the same temperature without metadata: a plain number on the dashboard
    DoorOpen         a bool shown as a door rather than a number
    Mode             text with a declared set of possible values
    Uptime           information about the device, filed under Diagnostic
    Sine_1..Sine_5    an array registered in a loop, with numbered names kept in flash

  No sensor hardware is needed. The values below simulate a room that cools while a door
  is open and warms when it closes, alongside five phase-shifted sine waves.

  With a serial monitor instead of a host, send <BLAECK.ACTIVATE,1000> to request one binary
  data frame per second, and <BLAECK.DEACTIVATE> to stop. The frames are not readable text.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// The library keeps pointers, so the variables must outlive setup().
float Temperature = 21.5f;
bool DoorOpen = false;
char Mode[16] = "warming";
unsigned long Uptime = 0;
constexpr byte SINE_COUNT = 5;
float Sine[SINE_COUNT];

void setup()
{
  Serial.begin(115200);
  Blaeck.begin(&Serial).withSignals(5 + SINE_COUNT);

  Blaeck.DeviceName = "Signals";
  Blaeck.DeviceFWVersion = "1.0";

  // The display name changes the dashboard label, not the logged column name.
  // The unit is UTF-8 degrees Celsius; the escape keeps this source file ASCII.
  Blaeck.addSignal(F("Temperature [C]"), &Temperature)
      .withDisplayName(F("Temperature"))
      .withUnit(F("\xC2\xB0" "C"))
      .withDeviceClass(F("temperature"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .withDisplayPrecision(1);

  // Both signals read the same variable and log identical values. Only their metadata differs.
  Blaeck.addSignal(F("TemperaturePlain"), &Temperature);

  // A bool becomes a binary sensor. The device class gives true/false a meaning.
  Blaeck.addSignal(F("DoorOpen"), &DoorOpen)
      .withDeviceClass(F("door"));

  // Text takes the buffer itself, not &Mode. Every reported value must be in the options.
  Blaeck.addSignal(F("Mode"), Mode)
      .withDeviceClass(F("enum"))
      .withOptions(F("warming,cooling"))
      .withIcon(F("mdi:state-machine"));

  // Diagnostic describes its dashboard category; this is still a logged signal.
  Blaeck.addSignal(F("Uptime"), &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .diagnostic();

  // The suffix gives each array element a name without building or copying a string.
  for (byte i = 0; i < SINE_COUNT; i++)
    Blaeck.addSignal(F("Sine_"), &Sine[i]).withNameSuffix(i + 1);

  Blaeck.printRejections(&Serial);
}

void loop()
{
  UpdateSignals();
  Blaeck.tick();
}

void UpdateSignals()
{
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();
  Uptime = now / 1000;

  const float phase = now * 0.00005f;
  for (byte i = 0; i < SINE_COUNT; i++)
    Sine[i] = sin(phase + i * (TWO_PI / SINE_COUNT));

  if (now - lastUpdate < 1000UL)
    return;
  lastUpdate = now;

  DoorOpen = (Uptime / 10) % 2 != 0;
  Temperature += ((DoorOpen ? 18.0f : 22.0f) - Temperature) * 0.25f;
  strcpy(Mode, DoorOpen ? "cooling" : "warming");
}
