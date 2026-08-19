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
    signal   a value that is sampled and logged       -> Output, Frequency, RunNote
    state    what a control is set to, not logged     -> Amplitude, Wave, "running Sine @ 1.00 Hz"
    event    a discrete event from a fixed list       -> idle_warning, resumed

  Controls (shown under their display name, sent as their command name):
    SET_FREQ    number  0..2 Hz, step 0.01              "Frequency"       state: Frequency signal
    SET_AMP     number  0..100, step 0.1                "Amplitude"       state: own channel
    SET_OFFSET  number  -100..100, step 0.1             "Offset"          state: own channel
    SET_WAVE    select  Sine/Square/Triangle/Sawtooth   "Waveform"        state: own channel
    SET_ENABLE  switch  off -> Output = Offset          "Output enabled"  state: own channel
    SET_LABEL   text    max 32 bytes, config category   "Device label"    state: own channel,
                and the name the status line reports under
    SET_NOTE    text    max 24 bytes                    "Run note"        state: RunNote signal
    STATUS      button  writes the StatusOnDemand channel   "Request status"

  Signals that describe themselves (Frequency declares nothing, and costs nothing):
    Output      measurement, 3 decimals, mdi:sine-wave, shown as "Output" while the column
                it is logged to stays "Output [V]"
    RunNote     free text, logged with every row - what SET_NOTE wrote

  Shown but never logged (state channels):
    Uptime      seconds since boot, which a host may show in minutes or hours instead
    Status      the same line every 10 s; StatusOnDemand, the same on the STATUS button

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

BlaeckSerial Blaeck;

//---PUBLISHED AS SIGNALS, AND SO LOGGED
// addSignal() keeps a pointer to these, so they have to be globals.
float Output = 0.0;
float Frequency = 1.0; // [Hz]
char RunNote[25] = ""; // "swapped probe", "run 3 after warm-up"

//---PUBLISHED AS A STATE CHANNEL, AND SO NOT LOGGED
unsigned long Uptime = 0; // [s], about the board rather than the wave

//---PUBLISHED AS THEIR COMMANDS' OWN STATE, AND SO NOT LOGGED
float Amplitude = 1.0;
float Offset = 0.0;
// The wave, as its position in the SET_WAVE option list: a select command reports which
// option was picked, never its name, so these and withOptions() below must stay in step.
enum Wave : byte { Sine, Square, Triangle, Sawtooth };
byte waveIndex = Sine;
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL

// The step each number control declares
const float FreqStep = 0.01f;  // Hz
const float AmpStep = 0.1f;
const float OffsetStep = 0.1f;

//---GENERATOR STATE (never leaves the sketch)
float phase = 0.0f; // normalized phase 0..1
unsigned long lastMicros = 0;

// The handle addSignal() returns, kept so the icon can be changed after setup() - see
// ShowWaveInIcon()
BlaeckNumericSignalRef OutputSignal;

void setup()
{
  Serial.begin(115200);

  // Sizing every table the sketch fills, so nothing is left to a default: three signals, eight
  // commands, eight state channels - three declared here and five by the commands'
  // withOwnState - and three event channels, of which this sketch declares one. Every one of
  // these calls is optional.
  Blaeck.begin(&Serial)
      .withSignals(3)
      .withCommands(8)
      .withStateChannels(8)
      .withEventChannels(3);

  Blaeck.DeviceName = "Waveform Generator Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  // Everything after addSignal() is optional, and each call changes how a host shows it.
  OutputSignal = Blaeck.addSignal(F("Output [V]"), &Output)
                     .withDisplayName(F("Output"))
                     .withUnit(F("V"))
                     .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
                     .withDisplayPrecision(3)
                     .withIcon(F("mdi:sine-wave"));

  Blaeck.addSignal(F("Frequency"), &Frequency);

  // A string signal: logged like any other, one text column in the table. Every data frame
  // carries it, so keep the buffer as small as the note needs to be.
  Blaeck.addSignal(F("RunNote"), RunNote)
      .withIcon(F("mdi:note-text"));

  Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
      .withRange(0.0f, 2.0f, FreqStep)
      .withUnit(F("Hz"))
      .withDisplayName(F("Frequency"))
      .withStateFromSignal(F("Frequency"))
      .withMode(BLAECK_NUMBER_MODE_BOX);
  Blaeck.onNumberCommand("SET_AMP", onSetAmp)
      .withRange(0.0f, 100.0f, AmpStep)
      .withDisplayName(F("Amplitude"))
      .withMode(BLAECK_NUMBER_MODE_SLIDER)
      .withOwnState(F("Amplitude"), &Amplitude);
  Blaeck.onNumberCommand("SET_OFFSET", onSetOffset)
      .withRange(-100.0f, 100.0f, OffsetStep)
      .withDisplayName(F("Offset"))
      .withOwnState(F("Offset"), &Offset);
  // A select takes no device class - Home Assistant has none for it - so an icon is the only
  // thing this control can say about how it should look.
  Blaeck.onSelectCommand("SET_WAVE", onSetWave)
      .withOptions(F("Sine,Square,Triangle,Sawtooth"))
      .withDisplayName(F("Waveform"))
      .withIcon(F("mdi:waveform"))
      .withOwnState(F("Wave"), &waveIndex);
  Blaeck.onSwitchCommand("SET_ENABLE", onSetEnable)
      .withDisplayName(F("Output enabled"))
      .withOwnState(F("Enabled"), &Enabled);
  // Host percent-encodes the value; the device decodes it and enforces the 32-byte max.
  Blaeck.onTextCommand("SET_LABEL", onSetLabel)
      .withMaxLength(sizeof(DeviceLabel) - 1)
      .withDisplayName(F("Device label"))
      .withIcon(F("mdi:tag"))
      .withOwnState(F("DeviceLabel"), DeviceLabel)
      .config();
  // The pair: SET_LABEL keeps its value on a state channel, SET_NOTE on a signal, so only the
  // note reaches the table. Nothing else about the two calls differs.
  Blaeck.onTextCommand("SET_NOTE", onSetNote)
      .withMaxLength(sizeof(RunNote) - 1)
      .withDisplayName(F("Run note"))
      .withStateFromSignal(F("RunNote"));
  Blaeck.onButtonCommand("STATUS", onStatus)
      .withDisplayName(F("Request status"))
      .diagnostic();
  // The device class is what lets a host offer minutes or hours - without one the unit is only a label.
  Blaeck.addStateChannel(F("Uptime"), &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"))
      .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
      .diagnostic();

  Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:pulse")).diagnostic();
  Blaeck.addStateChannel(F("StatusOnDemand")).withIcon(F("mdi:message-text")).diagnostic();

  // addEventType() does the same one name at a time, for a list built conditionally.
  Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"))
      .withIcon(F("mdi:bell-alert"));

  // Silent unless a table ran out of room, in which case it prints how many entries each
  // dropped and the begin() call that would have fitted them.
  Blaeck.printRejections(&Serial);

  lastMicros = micros();
}

void loop()
{
  Uptime = millis() / 1000;
  UpdateWaveform();
  ShowWaveInIcon();
  Blaeck.tick();
  StatusEvery10s();
  CheckActivity();
}

// Gives the Output signal the icon of the wave it is currently producing, so the entity in a
// host changes shape with the control rather than staying whatever setup() said.
//
// Safe to call every pass: setting an icon that is already set does nothing, so the signal
// config goes out once per waveform change and not in between.
void ShowWaveInIcon()
{
  const __FlashStringHelper *icon;
  switch (waveIndex)
  {
  case Square:   icon = F("mdi:square-wave"); break;
  case Triangle: icon = F("mdi:triangle-wave"); break;
  case Sawtooth: icon = F("mdi:sawtooth-wave"); break;
  default:       icon = F("mdi:sine-wave"); break;
  }

  OutputSignal.withIcon(icon);
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
  case Square:
    w = (phase < 0.5f) ? 1.0f : -1.0f;
    break;
  case Triangle: // -1 at phase 0, +1 at phase 0.5
    w = 1.0f - 4.0f * fabsf(phase - 0.5f);
    break;
  case Sawtooth: // -1 .. +1 ramp
    w = 2.0f * phase - 1.0f;
    break;
  default:
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

  Blaeck.writeState(F("Uptime"));
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

  // DeviceLabel is what SET_LABEL wrote. Reading it here is the whole point of storing it:
  // a control that only fills a variable no one looks at teaches the call and nothing else.
  char text[80]; // 60 at most: a 32-byte DeviceLabel, "stopped Triangle @ 2.00 Hz"
  snprintf(text, sizeof(text), "%s: %s %s @ %s Hz", DeviceLabel, runState, waveName, freqText);
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

// AVR's atof() is not correctly rounded: "0.9" can arrive as 0.90000004 and reach a dashboard.
// Snapping to the step fixes it; multiply then divide, as 0.1f is a shade over a tenth.
static float SnapToStep(const char *text, float step)
{
  const float stepsPerUnit = 1.0f / step;
  return roundf((float)atof(text) * stepsPerUnit) / stepsPerUnit;
}

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  Frequency = SnapToStep(params[0], FreqStep);
  // Reports by writing the signal it is backed by, so this value is logged. The others carry
  // their own state instead, which is not; see onSetAmp().
  Blaeck.write("Frequency", Frequency);
}

void onSetAmp(const char *command, const char *const *params, byte paramCount)
{
  Amplitude = SnapToStep(params[0], AmpStep);
  // Pushing is what makes the new value visible at once - the channel is otherwise only read
  // when a host polls the catalog.
  Blaeck.writeCommandState(command);
}

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  Offset = SnapToStep(params[0], OffsetStep);
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

void onSetNote(const char *command, const char *const *params, byte paramCount)
{
  // Arrives decoded: a host percent-encodes the value, so a note may hold the characters the
  // frame itself is built from - a comma, an angle bracket, a percent sign.
  strcpy(RunNote, params[0]);
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  WriteStatus(F("StatusOnDemand"));
}
