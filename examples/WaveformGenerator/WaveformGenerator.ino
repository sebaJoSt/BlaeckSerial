/*
  WaveformGenerator.ino

  A dashboard-friendly demo: BlaeckSerial -> Loggbok (MQTT bridge) -> Home Assistant (MQTT Discovery).

  One fully controllable waveform, driven entirely over MQTT. The device describes what it
  exposes - its signals, controls, messages and events - and Loggbok turns that into Home
  Assistant MQTT Discovery entities, so a dashboard comes from the sketch rather than being
  built by hand.

  Log fast enough to resolve the wave: at the default 1 Hz, a 20 ms interval gives 50 points
  per cycle. Sample slower than that and Output aliases into a shape the device never made.

  Signals, messages and events are three different jobs:
    signal   a value that is sampled and logged       -> Output, Frequency, ...
    message  a line of free text, not logged          -> "running Sine @ 1.00 Hz"
    event    a discrete event from a fixed list       -> idle_warning, resumed

  Controls:
    SET_FREQ    number  0..2 Hz, step 0.01              state: Frequency signal
    SET_AMP     number  0..100, step 0.1                state: Amplitude signal
    SET_OFFSET  number  -100..100, step 0.1             state: its own message channel
    SET_WAVE    select  Sine/Square/Triangle/Sawtooth   state: WaveName signal
    SET_ENABLE  switch  off -> Output = Offset          state: Enabled signal
    SET_LABEL   text    max 32 bytes, config category   state: DeviceLabel signal
    STATUS      button  writes the StatusOnDemand channel

  Signals that describe themselves (the other five declare nothing, and cost nothing):
    Output      measurement, 3 decimals, mdi:sine-wave

  --- HOW A CONTROL GETS ITS VALUE BACK ---

   Home Assistant          MQTT broker           Loggbok         this sketch
          |                     |                   |                 |
          | .../_cmd/SET_FREQ   |      "1.5"        | <SET_FREQ,1.5>  |
  command |-------------------->|------------------>|---------------->|  onSetFreq()
          |                     |                   |                 |    Frequency = 1.5
          |   ../Frequency      |      "1.5"        |  signal frame   |
    state |<--------------------|<------------------|<----------------|  write("Frequency", Frequency)

  Sending a value is a request, not a fact: it can be clamped, rejected, lost on the way, or
  replaced by the device on its next boot. So every control reports its state back, and what a
  dashboard shows is the device's value rather than its own guess. SET_OFFSET makes the same
  trip from a message channel instead of a signal.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial BlaeckSerial;

//---PUBLISHED AS SIGNALS
// addSignal() keeps a pointer to these, so they have to be globals.
float Output = 0.0;
float Frequency = 1.0; // [Hz]
float Amplitude = 1.0;
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL
// Current shape as text. Starts on option 0 of SET_WAVE's list, so the control has a state
// before the first command; RefreshWaveName() keeps it in step from then on.
char WaveName[12] = "Sine"; // fits the longest option, "Triangle"

//---PUBLISHED AS A COMMAND'S OWN STATE
// Not a signal and not logged: SET_OFFSET carries this itself, rendered by OffsetState().
float Offset = 0.0;

//---GENERATOR STATE (never leaves the sketch)
float phase = 0.0f; // normalized phase 0..1
unsigned long lastMicros = 0;
byte waveIndex = 0; // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth; WaveName is what ships

void setup()
{
  Serial.begin(115200);

  BlaeckSerial.begin(&Serial, 6); // signal capacity: exactly the six added below

  BlaeckSerial.DeviceName = "Waveform Generator Demo";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = "1.0";

  // addSignal() returns a handle describing how a host shows the signal. Every call is optional.
  BlaeckSerial.addSignal("Output", &Output)
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .withDisplayPrecision(3)
      .withIcon(F("mdi:sine-wave"));
  BlaeckSerial.addSignal("Frequency", &Frequency);
  BlaeckSerial.addSignal("Amplitude", &Amplitude);
  BlaeckSerial.addSignal("Enabled", &Enabled);
  BlaeckSerial.addSignal("WaveName", WaveName);
  BlaeckSerial.addSignal("DeviceLabel", DeviceLabel);

  if (BlaeckSerial.hasRejectedSignals())
  {
    Serial.print(F("Signals not added: "));
    Serial.println(BlaeckSerial.getRejectedSignalCount());
  }

  BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq)
      .withRange(0.0f, 2.0f, 0.01f)
      .withUnit(F("Hz"))
      .withStateSignal(F("Frequency"));
  BlaeckSerial.onNumberCommand("SET_AMP", onSetAmp)
      .withRange(0.0f, 100.0f, 0.1f)
      .withStateSignal(F("Amplitude"));
  BlaeckSerial.onNumberCommand("SET_OFFSET", onSetOffset)
      .withRange(-100.0f, 100.0f, 0.1f)
      .withOwnState(F("Offset"), OffsetState);
  BlaeckSerial.onSelectCommand("SET_WAVE", onSetWave)
      .withOptions(F("Sine,Square,Triangle,Sawtooth"))
      .withStateSignal(F("WaveName"));
  BlaeckSerial.onSwitchCommand("SET_ENABLE", onSetEnable)
      .withStateSignal(F("Enabled"));
  // Host percent-encodes the value; the device decodes it and enforces the 32-byte max.
  BlaeckSerial.onTextCommand("SET_LABEL", onSetLabel)
      .withMaxLength(sizeof(DeviceLabel) - 1)
      .withStateSignal(F("DeviceLabel"))
      .config();
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  if (BlaeckSerial.hasRejectedCommands())
  {
    Serial.print(F("Commands not registered: "));
    Serial.print(BlaeckSerial.getRejectedCommandCount());
    Serial.print(F(", handler slots: "));
    Serial.println(BLAECK_COMMAND_MAX_HANDLERS_DEFAULT);
  }

  // Declared up-front so the host can announce one text sensor per channel before
  // the first line is written.
  // forceUpdate: the heartbeat repeats the same line, which a host would otherwise collapse
  // into the entry it already holds.
  BlaeckSerial.addMessageChannel("Status").withIcon(F("mdi:pulse")).diagnostic().forceUpdate();
  BlaeckSerial.addMessageChannel("StatusOnDemand").withIcon(F("mdi:message-text")).diagnostic();

  // Each event channel declares up-front the closed set of events it can report.
  // addEventType() does the same one name at a time, for a list built conditionally.
  BlaeckSerial.addEventChannel("Activity", F("idle_warning,resumed"))
      .withIcon(F("mdi:sine-wave"));

  lastMicros = micros();
}

void loop()
{
  UpdateWaveform();
  BlaeckSerial.tick();
  SendStatusMessage();
  CheckActivity();
}

void UpdateWaveform()
{
  unsigned long now = micros();
  float dt = (now - lastMicros) * 1e-6f; // [s]
  lastMicros = now;

  if (!Enabled)
  {
    Output = Offset;
    return;
  }

  // Advance and wrap the normalized phase (0..1).
  phase += Frequency * dt;
  phase -= floorf(phase);

  float w = 0.0f;
  switch (waveIndex)
  {
  case 1: // Square
    w = (phase < 0.5f) ? 1.0f : -1.0f;
    break;
  case 2: // Triangle: -1 at phase 0, +1 at phase 0.5
    w = 1.0f - 4.0f * fabsf(phase - 0.5f);
    break;
  case 3: // Sawtooth: -1 .. +1 ramp
    w = 2.0f * phase - 1.0f;
    break;
  default: // Sine
    w = sinf((float)TWO_PI * phase);
    break;
  }

  Output = Offset + Amplitude * w;
}

void SendStatusMessage()
{
  static unsigned long lastStatusMs = 0;
  static bool first = true;

  if (!first && millis() - lastStatusMs < 10000UL)
    return;

  first = false;
  lastStatusMs = millis();
  WriteStatus("Status");
}

// The same status line on two channels, each driving its own Home Assistant sensor:
// a 10 s heartbeat on "Status", and the STATUS button on "StatusOnDemand".
void WriteStatus(const char *channel)
{
  char hz[10]; // fits "2.00"
  String(Frequency, 2).toCharArray(hz, sizeof(hz));
  char text[80]; // fits "stopped Triangle @ 2.00 Hz"
  snprintf(text, sizeof(text), "%s %s @ %s Hz", Enabled ? "running" : "stopped", WaveName, hz);
  BlaeckSerial.writeMessage(channel, text);
}

// Warns once per idle stretch (>=5s -> "idle_warning"), and only reports "resumed" if a warning
// was raised. Idle means SET_ENABLE is off.
void CheckActivity()
{
  static unsigned long idleSinceMs = 0;
  static bool warned = false;

  if (!Enabled)
  {
    if (idleSinceMs == 0)
      idleSinceMs = millis();

    if (!warned && (millis() - idleSinceMs) >= 5000UL)
    {
      BlaeckSerial.writeEvent("Activity", F("idle_warning"));
      warned = true;
    }
    return;
  }

  if (warned)
    BlaeckSerial.writeEvent("Activity", F("resumed"));

  idleSinceMs = 0;
  warned = false;
}

// The current shape's name, read back from the option list SET_WAVE declared, so those names
// live in flash once instead of being repeated in the sketch. The lookup fails if the command
// was never registered or its metadata is compiled out; the bare index then at least says which
// shape was asked for, where a stale name would say nothing.
void RefreshWaveName()
{
  if (!BlaeckSerial.getSelectOption("SET_WAVE", waveIndex, WaveName, sizeof(WaveName)))
    snprintf(WaveName, sizeof(WaveName), "%u", (unsigned)waveIndex);
}

// Offset as text for its message channel: one decimal (SET_OFFSET's step) and no unit, since a
// Home Assistant number reads its state as a bare numeric.
const char *OffsetState()
{
  static char text[12]; // fits "-100.0"
  String(Offset, 1).toCharArray(text, sizeof(text));
  return text;
}

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  Frequency = (float)atof(params[0]);
  BlaeckSerial.write("Frequency", Frequency);
}

void onSetAmp(const char *command, const char *const *params, byte paramCount)
{
  Amplitude = (float)atof(params[0]);
  BlaeckSerial.write("Amplitude", Amplitude);
}

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  Offset = (float)atof(params[0]);
  // The others write their signal back; this one publishes the command's own state. Pushing
  // is what makes it visible at once - the getter is only read when a host polls the catalog.
  BlaeckSerial.writeCommandState(command);
}

void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  // atoi even though Home Assistant sends the option NAME: the library resolves it against
  // the list declared above and rewrites the parameter to that index before calling this.
  waveIndex = (byte)atoi(params[0]);
  RefreshWaveName();
  BlaeckSerial.write("WaveName", WaveName);
}

void onSetEnable(const char *command, const char *const *params, byte paramCount)
{
  // The library has already rejected anything that is not 0 or 1.
  Enabled = atoi(params[0]) == 1;
  BlaeckSerial.write("Enabled", Enabled);
}

void onSetLabel(const char *command, const char *const *params, byte paramCount)
{
  // params[0] is always present, already percent-decoded and length-checked by the
  // library. An empty one clears the label.
  strncpy(DeviceLabel, params[0], sizeof(DeviceLabel) - 1);
  DeviceLabel[sizeof(DeviceLabel) - 1] = '\0';
  BlaeckSerial.write("DeviceLabel", DeviceLabel);
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  // Updates only on button press, independent of the 10 s "Status" heartbeat.
  WriteStatus("StatusOnDemand");
}
