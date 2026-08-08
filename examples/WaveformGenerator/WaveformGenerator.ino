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
  device actually applied.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial

  --- DASHBOARD MAPPING (Loggbok topic prefix: "loggbok" table name: "wave") ---
    Topic                       Widget          Meaning
    loggbok/wave/Output         chart           live generated sample
    loggbok/wave/Frequency      number          wave frequency [Hz]   (0..50)
    loggbok/wave/Amplitude      number          peak amplitude        (0..100)
    loggbok/wave/Offset         number          DC offset             (-100..100)
    loggbok/wave/Waveform       select          0=Sine 1=Square 2=Triangle 3=Sawtooth
    loggbok/wave/Enabled        switch          output on/off (off -> Output = Offset)

  --- COMMANDS (publish to loggbok/<table>/_cmd/<NAME>, or loggbok/_all/_cmd/<NAME>) ---
    SET_FREQ    <0..2>      frequency [Hz]            -> Frequency  (HA number, step 0.01)
    SET_AMP     <0..100>    peak amplitude            -> Amplitude  (HA number, step 0.1)
    SET_OFFSET  <-100..100> DC offset                 -> Offset     (HA number, step 0.1)
    SET_WAVE    <0..3>      Sine/Square/Triangle/Saw  -> Waveform   (HA select; name or index)
    SET_ENABLE  <0|1>       output on/off             -> Enabled    (HA switch)
    SET_LABEL   <text>      free-text label (<=20)                  (HA text; open-loop)
    STATUS                  print info to serial                    (HA button)

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
char Label[24] = "wave"; // free-text label set via SET_LABEL

//---COMMAND HANDLERS
void onSetFreq(const char *command, const char *const *params, byte paramCount);
void onSetAmp(const char *command, const char *const *params, byte paramCount);
void onSetOffset(const char *command, const char *const *params, byte paramCount);
void onSetWave(const char *command, const char *const *params, byte paramCount);
void onSetEnable(const char *command, const char *const *params, byte paramCount);
void onSetLabel(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);

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

  BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq, F("Frequency"), 0.0f, 2.0f, 0.01f, F("Hz"));
  BlaeckSerial.onNumberCommand("SET_AMP", onSetAmp, F("Amplitude"), 0.0f, 100.0f, 0.1f);
  BlaeckSerial.onNumberCommand("SET_OFFSET", onSetOffset, F("Offset"), -100.0f, 100.0f, 0.1f);
  BlaeckSerial.onSelectCommand("SET_WAVE", onSetWave, F("Waveform"), F("Sine,Square,Triangle,Sawtooth"));
  BlaeckSerial.onSwitchCommand("SET_ENABLE", onSetEnable, F("Enabled"));
  // HA text: open-loop (no state signal until string signals land). The host
  // percent-encodes the value; the device decodes it and enforces the 20-byte max.
  BlaeckSerial.onTextCommand("SET_LABEL", onSetLabel, nullptr, 20);
  BlaeckSerial.onButtonCommand("STATUS", onStatus);

  lastMicros = micros();
}

void loop()
{
  UpdateWaveform();
  BlaeckSerial.tick();
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
    strncpy(Label, params[0], sizeof(Label) - 1);
    Label[sizeof(Label) - 1] = '\0';
  }
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  Serial.print(F("Enabled=")), Serial.print(Enabled);
  Serial.print(F(" Wave=")), Serial.print(Waveform);
  Serial.print(F(" Freq=")), Serial.print(Frequency);
  Serial.print(F(" Amp=")), Serial.print(Amplitude);
  Serial.print(F(" Offset=")), Serial.print(Offset);
  Serial.print(F(" Label=")), Serial.println(Label);
}
