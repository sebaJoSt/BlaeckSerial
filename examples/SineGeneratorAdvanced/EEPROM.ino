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
  eepromaddress.loggingActivated = EEPROM.getAddress(sizeof(bool));
  eepromaddress.loggingInterval = EEPROM.getAddress(sizeof(unsigned long));
  eepromaddress.signalActivated = EEPROM.getAddress(sizeof(bool) * 41);
  eepromaddress.masterSlaveMode = EEPROM.getAddress(sizeof(int));
  eepromaddress.slaveID = EEPROM.getAddress(sizeof(int));
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
    EEPROM.update(eepromaddress.loggingActivated, false);
    bool isActivated[41];
    for (byte i = 0; i <= 40; i++)
    {
      isActivated[i] = true;
    }
    EEPROM.updateBlock<bool>(eepromaddress.signalActivated, isActivated, 41);
    EEPROM.updateLong(eepromaddress.loggingInterval, 1000); //[ms]
    EEPROM.updateByte(eepromaddress.masterSlaveMode, 0);
    EEPROM.updateByte(eepromaddress.slaveID, 1);
  }
  // END FIRWARE Update Case
}

void EEPROMReadStartupValues()
{
  loggingActivated = EEPROM.read(eepromaddress.loggingActivated);
  loggingInterval = EEPROM.readLong(eepromaddress.loggingInterval);
  masterSlaveMode = EEPROM.readByte(eepromaddress.masterSlaveMode);
  slaveID = EEPROM.readByte(eepromaddress.slaveID);

  bool isActivated[MAXIMUM_SIGNALS + 1];
  EEPROM.readBlock<bool>(eepromaddress.signalActivated, isActivated, MAXIMUM_SIGNALS + 1);
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    sine[i].isActivated = isActivated[i];
  }
}
