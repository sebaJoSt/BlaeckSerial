/*
  CommandTest.ino

  Every way a command can be declared, crossed with the values a host might send it -
  including the ones it must refuse.

    P   plain     onCommand(), parses its own parameters
    N   number    onNumberCommand(), bounded by withRange()
    S   switch    onSwitchCommand(), 0 or 1
    L   select    onSelectCommand(), one of a named list
    B   button    onButtonCommand(), no value at all
    T   text      onTextCommand(), bounded by withMaxLength()

  A typed command is checked before its handler runs, so a refused value is observable
  only as an absence: the handler does not fire and the variable does not move. Every
  handler here prints one line and nothing else does, so silence is the assertion.

  What to look for:
    Serial   PASS/FAIL at startup, then one CMD line per accepted command
    Broker   the controls a host builds from the metadata, which the sketch cannot see

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// ---- what the commands write to -------------------------------------------------------------
int nInt = 0;
float nFloat = 0.0f;
byte nLevel = 0;
bool sEnabled = false;
bool sFlag = false;
byte lIndex = 0;
char lName[16] = "Sine";
char tLabel[32] = "unnamed";
char tSecret[16] = "";
long pRepeats = 0;

// Signals, so a logging session has something to log and withStateFromSignal() has a target.
unsigned long Uptime = 0;

// Every accepted command bumps this. A refused one must leave it alone.
unsigned long Accepted = 0;

int checks = 0;
int failures = 0;

void Check(const __FlashStringHelper *what, bool ok)
{
  checks++;
  if (!ok) failures++;
  Serial.print(ok ? F("PASS  ") : F("FAIL  "));
  Serial.println(what);
}

// One line per accepted command, in a shape a driver can parse: CMD <name> <value>
void Accept(const char *command, const char *value)
{
  Accepted++;
  Serial.print(F("CMD "));
  Serial.print(command);
  Serial.print(' ');
  Serial.println(value);
}

// ---- handlers -------------------------------------------------------------------------------

// Plain: nothing was checked, so everything is this handler's problem.
void onPrint(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount < 2 || params[0][0] == '\0')
  {
    Serial.println(F("CMD P_print refused-by-handler"));
    return;
  }
  pRepeats = atol(params[1]);
  Accept(command, params[0]);
}

void onSetInt(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  nInt = atoi(params[0]);
  Accept(command, params[0]);
}

void onSetFloat(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  nFloat = atof(params[0]);
  Accept(command, params[0]);
}

void onSetLevel(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  nLevel = (byte)atoi(params[0]);
  Accept(command, params[0]);
}

// Declared with a range the library must refuse, so this should be unreachable by value.
void onSetBadRange(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  Accept(command, params[0]);
}

void onSetEnabled(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  sEnabled = atoi(params[0]) == 1;
  Accept(command, params[0]);
}

void onSetFlag(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  sFlag = atoi(params[0]) == 1;
  Accept(command, params[0]);
}

// A select hands over the position, whichever of the two a host sent - so the name has to be
// read back if that is what the state channel carries.
void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  byte index = (byte)atoi(params[0]);
  if (!Blaeck.getSelectOptionNameAt(command, index, lName, sizeof(lName)))
  {
    Serial.println(F("CMD L_wave readback-failed"));
    return;
  }
  Accept(command, lName);
}

void onSetRange(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  lIndex = (byte)atoi(params[0]);
  Accept(command, params[0]);
}

void onPing(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  char text[40];
  snprintf(text, sizeof(text), "alive, %lu accepted", Accepted + 1);
  Blaeck.writeState(F("Status"), text);
  Accept(command, "pressed");
}

void onReboot(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;
  Accept(command, "pressed");
}

void onSetLabel(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  strncpy(tLabel, params[0], sizeof(tLabel) - 1);
  tLabel[sizeof(tLabel) - 1] = '\0';
  Accept(command, params[0]);
}

void onSetSecret(const char *command, const char *const *params, byte paramCount)
{
  (void)paramCount;
  strncpy(tSecret, params[0], sizeof(tSecret) - 1);
  tSecret[sizeof(tSecret) - 1] = '\0';
  // The value is masked in a host's input box, not on the wire and not here.
  Accept(command, params[0]);
}

void setup()
{
  Serial.begin(115200);

  // No withDebugStream(&Serial) here, deliberately. The catalog writer warns about a number
  // with no range while the 0xA0 frame is open, so pointing the debug stream at the stream the
  // frames go out on writes the warning into the middle of one. Loggbok then loses every
  // control after that point: with it on, N_badrange, both switches and the L_wave select
  // never reach the broker at all, and L_range arrives named "_range".
  Blaeck.begin(&Serial)
      .withSignals(3)
      .withCommands(13)
      // Every withOwnState() below claims a channel of its own, on top of the two declared
      // outright - a command's state is a state channel like any other.
      .withStateChannels(8)
      ;

  Blaeck.DeviceName = "Command Test";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));
  Blaeck.addSignal(F("Level"), &nLevel);
  Blaeck.addSignal(F("Flag"), &sFlag);

  Blaeck.addStateChannel(F("Status"), BlaeckText);
  Blaeck.addStateChannel(F("Label"), tLabel);

  // ---- P: plain, so serial only and unchecked ------------------------------------------
  Blaeck.onCommand("P_print", onPrint);

  // ---- N: bounded numbers ---------------------------------------------------------------
  Blaeck.onNumberCommand("N_int", onSetInt)
      .withRange(0.0f, 100.0f, 1.0f)
      .withOwnState(F("N_int_state"), &nInt);

  Blaeck.onNumberCommand("N_float", onSetFloat)
      .withRange(-5.0f, 5.0f, 0.25f)
      .withUnit(F("V"))
      .withMode(BLAECK_NUMBER_MODE_BOX)
      .withOwnState(F("N_float_state"), &nFloat);

  // The only number pointing at a logged signal rather than its own state channel.
  Blaeck.onNumberCommand("N_level", onSetLevel)
      .withRange(0.0f, 255.0f, 1.0f)
      .withMode(BLAECK_NUMBER_MODE_SLIDER)
      .withStateFromSignal(F("Level"));

  // Refused at declaration: the library keeps the command and drops the range.
  Blaeck.onNumberCommand("N_badrange", onSetBadRange)
      .withRange(10.0f, 5.0f, 1.0f);

  // ---- S: switches ----------------------------------------------------------------------
  Blaeck.onSwitchCommand("S_enabled", onSetEnabled)
      .withOwnState(F("S_enabled_state"), &sEnabled);

  Blaeck.onSwitchCommand("S_flag", onSetFlag)
      .withStateFromSignal(F("Flag"));

  // ---- L: selects -----------------------------------------------------------------------
  Blaeck.onSelectCommand("L_wave", onSetWave)
      .withOptions(F("Sine,Square,Triangle,Sawtooth"))
      .withOwnState(F("L_wave_state"), lName);

  Blaeck.onSelectCommand("L_range", onSetRange)
      .withOptions(F("1V,10V,100V"))
      .withOwnState(F("L_range_state"), &lIndex);

  // ---- B: buttons, which carry no value --------------------------------------------------
  Blaeck.onButtonCommand("B_ping", onPing);

  Blaeck.onButtonCommand("B_reboot", onReboot)
      .withDeviceClass(F("restart"))
      .diagnostic();

  // ---- T: text ---------------------------------------------------------------------------
  Blaeck.onTextCommand("T_label", onSetLabel)
      .withMaxLength(sizeof(tLabel) - 1)
      .withOwnState(F("T_label_state"), tLabel);

  Blaeck.onTextCommand("T_secret", onSetSecret)
      .withMaxLength(sizeof(tSecret) - 1)
      .withMode(BLAECK_TEXT_MODE_PASSWORD);

  RunLocalChecks();
}

// Everything the sketch can prove without a host. The rest needs a broker or a driver
// sending commands, because a refused value is an absence and absences do not print.
void RunLocalChecks()
{
  Serial.println();
  Serial.println(F("---- CommandTest ----"));

  Check(F("every command registered"), !Blaeck.hasRejectedCommands());
  if (Blaeck.hasRejectedCommands())
  {
    Serial.print(F("      dropped: "));
    Serial.println(Blaeck.getRejectedCommandCount());
  }

  Check(F("every state channel registered"), !Blaeck.hasRejectedStateChannels());
  if (Blaeck.hasRejectedStateChannels())
  {
    Serial.print(F("      dropped: "));
    Serial.println(Blaeck.getRejectedStateChannelCount());
  }

  Check(F("every signal registered"), !Blaeck.hasRejectedSignals());
  Check(F("nothing accepted before a host sends anything"), Accepted == 0);
  Check(F("defaults intact: nInt"), nInt == 0);
  Check(F("defaults intact: sEnabled"), sEnabled == false);
  Check(F("defaults intact: lName"), strcmp(lName, "Sine") == 0);
  Check(F("defaults intact: tLabel"), strcmp(tLabel, "unnamed") == 0);

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
