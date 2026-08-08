void EEPROMConfiguration()
{
  EEPROMAddressSetup();
  EEPROMWriteDefaultValuesAtFirmwareUpdate();
  EEPROMReadStartupValues();
}

void EEPROMAddressSetup()
{
  EEPROM.setMemPool(0, EEPROMSizeMega);
  // Always get the adresses first and in the same order
  eepromaddress.firmware_version = EEPROM.getAddress(sizeof(char) * 6);
  eepromaddress.signalActivated = EEPROM.getAddress(sizeof(bool) * (MAXIMUM_SIGNALS + 1));
  eepromaddress.signalFirst = EEPROM.getAddress(sizeof(byte));
  eepromaddress.signalLast = EEPROM.getAddress(sizeof(byte));
}

void EEPROMWriteDefaultValuesAtFirmwareUpdate()
{
  // Check if FIRMARE was updated
  bool isFirmwareUpdated;
  char storedfirmware[6];
  EEPROM.readBlock<char>(eepromaddress.firmware_version, storedfirmware, 6);
  if (strcmp(storedfirmware, FW_VERSION) == 0)
    isFirmwareUpdated = false;
  else
    isFirmwareUpdated = true;

  // FIRWARE Update Case
  if (isFirmwareUpdated == true)
  //--INIT EEPROM - Write Default Values
  {
    for (int i = 0; i < EEPROMSizeMega; i++)
    {
      EEPROM.update(i, 255);
    }
    EEPROM.updateBlock<char>(eepromaddress.firmware_version, FW_VERSION, 6);
    bool isActivated[MAXIMUM_SIGNALS + 1];
    for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
    {
      isActivated[i] = true;
    }
    EEPROM.updateBlock<bool>(eepromaddress.signalActivated, isActivated, MAXIMUM_SIGNALS + 1);
    EEPROM.updateByte(eepromaddress.signalFirst, 1);
    EEPROM.updateByte(eepromaddress.signalLast, MAXIMUM_SIGNALS);
  }
  // END FIRWARE Update Case
}

void EEPROMReadStartupValues()
{
  bool isActivated[MAXIMUM_SIGNALS + 1];
  EEPROM.readBlock<bool>(eepromaddress.signalActivated, isActivated, MAXIMUM_SIGNALS + 1);
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    sine[i].isActivated = isActivated[i];
  }

  // Clamped rather than trusted: the typed commands guarantee a valid value on
  // the way in, but EEPROM contents survive a firmware change that this
  // version marker did not catch.
  signalFirst = EEPROM.readByte(eepromaddress.signalFirst);
  signalLast = EEPROM.readByte(eepromaddress.signalLast);
  if (signalFirst < 1 || signalFirst > MAXIMUM_SIGNALS)
    signalFirst = 1;
  if (signalLast < 1 || signalLast > MAXIMUM_SIGNALS)
    signalLast = MAXIMUM_SIGNALS;
}
