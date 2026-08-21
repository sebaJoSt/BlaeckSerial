/*
  StateChannels.ino

  Every way a state channel can get its value, side by side, so the choice is easy to see.
  A state channel reports what something *is* - a status line, a setting, a reading - and is
  shown but never logged. A signal is the opposite: sampled on an interval and kept as history.

  There are three ways a channel gets its value, and the question that picks one is always the
  same: where does the value live?

    A  POINTER  the value is a variable you keep      the channel reads it
    B  GETTER   the value is worked out from others   a function is asked for it
    C  TAG      there is no value until something     writeState() hands it over
                happens

  Text, numbers and bool all support all three, so there are nine combinations. This sketch
  declares one channel per combination and marks each with its letter.

  What to look for once it is logging:
    A   Mode / Uptime / DoorOpen        read a variable the sketch keeps
    B   Status / Efficiency / Running   worked out when read, so never stale
    C   LastError / SetPoint / SelfTest empty until something writes them

  A and B both need a push to reach a host - writeState(name), with no value, because both
  already know where their value comes from. The difference is what that value is worth: A
  reports a variable, B works one out at the moment it is asked. Only C is handed a value.

  C is the one a pointer cannot imitate. A channel pointed at a variable always reports
  something, and 0 or false cannot be told apart from a reading not yet taken. A channel
  declared with a tag reports nothing at all until it has something to say.

  Author: Sebastian Strobl, https://github.com/sebaJoSt/BlaeckSerial
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

BlaeckSerial Blaeck;

// A: a channel keeps a pointer to these, so they have to be globals - as a signal's do.
char Mode[16] = "starting";
unsigned long Uptime = 0;
bool DoorOpen = false;

// B: read by the getters below. No channel points at them - they are what the getters are
// worked out from, which is the whole difference between A and B.
float Output = 0.0f;
float Input = 1.0f;
byte Clients = 0;
bool Fault = false;

void setup()
{
  Serial.begin(115200);

  Blaeck.begin(&Serial)
      .withSignals(1)
      .withStateChannels(9)
      .withDebugStream(&Serial);

  Blaeck.DeviceName = "State Channels Demo";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = "1.0";

  // A logging session needs something to log; state channels are never logged themselves.
  Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s"));

  // --- A: POINTER - the channel reads a variable you keep --------------------------------
  // Cheapest, and right whenever the value already lives somewhere. The variable *is* the
  // value, so what the channel reports cannot be out of date.

  Blaeck.addStateChannel(F("Mode"), Mode)
      .withIcon(F("mdi:state-machine"));

  Blaeck.addStateChannel(F("Uptime"), &Uptime)
      .withUnit(F("s"))
      .withDeviceClass(F("duration"));

  Blaeck.addStateChannel(F("DoorOpen"), &DoorOpen)
      .withDeviceClass(F("door"));

  // --- B: GETTER - the channel works the value out when asked ----------------------------
  // For a value *derived* from other state. A variable holding a calculation is a copy of one,
  // right only until something it was worked out from moves - and the catalog is rebuilt at
  // moments the sketch cannot anticipate: at startup, when the channel list changes, and
  // whenever a host asks. A getter has no copy to fall out of step.
  //
  // The getter guarantees the value is current whenever it is read; it does not decide when
  // anyone is told. A host is told when the catalog is rebuilt - at startup, when the channel
  // list changes, or when it asks - and whenever the sketch pushes with writeState(name), which
  // takes no value because the getter supplies it. PushTheDerived() below does that on a timer.
  //
  // The getter runs while a frame is being assembled. Read variables and compute, nothing else.

  Blaeck.addStateChannel(F("Status"), BlaeckText)
      .withStateText(statusText)
      .withIcon(F("mdi:message-text"));

  Blaeck.addStateChannel(F("Efficiency"), BlaeckFloat)
      .withStateValue(efficiency)
      .withUnit(F("%"))
      .withDisplayPrecision(1);

  Blaeck.addStateChannel(F("Running"), BlaeckBool)
      .withStateValue(isRunning);

  // --- C: TAG - the channel holds nothing until something happens ------------------------
  // Declared with a tag naming the type. Until writeState() is called the catalog says the
  // channel has no value, which is the honest answer for something that has not happened yet -
  // and the one thing a pointer cannot express.

  Blaeck.addStateChannel(F("LastError"), BlaeckText)
      .withIcon(F("mdi:alert-circle"))
      .diagnostic();

  Blaeck.addStateChannel(F("SetPoint"), BlaeckFloat)
      .withUnit(F("C"));

  Blaeck.addStateChannel(F("SelfTest"), BlaeckBool)
      .withDeviceClass(F("problem"))
      .diagnostic();

  // One summary for every table, printed only if something was dropped. Safe here: nothing
  // has been written to Serial as a Blaeck frame yet.
  Blaeck.printRejections(&Serial);
}

void loop()
{
  Uptime = millis() / 1000;
  Blaeck.tick();
  UpdateTheVariables();
  PushTheDerived();
  ReportWhatHappened();
}

// ---- B: the getters ----------------------------------------------------------------------
// Each is worked out from two or more variables, which is why none of them is a variable.

const char *statusText()
{
  static char text[32];
  snprintf(text, sizeof(text), "%s, %u client%s", Fault ? "fault" : "ok",
           Clients, Clients == 1 ? "" : "s");
  return text;
}

float efficiency()
{
  return Input == 0.0f ? 0.0f : (Output / Input) * 100.0f;
}

bool isRunning()
{
  return Mode[0] == 'r' && !Fault;
}

// ---- Moving the variables A reports -------------------------------------------------------

void UpdateTheVariables()
{
  static unsigned long last = 0;
  if (millis() - last < 2000)
    return;
  last = millis();

  Output = (float)(millis() % 1000) / 1000.0f;
  Clients = (byte)((millis() / 2000) % 4);
  DoorOpen = !DoorOpen;

  if (Uptime >= 5 && strcmp(Mode, "running") != 0)
  {
    // The variable is the value, so it is set where the sketch already knows it changed.
    strcpy(Mode, "running");
    Blaeck.writeState(F("Mode"));
  }
}

// ---- B: telling a host what the getters now say -------------------------------------------
// The getters are asked when the catalog is built, which is rare. These pushes are what make a
// dashboard move: writeState(name) with no value asks the getter and sends the answer. Nothing
// here recalculates - that is the getter's job, and it cannot be out of date when it runs.

void PushTheDerived()
{
  static unsigned long last = 0;
  if (millis() - last < 3000)
    return;
  last = millis();

  Blaeck.writeState(F("Status"));
  Blaeck.writeState(F("Efficiency"));
  Blaeck.writeState(F("Running"));
}

// ---- C: writing the channels that hold nothing --------------------------------------------

void ReportWhatHappened()
{
  static unsigned long last = 0;
  if (millis() - last < 7000)
    return;
  last = millis();

  // Text: the buffer is local, because writeState() copies it into the frame before returning.
  char line[40];
  snprintf(line, sizeof(line), "sensor timeout after %lu s", Uptime);
  Blaeck.writeState(F("LastError"), line);

  // Numbers convert to whatever the channel was declared as, so the literal does not have to
  // match: 21 lands on a float channel as 21.0.
  Blaeck.writeState(F("SetPoint"), 21);

  Blaeck.writeState(F("SelfTest"), Fault);
}
