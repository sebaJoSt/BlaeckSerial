/*
  WaveformGenerator.ino

  A dashboard-friendly demo for the BlaeckSerial -> Loggbok -> MQTT bridge.

  It generates one fully controllable waveform. Frequency, amplitude, offset and waveform
  shape are all set over MQTT commands. The commands are registered with typed helpers
  (onNumberCommand / onSelectCommand / onSwitchCommand / onTextCommand / onButtonCommand) so the device is
  self-describing: it advertises range, unit, options and the mirrored signal in a 0xE0
  "Command List" frame, which Loggbok turns into Home Assistant MQTT Discovery entities.
  Out-of-range values are rejected by the library (and reported on the debug stream); each
  accepted value is written back to its signal, so a dashboard always shows the value the
  device actually applied. A read-only string signal (WaveName) mirrors the selected shape
  as human-readable text, and a writable free-text command (SET_LABEL / DeviceLabel) round-trips
  an arbitrary string as a Home Assistant text entity via onTextCommand. Two 0x90 message
  channels ("status" heartbeat and "status_ondemand" on the STATUS button) surface as
  Home Assistant text sensors via writeMessage.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial

  --- DASHBOARD MAPPING (Loggbok topic prefix: "loggbok" table name: "wave") ---
    Topic                       Widget          Meaning
    loggbok/wave/Output         chart           live generated sample
    loggbok/wave/Frequency      number          wave frequency [Hz]   (0..50)
    loggbok/wave/Amplitude      number          peak amplitude        (0..100)
    loggbok/wave/Offset         number          DC offset             (-100..100)
    loggbok/wave/Waveform       select          0=Sine 1=Square 2=Triangle 3=Sawtooth
    loggbok/wave/Enabled        switch          output on/off (off -> Output = Offset)
    loggbok/wave/WaveName       text sensor     current waveform shape name (mirrors Waveform)
    loggbok/wave/DeviceLabel    text            free-text label (set via SET_LABEL)
    loggbok/wave/msg/status         text sensor  status heartbeat (0x90 message, every 10 s)
    loggbok/wave/msg/status_ondemand text sensor status on STATUS button press (0x90 message)

  --- COMMANDS (publish to loggbok/<table>/_cmd/<NAME>, or loggbok/_all/_cmd/<NAME>) ---
    SET_FREQ    <0..2>      frequency [Hz]            -> Frequency  (HA number, step 0.01)
    SET_AMP     <0..100>    peak amplitude            -> Amplitude  (HA number, step 0.1)
    SET_OFFSET  <-100..100> DC offset                 -> Offset     (HA number, step 0.1)
    SET_WAVE    <0..3>      Sine/Square/Triangle/Saw  -> Waveform   (HA select; name or index)
    SET_ENABLE  <0|1>       output on/off             -> Enabled    (HA switch)
    SET_LABEL   <text>      free-text device label    -> DeviceLabel (HA text, max 32)
    STATUS                  push status to status_ondemand channel  (HA button)

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
byte Waveform = 0; // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth
bool Enabled = true;
char DeviceLabel[33] = "wave-gen"; // free-text label set via SET_LABEL
// Human-readable shape names for the WaveName string signal (-> HA text sensor).
// Single source for the sensor text; keep in sync with the SET_WAVE options CSV in setup().
const char *const WAVE_NAMES[] = {"Sine", "Square", "Triangle", "Sawtooth"};

//---COMMAND HANDLERS
void onSetFreq(const char *command, const char *const *params, byte paramCount);
void onSetAmp(const char *command, const char *const *params, byte paramCount);
void onSetOffset(const char *command, const char *const *params, byte paramCount);
void onSetWave(const char *command, const char *const *params, byte paramCount);
void onSetEnable(const char *command, const char *const *params, byte paramCount);
void onSetLabel(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);
void WriteStatus(const char *channel);
void SendStatusMessage();

//---GENERATOR STATE
double phase = 0.0; // normalized phase 0..1
unsigned long lastMicros = 0;

void setup()
{
  Serial.begin(115200);
  BlaeckSerial.begin(&Serial, 6);

  BlaeckSerial.DeviceName = "Waveform Generator Demo Serial Discovery";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  BlaeckSerial.addSignal("Output", &Output);
  BlaeckSerial.addSignal("Frequency", &Frequency);
  BlaeckSerial.addSignal("Amplitude", &Amplitude);
  BlaeckSerial.addSignal("Offset", &Offset);
  BlaeckSerial.addSignal("Waveform", &Waveform);
  BlaeckSerial.addSignal("Enabled", &Enabled);
  BlaeckSerial.addSignal("WaveName", (char *)WAVE_NAMES[Waveform]);
  BlaeckSerial.addSignal("DeviceLabel", DeviceLabel);

  BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq, F("Frequency"), 0.0f, 2.0f, 0.01f, F("Hz"));
  BlaeckSerial.onNumberCommand("SET_AMP", onSetAmp, F("Amplitude"), 0.0f, 100.0f, 0.1f);
  BlaeckSerial.onNumberCommand("SET_OFFSET", onSetOffset, F("Offset"), -100.0f, 100.0f, 0.1f);
  BlaeckSerial.onSelectCommand("SET_WAVE", onSetWave, F("Waveform"), F("Sine,Square,Triangle,Sawtooth"));
  BlaeckSerial.onSwitchCommand("SET_ENABLE", onSetEnable, F("Enabled"));
  // HA text: closed-loop, mirrored to the "DeviceLabel" string signal. The host
  // percent-encodes the value; the device decodes it and enforces the 32-byte max.
  BlaeckSerial.onTextCommand("SET_LABEL", onSetLabel, F("DeviceLabel"), 32);
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  lastMicros = micros();
}

void loop()
{
  UpdateWaveform();
  BlaeckSerial.tick();
  SendStatusMessage();
}

// Demonstrates the 0x90 message frame: a fire-and-forget, named free-text status/log channel.
// Unlike signals, messages are NOT logged/stored by the host - a host such as Loggbok surfaces
// each channel as its own Home Assistant text sensor.
//
// The same status line is produced two ways, both via WriteStatus(), on SEPARATE channels so
// each drives its own Home Assistant sensor:
//   - SendStatusMessage(): a periodic 10 s heartbeat on "status"          ("Status (live)")
//   - onStatus():          the STATUS button handler on "status_ondemand" ("Get Status")
void WriteStatus(const char *channel)
{
  const char *shape;
  switch (Waveform)
  {
  case 1: shape = "Square"; break;
  case 2: shape = "Triangle"; break;
  case 3: shape = "Sawtooth"; break;
  default: shape = "Sine"; break;
  }

  // Format frequency to 2 decimals without snprintf's %f: AVR (e.g. Mega 2560) does not link
  // float printf by default, so %f would print blank. Frequency is always >= 0 here.
  int hz100 = (int)(Frequency * 100.0 + 0.5);
  char text[80];
  snprintf(text, sizeof(text), "%s %s @ %d.%02d Hz", Enabled ? "running" : "stopped", shape, hz100 / 100, hz100 % 100);
  BlaeckSerial.writeMessage(channel, text);
}

void SendStatusMessage()
{
  static unsigned long lastStatusMs = 0;
  if (millis() - lastStatusMs < 10000UL)
    return;
  lastStatusMs = millis();
  WriteStatus("status");
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
    BlaeckSerial.write("Offset", Offset);
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
  // On-demand: push a fresh status line to the dedicated "status_ondemand" channel so its HA
  // sensor updates only on button press (independent of the 10 s "status" heartbeat).
  WriteStatus("status_ondemand");
}
