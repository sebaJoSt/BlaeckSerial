/*
  WaveformGenerator.ino

  A dashboard-friendly demo for the BlaeckSerial -> Loggbok -> MQTT bridge.

  One fully controllable waveform, driven entirely over MQTT. Commands are registered with
  the typed helpers (onNumberCommand / onSelectCommand / onSwitchCommand / onTextCommand /
  onButtonCommand), so the device is self-describing: it advertises range, unit, options and
  the mirrored signal, which Loggbok turns into Home Assistant MQTT Discovery entities. Every
  accepted value is written back to its signal, so a dashboard always shows what the device
  actually applied.

  Signals, messages and events are three different jobs:
    signal   a value that is sampled and logged  -> Output, Frequency, ...
    message  a line of free text, not logged     -> "running Sine @ 1.00 Hz"
    event    a discrete occurrence, no text      -> idle_warning, resumed

  A control's state can come from either of the first two, and SET_OFFSET shows the second
  route: its state lives on a message channel rather than a signal, so Offset is never
  logged, yet the control still shows the value the device applied.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial

  --- DASHBOARD MAPPING (Loggbok topic prefix: "loggbok" table name: "wave") ---
    Topic                             Widget           Meaning
    loggbok/wave/Output               chart           live generated sample
    loggbok/wave/Frequency            number          wave frequency [Hz]   (0..2)
    loggbok/wave/Amplitude            number          peak amplitude        (0..100)
    loggbok/wave/Waveform             select          0=Sine 1=Square 2=Triangle 3=Sawtooth
    loggbok/wave/Enabled              switch          output on/off (off -> Output = Offset)
    loggbok/wave/WaveName             text sensor     current waveform shape name (mirrors Waveform)
    loggbok/wave/DeviceLabel          text            free-text label (set via SET_LABEL)
    loggbok/wave/msg/Offset           number          DC offset             (-100..100)
    loggbok/wave/msg/Status           text sensor     status heartbeat, every 10 s
    loggbok/wave/msg/StatusOnDemand   text sensor     status on STATUS button press
    loggbok/wave/evt/Output           event           idle_warning / resumed

  --- COMMANDS (publish to loggbok/<table>/_cmd/<NAME>, or loggbok/_all/_cmd/<NAME>) ---
    SET_FREQ    <0..2>      frequency [Hz]            -> Frequency  (HA number, step 0.01)
    SET_AMP     <0..100>    peak amplitude            -> Amplitude  (HA number, step 0.1)
    SET_OFFSET  <-100..100> DC offset                 -> msg/Offset (HA number, step 0.1)
    SET_WAVE    <0..3>      Sine/Square/Triangle/Saw  -> Waveform   (HA select; name or index)
    SET_ENABLE  <0|1>       output on/off             -> Enabled    (HA switch)
    SET_LABEL   <text>      free-text device label    -> DeviceLabel (HA text, max 32, config)
    STATUS                  push status to StatusOnDemand channel   (HA button)

  Loggbok CLI (log fast enough to resolve the wave, e.g. 20 ms):
    lgbk log --port COM24 --table wave --signals * --interval 20 \
      --mqtt --mqtt-endpoint mqtt://127.0.0.1:1884
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

BlaeckSerial BlaeckSerial;

//---SIGNALS (fixed set -> safe to control while logging)
float Output = 0.0;
float Frequency = 1.0; // [Hz]
float Amplitude = 1.0;
float Offset = 0.0;
// Offset is the one control whose state travels on a message channel instead of a
// signal (see setup()), so it needs its value as text. addMessageChannel() borrows
// this buffer and reads it whenever the channel catalog is built, so it must be a
// global - never a local. Left empty here and filled by FormatOffset() in setup(),
// so the text is only ever derived from Offset and the two cannot disagree.
char OffsetText[12];
byte Waveform = 0; // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL
// Human-readable shape names for the WaveName signal; keep in sync with the
// SET_WAVE options CSV in setup().
const char *const WAVE_NAMES[] = {"Sine", "Square", "Triangle", "Sawtooth"};

//---GENERATOR STATE
double phase = 0.0; // normalized phase 0..1
unsigned long lastMicros = 0;

void setup()
{
  Serial.begin(115200);
  BlaeckSerial.begin(&Serial, 8);

  BlaeckSerial.DeviceName = "Waveform Generator Demo";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  BlaeckSerial.addSignal("Output", &Output);
  BlaeckSerial.addSignal("Frequency", &Frequency);
  BlaeckSerial.addSignal("Amplitude", &Amplitude);
  // No "Offset" signal on purpose: its control reports state from a message channel
  // instead, declared below.
  BlaeckSerial.addSignal("Waveform", &Waveform);
  BlaeckSerial.addSignal("Enabled", &Enabled);
  BlaeckSerial.addSignal("WaveName", (char *)WAVE_NAMES[Waveform]);
  BlaeckSerial.addSignal("DeviceLabel", DeviceLabel);

  BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq, F("Frequency"), 0.0f, 2.0f, 0.01f, F("Hz"));
  BlaeckSerial.onNumberCommand("SET_AMP", onSetAmp, F("Amplitude"), 0.0f, 100.0f, 0.1f);
  // Offset reports its state from the "Offset" MESSAGE CHANNEL, not a signal - the one
  // control in this sketch wired that way, so both routes can be compared side by side.
  // A message channel is independent of the signal table and of whatever the host's user
  // selects for logging, so this control keeps showing state either way. onSetOffset
  // pushes each new value with writeMessage(), and the channel also hands the catalog its
  // current value (see addMessageChannel below), so a host that reconnects reads the right
  // number straight away.
  BlaeckSerial.onNumberCommand("SET_OFFSET", onSetOffset, F("Offset"), -100.0f, 100.0f, 0.1f,
                               nullptr, BLAECK_CAT_NONE, BLAECK_STATE_MESSAGE);
  BlaeckSerial.onSelectCommand("SET_WAVE", onSetWave, F("Waveform"), F("Sine,Square,Triangle,Sawtooth"));
  BlaeckSerial.onSwitchCommand("SET_ENABLE", onSetEnable, F("Enabled"));
  // Host percent-encodes the value; the device decodes it and enforces the 32-byte max.
  // Config category: a device label is a setting, not a control, so Home Assistant keeps
  // it off the auto-generated dashboards.
  BlaeckSerial.onTextCommand("SET_LABEL", onSetLabel, F("DeviceLabel"), 32, BLAECK_CAT_CONFIG);
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  // Declared up-front so the host can announce one text sensor per channel before
  // the first line is written.
  BlaeckSerial.addMessageChannel("Status", F("mdi:pulse"), true);
  BlaeckSerial.addMessageChannel("StatusOnDemand", F("mdi:message-text"), true);
  // Backs the SET_OFFSET control, so it is a normal entity rather than a diagnostic one.
  // The fourth argument registers OffsetText as this channel's current value: the catalog
  // reads it live, so the value is right from the first poll without the sketch re-sending
  // anything. FormatOffset() runs first so the buffer is never blank.
  FormatOffset();
  BlaeckSerial.addMessageChannel("Offset", F("mdi:arrow-up-down"), false, OffsetText);

  // Each event channel declares up-front the closed set of events it can report.
  BlaeckSerial.addEventChannel("Output", F("mdi:sine-wave"));
  BlaeckSerial.addEventType("Output", F("idle_warning"));
  BlaeckSerial.addEventType("Output", F("resumed"));

  lastMicros = micros();
}

void loop()
{
  UpdateWaveform();
  BlaeckSerial.tick();
  SendStatusMessage();
  CheckOutputIdle();
}

// Warns once per idle stretch (>=5s -> "idle_warning"), and only reports "resumed" if a warning was raised.
void CheckOutputIdle()
{
  static unsigned long idleSinceMs = 0;
  static bool warned = false;

  if (!Enabled)
  {
    if (idleSinceMs == 0)
      idleSinceMs = millis();

    if (!warned && (millis() - idleSinceMs) >= 5000UL)
    {
      BlaeckSerial.writeEvent("Output", F("idle_warning"));
      warned = true;
    }
    return;
  }

  if (warned)
    BlaeckSerial.writeEvent("Output", F("resumed"));

  idleSinceMs = 0;
  warned = false;
}

// Offset as text for its message channel, to one decimal (SET_OFFSET's step). Same integer
// trick as WriteStatus - AVR does not link float printf, so %f would print blank - except
// Offset is signed, so the sign is written separately: integer division of a negative value
// would otherwise strand a minus in the fractional part.
// A Home Assistant number reads its state as a bare numeric, so the text carries no unit.
void FormatOffset()
{
  long tenths = (long)(Offset * 10.0 + (Offset >= 0 ? 0.5 : -0.5));
  long magnitude = tenths < 0 ? -tenths : tenths;
  snprintf(OffsetText, sizeof(OffsetText), "%s%ld.%ld",
           tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

// The same status line on two channels, each driving its own Home Assistant sensor:
// a 10 s heartbeat on "Status", and the STATUS button on "StatusOnDemand".
void WriteStatus(const char *channel)
{
  // Format frequency to 2 decimals without snprintf's %f: AVR does not link float
  // printf by default, so %f would print blank. Frequency is always >= 0 here.
  int hz100 = (int)(Frequency * 100.0 + 0.5);
  char text[80];
  snprintf(text, sizeof(text), "%s %s @ %d.%02d Hz", Enabled ? "running" : "stopped", WAVE_NAMES[Waveform], hz100 / 100, hz100 % 100);
  BlaeckSerial.writeMessage(channel, text);
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

void UpdateWaveform()
{
  unsigned long now = micros();
  double dt = (now - lastMicros) * 1e-6; // [s]
  lastMicros = now;

  if (!Enabled)
  {
    Output = Offset;
    return;
  }

  // Advance and wrap the normalized phase (0..1).
  phase += (double)Frequency * dt;
  phase -= floor(phase);

  double w = 0.0;
  switch (Waveform)
  {
  case 1: // Square
    w = (phase < 0.5) ? 1.0 : -1.0;
    break;
  case 2: // Triangle: +1 at phase 0, -1 at phase 0.5
    w = 1.0 - 4.0 * fabs(phase - 0.5);
    break;
  case 3: // Sawtooth: -1 .. +1 ramp
    w = 2.0 * phase - 1.0;
    break;
  default: // Sine
    w = sin(2.0 * PI * phase);
    break;
  }

  Output = Offset + Amplitude * w;
}

// Rounds to a fixed number of decimal places, cleaning up tiny float rounding noise from atof()
// (e.g. "0.15" -> 0.14999999...).
float roundToDecimals(float value, byte decimals)
{
  float scale = pow(10, decimals);
  return roundf(value * scale) / scale;
}

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    Frequency = roundToDecimals((float)atof(params[0]), 4);
    BlaeckSerial.write("Frequency", Frequency);
  }
}

void onSetAmp(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    Amplitude = roundToDecimals((float)atof(params[0]), 4);
    BlaeckSerial.write("Amplitude", Amplitude);
  }
}

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    Offset = roundToDecimals((float)atof(params[0]), 4);
    // The other handlers write their signal back; this one pushes a message instead,
    // because SET_OFFSET declares the channel as its state source.
    FormatOffset();
    BlaeckSerial.writeMessage("Offset", OffsetText);
  }
}

void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    Waveform = (byte)atoi(params[0]);
    BlaeckSerial.write("Waveform", Waveform);
    BlaeckSerial.write("WaveName", (char *)WAVE_NAMES[Waveform]);
  }
}

void onSetEnable(const char *command, const char *const *params, byte paramCount)
{
  Enabled = paramCount >= 1 && atoi(params[0]) == 1;
  BlaeckSerial.write("Enabled", Enabled);
}

void onSetLabel(const char *command, const char *const *params, byte paramCount)
{
  // params[0] is already percent-decoded and length-checked by the library.
  if (paramCount >= 1 && params[0] != nullptr)
  {
    strncpy(DeviceLabel, params[0], sizeof(DeviceLabel) - 1);
    DeviceLabel[sizeof(DeviceLabel) - 1] = '\0';
    BlaeckSerial.write("DeviceLabel", DeviceLabel);
  }
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  // Updates only on button press, independent of the 10 s "Status" heartbeat.
  WriteStatus("StatusOnDemand");
}
