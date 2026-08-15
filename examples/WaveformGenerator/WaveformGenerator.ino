/*
  WaveformGenerator.ino

  A dashboard-friendly demo: BlaeckSerial -> Loggbok (MQTT bridge) -> Home Assistant (MQTT Discovery).

  One fully controllable waveform, driven entirely over MQTT. The device describes what it
  exposes - its signals, controls, state channels and events - and Loggbok turns that into Home
  Assistant MQTT Discovery entities, so a dashboard comes from the sketch rather than being
  built by hand.

  Log fast enough to resolve the wave: at the default 1 Hz, a 20 ms interval gives 50 points
  per cycle. Sample slower than that and Output aliases into a waveform the device never made.

  Signals, state channels and events are three different jobs:
    signal   a value that is sampled and logged       -> Output, Frequency, Uptime
    state    what a control is set to, not logged     -> Amplitude, Wave, "running Sine @ 1.00 Hz"
    event    a discrete event from a fixed list       -> idle_warning, resumed

  Controls:
    SET_FREQ    number  0..2 Hz, step 0.01              state: Frequency signal
    SET_AMP     number  0..100, step 0.1                state: its own state channel
    SET_OFFSET  number  -100..100, step 0.1             state: its own state channel
    SET_WAVE    select  Sine/Square/Triangle/Sawtooth   state: its own state channel
    SET_ENABLE  switch  off -> Output = Offset          state: its own state channel
    SET_LABEL   text    max 32 bytes, config category   state: its own state channel
    STATUS      button  writes the StatusOnDemand channel

  Signals that describe themselves (Frequency declares nothing, and costs nothing):
    Output      measurement, 3 decimals, mdi:sine-wave
    Uptime      seconds since boot, which a host may show in minutes or hours instead

  --- HOW A CONTROL GETS ITS VALUE BACK ---

  Sending a value is a request, not a fact: it can be clamped, rejected, lost on the way, or
  replaced by the device on its next boot. So every control reports its state back, and what a
  dashboard shows is the device's value rather than its own guess.

   Home Assistant          MQTT broker           Loggbok         this sketch
          |                     |                   |                 |
          | .../_cmd/SET_AMP    |      "40"         | <SET_AMP,40>    |
  command |-------------------->|------------------>|---------------->|  onSetAmp()
          |                     |                   |                 |    Amplitude = 40
          | ../_state/Amplitude |      "40.00"      |  0x95 State     |
    state |<--------------------|<------------------|<----------------|  writeCommandState(command)

  That is the usual shape: the command owns a state channel and reports on it. SET_FREQ points
  at the Frequency signal instead, so its value is logged - one control that way, so both are
  shown in the example here.

  

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

//---PUBLISHED AS SIGNALS, AND SO LOGGED
// addSignal() keeps a pointer to these, so they have to be globals. What the waveform is
// doing (Output) and the one setting worth a history (Frequency).
float Output = 0.0;
float Frequency = 1.0; // [Hz]

//---PUBLISHED AS THEIR COMMANDS' OWN STATE, AND SO NOT LOGGED
// A control reports what it is set to, which is not a measurement: it changes when someone
// changes it and is worth nothing between times. withOwnState() points the command straight
// at the variable, so the value travels typed and the sketch never formats it.
float Amplitude = 1.0;
float Offset = 0.0;
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL

//---PUBLISHED AS A SIGNAL, BUT ABOUT THE BOARD RATHER THAN THE WAVE
unsigned long Uptime = 0; // [s]

//---GENERATOR STATE (never leaves the sketch)
float phase = 0.0f; // normalized phase 0..1
unsigned long lastMicros = 0;
// The wave, as an index: what onSetWave() is handed and what UpdateWaveform() switches on.
byte waveIndex = 0; // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth

void setup()
{
  Serial.begin(115200);

  // Sizing every table the sketch fills, so nothing is left to a default: three signals, seven
  // commands, seven state channels (two declared here, five by the commands' withOwnState),
  // three event channels. Every one of these calls is optional.
  Blaeck.begin(&Serial)
      .withSignals(3)
      .withCommands(7)
      .withStateChannels(7)
      .withEventChannels(3);

  Blaeck.DeviceName = "Waveform Generator Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  // addSignal() returns a handle describing how a host shows the signal. Every call is optional.
  Blaeck.addSignal(F("Output"), &Output)
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .withDisplayPrecision(3)
      .withIcon(F("mdi:sine-wave"));
  Blaeck.addSignal(F("Frequency"), &Frequency);

  // Describes the board rather than the waveform, so it is filed as diagnostic. The device
  // class is what lets a host offer minutes or hours - without one the unit is only a label.
  Blaeck.addSignal(F("Uptime"), &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .diagnostic();

  Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
      .withRange(0.0f, 2.0f, 0.01f)
      .withUnit(F("Hz"))
      .withStateSignal(F("Frequency"));
  Blaeck.onNumberCommand("SET_AMP", onSetAmp)
      .withRange(0.0f, 100.0f, 0.1f)
      .withOwnState(F("Amplitude"), &Amplitude);
  Blaeck.onNumberCommand("SET_OFFSET", onSetOffset)
      .withRange(-100.0f, 100.0f, 0.1f)
      .withOwnState(F("Offset"), &Offset);
  Blaeck.onSelectCommand("SET_WAVE", onSetWave)
      .withOptions(F("Sine,Square,Triangle,Sawtooth"))
      .withOwnState(F("Wave"), &waveIndex);
  Blaeck.onSwitchCommand("SET_ENABLE", onSetEnable)
      .withOwnState(F("Enabled"), &Enabled);
  // Host percent-encodes the value; the device decodes it and enforces the 32-byte max.
  Blaeck.onTextCommand("SET_LABEL", onSetLabel)
      .withMaxLength(sizeof(DeviceLabel) - 1)
      .withOwnState(F("DeviceLabel"), DeviceLabel)
      .config();
  Blaeck.onButtonCommand("STATUS", onStatus);

  Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:pulse")).diagnostic();
  Blaeck.addStateChannel(F("StatusOnDemand")).withIcon(F("mdi:message-text")).diagnostic();

  // Each event channel declares up-front the closed set of events it can report.
  // addEventType() does the same one name at a time, for a list built conditionally.
  Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"))
      .withIcon(F("mdi:sine-wave"));

  // Everything is declared: one summary of anything a table had no room for, naming the
  // begin() call that would have kept it. Prints nothing when all of it fitted.
  Blaeck.printRejections(&Serial);

  lastMicros = micros();
}

void loop()
{
  Uptime = millis() / 1000;
  UpdateWaveform();
  Blaeck.tick();
  StatusEvery10s();
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

void StatusEvery10s()
{
  static unsigned long lastStatusMs = 0;
  static bool first = true;

  if (!first && millis() - lastStatusMs < 10000UL)
    return;

  first = false;
  lastStatusMs = millis();
  WriteStatus(F("Status"));
}

// The same status line on two channels, each driving its own Home Assistant sensor:
// every 10 s on "Status", and on the STATUS button for "StatusOnDemand".
void WriteStatus(const __FlashStringHelper *channel)
{
  char freqText[10] = ""; // fits "2.00"
  Blaeck.toText(Frequency, 2, freqText, sizeof(freqText));
  char waveName[12] = ""; // fits the longest option, "Triangle"
  Blaeck.getSelectOptionNameAt("SET_WAVE", waveIndex, waveName, sizeof(waveName));
  const char *runState = Enabled ? "running" : "stopped";

  char text[80]; // fits "stopped Triangle @ 2.00 Hz"
  snprintf(text, sizeof(text), "%s %s @ %s Hz", runState, waveName, freqText);
  Blaeck.writeState(channel, text);
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
      Blaeck.writeEvent(F("Activity"), F("idle_warning"));
      warned = true;
    }
    return;
  }

  if (warned)
    Blaeck.writeEvent(F("Activity"), F("resumed"));

  idleSinceMs = 0;
  warned = false;
}

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  Frequency = (float)atof(params[0]);
  // The one control backed by a signal, so it reports by writing that signal - and the value
  // is logged. The others carry their own state instead, which is not; see onSetAmp().
  Blaeck.write("Frequency", Frequency);
}

void onSetAmp(const char *command, const char *const *params, byte paramCount)
{
  Amplitude = (float)atof(params[0]);
  // Publishes the command's own state. Pushing is what makes the new value visible at once -
  // the channel is otherwise only read when a host polls the catalog.
  Blaeck.writeCommandState(command);
}

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  Offset = (float)atof(params[0]);
  Blaeck.writeCommandState(command);
}

void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  // params[0] is always the option index, whatever the host sent.
  waveIndex = (byte)atoi(params[0]);
  Blaeck.writeCommandState(command);
}

void onSetEnable(const char *command, const char *const *params, byte paramCount)
{
  // The library has already rejected anything that is not 0 or 1.
  Enabled = atoi(params[0]) == 1;
  Blaeck.writeCommandState(command);
}

void onSetLabel(const char *command, const char *const *params, byte paramCount)
{
  // Never longer than withMaxLength(sizeof(DeviceLabel) - 1) above; empty clears the label.
  strcpy(DeviceLabel, params[0]);
  Blaeck.writeCommandState(command);
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  // Updates only on button press, independent of the 10 s writes to "Status".
  WriteStatus(F("StatusOnDemand"));
}
