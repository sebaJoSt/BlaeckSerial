// The globals that the @code blocks in BlaeckSerial.h are written against.
//
// "Example" means three things in this repository, so to be exact: this file serves
// the @code blocks inside doc comments - the two or three lines an editor shows when
// someone hovers a method. Not the sketches under examples/, which are unrelated.
//
// --- How this file is reached ---
//
// Only one thing includes it, and that thing does not exist until it is generated:
//
//   extras/scripts/checkdocs.py --extract  pulls every @code block out of the header,
//   writes each one into DocExamples.ino as a function body, and puts an
//   #include of this file at the top. CI then compiles that sketch.
//
// So a @code block calling a method that has since been renamed fails the build,
// instead of being shown on hover as instructions that do not work. That is the whole
// point: a comment is invisible to the compiler until something puts it in front of one.
//
// DocExamples.ino is generated and gitignored - never edit it, and never commit it.
// Nothing in src/ or examples/ includes this file, and no sketch of yours should.
//
// This folder cannot be renamed or moved: arduino-cli requires a sketch folder to
// match its .ino, and the #include resolves relative to that sketch.
//
// --- Why the names are shared ---
//
// An example has to compile, but it also has to read well in a hover, and a snippet
// that declares two globals before it can say anything reads badly. So the names below
// are shared and an example uses them without introducing them - the way the Arduino
// reference assumes a pin is already set up.
//
// Add a name when an example needs one that is genuinely new, and reach for an existing
// one first: examples that all speak of Temperature and Frequency teach the library
// faster than examples that each invent a cast of characters.

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

// --- Values a control might carry as its own state ---
float Amplitude = 1.0f;
float Offset = 0.0f;
bool Enabled = true;
byte waveIndex = 0;
char DeviceLabel[33] = "wave-gen";

// --- For the write() example, which shows a signal that pulses ---
bool Pulse = false;
bool triggered = false;
unsigned long pulseSince = 0;

// --- Where a restored setting is kept, for the getSelectOptionIndexOf() example ---
const int addr = 0;

// Stands in for whatever a sketch actually reads, so an example can show a fresh
// value arriving without dragging a sensor library in with it.
inline float readSensor() { return 21.5f; }

// --- Handlers, so a command example has something to point at ---
inline void onSetFreq(const char *command, const char *const *params, byte paramCount) {}
inline void onSetWave(const char *command, const char *const *params, byte paramCount) {}
inline void onSetEnable(const char *command, const char *const *params, byte paramCount) {}
inline void onStatus(const char *command, const char *const *params, byte paramCount) {}
