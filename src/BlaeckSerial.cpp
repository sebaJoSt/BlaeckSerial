/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include "BlaeckSerial.h"
// #include "Arduino.h"

// static initializer for the static member.
BlaeckSerial *BlaeckSerial::_pSingletonInstance = 0;

BlaeckSerial::BlaeckSerial()
{
}
BlaeckSerial::~BlaeckSerial()
{
  delete Signals;
}

void BlaeckSerial::begin(HardwareSerial *Ref, unsigned int size)
{
  SerialRef = Ref;
  Signals = new Signal[size];
  // Assign the static singleton used in the static handlers.
  BlaeckSerial::_pSingletonInstance = this;
}
void BlaeckSerial::beginMaster(HardwareSerial *Ref, unsigned int size, uint32_t WireClockFrequency)
{
  _masterSlaveConfig = Master;
  Wire.setClock(WireClockFrequency);
  Wire.begin();

  begin(Ref, size);
}
void BlaeckSerial::beginSlave(HardwareSerial *Ref, unsigned int size, byte slaveID)
{
  _masterSlaveConfig = Slave;
  _slaveID = slaveID;
  if (_slaveID > 127)
    _slaveID = 127;
  String s = "S";
  _slaveSymbolPrefix = s + _slaveID + "_";

  Wire.onReceive(OnSendHandler);
  Wire.onRequest(OnReceiveHandler);
  Wire.begin(_slaveID);

  begin(Ref, size);
}

void BlaeckSerial::addSignal(String symbolName, bool *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, byte *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, short *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned short *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, int *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_int;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned int *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_uint;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, long *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned long *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, float *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, double *value, bool prefixSlaveID)
{
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
    and is exactly the same as the float, with no gain in precision.*/
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::deleteSignals()
{
  _signalIndex = 0;
}

void BlaeckSerial::read()
{

  if (recvWithStartEndMarkers() == true)
  {
    parseData();
    SerialRef->print("<");
    SerialRef->print(receivedChars);
    SerialRef->println(">");

    if (strcmp(COMMAND, "BLAECK.WRITE_SYMBOLS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeSymbols(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DATA") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeData(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.GET_DEVICES") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDevices(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.ACTIVATE") == 0)
    {
      unsigned long parameter = PARAMETER[0];
      if (parameter > 32767)
        parameter = 32767;
      unsigned long unit_multiplicator = 1000;
      unsigned long timedInterval_ms = parameter * unit_multiplicator;

      this->setTimedData(true, timedInterval_ms);
    }
    else if (strcmp(COMMAND, "BLAECK.DEACTIVATE") == 0)
    {
      this->setTimedData(false, _timedInterval_ms);
    }

    if (_readCallback != NULL)
      _readCallback(COMMAND, PARAMETER, STRING_01);
  }
}
void BlaeckSerial::attachRead(void (*readCallback)(char *command, int *parameter, char *string01))
{
  _readCallback = readCallback;
}

void BlaeckSerial::attachUpdate(void (*updateCallback)())
{
  _updateCallback = updateCallback;
}

bool BlaeckSerial::recvWithStartEndMarkers()
{
  bool newData = false;
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (SerialRef->available() > 0 && newData == false)
  {
    rc = SerialRef->read();
    if (recvInProgress == true)
    {
      if (rc != endMarker)
      {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= MAXIMUM_CHAR_COUNT)
        {
          ndx = MAXIMUM_CHAR_COUNT - 1;
        }
      }
      else
      {
        // terminate the string
        receivedChars[ndx] = '\0';
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }
    else if (rc == startMarker)
    {
      recvInProgress = true;
    }
  }

  return newData;
}
void BlaeckSerial::parseData()
{
  // split the data into its parts
  char tempChars[sizeof(receivedChars)];
  strcpy(tempChars, receivedChars);
  char *strtokIndx;
  strtokIndx = strtok(tempChars, ",");
  strcpy(COMMAND, strtokIndx);
  strtokIndx = strtok(NULL, ",");
  //PARAMETER 1 is stored in PARAMETER_01 & STRING_01 (if PARAMETER 1 is a string)
  //Only copy first 15 chars
  strncpy(STRING_01, strtokIndx, 15);
  //16th Char = Null Terminator
  STRING_01[15] = '\0';
  PARAMETER[0] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[1] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[2] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[3] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[4] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[5] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[6] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[7] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[8] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[9] = atoi(strtokIndx);
}

void BlaeckSerial::setTimedData(bool timedActivated, unsigned long timedInterval_ms)
{
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    if (timedInterval_ms == 0)
    {
      _timedSetPoint_ms = 10;
      _timedInterval_ms = 10;
    }
    else if (timedInterval_ms > 32767000)
    {
      _timedSetPoint_ms = 32767000;
      _timedInterval_ms = 32767000;
    }
    else
    {
      _timedSetPoint_ms = timedInterval_ms;
      _timedInterval_ms = timedInterval_ms;
    }
    _timedFirstTime = true;
  }
}

void BlaeckSerial::writeSymbols()
{
  this->writeSymbols(1);
}
void BlaeckSerial::writeSymbols(unsigned long msg_id)
{
  if (_masterSlaveConfig == Single || _masterSlaveConfig == Slave)
  {
    this->writeLocalSymbols(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    this->writeLocalSymbols(msg_id, false);
    this->writeSlaveSymbols(true);
  }
}

void BlaeckSerial::writeData()
{
  this->writeData(1);
}
void BlaeckSerial::writeData(unsigned long msg_id)
{
  if (_masterSlaveConfig == Single)
  {
    if (_updateCallback != NULL)
      _updateCallback();
    this->writeLocalData(msg_id, true);
  }
  else if (_masterSlaveConfig == Slave)
  {
    //updateCallback is called in BlaeckSerial::wireSlaveReceive()
    this->writeLocalData(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    if (_updateCallback != NULL)
      _updateCallback();
    this->writeLocalData(msg_id, false);
    this->writeSlaveData(true);
  }
}

void BlaeckSerial::timedWriteData()
{
  this->timedWriteData(185273099);
}
void BlaeckSerial::timedWriteData(unsigned long msg_id)
{

  if (_timedFirstTime == true)
    _timedFirstTimeDone_ms = millis();
  unsigned long _timedElapsedTime_ms = (millis() - _timedFirstTimeDone_ms);

  if (((_timedElapsedTime_ms >= _timedSetPoint_ms) || _timedFirstTime == true) && _timedActivated == true)
  {
    if (_timedFirstTime == false)
      _timedSetPoint_ms += _timedInterval_ms;
    _timedFirstTime = false;

    this->writeData(msg_id);
  }
}

void BlaeckSerial::writeDevices()
{
  this->writeDevices(1);
}
void BlaeckSerial::writeDevices(unsigned long msg_id)
{
  if (_masterSlaveConfig == Single || _masterSlaveConfig == Slave)
  {
    this->writeLocalDevices(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    this->writeLocalDevices(msg_id, false);
    this->writeSlaveDevices(true);
  }
}

void BlaeckSerial::writeLocalDevices(unsigned long msg_id, bool send_eol)
{
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB2;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");
  SerialRef->write(_masterSlaveConfig);
  SerialRef->write(_slaveID);
  SerialRef->print(DeviceName);
  SerialRef->write('\0');
  SerialRef->print(DeviceHWVersion);
  SerialRef->write('\0');
  SerialRef->print(DeviceFWVersion);
  SerialRef->write('\0');
  SerialRef->print(BLAECKSERIAL_VERSION);
  SerialRef->write('\0');

  if (send_eol)
  {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeSlaveDevices(bool send_eol)
{

  //Cycle through slaves
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 4; retries++)
      {
        Wire.beginTransmission(slaveindex);
        Wire.write(3);
        transmissionIsSuccess = Wire.endTransmission();
        //0: success
        if (transmissionIsSuccess == 0)
        {
          break;
        }
      }

      if (transmissionIsSuccess == 0)
      {
        SerialRef->write(2);          //Slave config
        SerialRef->write(slaveindex); //Slave ID

        bool eolist_found = false;

        for (int i = 0; i < 1000; i++)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);

          bool eosignal_found = false;

          int charsToRead = 32;

          for (int symbolchar = 0; symbolchar <= charsToRead - 1; symbolchar++)
          {
            //Slave may send less than requested
            //DeviceInfo + \0
            // receive a byte as character
            char c = Wire.read();
            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
            }
            //'\n'
            if (c == char(0x0A))
              eolist_found = true;
            if (eosignal_found != true && eolist_found != true)
              SerialRef->print(c);
          }
          if (eolist_found)
            break;
        }
      }
    }
  }
  if (send_eol)
  {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeLocalData(unsigned long msg_id, bool send_eol)
{
  _crc.setPolynome(0x04C11DB7);
  _crc.setStartXOR(0xFFFFFFFF);
  _crc.setEndXOR(0xFFFFFFFF);
  _crc.setReverseIn(true);
  _crc.setReverseOut(true);
  _crc.restart();

  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB1;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");

  _crc.add(msg_key);
  _crc.add(':');
  _crc.add(ulngCvt.bval, 4);
  _crc.add(':');


  for (int i = 0; i < _signalIndex; i++)
  {
    intCvt.val = i;
    SerialRef->write(intCvt.bval, 2);
    _crc.add(intCvt.bval, 2);

    Signal signal = Signals[i];
    switch (signal.DataType)
    {
      case (Blaeck_bool):
        {
          boolCvt.val = *((bool *)signal.Address);
          SerialRef->write(boolCvt.bval, 1);
          _crc.add(boolCvt.bval, 1);
        }
        break;
      case (Blaeck_byte):
        {
          SerialRef->write(*((byte *)signal.Address));
          _crc.add(*((byte *)signal.Address));
        }
        break;
      case (Blaeck_short):
        {
          shortCvt.val = *((short *)signal.Address);
          SerialRef->write(shortCvt.bval, 2);
          _crc.add(shortCvt.bval, 2);
        }
        break;
      case (Blaeck_ushort):
        {
          ushortCvt.val = *((unsigned short *)signal.Address);
          SerialRef->write(ushortCvt.bval, 2);
          _crc.add(ushortCvt.bval, 2);
        }
        break;
      case (Blaeck_int):
        {
          intCvt.val = *((int *)signal.Address);
          SerialRef->write(intCvt.bval, 2);
          _crc.add(intCvt.bval, 2);
        }
        break;
      case (Blaeck_uint):
        {
          uintCvt.val = *((unsigned int *)signal.Address);
          SerialRef->write(uintCvt.bval, 2);
          _crc.add(uintCvt.bval, 2);
        }
        break;
      case (Blaeck_long):
        {
          lngCvt.val = *((long *)signal.Address);
          SerialRef->write(lngCvt.bval, 4);
          _crc.add(lngCvt.bval, 4);
        }
        break;
      case (Blaeck_ulong):
        {
          ulngCvt.val = *((unsigned long *)signal.Address);
          SerialRef->write(ulngCvt.bval, 4);
          _crc.add(ulngCvt.bval, 4);
        }
        break;
      case (Blaeck_float):
        {
          fltCvt.val = *((float *)signal.Address);
          SerialRef->write(fltCvt.bval, 4);
          _crc.add(fltCvt.bval, 4);
        }
        break;
    }
  }

  if (send_eol)
  {
    //StatusByte 0: Normal transmission
    //StatusByte + CRC First Byte + CRC Second Byte + CRC Third Byte + CRC Fourth Byte
    SerialRef->write(0);

    uint32_t crc_value = _crc.getCRC();
    SerialRef->write((byte *) &crc_value, 4);

    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}
void BlaeckSerial::writeSlaveData(bool send_eol)
{

  int signalCount = 0;
  bool slaveCRCErrorOccured = false;
  int slaveIDWithCRCError;
  int slaveSignalKeyWithCRCError;

  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  { //Cycle through slaves
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 40; retries++)
      {
        Wire.beginTransmission(slaveindex);
        Wire.write(1);
        transmissionIsSuccess = Wire.endTransmission();
        //0: success
        if (transmissionIsSuccess == 0)
          break;
      }

      if (transmissionIsSuccess == 0)
      {
        bool eolist_found = false;
        for (int slaveSignal = 0; slaveSignal < 1000; slaveSignal++)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          //try again
          if (receivedBytes < 2)
            continue;

          // slave may send less than requested
          for (int symbolchar = 0; symbolchar <= 31; symbolchar++)
          {
            //first receive number of bytes to expect
            char bytecount = Wire.read();
            char c;
            if (bytecount > 0 && bytecount < 127)
            {
              _crcWireCalc.restart();
              _crcWireCalc.add(bytecount);

              //Signal Key
              SerialRef->write(lowByte(_signalIndex + signalCount));
              SerialRef->write(highByte(_signalIndex + signalCount));

              intCvt.val = _signalIndex + signalCount;
              _crc.add(intCvt.bval, 2);

              for (int i = 0; i < bytecount; i++)
              {
                //then read the data bytes
                c = Wire.read();
                SerialRef->write(c);
                _crc.add(c);
                _crcWireCalc.add(c);

                if (i == (bytecount - 1))
                {
                  uint8_t crcWireTransmittedByte0 = Wire.read();
                  uint8_t crcWireTransmittedByte1 = Wire.read();

                  uint16_t crcWireTransmitted = ((uint16_t)crcWireTransmittedByte1 << 8) | ((uint16_t)crcWireTransmittedByte0);

                  uint16_t crcWireCalculated = _crcWireCalc.getCRC();

                  if (crcWireTransmitted != crcWireCalculated && slaveCRCErrorOccured == false)
                  {
                    //only first CRCError is sent
                    slaveCRCErrorOccured = true;
                    slaveIDWithCRCError = slaveindex;
                    slaveSignalKeyWithCRCError = _signalIndex + signalCount;
                    break;
                  }
                }
              }
              signalCount += 1;
            }
            else
            {
              if (bytecount == 127)
              {
                eolist_found = true;
                break;
              }
            }
          }
          if (eolist_found) break;
        }
      }
    }
  }
  if (send_eol)
  {
    if (slaveCRCErrorOccured)
    {
      //StatusByte 1: CRC Error at I2C transmission from Slave to Master
      //StatusByte + 0 + SignalKey First Byte + SignalKey Second Byte + SlaveID
      SerialRef->write(1);
      SerialRef->write(0);
      intCvt.val = slaveSignalKeyWithCRCError;
      SerialRef->write(intCvt.bval, 2);
      SerialRef->write(slaveIDWithCRCError);
    }
    else
    {
      //StatusByte 0: Normal transmission, no wire CRC errors occured
      //StatusByte + CRC First Byte + CRC Second Byte + CRC Third Byte + CRC Fourth Byte
      SerialRef->write(0);

      uint32_t crc_value = _crc.getCRC();
      SerialRef->write((byte *) &crc_value, 4);
    }

    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeLocalSymbols(unsigned long msg_id, bool send_eol)
{
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB0;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");

  for (int i = 0; i < _signalIndex; i++)
  {
    SerialRef->write(_masterSlaveConfig);
    SerialRef->write(_slaveID);

    Signal signal = Signals[i];
    SerialRef->print(signal.SymbolName);
    SerialRef->write('\0');

    switch (signal.DataType)
    {
      case (Blaeck_bool):
        {
          SerialRef->write(0x0);
          break;
        }
      case (Blaeck_byte):
        {
          SerialRef->write(0x1);
          break;
        }
      case (Blaeck_short):
        {
          SerialRef->write(0x2);
          break;
        }
      case (Blaeck_ushort):
        {
          SerialRef->write(0x3);
          break;
        }
      case (Blaeck_int):
        {
          SerialRef->write(0x4);
          break;
        }
      case (Blaeck_uint):
        {
          SerialRef->write(0x5);
          break;
        }
      case (Blaeck_long):
        {
          SerialRef->write(0x6);
          break;
        }
      case (Blaeck_ulong):
        {
          SerialRef->write(0x7);
          break;
        }
      case (Blaeck_float):
        {
          SerialRef->write(0x8);
          break;
        }
    }
  }
  if (send_eol)
  {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeSlaveSymbols(bool send_eol)
{

  int signalCount = 0;

  //Cycle through slaves
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 4; retries++)
      {
        Wire.beginTransmission(slaveindex);
        Wire.write(0);
        transmissionIsSuccess = Wire.endTransmission();
        //0: success
        if (transmissionIsSuccess == 0)
          break;
      }

      if (transmissionIsSuccess == 0)
      {

        bool eolist_found = false;

        for (int i = 0; i < 1000; i++)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          if (receivedBytes < 2)
            continue; //try again

          bool eosignal_found = false;

          int charsToRead = 32;

          for (int symbolchar = 0; symbolchar <= charsToRead - 1; symbolchar++)
          {
            //Slave may send less than requested
            //SymbolName + \0 + DataType
            // receive a byte as character
            char c = Wire.read();

            //'\0'
            if (c != char(0x00) && symbolchar == 0)
            {
              SerialRef->write(2);          //Slave config
              SerialRef->write(slaveindex); //Slave ID
            }
            if (c == char(0x00) && symbolchar == 0)
            {
              continue;
            }

            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
              signalCount += 1;
            }

            //'\n'
            if (c == char(0x0A))
              eolist_found = true;

            if (eosignal_found != true && eolist_found != true)
              SerialRef->print(c);
          }
          if (eolist_found)
            break;
        }
      }
    }
  }
  if (send_eol)
  {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::scanI2CSlaves(char addressStart, char addressEnd)
{
  //Cycle through slaves
  for (int slaveindex = addressStart; slaveindex <= addressEnd; slaveindex++)
  {
    byte transmissionIsSuccess = false;

    for (byte retries = 0; retries < 4; retries++)
    {
      Wire.beginTransmission(slaveindex);
      Wire.write(2);
      transmissionIsSuccess = Wire.endTransmission();
      //0: success
      if (transmissionIsSuccess == 0)
        break;
    }

    if (transmissionIsSuccess == 0)
    {
      storeSlave(slaveindex, false);

      byte receivedBytes = Wire.requestFrom(slaveindex, 1);

      //Expecting response 0xAA from slave -> Slave found
      //Receive a byte as character
      char c = Wire.read();

      if (c == char(0xAA))
      {
        storeSlave(slaveindex, true);
      }
      else
      {
        storeSlave(slaveindex, false);
      }
    }
  }
}

void BlaeckSerial::tick()
{
  this->tick(185273099);
}

void BlaeckSerial::tick(unsigned long msg_id)
{
  this->read();
  this->timedWriteData(msg_id);
}

void BlaeckSerial::wireSlaveTransmitStatusByte()
{
  Wire.write(0xAA);
}

void BlaeckSerial::wireSlaveTransmitSingleDevice()
{
  char little_s_string[32] = "";

  if (_wireDeviceIndex == 0)
  {
    DeviceName.toCharArray(little_s_string, 32);
    Wire.write(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 1)
  {
    DeviceHWVersion.toCharArray(little_s_string, 32);
    Wire.write(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 2)
  {
    DeviceFWVersion.toCharArray(little_s_string, 32);
    Wire.write(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 3)
  {
    BLAECKSERIAL_VERSION.toCharArray(little_s_string, 32);
    Wire.write(little_s_string);
    Wire.write('\0');
  }

  Wire.write(0x0D);

  _wireDeviceIndex += 1;
  if (_wireDeviceIndex > 3)
  {
    _wireDeviceIndex = 0;
    Wire.write(0x0A);
  }
}

void BlaeckSerial::wireSlaveTransmitSingleSymbol()
{
  Signal signal = Signals[_wireSignalIndex];

  char little_s_string[32] = "";
  signal.SymbolName.toCharArray(little_s_string, 32);
  Wire.write(little_s_string);

  Wire.write('\0');

  switch (signal.DataType)
  {
    case (Blaeck_bool):
      {
        Wire.write(0x0);
        break;
      }
    case (Blaeck_byte):
      {
        Wire.write(0x1);
        break;
      }
    case (Blaeck_short):
      {
        Wire.write(0x2);
        break;
      }
    case (Blaeck_ushort):
      {
        Wire.write(0x3);
        break;
      }
    case (Blaeck_int):
      {
        Wire.write(0x4);
        break;
      }
    case (Blaeck_uint):
      {
        Wire.write(0x5);
        break;
      }
    case (Blaeck_long):
      {
        Wire.write(0x6);
        break;
      }
    case (Blaeck_ulong):
      {
        Wire.write(0x7);
        break;
      }
    case (Blaeck_float):
      {
        Wire.write(0x8);
        break;
      }
  }

  Wire.write(0x0D);

  _wireSignalIndex += 1;
  if (_wireSignalIndex >= _signalIndex)
  {
    _wireSignalIndex = 0;
    Wire.write(0x0A);
  }
}
void BlaeckSerial::wireSlaveTransmitSingleDataPoint()
{
  _crcWire.restart();

  Signal signal = Signals[_wireSignalIndex];

  switch (signal.DataType)
  {
    case (Blaeck_bool):
      {
        boolCvt.val = *((bool *)signal.Address);
        Wire.write(1);
        Wire.write(boolCvt.bval, 1);
        _crcWire.add(1);
        _crcWire.add(boolCvt.bval, 1);
      }
      break;
    case (Blaeck_byte):
      {
        Wire.write(1);
        Wire.write(*((byte *)signal.Address));
        _crcWire.add(1);
        _crcWire.add(*((byte *)signal.Address));
      }
      break;
    case (Blaeck_short):
      {
        shortCvt.val = *((short *)signal.Address);
        Wire.write(2);
        Wire.write(shortCvt.bval, 2);
        _crcWire.add(2);
        _crcWire.add(shortCvt.bval, 2);
      }
      break;
    case (Blaeck_ushort):
      {
        ushortCvt.val = *((unsigned short *)signal.Address);
        Wire.write(2);
        Wire.write(ushortCvt.bval, 2);
        _crcWire.add(2);
        _crcWire.add(ushortCvt.bval, 2);
      }
      break;
    case (Blaeck_int):
      {
        intCvt.val = *((int *)signal.Address);
        Wire.write(2);
        Wire.write(intCvt.bval, 2);
        _crcWire.add(2);
        _crcWire.add(intCvt.bval, 2);
      }
      break;
    case (Blaeck_uint):
      {
        uintCvt.val = *((unsigned int *)signal.Address);
        Wire.write(2);
        Wire.write(uintCvt.bval, 2);
        _crcWire.add(2);
        _crcWire.add(uintCvt.bval, 2);
      }
      break;
    case (Blaeck_long):
      {
        lngCvt.val = *((long *)signal.Address);
        Wire.write(4);
        Wire.write(lngCvt.bval, 4);
        _crcWire.add(4);
        _crcWire.add(lngCvt.bval, 4);
      }
      break;
    case (Blaeck_ulong):
      {
        ulngCvt.val = *((unsigned long *)signal.Address);
        Wire.write(4);
        Wire.write(ulngCvt.bval, 4);
        _crcWire.add(4);
        _crcWire.add(ulngCvt.bval, 4);
      }
      break;
    case (Blaeck_float):
      {
        fltCvt.val = *((float *)signal.Address);
        Wire.write(4);
        Wire.write(fltCvt.bval, 4);
        _crcWire.add(4);
        _crcWire.add(fltCvt.bval, 4);
      }
      break;
  }

  _wireSignalIndex += 1;

  if (_signalIndex == 0)
  {
    Wire.write(0);
  }
  else
  {
    uint16_t crc_value = _crcWire.getCRC();
    Wire.write((byte *) &crc_value, 2);
  }

  if (_wireSignalIndex >= _signalIndex)
  {
    _wireSignalIndex = 0;
    Wire.write(0x7F);
  }
}

void BlaeckSerial::wireSlaveReceive()
{
  _wireMode = Wire.read();
  _wireSignalIndex = 0;
  _wireDeviceIndex = 0;

  if (_masterSlaveConfig == Slave && _wireMode == 1)
  {
    if (_updateCallback != NULL)
      _updateCallback();
  };
}
void BlaeckSerial::wireSlaveTransmitToMaster()
{
  if (_wireMode == 0)
    this->wireSlaveTransmitSingleSymbol();
  if (_wireMode == 1)
    this->wireSlaveTransmitSingleDataPoint();
  if (_wireMode == 2)
    this->wireSlaveTransmitStatusByte();
  if (_wireMode == 3)
    this->wireSlaveTransmitSingleDevice();
}

bool BlaeckSerial::slaveFound(const unsigned int index)
{
  if (index > 127)
    return false;
  return (boolean)bitRead(_slaveFound[index / 8], index % 8);
}

void BlaeckSerial::storeSlave(const unsigned int index, const boolean value)
{
  if (index > 127)
    return;
  bitWrite(_slaveFound[index / 8], index % 8, value);
}
