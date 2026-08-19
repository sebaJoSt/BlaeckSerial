// Globals the @code blocks in BlaeckSerial.h are written against.
// Only one file includes this, and it does not exist until it is generated: DocCodeBlocks.ino
//
// Before adding a name, read extras/API-STYLE.md - it explains why these are shared
// and when a new one is warranted.

#pragma once

#include "Arduino.h"
#include <EEPROM.h>
#include "BlaeckSerial.h"

// The instance, named as every example sketch in this library names it.
BlaeckSerial Blaeck;

// --- Values a sketch might publish as signals ---
float Temperature = 0.0f;
float Frequency = 1.0f;
float Output = 0.0f;
unsigned long Uptime = 0;

// A run of signals sharing a prefix, for the withNameSuffix() block.
float sine[8];

// Something worth exposing but not showing by default.
int rawAdc = 0;

// A text signal reporting one of a closed set, for the withOptions() blocks.
char modeText[12] = "idle";

// --- Values a control might carry as its own state ---
float Amplitude = 1.0f;
float Offset = 0.0f;
bool Enabled = true;
byte waveIndex = 0;
char DeviceLabel[33] = "wave-gen";
// A secret-ish setting, so a block can show a field worth masking. Nothing else in the
// cast is one: masking a device label would teach the modifier and misplace it at once.
char ApiKey[33] = "";

// --- For the write() example, which shows a signal that pulses ---
bool Pulse = false;
bool triggered = false;

// Stands in for hardware a board may or may not have fitted, so a block can show
// something being declared conditionally.
const bool hasBatteryMonitor = false;
unsigned long pulseSince = 0;

// --- Where a restored setting is kept, for the getSelectOptionIndexOf() example ---
const int addr = 0;

// Stands in for whatever a sketch actually reads, so an example can show a fresh
// value arriving without dragging a sensor library in with it.
inline float readSensor() { return 21.5f; }

// Stands in for whatever a sketch samples before a write.
inline void readAllSensors() {}

// Stands in for a real clock, for the BLAECK_UNIX timestamp block.
inline unsigned long long unixMicros() { return 0; }

// Reports a value as text, for the withOwnState() getter form.
inline const char *offsetText()
{
  static char buf[12];
  Blaeck.toText(Offset, 1, buf, sizeof(buf));
  return buf;
}

// --- A signal a switch command can mirror ---
bool ledState = false;

// Declared, not defined: a @code block on writeCommandState() shows this handler in
// full, and the extractor emits it at file scope. Blocks are emitted in header order,
// so one using the name earlier needs it declared here first.
void onSetOffset(const char *command, const char *const *params, byte paramCount);

// --- Handlers, so a command example has something to point at ---
inline void onSetFreq(const char *command, const char *const *params, byte paramCount) {}
inline void onSetAmp(const char *command, const char *const *params, byte paramCount) {}
inline void onSetWave(const char *command, const char *const *params, byte paramCount) {}
inline void onSetEnable(const char *command, const char *const *params, byte paramCount) {}
inline void onStatus(const char *command, const char *const *params, byte paramCount) {}
inline void onLED(const char *command, const char *const *params, byte paramCount) {}
inline void onReboot(const char *command, const char *const *params, byte paramCount) {}
inline void onSetLabel(const char *command, const char *const *params, byte paramCount) {}
inline void onSetApiKey(const char *command, const char *const *params, byte paramCount) {}
inline void onSetTemp(const char *command, const char *const *params, byte paramCount) {}
inline void onSetRelay(const char *command, const char *const *params, byte paramCount) {}
inline void onCalibrate(const char *command, const char *const *params, byte paramCount) {}
inline void onDutActivate(const char *command, const char *const *params, byte paramCount) {}
inline void onSwitchLED(const char *command, const char *const *params, byte paramCount) {}
inline void onAny(const char *command, const char *const *params, byte count) {}
