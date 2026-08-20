// The library has already checked that the value is within [1, MAXIMUM_SIGNALS]
// before these run, so an out-of-range bound never reaches the sketch.
void onSetSignalFirst(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalFirst = (byte)atoi(params[0]);
    EEPROM.put(EEPROM_ADDR_SIGNAL_FIRST, signalFirst);
    EepromCommit();
    // Pushes the bound's own state channel, so the control shows the new value at once -
    // the channel is otherwise only read when Loggbok polls the catalog.
    Blaeck.writeCommandState(command);
  }
}

void onSetSignalLast(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalLast = (byte)atoi(params[0]);
    EEPROM.put(EEPROM_ADDR_SIGNAL_LAST, signalLast);
    EepromCommit();
    Blaeck.writeCommandState(command);
  }
}

void onSignalActivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  // A button's parameters are optional. Pressed from a dashboard it sends none and applies the
  // bounds SIGNAL_FIRST and SIGNAL_LAST last set. Sent over serial as <SIGNAL_ACTIVATE,3,7> it
  // carries its own range, which nothing has validated - a button is the one kind the library
  // passes through unchecked - so it goes to the same clamping the stored bounds get.
  if (paramCount >= 2)
    ApplySignalRange(true, (byte)atoi(params[0]), (byte)atoi(params[1]));
  else
    ApplySignalRange(true, signalFirst, signalLast);
}

void onSignalDeactivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount >= 2)
    ApplySignalRange(false, (byte)atoi(params[0]), (byte)atoi(params[1]));
  else
    ApplySignalRange(false, signalFirst, signalLast);
}

void onSignalActivateAll(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  ApplySignalRange(true, 1, MAXIMUM_SIGNALS);
}

// Applies the bounds it is given. Only the signals inside the range change, so
// activating 1-10 and then 15-20 leaves both ranges on - use the deactivate
// button to clear what you no longer want.
void ApplySignalRange(bool activate, byte lo, byte hi)
{
  if (lo > hi)
  {
    byte tmp = lo;
    lo = hi;
    hi = tmp;
  }
  if (lo < 1)
    lo = 1;
  if (hi > MAXIMUM_SIGNALS)
    hi = MAXIMUM_SIGNALS;

  for (byte i = lo; i <= hi; i++)
  {
    sine[i].isActivated = activate;
  }

  PersistActivatedSignals();
  UpdateLoggingSignals();
}

void PersistActivatedSignals()
{
  bool isActivated[MAXIMUM_SIGNALS + 1];
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    isActivated[i] = sine[i].isActivated;
  }
  EEPROM.put(EEPROM_ADDR_SIGNAL_ACTIVATED, isActivated);
  EepromCommit();
}
