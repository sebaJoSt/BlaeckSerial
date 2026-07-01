/*
  WaveformGenerator.ino

  A dashboard-friendly demo for the BlaeckSerial -> Loggbok -> MQTT bridge.

  It generates ONE fully controllable waveform on a FIXED set of signals, so it is safe
  to control live while Loggbok is logging (the signal list never changes). Frequency,
  amplitude, offset and waveform shape are all set over MQTT commands, and each command
  updates a PAIRED signal - so a dashboard reacts to the reported signal (state-reflection
  pattern), not to the control widget.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial

  --- DASHBOARD MAPPING (Loggbok topic prefix: "loggbok" table name: "wave") ---
    Topic                       Widget          Meaning
    loggbok/wave/Output         chart           live generated sample
    loggbok/wave/Frequency      slider / text   wave frequency [Hz]
    loggbok/wave/Amplitude      slider / text   peak amplitude
    loggbok/wave/Offset         slider / text   DC offset
    loggbok/wave/Waveform       text/select     0=Sine 1=Square 2=Triangle 3=Sawtooth
    loggbok/wave/Enabled        switch          output on/off (off -> Output = Offset)

  --- COMMANDS (publish to loggbok/<table>/_cmd/<NAME>, or loggbok/_all/_cmd/<NAME>) ---
    SET_FREQ    <float>   frequency [Hz]            -> Frequency
    SET_AMP     <float>   peak amplitude            -> Amplitude
    SET_OFFSET  <float>   DC offset                 -> Offset
    SET_WAVE    <0..3>    Sine/Square/Triangle/Saw  -> Waveform
    SET_ENABLE  <0|1>     output on/off             -> Enabled
    STATUS                print info to serial

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
byte Waveform = 0;     // 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth
bool Enabled = true;

//---COMMAND HANDLERS
void onSetFreq(const char *command, const char *const *params, byte paramCount);
void onSetAmp(const char *command, const char *const *params, byte paramCount);
void onSetOffset(const char *command, const char *const *params, byte paramCount);
void onSetWave(const char *command, const char *const *params, byte paramCount);
void onSetEnable(const char *command, const char *const *params, byte paramCount);
void onStatus(const char *command, const char *const *params, byte paramCount);

//---GENERATOR STATE
double phase = 0.0; // normalized phase 0..1
unsigned long lastMicros = 0;

void setup()
{
  Serial.begin(115200);
  BlaeckSerial.begin(&Serial, 6);

  BlaeckSerial.DeviceName = "Waveform Generator Demo";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  BlaeckSerial.addSignal("Output", &Output);
  BlaeckSerial.addSignal("Frequency", &Frequency);
  BlaeckSerial.addSignal("Amplitude", &Amplitude);
  BlaeckSerial.addSignal("Offset", &Offset);
  BlaeckSerial.addSignal("Waveform", &Waveform);
  BlaeckSerial.addSignal("Enabled", &Enabled);

  BlaeckSerial.onCommand("SET_FREQ", onSetFreq);
  BlaeckSerial.onCommand("SET_AMP", onSetAmp);
  BlaeckSerial.onCommand("SET_OFFSET", onSetOffset);
  BlaeckSerial.onCommand("SET_WAVE", onSetWave);
  BlaeckSerial.onCommand("SET_ENABLE", onSetEnable);
  BlaeckSerial.onCommand("STATUS", onStatus);

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

void onSetFreq(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
    Frequency = constrain((float)atof(params[0]), 0.0f, 50.0f);
}

void onSetAmp(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
    Amplitude = constrain((float)atof(params[0]), 0.0f, 100.0f);
}

void onSetOffset(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
    Offset = constrain((float)atof(params[0]), -100.0f, 100.0f);
}

void onSetWave(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
    Waveform = (byte)constrain(atoi(params[0]), 0, 3);
}

void onSetEnable(const char *command, const char *const *params, byte paramCount)
{
  Enabled = paramCount >= 1 && atoi(params[0]) == 1;
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  Serial.print(F("Enabled=")), Serial.print(Enabled);
  Serial.print(F(" Wave=")), Serial.print(Waveform);
  Serial.print(F(" Freq=")), Serial.print(Frequency);
  Serial.print(F(" Amp=")), Serial.print(Amplitude);
  Serial.print(F(" Offset=")), Serial.println(Offset);
}
