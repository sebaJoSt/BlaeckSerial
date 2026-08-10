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

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial

  --- WHAT YOU GET (Loggbok topic prefix "loggbok", table name "wave") ---
  Commands are published to loggbok/wave/_cmd/<NAME>, or loggbok/_all/_cmd/<NAME>.

    HA entity     set with                       topic
    number        SET_FREQ    <0..2>             wave/Frequency           [Hz], step 0.01
    number        SET_AMP     <0..100>           wave/Amplitude           step 0.1
    number        SET_OFFSET  <-100..100>        wave/msg/Offset          step 0.1
    select        SET_WAVE    <Sine|Square|...>  wave/WaveName            or an index, 0..3
    switch        SET_ENABLE  <0|1>              wave/Enabled             off -> Output = Offset
    text          SET_LABEL   <text>             wave/DeviceLabel         max 32, config category
    button        STATUS                         --                       stateless; fills the sensor below
    sensor        --                             wave/Output              live sample, with statistics
    sensor        --                             wave/msg/Status          text: heartbeat, every 10 s
    sensor        --                             wave/msg/StatusOnDemand  text: on STATUS press
    event         --                             wave/evt/Activity        idle_warning / resumed

  --- HOW A CONTROL GETS ITS VALUE BACK ---

   Home Assistant          MQTT broker           Loggbok         this sketch
          |                     |                   |                 |
          | wave/_cmd/SET_FREQ  |      "1.5"        | <SET_FREQ,1.5>  |
  command |-------------------->|------------------>|---------------->|  onSetFreq()
          |                     |                   |                 |    Frequency = 1.5
          |   wave/Frequency    |      "1.5"        |  signal frame   |
    state |<--------------------|<------------------|<----------------|  write("Frequency", Frequency)

  Sending a value is a request, not a fact: it can be clamped, rejected, lost on the way, or
  replaced by the device on its next boot. So every control reports its state back, and what a
  dashboard shows is the device's value rather than its own guess. SET_OFFSET makes the same
  trip from a message channel instead of a signal.

  Loggbok CLI (log fast enough to resolve the wave, e.g. 20 ms):
    lgbk log --port COM24 --table wave --signals * --interval 20 \
      --mqtt --mqtt-endpoint mqtt://127.0.0.1:1884
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

BlaeckSerial BlaeckSerial;

//---PUBLISHED AS SIGNALS (fixed set -> safe to control while logging)
// addSignal() keeps a pointer to these, so they have to be globals.
float Output = 0.0;
float Frequency = 1.0; // [Hz]
float Amplitude = 1.0;
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL
// Current shape as text, refreshed by RefreshWaveName() from the option list SET_WAVE
// declares - so the names exist once, in flash, instead of being repeated here and kept
// in step by hand.
char WaveName[12];

//---PUBLISHED AS A COMMAND'S OWN STATE
// Not a signal and not logged: SET_OFFSET carries this itself, rendered by OffsetState().
float Offset = 0.0;

//---GENERATOR STATE (never leaves the sketch)
double phase = 0.0;        // normalized phase 0..1
unsigned long lastMicros = 0;
byte waveIndex = 0;        // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth; WaveName is what ships

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
  // No index signal: SET_WAVE reports its state as the option name, so waveIndex stays a
  // plain variable and never reaches the host.
  BlaeckSerial.addSignal("Enabled", &Enabled);
  BlaeckSerial.addSignal("WaveName", WaveName);
  BlaeckSerial.addSignal("DeviceLabel", DeviceLabel);

  BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq, F("Frequency"), 0.0f, 2.0f, 0.01f, F("Hz"));
  BlaeckSerial.onNumberCommand("SET_AMP", onSetAmp, F("Amplitude"), 0.0f, 100.0f, 0.1f);
  // The one control here that carries its OWN state instead of mirroring a signal, so both
  // routes can be compared side by side. BlaeckOwnState declares a channel called "Offset",
  // asks OffsetState for the value whenever a host polls the channel catalog, and announces it
  // once now - so a host already connected when the board resets is corrected. That state is
  // independent of the signal table and of what the host's user selects for logging, and no
  // separate sensor appears for it: the control is where you read the offset.
  BlaeckSerial.onNumberCommand("SET_OFFSET", onSetOffset,
                               BlaeckOwnState(F("Offset"), OffsetState),
                               -100.0f, 100.0f, 0.1f);
  // State comes from the WaveName string signal, not an index: the host matches the option
  // name straight against the list below, so nothing has to carry the index.
  BlaeckSerial.onSelectCommand("SET_WAVE", onSetWave, F("WaveName"), F("Sine,Square,Triangle,Sawtooth"));
  BlaeckSerial.onSwitchCommand("SET_ENABLE", onSetEnable, F("Enabled"));
  // Host percent-encodes the value; the device decodes it and enforces the 32-byte max.
  // Config category: a device label is a setting, not a control, so Home Assistant keeps
  // it off the auto-generated dashboards.
  BlaeckSerial.onTextCommand("SET_LABEL", onSetLabel, F("DeviceLabel"), 32, BLAECK_CAT_CONFIG);
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  // After the commands, because it reads SET_WAVE's option list. addSignal() above only
  // stored the buffer's address, so filling it now is in time for the first transmit.
  RefreshWaveName();

  // Declared up-front so the host can announce one text sensor per channel before
  // the first line is written.
  BlaeckSerial.addMessageChannel("Status", F("mdi:pulse"), true);
  BlaeckSerial.addMessageChannel("StatusOnDemand", F("mdi:message-text"), true);

  // Each event channel declares up-front the closed set of events it can report.
  // addEventType() does the same one name at a time, for a list built conditionally.
  BlaeckSerial.addEventChannel("Activity", F("mdi:sine-wave"), false, F("idle_warning,resumed"));

  lastMicros = micros();
}

void loop()
{
  UpdateWaveform();
  BlaeckSerial.tick();
  SendStatusMessage();
  CheckActivity();
}

// Warns once per idle stretch (>=5s -> "idle_warning"), and only reports "resumed" if a warning was raised.
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

// The current shape's name, read back from the option list SET_WAVE declared, so those
// names live in flash once instead of being repeated in the sketch. Must run after
// onSelectCommand() has registered SET_WAVE; the index is written instead if the lookup
// fails, which at least shows on the dashboard as a value that is not one of the options.
void RefreshWaveName()
{
  if (!BlaeckSerial.getSelectOption("SET_WAVE", waveIndex, WaveName, sizeof(WaveName)))
    snprintf(WaveName, sizeof(WaveName), "%u", (unsigned)waveIndex);
}

// Offset as text for its message channel, to one decimal (SET_OFFSET's step). Registered with
// the channel, so the library calls it whenever it builds the catalog: the value is fetched at
// that moment rather than stored, and no path that changes Offset can leave a stale copy behind.
// Formatting only - it runs while a frame is being built, and in unbuffered mode while that
// frame is going out, so anything slow would stall it. Sample slow sources in loop() instead.
// Same integer trick as WriteStatus - AVR does not link float printf, so %f would print blank -
// except Offset is signed, so the sign is written separately: integer division of a negative
// value would otherwise strand a minus in the fractional part.
// A Home Assistant number reads its state as a bare numeric, so the text carries no unit.
const char *OffsetState()
{
  static char text[12];
  long tenths = (long)(Offset * 10.0 + (Offset >= 0 ? 0.5 : -0.5));
  long magnitude = tenths < 0 ? -tenths : tenths;
  snprintf(text, sizeof(text), "%s%ld.%ld",
           tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
  return text;
}

// The same status line on two channels, each driving its own Home Assistant sensor:
// a 10 s heartbeat on "Status", and the STATUS button on "StatusOnDemand".
void WriteStatus(const char *channel)
{
  // Format frequency to 2 decimals without snprintf's %f: AVR does not link float
  // printf by default, so %f would print blank. Frequency is always >= 0 here.
  int hz100 = (int)(Frequency * 100.0 + 0.5);
  char text[80];
  snprintf(text, sizeof(text), "%s %s @ %d.%02d Hz", Enabled ? "running" : "stopped", WaveName, hz100 / 100, hz100 % 100);
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
  switch (waveIndex)
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
    // The others write their signal back; this one publishes the command's own state. Pushing
    // is what makes it visible at once - the getter is only read when a host polls the catalog.
    BlaeckSerial.writeCommandState(command);
  }
}

void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    // atoi even though Home Assistant sends the option NAME: the library resolves it against
    // the list declared above and rewrites the parameter to that index before calling this.
    waveIndex = (byte)atoi(params[0]);
    RefreshWaveName();
    BlaeckSerial.write("WaveName", WaveName);
  }
}

void onSetEnable(const char *command, const char *const *params, byte paramCount)
{
  // Guard first, then assign: folding the check into the assignment would make a
  // command that supplied no value read as 0 and switch the output off.
  // The library has already rejected anything that is not 0 or 1.
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    Enabled = atoi(params[0]) == 1;
    BlaeckSerial.write("Enabled", Enabled);
  }
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
