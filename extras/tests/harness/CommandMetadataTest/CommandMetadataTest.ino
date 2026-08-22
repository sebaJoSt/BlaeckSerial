/*
  CommandMetadataTest.ino

  SignalMetadataTest asked whether what a signal declares about itself survives to Home
  Assistant. This asks the same question of a command: one control per thing a command can
  declare, so a driver can check that each one arrives - min, max, step, unit, mode, device
  class, options, press payload, category, visibility, state binding, all of it.

  CommandTest already asks the other question about commands - does the library refuse what
  it should. Nobody had asked whether a control a host builds actually looks the way its
  declaration says it should, control by control, the way SignalMetadataTest did for
  signals. This is that harness.

  Every command is named for what it declares, and drive_command_metadata.py holds what each
  name should produce. A single handler per kind is enough - what a command does once
  accepted is CommandTest's question, not this one's - so this sketch checks in at startup
  and otherwise gets out of the way.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"
#if defined(__AVR__)
#include <avr/wdt.h>
#endif

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

// ---- signals, so withStateFromSignal() has something that already exists -------------------
unsigned long Uptime = 0;
char Mode[16] = "Idle";

// ---- what withOwnState() commands write to --------------------------------------------------
float nStateOwn = 0.0f;
bool sState = false;
byte lStateIdx = 0;

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

// ---- one handler per kind: accept, publish state back if it owns one, and say so -----------
// What a value does once accepted is CommandTest's question. This sketch only has to exist
// long enough for a host to build a control from it and, for the few that bind a state, to
// prove the binding is live rather than declared and forgotten.

void onNumber(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  nStateOwn = atof(params[0]);
  Blaeck.writeCommandState(command);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onSwitch(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  sState = atoi(params[0]) == 1;
  Blaeck.writeCommandState(command);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onSelect(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  lStateIdx = (byte)atoi(params[0]);
  Blaeck.writeCommandState(command);
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onButton(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Serial.print(F("CMD "));
  Serial.println(command);
}

void onText(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  (void)params;
  Serial.print(F("CMD "));
  Serial.println(command);
}

// The DTR-line reset every driver here relies on doesn't reach the Giga - it needs the port
// open, not a real toggle. A command lets a driver ask for a restart the same way regardless
// of board or transport (serial today, but the ask is the same over the MQTT bridge), and
// proves the round trip a physical reset never could: the device answering, then going away,
// then coming back and re-publishing its own discovery - unattended.
void onReboot(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Serial.print(F("CMD "));
  Serial.println(command);
  Serial.flush();
  delay(50); // let the ack leave the wire before the board does
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_GIGA)
  NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32)
  ESP.restart();
#elif defined(__AVR__)
  wdt_enable(WDTO_15MS);
  while (true) {}
#else
  Serial.println(F("Reboot: no reset path known for this board"));
#endif
}

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(2)
      .withCommands(29)
      .withStateChannels(3)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "Command Metadata Test";
  Blaeck.DeviceHWVersion = HARNESS_BOARD;
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));
  Blaeck.addSignal(F("Mode"), Mode);

  Blaeck.onCommand("WIDTHS", onWidths);

  // ---- N: numbers, bare first as the baseline every other line is read against -------------
  Blaeck.onNumberCommand("N_bare", onNumber)
      .withRange(0.0f, 100.0f, 1.0f);

  Blaeck.onNumberCommand("N_unit", onNumber)
      .withRange(-20.0f, 40.0f, 0.5f)
      .withUnit(F("V"));

  // Step 0 means "say nothing", not "zero resolution" - the key must not appear at all.
  Blaeck.onNumberCommand("N_step_unset", onNumber)
      .withRange(0.0f, 10.0f, 0.0f);

  Blaeck.onNumberCommand("N_mode_box", onNumber)
      .withRange(0.0f, 10.0f, 1.0f)
      .withMode(BLAECK_NUMBER_MODE_BOX);

  Blaeck.onNumberCommand("N_mode_slider", onNumber)
      .withRange(0.0f, 10.0f, 1.0f)
      .withMode(BLAECK_NUMBER_MODE_SLIDER);

  Blaeck.onNumberCommand("N_deviceclass", onNumber)
      .withRange(-40.0f, 80.0f, 0.1f)
      .withDeviceClass(F("temperature"));

  // A step Home Assistant's own number schema refuses (its floor is 0.001): declared and
  // published exactly as asked, same as every other step here - the question is what
  // happens after, where this sketch cannot see. A well-formed declaration that never
  // becomes a working control is a different failure than a missing key, and this is
  // the command-side match to Class_wins_over_options in SignalMetadataTest.
  Blaeck.onNumberCommand("N_tiny_step", onNumber)
      .withRange(0.0f, 1.0f, 0.0001f);

  Blaeck.onNumberCommand("N_state_own", onNumber)
      .withRange(0.0f, 5.0f, 0.1f)
      .withOwnState(F("N_state_own_state"), &nStateOwn);

  Blaeck.onNumberCommand("N_state_signal", onNumber)
      .withRange(0.0f, 1000.0f, 1.0f)
      .withStateFromSignal(F("Uptime"));

  // ---- S: switches ---------------------------------------------------------------------------
  Blaeck.onSwitchCommand("S_bare", onSwitch);

  Blaeck.onSwitchCommand("S_deviceclass", onSwitch)
      .withDeviceClass(F("outlet"));

  Blaeck.onSwitchCommand("S_state", onSwitch)
      .withOwnState(F("S_state_state"), &sState);

  // ---- L: selects, which take their state two different ways --------------------------------
  Blaeck.onSelectCommand("L_bare", onSelect)
      .withOptions(F("Sine,Square,Triangle,Sawtooth"));

  // Text state already names the option, so a host matches it directly - no value_template.
  Blaeck.onSelectCommand("L_state_text", onSelect)
      .withOptions(F("Idle,Running,Fault"))
      .withStateFromSignal(F("Mode"));

  // A numeric state is an index into the list, not a name - a host has to map it back, and
  // that mapping is what value_template is for.
  Blaeck.onSelectCommand("L_state_numeric", onSelect)
      .withOptions(F("1V,10V,100V"))
      .withOwnState(F("L_state_numeric_state"), &lStateIdx);

  // ---- B: buttons, which take a value only if one is declared --------------------------------
  Blaeck.onButtonCommand("B_bare", onButton);

  Blaeck.onButtonCommand("B_press_payload", onButton)
      .withPressPayload(F("PING"));

  Blaeck.onButtonCommand("B_deviceclass", onButton)
      .withDeviceClass(F("restart"));

  // ---- T: text --------------------------------------------------------------------------------
  Blaeck.onTextCommand("T_bare", onText)
      .withMaxLength(32);

  Blaeck.onTextCommand("T_password", onText)
      .withMaxLength(16)
      .withMode(BLAECK_TEXT_MODE_PASSWORD);

  // Longer than Home Assistant's own 255-character ceiling for the text platform's max. Passed
  // on as declared everywhere else, but this one key is left out instead of shrunk: a smaller
  // number here would be a limit the device never asked for, standing in for the one it did.
  Blaeck.onTextCommand("T_maxlen_toobig", onText)
      .withMaxLength(300);

  // No withMaxLength() at all - reachable, unlike a number with no range or a select with no
  // options, because onTextCommand() returns a full handle straight away rather than gating on
  // it first. The max key should be as absent as it is for T_bare's siblings that never call it.
  Blaeck.onTextCommand("T_no_maxlen", onText);

  // ---- how a host files it, isolated on their own controls so nothing else masks them --------
  Blaeck.onSwitchCommand("Named", onSwitch)
      .withDisplayName(F("A Friendly Command"));

  Blaeck.onSwitchCommand("Iconed", onSwitch)
      .withIcon(F("mdi:tune"));

  Blaeck.onSwitchCommand("Config_category", onSwitch)
      .config();

  Blaeck.onSwitchCommand("Diag_category", onSwitch)
      .diagnostic();

  Blaeck.onSwitchCommand("Hidden", onSwitch)
      .disabledByDefault();

  Blaeck.onButtonCommand("Reboot", onReboot)
      .withDeviceClass(F("restart"))
      .diagnostic();

  PrintWidths();
  Serial.println(F("---- CommandMetadataTest: 28 commands declared ----"));
}

void loop()
{
  Uptime = millis() / 1000UL;
  Blaeck.tick();
}
