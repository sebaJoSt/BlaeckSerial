void EEPROMConfiguration()
{
  EepromBegin();
  EEPROMWriteDefaultValuesAtFirmwareUpdate();
  EEPROMReadStartupValues();
}

void EEPROMWriteDefaultValuesAtFirmwareUpdate()
{
  // Check if FIRMWARE was updated
  char storedfirmware[sizeof(FW_VERSION)];
  EEPROM.get(EEPROM_ADDR_FW_VERSION, storedfirmware);
  // A board that has never held this layout returns whatever was there, which need not end
  // in a terminator. Forcing one keeps strcmp inside the buffer instead of reading on until
  // it happens to find a zero.
  storedfirmware[sizeof(storedfirmware) - 1] = '\0';

  if (strcmp(storedfirmware, FW_VERSION) != 0)
  //--INIT EEPROM - Write Default Values
  {
    bool isActivated[MAXIMUM_SIGNALS + 1];
    for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
    {
      isActivated[i] = true;
    }
    EEPROM.put(EEPROM_ADDR_SIGNAL_ACTIVATED, isActivated);
    EEPROM.put(EEPROM_ADDR_SIGNAL_FIRST, (byte)1);
    EEPROM.put(EEPROM_ADDR_SIGNAL_LAST, (byte)MAXIMUM_SIGNALS);
    // Written last, so it marks the layout as initialised only once every value it stands
    // for is actually there. A reset midway through leaves the marker unwritten and the
    // defaults are simply written again on the next boot.
    EEPROM.put(EEPROM_ADDR_FW_VERSION, FW_VERSION);
    EepromCommit();
  }
  // END FIRMWARE Update Case
}

void EEPROMReadStartupValues()
{
  bool isActivated[MAXIMUM_SIGNALS + 1];
  EEPROM.get(EEPROM_ADDR_SIGNAL_ACTIVATED, isActivated);
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    sine[i].isActivated = isActivated[i];
  }

  // Clamped rather than trusted. The typed commands guarantee a valid value on
  // the way in, but EEPROM outlives the sketch: a board that still holds an
  // older layout reads 255 here, and without the clamp that would go straight
  // into the activation loop as a signal index.
  EEPROM.get(EEPROM_ADDR_SIGNAL_FIRST, signalFirst);
  EEPROM.get(EEPROM_ADDR_SIGNAL_LAST, signalLast);
  if (signalFirst < 1 || signalFirst > MAXIMUM_SIGNALS)
    signalFirst = 1;
  if (signalLast < 1 || signalLast > MAXIMUM_SIGNALS)
    signalLast = MAXIMUM_SIGNALS;
}