/*
  SignalMetadataTest.ino

  One signal per thing a signal can declare about itself, so a driver can check that each
  one survives the whole way: declared here, carried in the discovery payload, and shown
  by Home Assistant. A gap between any two of those is the finding.

  The other harnesses ask what the library refuses. This one asks whether what it accepts
  arrives intact - the failure it looks for is silent, because a host that cannot use a
  key drops it and says nothing.

  Every signal is named for what it declares, and drive_signal_metadata.py holds what each
  name should produce. Nothing here asserts: the board cannot see what a host made of it.

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

// Values the signals point at. What they hold does not matter - only what they declare.
float temperature = 21.5f;
float humidity = 48.0f;
float power = 1234.5f;
float energy = 42.25f;
float angle = 137.0f;
float pressure = 1013.25f;
float ratio = 0.5f;
long counter = 7;
unsigned long uptime = 0;
int rawAdc = 512;
byte channelA = 1;
byte channelB = 2;
byte channelC = 3;
bool motion = false;
bool doorOpen = true;
bool problem = false;
char mode[16] = "Idle";
char label[24] = "bench";
char note[24] = "plain text";

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

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(24)
      .withCommands(1)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "Signal Metadata Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.onCommand("WIDTHS", onWidths);

  // ---- bare, as the baseline every other line is read against -----------------------------
  Blaeck.addSignal(F("Bare"), &ratio);

  // ---- unit and device class, the pair Home Assistant converts with ------------------------
  Blaeck.addSignal(F("Temp_C"), &temperature)
      .withUnit(F("°C"))
      .withDeviceClass(F("temperature"));

  Blaeck.addSignal(F("Pressure_hPa"), &pressure)
      .withUnit(F("hPa"))
      .withDeviceClass(F("pressure"));

  // A class with no unit: declared on purpose, since it is what leaves a host converting
  // from nothing.
  Blaeck.addSignal(F("Humidity_nounit"), &humidity)
      .withDeviceClass(F("humidity"));

  // ---- state class, one signal per value ---------------------------------------------------
  Blaeck.addSignal(F("SC_measurement"), &power)
      .withUnit(F("W"))
      .withDeviceClass(F("power"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT);

  Blaeck.addSignal(F("SC_total"), &energy)
      .withUnit(F("kWh"))
      .withDeviceClass(F("energy"))
      .withStateClass(BLAECK_STATE_CLASS_TOTAL);

  Blaeck.addSignal(F("SC_total_increasing"), &counter)
      .withStateClass(BLAECK_STATE_CLASS_TOTAL_INCREASING);

  Blaeck.addSignal(F("SC_measurement_angle"), &angle)
      .withUnit(F("°"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT_ANGLE);

  // ---- display precision, including zero, which must not read as "unset" -------------------
  Blaeck.addSignal(F("Prec_0"), &temperature).withDisplayPrecision(0);
  Blaeck.addSignal(F("Prec_3"), &temperature).withDisplayPrecision(3);

  // ---- presentation ------------------------------------------------------------------------
  Blaeck.addSignal(F("Icon_gauge"), &ratio).withIcon(F("mdi:gauge"));
  Blaeck.addSignal(F("Named"), &ratio).withDisplayName(F("A Friendly Name"));

  // A run sharing a prefix, numbered as it is sent rather than built in RAM.
  Blaeck.addSignal(F("Chan"), &channelA).withNameSuffix(1);
  Blaeck.addSignal(F("Chan"), &channelB).withNameSuffix(2);
  Blaeck.addSignal(F("Chan"), &channelC).withNameSuffix(3);

  // ---- how a host files it -----------------------------------------------------------------
  Blaeck.addSignal(F("Diag_uptime"), &uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .diagnostic();

  Blaeck.addSignal(F("Hidden_rawadc"), &rawAdc).disabledByDefault();

  // Republished on every write even when the value has not moved.
  Blaeck.addSignal(F("Forced"), &ratio).forceUpdate();

  // ---- booleans, which become binary sensors and take their own vocabulary -----------------
  Blaeck.addSignal(F("Bool_bare"), &problem);
  Blaeck.addSignal(F("Bool_motion"), &motion).withDeviceClass(F("motion"));
  Blaeck.addSignal(F("Bool_door"), &doorOpen).withDeviceClass(F("door"));

  // ---- text, which can offer a closed set --------------------------------------------------
  Blaeck.addSignal(F("Text_plain"), note);
  Blaeck.addSignal(F("Text_options"), mode)
      .withOptions(F("Idle,Running,Fault"));
  Blaeck.addSignal(F("Text_named"), label)
      .withDisplayName(F("Bench Label"))
      .withIcon(F("mdi:tag"));

  PrintWidths();
  Serial.println(F("---- SignalMetadataTest: 24 signals declared ----"));
}

void loop()
{
  uptime = millis() / 1000UL;
  Blaeck.tick();
}
